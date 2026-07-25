import Foundation

enum FujiState: String, Codable, CaseIterable {
    case idle = "IDLE"
    case listening = "LISTENING"
    case thinking = "THINKING"
    case clarifying = "CLARIFYING"
    case confirming = "CONFIRMING"
    case acting = "ACTING"
    case success = "SUCCESS"
    case error = "ERROR"
    case quiet = "QUIET"
    case muted = "MUTED"
    case disconnected = "DISCONNECTED"
}

enum FujiIntent: String, Codable {
    case foodSearch = "food_search"
    case foodSelection = "food_selection"
    case startNavigation = "start_navigation"
    case cancel
    case status
}

struct FujiEnvelope: Codable, Equatable, Identifiable {
    static let supportedVersion = 1

    let version: Int
    let requestID: UUID
    let intent: FujiIntent
    let state: FujiState
    let parameters: [String: String]
    let requiresConfirmation: Bool
    let createdAt: Date
    let expiresAt: Date

    var id: UUID { requestID }

    static func foodSearch(
        requestID: UUID = UUID(),
        now: Date = .now,
        lifetime: TimeInterval = 120
    ) -> FujiEnvelope {
        FujiEnvelope(
            version: supportedVersion,
            requestID: requestID,
            intent: .foodSearch,
            state: .clarifying,
            parameters: [:],
            requiresConfirmation: false,
            createdAt: now,
            expiresAt: now.addingTimeInterval(lifetime)
        )
    }

    static func navigationResult(
        requestID: UUID,
        restaurantName: String,
        succeeded: Bool,
        now: Date = .now
    ) -> FujiEnvelope {
        FujiEnvelope(
            version: supportedVersion,
            requestID: requestID,
            intent: .startNavigation,
            state: succeeded ? .success : .error,
            parameters: [
                "restaurant_name": restaurantName,
                "status": succeeded ? "completed" : "failed"
            ],
            requiresConfirmation: false,
            createdAt: now,
            expiresAt: now.addingTimeInterval(30)
        )
    }
}

enum FujiEnvelopeValidationError: LocalizedError, Equatable {
    case unsupportedVersion(Int)
    case expired
    case duplicate

    var errorDescription: String? {
        switch self {
        case .unsupportedVersion:
            "设备协议版本暂不受支持"
        case .expired:
            "设备请求已经过期"
        case .duplicate:
            "设备请求已经处理"
        }
    }
}

@MainActor
final class FujiEnvelopeValidator {
    private var seenRequestIDs = Set<UUID>()

    func validate(_ envelope: FujiEnvelope, now: Date = .now) throws {
        guard envelope.version == FujiEnvelope.supportedVersion else {
            throw FujiEnvelopeValidationError.unsupportedVersion(envelope.version)
        }
        guard envelope.expiresAt > now else {
            throw FujiEnvelopeValidationError.expired
        }
        guard seenRequestIDs.insert(envelope.requestID).inserted else {
            throw FujiEnvelopeValidationError.duplicate
        }
    }

    func reset() {
        seenRequestIDs.removeAll()
    }
}
