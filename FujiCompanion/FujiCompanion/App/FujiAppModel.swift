import Foundation
import Observation

enum AppTab: Hashable {
    case fuji
    case activity
    case settings
}

enum FoodFlowStage: Equatable {
    case idle
    case criteria
    case searching
    case results
    case confirming
    case navigating
    case completed
    case failed
}

enum SessionEventKind: Equatable {
    case device
    case search
    case action
    case error

    var symbol: String {
        switch self {
        case .device: "dot.radiowaves.left.and.right"
        case .search: "fork.knife"
        case .action: "arrow.triangle.turn.up.right.diamond"
        case .error: "exclamationmark.triangle"
        }
    }
}

struct SessionEvent: Identifiable {
    let id = UUID()
    let timestamp: Date
    let title: String
    let detail: String
    let kind: SessionEventKind
}

#if DEBUG
private enum BLETransportDiagnosticError: LocalizedError {
    case unavailable
    case disconnected
    case missingSnapshot
    case missingProtocolError(FujiProtocolErrorCode)
    case reconnectFailed

    var errorDescription: String? {
        switch self {
        case .unavailable: "当前传输不支持 BLE 诊断"
        case .disconnected: "Fuji 尚未连接"
        case .missingSnapshot: "设备未回传状态快照"
        case .missingProtocolError(let code): "设备未回传 \(code.rawValue)"
        case .reconnectFailed: "主动断线后未能恢复连接"
        }
    }
}
#endif

@Observable
@MainActor
final class FujiAppModel {
    var settings: AppSettings
    let audioRouteMonitor: AudioRouteMonitor

    var selectedTab: AppTab = .fuji
    private(set) var connectionState: DeviceConnectionState = .disconnected
    private(set) var deviceState: FujiState = .disconnected
    private(set) var flowStage: FoodFlowStage = .idle
    var criteria = RestaurantSearchCriteria()
    private(set) var recommendations: [RestaurantRecommendation] = []
    private(set) var pendingRecommendation: RestaurantRecommendation?
    private(set) var activity: [SessionEvent] = []
    private(set) var statusMessage = "等待 Fuji 的请求"
    private(set) var currentOrigin: GeoCoordinate?
    private(set) var activeRequestID: UUID?
    var presentedError: String?
#if DEBUG
    private(set) var isBLETransportTestRunning = false
    private(set) var bleTransportTestResult: (succeeded: Bool, message: String)?
#endif

    private let transport: DeviceTransport
    private let locationProvider: LocationProviding
    private let restaurantSearch: RestaurantSearching
    private let navigationLauncher: NavigationLaunching
    private let speechOutput: SpeechOutput
    private let validator = FujiMessageValidator()
    private var messageTask: Task<Void, Never>?
    private var started = false
    private var hasEstablishedConnection = false
    private var snapshotGeneration = 0
#if DEBUG
    private var diagnosticProtocolError: FujiProtocolErrorCode?
#endif

    init(environment: AppEnvironment) {
        settings = environment.settings
        audioRouteMonitor = environment.audioRouteMonitor
        transport = environment.transport
        locationProvider = environment.locationProvider
        restaurantSearch = environment.restaurantSearch
        navigationLauncher = environment.navigationLauncher
        speechOutput = environment.speechOutput
    }

    var locationAuthorizationLabel: String {
        locationProvider.authorizationLabel
    }

    func start() {
        guard !started else { return }
        started = true
        transport.connect()
        connectionState = transport.connectionState
        deviceState = connectionState == .connected ? .idle : .disconnected

        messageTask = Task { [weak self, events = transport.events] in
            for await event in events {
                guard let self else { return }
                self.handle(event)
            }
        }
    }

