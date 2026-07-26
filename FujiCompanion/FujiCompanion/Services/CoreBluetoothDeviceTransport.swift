import CoreBluetooth
import Foundation
import os

enum CoreBluetoothTransportError: LocalizedError {
    case disconnected
    case bluetoothUnavailable
    case missingService
    case missingCharacteristic
    case outgoingQueueFull
    case writeTimedOut
    case operationFailed(String)

    var errorDescription: String? {
        switch self {
        case .disconnected: "Fuji 蓝牙未连接"
        case .bluetoothUnavailable: "蓝牙不可用或未授权"
        case .missingService: "Fuji 蓝牙服务不可用"
        case .missingCharacteristic: "Fuji 蓝牙特征不完整"
        case .outgoingQueueFull: "Fuji 蓝牙发送队列已满"
        case .writeTimedOut: "Fuji 蓝牙写入超时"
        case .operationFailed(let message): "Fuji 蓝牙操作失败：\(message)"
        }
    }
}

@MainActor
final class CoreBluetoothDeviceTransport: NSObject, DeviceTransport {
    static let serviceUUID = CBUUID(string: "F157CFF7-0A18-4020-8CC0-CB1A0DA5BC22")
    static let commandUUID = CBUUID(string: "43265B1A-2D59-40A3-BC4D-0E2FA73FFC20")
    static let eventUUID = CBUUID(string: "FD1BA046-D0DD-415A-A757-A907E36AB912")
    static let stateUUID = CBUUID(string: "6406E94B-8721-4CA7-ACE4-5E67D0AFD1FD")

    private static let restorationIdentifier = "io.github.tossacointac.FujiCompanion.central"
    private static let peripheralDefaultsKey = "ble.boundPeripheralIdentifier"
    private static let maximumQueuedWrites = 8
    private static let writeTimeout: Duration = .seconds(5)
    private static let scanTimeout: Duration = .seconds(10)
    private static let connectionSetupTimeout: Duration = .seconds(10)
    private static let maximumReconnectDelaySeconds: UInt64 = 60

    private struct Metrics {
        var lastRSSI: Int?
        var reconnectCount = 0
        var protocolErrorCount = 0
        var reassemblyTimeoutCount = 0
        var queueOverflowCount = 0
    }

    private struct QueuedWrite {
        let id: UUID
        let frames: [Data]
        var nextFrameIndex: Int
        let interFrameDelay: Duration?
        let timeout: Duration
        let continuation: CheckedContinuation<Void, Error>
    }

    private(set) var connectionState: DeviceConnectionState = .disconnected
    let events: AsyncStream<DeviceTransportEvent>

    private let eventContinuation: AsyncStream<DeviceTransportEvent>.Continuation
    private let defaults: UserDefaults
    private let logger = Logger(subsystem: "io.github.tossacointac.FujiCompanion", category: "FujiBLE")
    private let session = FujiBLESession()

    private var centralManager: CBCentralManager!
    private var peripheral: CBPeripheral?
    private var commandCharacteristic: CBCharacteristic?
    private var eventCharacteristic: CBCharacteristic?
    private var stateCharacteristic: CBCharacteristic?
    private var eventSubscriptionReady = false
    private var stateSubscriptionReady = false
    private var wantsConnection = false
    private var isScanning = false
    private var reconnectDelaySeconds: UInt64 = 1
    private var reconnectTask: Task<Void, Never>?
    private var scanTimeoutTask: Task<Void, Never>?
    private var connectionSetupTimeoutTask: Task<Void, Never>?
    private var inboundTimeoutTask: Task<Void, Never>?
    private var inboundDeadlineMS: UInt64?
    private var nextTransferID: UInt32 = 1
    private var queuedWrites: [QueuedWrite] = []
    private var activeWrite: QueuedWrite?
    private var writeTimeoutTask: Task<Void, Never>?
    private var delayedWriteTask: Task<Void, Never>?
    private var metrics = Metrics()

