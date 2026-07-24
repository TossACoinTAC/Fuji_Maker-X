import Foundation

@MainActor
final class MockDeviceTransport: DeviceTransport {
    private(set) var connectionState: DeviceConnectionState = .disconnected
    let messages: AsyncStream<FujiEnvelope>
    private let continuation: AsyncStream<FujiEnvelope>.Continuation
    private(set) var sentMessages: [FujiEnvelope] = []
    private let connectsSuccessfully: Bool

    init(connectsSuccessfully: Bool = true) {
        self.connectsSuccessfully = connectsSuccessfully
        var capturedContinuation: AsyncStream<FujiEnvelope>.Continuation?
        messages = AsyncStream { continuation in
            capturedContinuation = continuation
        }
        continuation = capturedContinuation!
    }

    func connect() {
        connectionState = .connecting
        connectionState = connectsSuccessfully ? .connected : .disconnected
    }

    func disconnect() {
        connectionState = .disconnected
    }

    func send(_ envelope: FujiEnvelope) async throws {
        guard connectionState == .connected else {
            throw MockTransportError.disconnected
        }
        sentMessages.append(envelope)
    }

    func simulateFoodRequest(now: Date = .now) {
        guard connectionState == .connected else { return }
        continuation.yield(.foodSearch(now: now))
    }
}

enum MockTransportError: LocalizedError {
    case disconnected

    var errorDescription: String? { "模拟设备未连接" }
}