    func simulateFoodRequest() {
        guard let mock = transport as? MockDeviceTransport else { return }
        mock.simulateFoodRequest()
    }

#if DEBUG
    func runBLETransportTest() async {
        guard !isBLETransportTestRunning else { return }
        guard connectionState == .connected else {
            show(BLETransportDiagnosticError.disconnected)
            return
        }
        guard let diagnostics = transport as? DeviceTransportDiagnostics else {
            show(BLETransportDiagnosticError.unavailable)
            return
        }

        isBLETransportTestRunning = true
        bleTransportTestResult = nil
        diagnosticProtocolError = nil
        statusMessage = "正在测试蓝牙链路"
        record("开始 BLE 链路测试", detail: "成功、重复、超时、取消、断线恢复", kind: .device)
        defer {
            diagnosticProtocolError = nil
            isBLETransportTestRunning = false
        }

        do {
            let requestID = UUID()
            let accepted = FujiMessage.actionResult(
                requestID: requestID,
                result: .init(action: .foodSearch, status: .accepted, message: "BLE diagnostic")
            )

            let successSnapshot = snapshotGeneration
            try await transport.send(accepted)
            guard await waitForDiagnostic(condition: {
                self.snapshotGeneration > successSnapshot
            }) else {
                throw BLETransportDiagnosticError.missingSnapshot
            }
            record("BLE 成功路径通过", detail: "写入已确认，设备回传完整快照", kind: .action)

            diagnosticProtocolError = nil
            try await transport.send(accepted)
            guard await waitForDiagnostic(condition: {
                self.diagnosticProtocolError == .duplicate
            }) else {
                throw BLETransportDiagnosticError.missingProtocolError(.duplicate)
            }
            record("BLE 重复路径通过", detail: "相同 message_id 被设备拒绝", kind: .action)

            diagnosticProtocolError = nil
            let timeoutMessage = FujiMessage.actionResult(
                requestID: UUID(),
                result: .init(action: .foodSearch, status: .accepted, message: "BLE timeout diagnostic")
            )
            try await diagnostics.sendIncompleteTransferForTimeout(timeoutMessage)
            guard await waitForDiagnostic(attempts: 30, condition: {
                self.diagnosticProtocolError == .expired
            }) else {
                throw BLETransportDiagnosticError.missingProtocolError(.expired)
            }
            record("BLE 超时路径通过", detail: "不完整 transfer 在 5 秒后过期", kind: .action)

            let cancelSnapshot = snapshotGeneration
            try await transport.send(.cancel(requestID: requestID, reason: "BLE diagnostic complete"))
            guard await waitForDiagnostic(condition: {
                self.snapshotGeneration > cancelSnapshot
            }) else {
                throw BLETransportDiagnosticError.missingSnapshot
            }
            record("BLE 取消路径通过", detail: "cancel 写入后设备回传快照", kind: .action)

            transport.disconnect()
            guard await waitForDiagnostic(condition: {
                self.connectionState == .disconnected
            }) else {
                throw BLETransportDiagnosticError.reconnectFailed
            }
            transport.connect()
            guard await waitForDiagnostic(attempts: 150, condition: {
                self.connectionState == .connected
            }) else {
                throw BLETransportDiagnosticError.reconnectFailed
            }

            statusMessage = "蓝牙链路测试通过"
            bleTransportTestResult = (
                true,
                "通过：成功、重复、超时、取消和断线恢复均符合预期"
            )
            record("BLE 链路测试通过", detail: "断线后已恢复加密订阅", kind: .action)
        } catch {
            let message = (error as? LocalizedError)?.errorDescription ?? error.localizedDescription
            statusMessage = "蓝牙链路测试失败"
            bleTransportTestResult = (false, "失败：\(message)")
            presentedError = message
            record("BLE 链路测试失败", detail: message, kind: .error)
        }
    }

    private func waitForDiagnostic(
        attempts: Int = 40,
        condition: @escaping @MainActor () -> Bool
    ) async -> Bool {
        for _ in 0..<attempts {
            if condition() { return true }
            try? await Task.sleep(for: .milliseconds(100))
        }
        return condition()
    }
#endif