    init(defaults: UserDefaults = .standard) {
        self.defaults = defaults
        var continuation: AsyncStream<DeviceTransportEvent>.Continuation?
        events = AsyncStream(bufferingPolicy: .bufferingNewest(64)) {
            continuation = $0
        }
        eventContinuation = continuation!
        super.init()
        centralManager = CBCentralManager(
            delegate: self,
            queue: nil,
            options: [
                CBCentralManagerOptionRestoreIdentifierKey: Self.restorationIdentifier,
                CBCentralManagerOptionShowPowerAlertKey: true
            ]
        )
    }

    deinit {
        reconnectTask?.cancel()
        scanTimeoutTask?.cancel()
        connectionSetupTimeoutTask?.cancel()
        inboundTimeoutTask?.cancel()
        writeTimeoutTask?.cancel()
        delayedWriteTask?.cancel()
        eventContinuation.finish()
    }

    func connect() {
        wantsConnection = true
        updateConnectionState(.connecting)
        guard centralManager.state == .poweredOn else {
            if centralManager.state != .unknown, centralManager.state != .resetting {
                updateConnectionState(.disconnected)
            }
            return
        }
        startConnectionAttempt()
    }

    func disconnect() {
        wantsConnection = false
        reconnectTask?.cancel()
        reconnectTask = nil
        scanTimeoutTask?.cancel()
        scanTimeoutTask = nil
        connectionSetupTimeoutTask?.cancel()
        connectionSetupTimeoutTask = nil
        delayedWriteTask?.cancel()
        delayedWriteTask = nil
        if centralManager.state == .poweredOn {
            centralManager.stopScan()
        }
        isScanning = false
        failAllWrites(with: CoreBluetoothTransportError.disconnected)
        if let peripheral, peripheral.state != .disconnected {
            centralManager.cancelPeripheralConnection(peripheral)
        } else {
            clearConnectionState()
            updateConnectionState(.disconnected)
        }
    }

    func send(_ message: FujiMessage) async throws {
        guard connectionState == .connected,
              let peripheral else {
            throw CoreBluetoothTransportError.disconnected
        }
        let maximumWriteValueLength = peripheral.maximumWriteValueLength(for: .withResponse)
        let frames = try makeFrames(for: message, maximumWriteValueLength: maximumWriteValueLength)
        try await enqueue(
            frames: frames,
            interFrameDelay: nil,
            timeout: Self.writeTimeout
        )
    }

    private func makeFrames(
        for message: FujiMessage,
        maximumWriteValueLength: Int
    ) throws -> [Data] {
        let transferID = nextTransferID
        nextTransferID &+= 1
        return try session.frames(
            for: message,
            maximumWriteValueLength: maximumWriteValueLength,
            transferID: transferID
        )
    }

    private func enqueue(
        frames: [Data],
        interFrameDelay: Duration?,
        timeout: Duration
    ) async throws {
        guard connectionState == .connected,
              let peripheral,
              let commandCharacteristic else {
            throw CoreBluetoothTransportError.disconnected
        }
        guard queuedWrites.count + (activeWrite == nil ? 0 : 1) < Self.maximumQueuedWrites else {
            metrics.queueOverflowCount += 1
            logMetrics(reason: "outgoing queue full")
            throw CoreBluetoothTransportError.outgoingQueueFull
        }

        try await withCheckedThrowingContinuation { continuation in
            queuedWrites.append(
                QueuedWrite(
                    id: UUID(),
                    frames: frames,
                    nextFrameIndex: 0,
                    interFrameDelay: interFrameDelay,
                    timeout: timeout,
                    continuation: continuation
                )
            )
            beginNextWriteIfNeeded(peripheral: peripheral, characteristic: commandCharacteristic)
        }
    }

    private var boundPeripheralID: UUID? {
        defaults.string(forKey: Self.peripheralDefaultsKey).flatMap(UUID.init(uuidString:))
    }

