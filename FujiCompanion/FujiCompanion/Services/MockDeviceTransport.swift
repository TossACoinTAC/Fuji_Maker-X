import Foundation

@MainActor
final class MockDeviceTransport: DeviceTransport {
    private(set) var connectionState: DeviceConnectionState = .disconnected
    let events: AsyncStream<DeviceTransportEvent>
    private let continuation: AsyncStream<DeviceTransportEvent>.Continuation
    private(set) var sentMessages: [FujiMessage] = []
    private let connectsSuccessfully: Bool
    private var sentMessageIDs: [UUID] = []

    init(connectsSuccessfully: Bool = true) {
        self.connectsSuccessfully = connectsSuccessfully
        var capturedContinuation: AsyncStream<DeviceTransportEvent>.Continuation?
        events = AsyncStream { continuation in
            capturedContinuation = continuation
        }
        continuation = capturedContinuation!
    }

    func connect() {
        connectionState = .connecting
        continuation.yield(.connectionChanged(.connecting))
        connectionState = connectsSuccessfully ? .connected : .disconnected
        continuation.yield(.connectionChanged(connectionState))
    }

    func disconnect() {
        simulateConnectionState(.disconnected)
    }

    func send(_ message: FujiMessage) async throws {
        guard connectionState == .connected else {
            throw MockTransportError.disconnected
        }
        sentMessages.append(message)
        if sentMessageIDs.contains(message.messageID) {
            continuation.yield(
                .message(
                    .protocolError(
                        .duplicate,
                        message: "Mock rejected duplicate message",
                        direction: .deviceToPhone
                    )
                )
            )
            return
        }
        sentMessageIDs.append(message.messageID)
        if sentMessageIDs.count > 128 {
            sentMessageIDs.removeFirst(sentMessageIDs.count - 128)
        }
        if message.type == .actionResult || message.type == .cancel {
            continuation.yield(
                .stateSnapshot(
                    .init(
                        deviceState: .idle,
                        activeRequestID: nil,
                        earphonesVerified: false,
                        firmwareVersion: "mock",
                        capabilities: ["food_search", "start_navigation"]
                    )
                )
            )
        }
    }

    func simulateFoodRequest() {
        guard connectionState == .connected else { return }
        continuation.yield(.message(.foodSearch()))
    }

    func simulateSnapshot(_ snapshot: FujiStateSnapshotPayload) {
        guard connectionState == .connected else { return }
        continuation.yield(.stateSnapshot(snapshot))
    }

    func simulateConnectionState(_ state: DeviceConnectionState) {
        connectionState = state
        continuation.yield(.connectionChanged(state))
    }
}

#if DEBUG
extension MockDeviceTransport: DeviceTransportDiagnostics {
    func sendIncompleteTransferForTimeout(_ message: FujiMessage) async throws {
        guard connectionState == .connected else {
            throw MockTransportError.disconnected
        }
        sentMessages.append(message)
        continuation.yield(
            .message(
                .protocolError(
                    .expired,
                    message: "Mock reassembly timed out",
                    direction: .deviceToPhone
                )
            )
        )
    }
}
#endif

enum MockTransportError: LocalizedError {
    case disconnected

    var errorDescription: String? { "模拟设备未连接" }
}