    func searchRestaurants() async {
        guard settings.amapPrivacyAccepted else {
            presentedError = RestaurantSearchError.privacyConsentRequired.localizedDescription
            selectedTab = .settings
            record("搜索未开始", detail: "等待高德搜索隐私授权", kind: .error)
            return
        }

        flowStage = .searching
        deviceState = .thinking
        statusMessage = "正在查找附近餐馆"
        record(
            "开始附近搜索",
            detail: "范围 \(criteria.radiusMeters) 米，预算 ¥\(criteria.budgetRMB)",
            kind: .search
        )

        do {
            let coordinate = try await locationProvider.currentCoordinate()
            currentOrigin = coordinate
            let restaurants = try await restaurantSearch.search(near: coordinate, criteria: criteria)
            let ranked = RestaurantRanker.rank(restaurants, for: criteria)
            guard !ranked.isEmpty else { throw RestaurantSearchError.noResults }

            recommendations = ranked
            flowStage = .results
            deviceState = .idle
            statusMessage = "找到 \(ranked.count) 个合适选项"
            record(
                "推荐已就绪",
                detail: ranked.map(\.restaurant.name).joined(separator: "、"),
                kind: .search
            )

            if !settings.quietMode {
                let summary = ranked.enumerated()
                    .map { "第 \($0.offset + 1) 个，\($0.element.restaurant.name)，\($0.element.reason)" }
                    .joined(separator: "。")
                let outcome = await speechOutput.speak(
                    "Fuji 找到了 \(ranked.count) 个选择。\(summary)",
                    policy: settings.audioPolicy
                )
                recordSpeechOutcome(outcome)
            }
        } catch {
            show(error)
        }
    }

    func select(_ recommendation: RestaurantRecommendation) {
        pendingRecommendation = recommendation
        flowStage = .confirming
        deviceState = .confirming
        statusMessage = "等待确认导航"
        record(
            "已选择 \(recommendation.restaurant.name)",
            detail: "尚未打开外部地图",
            kind: .action
        )
    }

    func rejectNavigation() {
        pendingRecommendation = nil
        flowStage = .results
        deviceState = .idle
        statusMessage = "可以选择其他餐馆"
        record("已取消导航", detail: "没有执行外部动作", kind: .action)
    }

    func confirmNavigation() async {
        guard let recommendation = pendingRecommendation else { return }
        flowStage = .navigating
        deviceState = .acting
        statusMessage = "正在打开步行路线"

        let outcome = await navigationLauncher.launchWalkingRoute(
            to: recommendation.restaurant,
            from: currentOrigin
        )
        record(
            outcome.succeeded ? "导航已启动" : "导航启动失败",
            detail: outcome.message,
            kind: outcome.succeeded ? .action : .error
        )

        if let requestID = activeRequestID {
            let response = FujiMessage.actionResult(
                requestID: requestID,
                result: FujiActionResultPayload(
                    action: .startNavigation,
                    status: outcome.succeeded ? .succeeded : .failed,
                    navigationState: outcome.succeeded ? .launched : nil,
                    errorCode: outcome.succeeded ? nil : .mapLaunchFailed,
                    message: outcome.message
                )
            )
            try? await transport.send(response)
        }

        pendingRecommendation = nil
        flowStage = outcome.succeeded ? .completed : .failed
        deviceState = outcome.succeeded ? .success : .error
        statusMessage = outcome.message

        if !settings.quietMode {
            let speech = await speechOutput.speak(
                outcome.succeeded ? "路线已发送。" : "路线没有打开，请查看手机。",
                policy: settings.audioPolicy
            )
            recordSpeechOutcome(speech)
        }
    }

    func restartFoodFlow() {
        recommendations = []
        pendingRecommendation = nil
        flowStage = .criteria
        deviceState = .clarifying
        statusMessage = "告诉 Fuji 这次的条件"
    }

    func acceptAMapPrivacy() {
        settings.amapPrivacyAccepted = true
        record("已启用高德搜索", detail: "仅在主动搜索时调用", kind: .action)
    }

    func withdrawAMapPrivacy() {
        settings.amapPrivacyAccepted = false
        restaurantSearch.withdrawPrivacyConsent()
        record("已停用高德搜索", detail: "后续搜索不会调用高德 SDK", kind: .action)
    }

    func deleteSessionData() {
        speechOutput.stop()
        activity.removeAll()
        recommendations.removeAll()
        pendingRecommendation = nil
        currentOrigin = nil
        activeRequestID = nil
        flowStage = .idle
        deviceState = connectionState == .connected ? .idle : .disconnected
        statusMessage = "本次会话数据已清除"
    }