    private func startConnectionAttempt() {
        guard wantsConnection, centralManager.state == .poweredOn else { return }
        reconnectTask?.cancel()
        reconnectTask = nil
        updateConnectionState(.connecting)

        if let boundPeripheralID,
           let known = centralManager.retrievePeripherals(withIdentifiers: [boundPeripheralID]).first {
            connect(to: known)
            return
        }

        let connected = centralManager.retrieveConnectedPeripherals(withServices: [Self.serviceUUID])
        if let known = connected.first(where: { boundPeripheralID == nil || $0.identifier == boundPeripheralID }) {
            connect(to: known)
            return
        }

        guard !isScanning else { return }
        isScanning = true
        centralManager.scanForPeripherals(
            withServices: [Self.serviceUUID],
            options: [CBCentralManagerScanOptionAllowDuplicatesKey: false]
        )
        scanTimeoutTask = Task { [weak self] in
            try? await Task.sleep(for: Self.scanTimeout)
            guard !Task.isCancelled, let self, self.isScanning else { return }
            if self.centralManager.state == .poweredOn {
                self.centralManager.stopScan()
            }
            self.isScanning = false
            self.logger.info("Fuji scan window elapsed")
            self.scheduleReconnect()
        }
        logger.info("Scanning for Fuji service")
    }

    private func connect(to peripheral: CBPeripheral) {
        scanTimeoutTask?.cancel()
        scanTimeoutTask = nil
        if centralManager.state == .poweredOn {
            centralManager.stopScan()
        }
        isScanning = false
        self.peripheral = peripheral
        peripheral.delegate = self
        armConnectionSetupTimeout(for: peripheral)
        if peripheral.state == .connected {
            discoverService(on: peripheral)
        } else if centralManager.state == .poweredOn, peripheral.state != .connecting {
            centralManager.connect(peripheral)
        }
    }

    private func discoverService(on peripheral: CBPeripheral) {
        resetCharacteristics()
        peripheral.delegate = self
        armConnectionSetupTimeout(for: peripheral)
        peripheral.discoverServices([Self.serviceUUID])
    }

    private func armConnectionSetupTimeout(for peripheral: CBPeripheral) {
        connectionSetupTimeoutTask?.cancel()
        let identifier = peripheral.identifier
        connectionSetupTimeoutTask = Task { [weak self] in
            try? await Task.sleep(for: Self.connectionSetupTimeout)
            guard !Task.isCancelled,
                  let self,
                  self.connectionState == .connecting,
                  self.peripheral?.identifier == identifier else { return }
            self.logger.error("Fuji connection setup timed out")
            let stalePeripheral = self.peripheral
            self.clearConnectionState()
            self.updateConnectionState(.disconnected)
            if let stalePeripheral, stalePeripheral.state != .disconnected {
                self.centralManager.cancelPeripheralConnection(stalePeripheral)
            }
            self.scheduleReconnect()
        }
    }

    private func markReadyIfPossible() {
        guard eventSubscriptionReady,
              stateSubscriptionReady,
              let peripheral,
              peripheral.state == .connected else { return }
        defaults.set(peripheral.identifier.uuidString, forKey: Self.peripheralDefaultsKey)
        connectionSetupTimeoutTask?.cancel()
        connectionSetupTimeoutTask = nil
        reconnectDelaySeconds = 1
        updateConnectionState(.connected)
        logger.info("Fuji encrypted GATT transport ready")
    }

    private func handleDisconnected(error: Error?) {
        if let error {
            logger.error("Fuji disconnected: \(error.localizedDescription, privacy: .public)")
        }
        failAllWrites(with: CoreBluetoothTransportError.disconnected)
        clearConnectionState()
        updateConnectionState(.disconnected)
        scheduleReconnect()
    }

    private func clearConnectionState() {
        peripheral = nil
        resetCharacteristics()
        session.resetConnection()
        scanTimeoutTask?.cancel()
        scanTimeoutTask = nil
        connectionSetupTimeoutTask?.cancel()
        connectionSetupTimeoutTask = nil
        inboundTimeoutTask?.cancel()
        inboundTimeoutTask = nil
        inboundDeadlineMS = nil
        delayedWriteTask?.cancel()
        delayedWriteTask = nil
    }

    private func resetCharacteristics() {
        commandCharacteristic = nil
        eventCharacteristic = nil
        stateCharacteristic = nil
        eventSubscriptionReady = false
        stateSubscriptionReady = false
    }

    private func scheduleReconnect() {
        guard wantsConnection, centralManager.state == .poweredOn else { return }
        reconnectTask?.cancel()
        let delay = reconnectDelaySeconds
        reconnectDelaySeconds = min(delay * 2, Self.maximumReconnectDelaySeconds)
        metrics.reconnectCount += 1
        logMetrics(reason: "reconnect scheduled in \(delay)s")
        reconnectTask = Task { [weak self] in
            try? await Task.sleep(for: .seconds(delay))
            guard !Task.isCancelled else { return }
            self?.startConnectionAttempt()
        }
    }

    private func updateConnectionState(_ state: DeviceConnectionState) {
        guard connectionState != state else { return }
        connectionState = state
        eventContinuation.yield(.connectionChanged(state))
    }

    private func handleIncoming(_ data: Data) {
        let nowMS = monotonicMilliseconds()
        do {
            if let message = try session.accept(data, nowMS: nowMS) {
                updateInboundTimeout()
                if case .stateSnapshot(let snapshot) = message.payload {
                    eventContinuation.yield(.stateSnapshot(snapshot))
                } else {
                    eventContinuation.yield(.message(message))
                }
            } else {
                updateInboundTimeout()
            }
        } catch {
            updateInboundTimeout()
            let code = protocolErrorCode(for: error)
            metrics.protocolErrorCount += 1
            logger.error("Rejected Fuji frame: \(code.rawValue, privacy: .public)")
            logMetrics(reason: "protocol rejection")
            sendProtocolError(code)
        }
    }

    private func updateInboundTimeout() {
        let deadline = session.transferDeadlineMS
        guard deadline != inboundDeadlineMS else { return }
        inboundTimeoutTask?.cancel()
        inboundDeadlineMS = deadline
        guard let deadline else {
            inboundTimeoutTask = nil
            return
        }
        let nowMS = monotonicMilliseconds()
        let delayMS = deadline > nowMS ? deadline - nowMS : 0
        inboundTimeoutTask = Task { [weak self] in
            try? await Task.sleep(for: .milliseconds(delayMS))
            guard !Task.isCancelled, let self else { return }
            if self.session.expireTransferIfNeeded(at: self.monotonicMilliseconds()) {
                self.inboundDeadlineMS = nil
                self.metrics.protocolErrorCount += 1
                self.metrics.reassemblyTimeoutCount += 1
                self.logger.error("Fuji frame reassembly timed out")
                self.logMetrics(reason: "reassembly timeout")
                self.sendProtocolError(.expired)
            }
        }
    }

    private func protocolErrorCode(for error: Error) -> FujiProtocolErrorCode {
        if case FujiMessageValidationError.protocolError(let code) = error {
            return code
        }
        if case FujiFrameError.timedOut = error {
            return .expired
        }
        return .fragmentError
    }

    private func sendProtocolError(_ code: FujiProtocolErrorCode) {
        guard connectionState == .connected else { return }
        Task { [weak self] in
            try? await self?.send(
                .protocolError(code, message: "Device message rejected by Fuji protocol v1")
            )
        }
    }