    func clearPresentedError() {
        presentedError = nil
    }

    private func handle(_ event: DeviceTransportEvent) {
        switch event {
        case .connectionChanged(let state):
            let previousState = connectionState
            connectionState = state
            if state == .disconnected {
                deviceState = .disconnected
                validator.reset()
                activeRequestID = nil
            } else if state == .connected, deviceState == .disconnected {
                deviceState = .idle
            }
            guard state != previousState else { return }
            switch state {
            case .connecting:
                break
            case .connected:
                hasEstablishedConnection = true
                record("Fuji 已连接", detail: "加密蓝牙链路和状态订阅已就绪", kind: .device)
            case .disconnected:
                if hasEstablishedConnection {
                    hasEstablishedConnection = false
                    record("Fuji 已断开", detail: "等待蓝牙重连并清除未完成事务", kind: .error)
                }
            }
        case .stateSnapshot(let snapshot):
            snapshotGeneration += 1
            activeRequestID = snapshot.activeRequestID
            deviceState = appState(from: snapshot.deviceState)
        case .message(let message):
            handle(message)
        }
    }

    private func handle(_ message: FujiMessage) {
        do {
            _ = try validator.validate(message, receivedAtMS: monotonicMilliseconds())
            switch message.payload {
            case .actionRequest(.foodSearch(let payload)):
                activeRequestID = message.requestID
                if let radius = payload.radiusM { criteria.radiusMeters = radius }
                if let budget = payload.budgetRMB { criteria.budgetRMB = budget }
                if let avoid = payload.avoidTerms { criteria.avoidText = avoid.joined(separator: "、") }
                recommendations = []
                pendingRecommendation = nil
                flowStage = .criteria
                deviceState = .clarifying
                statusMessage = "告诉 Fuji 这次的条件"
                selectedTab = .fuji
                record("收到吃什么请求", detail: "请求已通过协议校验", kind: .device)
            case .cancel(let payload):
                if activeRequestID == payload.targetRequestID {
                    deleteSessionData()
                }
            case .protocolError(let payload):
#if DEBUG
                diagnosticProtocolError = payload.errorCode
                let expectedDiagnostic = isBLETransportTestRunning &&
                    (payload.errorCode == .duplicate || payload.errorCode == .expired)
#else
                let expectedDiagnostic = false
#endif
                record(
                    expectedDiagnostic ? "设备协议测试响应" : "设备协议错误",
                    detail: "\(payload.errorCode.rawValue)：\(payload.message)",
                    kind: expectedDiagnostic ? .device : .error
                )
            default:
                record("收到设备消息", detail: message.type.rawValue, kind: .device)
            }
        } catch {
            show(error)
        }
    }

    private func appState(from state: FujiDeviceState) -> FujiState {
        switch state {
        case .idle: .idle
        case .listening: .listening
        case .thinking: .thinking
        case .speaking: .acting
        case .success: .success
        case .error: .error
        case .offline: .disconnected
        case .muted: .muted
        }
    }

    private func monotonicMilliseconds() -> UInt64 {
        DispatchTime.now().uptimeNanoseconds / 1_000_000
    }

    private func recordSpeechOutcome(_ outcome: SpeechOutcome) {
        switch outcome {
        case .spoken:
            record("私密播报完成", detail: audioRouteMonitor.routeName, kind: .action)
        case .privateRouteUnavailable:
            record("未进行语音播报", detail: "未确认耳机路由", kind: .device)
        case .routeLost:
            record("播报已停止", detail: "耳机路由已断开", kind: .error)
        case .failed(let reason):
            record("播报失败", detail: reason, kind: .error)
        }
    }

    private func show(_ error: Error) {
        let message = (error as? LocalizedError)?.errorDescription ?? error.localizedDescription
        presentedError = message
        flowStage = .failed
        deviceState = .error
        statusMessage = message
        record("操作失败", detail: message, kind: .error)
    }

    private func record(_ title: String, detail: String, kind: SessionEventKind) {
        activity.insert(
            SessionEvent(timestamp: .now, title: title, detail: detail, kind: kind),
            at: 0
        )
    }
}