    private func beginNextWriteIfNeeded(
        peripheral: CBPeripheral,
        characteristic: CBCharacteristic
    ) {
        guard activeWrite == nil, !queuedWrites.isEmpty else { return }
        activeWrite = queuedWrites.removeFirst()
        guard let activeWrite else { return }
        writeTimeoutTask?.cancel()
        writeTimeoutTask = Task { [weak self] in
            try? await Task.sleep(for: activeWrite.timeout)
            guard !Task.isCancelled else { return }
            self?.expireWrite(id: activeWrite.id)
        }
        writeCurrentFrame(peripheral: peripheral, characteristic: characteristic)
    }

    private func writeCurrentFrame(peripheral: CBPeripheral, characteristic: CBCharacteristic) {
        guard let activeWrite, activeWrite.nextFrameIndex < activeWrite.frames.count else { return }
        peripheral.writeValue(
            activeWrite.frames[activeWrite.nextFrameIndex],
            for: characteristic,
            type: .withResponse
        )
    }

    private func expireWrite(id: UUID) {
        guard activeWrite?.id == id else { return }
        logger.error("Fuji write response timed out")
        abortConnection(with: CoreBluetoothTransportError.writeTimedOut)
    }

    private func abortConnection(with error: Error) {
        let connectedPeripheral = peripheral
        failAllWrites(with: error)
        clearConnectionState()
        updateConnectionState(.disconnected)
        if let connectedPeripheral, connectedPeripheral.state != .disconnected {
            centralManager.cancelPeripheralConnection(connectedPeripheral)
        } else {
            scheduleReconnect()
        }
    }

    private func finishActiveWrite(_ result: Result<Void, Error>) {
        guard let completed = activeWrite else { return }
        activeWrite = nil
        delayedWriteTask?.cancel()
        delayedWriteTask = nil
        writeTimeoutTask?.cancel()
        writeTimeoutTask = nil
        completed.continuation.resume(with: result)
        if let peripheral, let commandCharacteristic, connectionState == .connected {
            beginNextWriteIfNeeded(peripheral: peripheral, characteristic: commandCharacteristic)
        }
    }

    private func failAllWrites(with error: Error) {
        delayedWriteTask?.cancel()
        delayedWriteTask = nil
        writeTimeoutTask?.cancel()
        writeTimeoutTask = nil
        if let activeWrite {
            activeWrite.continuation.resume(throwing: error)
            self.activeWrite = nil
        }
        let queued = queuedWrites
        queuedWrites.removeAll()
        for write in queued {
            write.continuation.resume(throwing: error)
        }
    }

    private func monotonicMilliseconds() -> UInt64 {
        DispatchTime.now().uptimeNanoseconds / 1_000_000
    }

    private func logMetrics(reason: String) {
        logger.info(
            "metrics reason=\(reason, privacy: .public) rssi=\(self.metrics.lastRSSI ?? 0) reconnects=\(self.metrics.reconnectCount) protocol_errors=\(self.metrics.protocolErrorCount) reassembly_timeouts=\(self.metrics.reassemblyTimeoutCount) queue_overflows=\(self.metrics.queueOverflowCount)"
        )
    }
}

extension CoreBluetoothDeviceTransport: CBCentralManagerDelegate {
    func centralManagerDidUpdateState(_ central: CBCentralManager) {
        switch central.state {
        case .poweredOn:
            if wantsConnection {
                startConnectionAttempt()
            }
        case .unknown:
            break
        case .resetting, .unsupported, .unauthorized, .poweredOff:
            isScanning = false
            scanTimeoutTask?.cancel()
            scanTimeoutTask = nil
            failAllWrites(with: CoreBluetoothTransportError.bluetoothUnavailable)
            clearConnectionState()
            updateConnectionState(.disconnected)
        @unknown default:
            updateConnectionState(.disconnected)
        }
    }

    func centralManager(_ central: CBCentralManager, willRestoreState dict: [String: Any]) {
        guard let restored = dict[CBCentralManagerRestoredStatePeripheralsKey] as? [CBPeripheral],
              let selected = restored.first(where: {
                  boundPeripheralID == nil || $0.identifier == boundPeripheralID
              }) else { return }
        wantsConnection = true
        updateConnectionState(.connecting)
        connect(to: selected)
    }

    func centralManager(
        _ central: CBCentralManager,
        didDiscover peripheral: CBPeripheral,
        advertisementData: [String: Any],
        rssi RSSI: NSNumber
    ) {
        guard wantsConnection,
              boundPeripheralID == nil || peripheral.identifier == boundPeripheralID,
              self.peripheral == nil else {
            return
        }
        metrics.lastRSSI = RSSI.intValue
        logger.info("Discovered Fuji RSSI=\(RSSI.intValue)")
        connect(to: peripheral)
    }

    func centralManager(_ central: CBCentralManager, didConnect peripheral: CBPeripheral) {
        guard wantsConnection, self.peripheral?.identifier == peripheral.identifier else {
            central.cancelPeripheralConnection(peripheral)
            return
        }
        logger.info("Connected to Fuji peripheral")
        discoverService(on: peripheral)
    }

    func centralManager(
        _ central: CBCentralManager,
        didFailToConnect peripheral: CBPeripheral,
        error: Error?
    ) {
        guard self.peripheral == nil || self.peripheral?.identifier == peripheral.identifier else {
            return
        }
        handleDisconnected(error: error)
    }

    func centralManager(
        _ central: CBCentralManager,
        didDisconnectPeripheral peripheral: CBPeripheral,
        error: Error?
    ) {
        guard self.peripheral == nil || self.peripheral?.identifier == peripheral.identifier else {
            return
        }
        handleDisconnected(error: error)
    }
}

extension CoreBluetoothDeviceTransport: CBPeripheralDelegate {
    func peripheral(_ peripheral: CBPeripheral, didModifyServices invalidatedServices: [CBService]) {
        guard self.peripheral?.identifier == peripheral.identifier,
              invalidatedServices.contains(where: { $0.uuid == Self.serviceUUID }) else { return }
        failAllWrites(with: CoreBluetoothTransportError.operationFailed("GATT service changed"))
        session.resetConnection()
        updateConnectionState(.connecting)
        logger.info("Fuji GATT service changed; rediscovering")
        discoverService(on: peripheral)
    }

    func peripheral(_ peripheral: CBPeripheral, didDiscoverServices error: Error?) {
        guard self.peripheral?.identifier == peripheral.identifier else { return }
        if let error {
            centralManager.cancelPeripheralConnection(peripheral)
            logger.error("Service discovery failed: \(error.localizedDescription, privacy: .public)")
            return
        }
        guard let service = peripheral.services?.first(where: { $0.uuid == Self.serviceUUID }) else {
            centralManager.cancelPeripheralConnection(peripheral)
            logger.error("Fuji service missing")
            return
        }
        peripheral.discoverCharacteristics(
            [Self.commandUUID, Self.eventUUID, Self.stateUUID],
            for: service
        )
    }

    func peripheral(
        _ peripheral: CBPeripheral,
        didDiscoverCharacteristicsFor service: CBService,
        error: Error?
    ) {
        guard self.peripheral?.identifier == peripheral.identifier else { return }
        if let error {
            centralManager.cancelPeripheralConnection(peripheral)
            logger.error("Characteristic discovery failed: \(error.localizedDescription, privacy: .public)")
            return
        }
        for characteristic in service.characteristics ?? [] {
            switch characteristic.uuid {
            case Self.commandUUID: commandCharacteristic = characteristic
            case Self.eventUUID: eventCharacteristic = characteristic
            case Self.stateUUID: stateCharacteristic = characteristic
            default: break
            }
        }
        guard commandCharacteristic != nil,
              let eventCharacteristic,
              let stateCharacteristic else {
            centralManager.cancelPeripheralConnection(peripheral)
            logger.error("Fuji characteristics incomplete")
            return
        }
        guard commandCharacteristic?.properties.contains(.write) == true,
              eventCharacteristic.properties.contains(.indicate),
              stateCharacteristic.properties.contains(.read),
              stateCharacteristic.properties.contains(.notify) else {
            centralManager.cancelPeripheralConnection(peripheral)
            logger.error("Fuji characteristic properties invalid")
            return
        }
        eventSubscriptionReady = eventCharacteristic.isNotifying
        stateSubscriptionReady = stateCharacteristic.isNotifying
        if !eventSubscriptionReady {
            peripheral.setNotifyValue(true, for: eventCharacteristic)
        }
        if !stateSubscriptionReady {
            peripheral.setNotifyValue(true, for: stateCharacteristic)
        }
        markReadyIfPossible()
    }

    func peripheral(
        _ peripheral: CBPeripheral,
        didUpdateNotificationStateFor characteristic: CBCharacteristic,
        error: Error?
    ) {
        guard self.peripheral?.identifier == peripheral.identifier else { return }
        if let error {
            logger.error("Subscription failed: \(error.localizedDescription, privacy: .public)")
            centralManager.cancelPeripheralConnection(peripheral)
            return
        }
        if characteristic.uuid == Self.eventUUID {
            eventSubscriptionReady = characteristic.isNotifying
        } else if characteristic.uuid == Self.stateUUID {
            stateSubscriptionReady = characteristic.isNotifying
        }
        markReadyIfPossible()
    }

    func peripheral(
        _ peripheral: CBPeripheral,
        didUpdateValueFor characteristic: CBCharacteristic,
        error: Error?
    ) {
        guard self.peripheral?.identifier == peripheral.identifier else { return }
        if let error {
            logger.error("Characteristic update failed: \(error.localizedDescription, privacy: .public)")
            return
        }
        guard characteristic.uuid == Self.eventUUID || characteristic.uuid == Self.stateUUID,
              let value = characteristic.value else { return }
        handleIncoming(value)
    }

    func peripheral(
        _ peripheral: CBPeripheral,
        didWriteValueFor characteristic: CBCharacteristic,
        error: Error?
    ) {
        guard self.peripheral?.identifier == peripheral.identifier else { return }
        guard characteristic.uuid == Self.commandUUID, var activeWrite else { return }
        if let error {
            abortConnection(
                with: CoreBluetoothTransportError.operationFailed(error.localizedDescription)
            )
            return
        }
        activeWrite.nextFrameIndex += 1
        self.activeWrite = activeWrite
        if activeWrite.nextFrameIndex >= activeWrite.frames.count {
            finishActiveWrite(.success(()))
        } else if let delay = activeWrite.interFrameDelay {
            let writeID = activeWrite.id
            delayedWriteTask?.cancel()
            delayedWriteTask = Task { [weak self] in
                try? await Task.sleep(for: delay)
                guard !Task.isCancelled,
                      let self,
                      self.activeWrite?.id == writeID,
                      let peripheral = self.peripheral,
                      let commandCharacteristic = self.commandCharacteristic else { return }
                self.writeCurrentFrame(
                    peripheral: peripheral,
                    characteristic: commandCharacteristic
                )
            }
        } else if let commandCharacteristic {
            writeCurrentFrame(peripheral: peripheral, characteristic: commandCharacteristic)
        }
    }
}

#if DEBUG
extension CoreBluetoothDeviceTransport: DeviceTransportDiagnostics {
    func sendIncompleteTransferForTimeout(_ message: FujiMessage) async throws {
        let frames = try makeFrames(for: message, maximumWriteValueLength: 20)
        guard frames.count > 2 else {
            throw CoreBluetoothTransportError.operationFailed("Diagnostic message did not fragment")
        }
        try await enqueue(
            frames: Array(frames.prefix(2)),
            interFrameDelay: .milliseconds(5_100),
            timeout: .seconds(7)
        )
    }
}
#endif
