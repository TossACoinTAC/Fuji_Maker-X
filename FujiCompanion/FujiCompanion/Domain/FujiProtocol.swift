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

enum FujiDirection: String, Codable, Equatable {
    case deviceToPhone = "device_to_phone"
    case phoneToDevice = "phone_to_device"
}

enum FujiMessageType: String, Codable, Equatable {
    case capabilityReport = "capability_report"
    case actionRequest = "action_request"
    case actionResult = "action_result"
    case cancel
    case stateSnapshot = "state_snapshot"
    case protocolError = "protocol_error"
}

enum FujiAction: String, Codable, Equatable {
    case foodSearch = "food_search"
    case startNavigation = "start_navigation"
}

enum FujiResultStatus: String, Codable, Equatable {
    case accepted
    case inProgress = "in_progress"
    case needsConfirmation = "needs_confirmation"
    case succeeded
    case failed
    case cancelled
    case expired
}

enum FujiProtocolErrorCode: String, Codable, Equatable, Error {
    case invalidJSON = "invalid_json"
    case unsupportedVersion = "unsupported_version"
    case unknownType = "unknown_type"
    case invalidPayload = "invalid_payload"
    case payloadTooLarge = "payload_too_large"
    case fragmentError = "fragment_error"
    case duplicate
    case expired
    case cancelled
    case disconnected
    case bluetoothUnauthorized = "bluetooth_unauthorized"
    case locationPermissionDenied = "location_permission_denied"
    case locationUnavailable = "location_unavailable"
    case noResults = "no_results"
    case privateRouteUnavailable = "private_route_unavailable"
    case routeLost = "route_lost"
    case foregroundRequired = "foreground_required"
    case mapLaunchFailed = "map_launch_failed"
    case internalError = "internal_error"
}

enum FujiDeviceState: String, Codable, Equatable {
    case idle
    case listening
    case thinking
    case speaking
    case success
    case error
    case offline
    case muted
}

enum FujiNavigationState: String, Codable, Equatable {
    case ready = "navigation_ready"
    case launched = "navigation_launched"
}

struct FujiCapabilityPayload: Codable, Equatable {
    let protocolVersions: [Int]
    let firmwareVersion: String
    let buildID: String
    let maxPayloadBytes: Int
    let capabilities: [String]

    enum CodingKeys: String, CodingKey {
        case protocolVersions = "protocol_versions"
        case firmwareVersion = "firmware_version"
        case buildID = "build_id"
        case maxPayloadBytes = "max_payload_bytes"
        case capabilities
    }
}

struct FujiFoodSearchCriteriaPayload: Codable, Equatable {
    var radiusM: Int?
    var budgetRMB: Int?
    var avoidTerms: [String]?

    init(radiusM: Int? = nil, budgetRMB: Int? = nil, avoidTerms: [String]? = nil) {
        self.radiusM = radiusM
        self.budgetRMB = budgetRMB
        self.avoidTerms = avoidTerms
    }

    enum CodingKeys: String, CodingKey {
        case radiusM = "radius_m"
        case budgetRMB = "budget_rmb"
        case avoidTerms = "avoid_terms"
    }
}

enum FujiActionRequestPayload: Equatable {
    case foodSearch(criteria: FujiFoodSearchCriteriaPayload)
    case startNavigation(candidateID: String, parentRequestID: UUID, confirmed: Bool)

    var action: FujiAction {
        switch self {
        case .foodSearch: .foodSearch
        case .startNavigation: .startNavigation
        }
    }
}

extension FujiActionRequestPayload: Codable {
    private enum CodingKeys: String, CodingKey {
        case action
        case criteria
        case candidateID = "candidate_id"
        case parentRequestID = "parent_request_id"
        case confirmation
    }

    init(from decoder: Decoder) throws {
        let container = try decoder.container(keyedBy: CodingKeys.self)
        let action = try container.decode(FujiAction.self, forKey: .action)
        switch action {
        case .foodSearch:
            self = .foodSearch(criteria: try container.decode(FujiFoodSearchCriteriaPayload.self, forKey: .criteria))
        case .startNavigation:
            let confirmation = try container.decode(String.self, forKey: .confirmation)
            guard confirmation == "confirmed" else {
                throw DecodingError.dataCorruptedError(
                    forKey: .confirmation,
                    in: container,
                    debugDescription: "Navigation requires explicit confirmation"
                )
            }
            self = .startNavigation(
                candidateID: try container.decode(String.self, forKey: .candidateID),
                parentRequestID: try container.decode(UUID.self, forKey: .parentRequestID),
                confirmed: true
            )
        }
    }

    func encode(to encoder: Encoder) throws {
        var container = encoder.container(keyedBy: CodingKeys.self)
        try container.encode(action, forKey: .action)
        switch self {
        case .foodSearch(let criteria):
            try container.encode(criteria, forKey: .criteria)
        case .startNavigation(let candidateID, let parentRequestID, let confirmed):
            guard confirmed else {
                throw EncodingError.invalidValue(
                    confirmed,
                    .init(codingPath: encoder.codingPath, debugDescription: "Navigation requires explicit confirmation")
                )
            }
            try container.encode(candidateID, forKey: .candidateID)
            try container.encode(parentRequestID, forKey: .parentRequestID)
            try container.encode("confirmed", forKey: .confirmation)
        }
    }
}

struct FujiCandidatePayload: Codable, Equatable {
    let candidateID: String
    let name: String
    let reason: String

    enum CodingKeys: String, CodingKey {
        case candidateID = "candidate_id"
        case name
        case reason
    }
}

struct FujiActionResultPayload: Codable, Equatable {
    let action: FujiAction
    let status: FujiResultStatus
    var candidates: [FujiCandidatePayload]?
    var navigationState: FujiNavigationState?
    var errorCode: FujiProtocolErrorCode?
    var message: String?

    init(
        action: FujiAction,
        status: FujiResultStatus,
        candidates: [FujiCandidatePayload]? = nil,
        navigationState: FujiNavigationState? = nil,
        errorCode: FujiProtocolErrorCode? = nil,
        message: String? = nil
    ) {
        self.action = action
        self.status = status
        self.candidates = candidates
        self.navigationState = navigationState
        self.errorCode = errorCode
        self.message = message
    }

    enum CodingKeys: String, CodingKey {
        case action
        case status
        case candidates
        case navigationState = "navigation_state"
        case errorCode = "error_code"
        case message
    }
}

struct FujiCancelPayload: Codable, Equatable {
    let targetRequestID: UUID
    var reason: String?

    enum CodingKeys: String, CodingKey {
        case targetRequestID = "target_request_id"
        case reason
    }
}

struct FujiStateSnapshotPayload: Codable, Equatable {
    let deviceState: FujiDeviceState
    var activeRequestID: UUID?
    let earphonesVerified: Bool
    let firmwareVersion: String
    let capabilities: [String]

    enum CodingKeys: String, CodingKey {
        case deviceState = "device_state"
        case activeRequestID = "active_request_id"
        case earphonesVerified = "earphones_verified"
        case firmwareVersion = "firmware_version"
        case capabilities
    }
}

struct FujiProtocolErrorPayload: Codable, Equatable {
    let errorCode: FujiProtocolErrorCode
    let message: String

    enum CodingKeys: String, CodingKey {
        case errorCode = "error_code"
        case message
    }
}

enum FujiPayload: Equatable {
    case capability(FujiCapabilityPayload)
    case actionRequest(FujiActionRequestPayload)
    case actionResult(FujiActionResultPayload)
    case cancel(FujiCancelPayload)
    case stateSnapshot(FujiStateSnapshotPayload)
    case protocolError(FujiProtocolErrorPayload)
}

struct FujiMessage: Equatable, Identifiable {
    static let supportedVersion = 1
    static let maximumJSONBytes = 8_192
    static let minimumTTLMS = 1
    static let maximumTTLMS = 120_000

    let version: Int
    let messageID: UUID
    var requestID: UUID?
    let direction: FujiDirection
    let type: FujiMessageType
    let ttlMS: Int
    var sentAtUTC: String?
    let payload: FujiPayload

    var id: UUID { messageID }

    init(
        version: Int = supportedVersion,
        messageID: UUID = UUID(),
        requestID: UUID? = nil,
        direction: FujiDirection,
        type: FujiMessageType,
        ttlMS: Int,
        sentAtUTC: String? = nil,
        payload: FujiPayload
    ) {
        self.version = version
        self.messageID = messageID
        self.requestID = requestID
        self.direction = direction
        self.type = type
        self.ttlMS = ttlMS
        self.sentAtUTC = sentAtUTC
        self.payload = payload
    }

    static func foodSearch(
        requestID: UUID = UUID(),
        criteria: FujiFoodSearchCriteriaPayload = .init(),
        ttlMS: Int = maximumTTLMS
    ) -> FujiMessage {
        FujiMessage(
            requestID: requestID,
            direction: .deviceToPhone,
            type: .actionRequest,
            ttlMS: ttlMS,
            payload: .actionRequest(.foodSearch(criteria: criteria))
        )
    }

    static func actionResult(
        requestID: UUID,
        result: FujiActionResultPayload,
        ttlMS: Int = 30_000
    ) -> FujiMessage {
        FujiMessage(
            requestID: requestID,
            direction: .phoneToDevice,
            type: .actionResult,
            ttlMS: ttlMS,
            payload: .actionResult(result)
        )
    }
}

extension FujiMessage: Codable {
    private enum CodingKeys: String, CodingKey {
        case version
        case messageID = "message_id"
        case requestID = "request_id"
        case direction
        case type
        case ttlMS = "ttl_ms"
        case sentAtUTC = "sent_at_utc"
        case payload
    }

    init(from decoder: Decoder) throws {
        let container = try decoder.container(keyedBy: CodingKeys.self)
        version = try container.decode(Int.self, forKey: .version)
        messageID = try container.decode(UUID.self, forKey: .messageID)
        requestID = try container.decodeIfPresent(UUID.self, forKey: .requestID)
        direction = try container.decode(FujiDirection.self, forKey: .direction)
        type = try container.decode(FujiMessageType.self, forKey: .type)
        ttlMS = try container.decode(Int.self, forKey: .ttlMS)
        sentAtUTC = try container.decodeIfPresent(String.self, forKey: .sentAtUTC)
        switch type {
        case .capabilityReport:
            payload = .capability(try container.decode(FujiCapabilityPayload.self, forKey: .payload))
        case .actionRequest:
            payload = .actionRequest(try container.decode(FujiActionRequestPayload.self, forKey: .payload))
        case .actionResult:
            payload = .actionResult(try container.decode(FujiActionResultPayload.self, forKey: .payload))
        case .cancel:
            payload = .cancel(try container.decode(FujiCancelPayload.self, forKey: .payload))
        case .stateSnapshot:
            payload = .stateSnapshot(try container.decode(FujiStateSnapshotPayload.self, forKey: .payload))
        case .protocolError:
            payload = .protocolError(try container.decode(FujiProtocolErrorPayload.self, forKey: .payload))
        }
    }

    func encode(to encoder: Encoder) throws {
        var container = encoder.container(keyedBy: CodingKeys.self)
        try container.encode(version, forKey: .version)
        try container.encode(messageID, forKey: .messageID)
        try container.encodeIfPresent(requestID, forKey: .requestID)
        try container.encode(direction, forKey: .direction)
        try container.encode(type, forKey: .type)
        try container.encode(ttlMS, forKey: .ttlMS)
        try container.encodeIfPresent(sentAtUTC, forKey: .sentAtUTC)
        switch payload {
        case .capability(let value): try container.encode(value, forKey: .payload)
        case .actionRequest(let value): try container.encode(value, forKey: .payload)
        case .actionResult(let value): try container.encode(value, forKey: .payload)
        case .cancel(let value): try container.encode(value, forKey: .payload)
        case .stateSnapshot(let value): try container.encode(value, forKey: .payload)
        case .protocolError(let value): try container.encode(value, forKey: .payload)
        }
    }
}

struct FujiValidatedMessage: Equatable {
    let message: FujiMessage
    let receivedAtMS: UInt64

    var expiresAtMS: UInt64 { receivedAtMS + UInt64(message.ttlMS) }

    func isExpired(at monotonicMS: UInt64) -> Bool {
        monotonicMS >= expiresAtMS
    }
}

enum FujiMessageValidationError: LocalizedError, Equatable {
    case protocolError(FujiProtocolErrorCode)

    var errorDescription: String? {
        switch self {
        case .protocolError(let code): "Fuji 协议拒绝消息：\(code.rawValue)"
        }
    }
}

@MainActor
final class FujiMessageValidator {
    private struct SeenMessage {
        let id: UUID
        let receivedAtMS: UInt64
    }

    private let capacity: Int
    private let retentionMS: UInt64
    private var seen: [SeenMessage] = []

    init(capacity: Int = 128, retentionMS: UInt64 = 600_000) {
        self.capacity = capacity
        self.retentionMS = retentionMS
    }

    func decode(_ data: Data, receivedAtMS: UInt64) throws -> FujiValidatedMessage {
        guard data.count <= FujiMessage.maximumJSONBytes else { throw failure(.payloadTooLarge) }
        let message: FujiMessage
        do {
            message = try JSONDecoder().decode(FujiMessage.self, from: data)
        } catch {
            throw failure(classifyDecodeFailure(data))
        }
        return try validate(message, receivedAtMS: receivedAtMS)
    }

    func validate(_ message: FujiMessage, receivedAtMS: UInt64) throws -> FujiValidatedMessage {
        guard message.version == FujiMessage.supportedVersion else { throw failure(.unsupportedVersion) }
        guard (FujiMessage.minimumTTLMS...FujiMessage.maximumTTLMS).contains(message.ttlMS) else {
            throw failure(.invalidPayload)
        }
        try validateSemantics(message)

        seen.removeAll { receivedAtMS >= $0.receivedAtMS && receivedAtMS - $0.receivedAtMS >= retentionMS }
        guard !seen.contains(where: { $0.id == message.messageID }) else { throw failure(.duplicate) }
        seen.append(.init(id: message.messageID, receivedAtMS: receivedAtMS))
        if seen.count > capacity {
            seen.removeFirst(seen.count - capacity)
        }
        return FujiValidatedMessage(message: message, receivedAtMS: receivedAtMS)
    }

    func reset() {
        seen.removeAll()
    }

    private func validateSemantics(_ message: FujiMessage) throws {
        let needsRequestID = message.type == .actionRequest || message.type == .actionResult || message.type == .cancel
        guard !needsRequestID || message.requestID != nil else { throw failure(.invalidPayload) }

        switch (message.type, message.direction, message.payload) {
        case (.capabilityReport, _, .capability(let payload)):
            guard payload.protocolVersions.contains(1),
                  payload.maxPayloadBytes == FujiMessage.maximumJSONBytes,
                  !payload.firmwareVersion.isEmpty,
                  !payload.buildID.isEmpty else { throw failure(.invalidPayload) }
        case (.actionRequest, .deviceToPhone, .actionRequest(let payload)):
            try validate(payload)
        case (.actionResult, .phoneToDevice, .actionResult(let payload)):
            guard (payload.candidates?.count ?? 0) <= 3 else { throw failure(.invalidPayload) }
        case (.cancel, _, .cancel(let payload)):
            guard payload.targetRequestID == message.requestID else { throw failure(.invalidPayload) }
        case (.stateSnapshot, .deviceToPhone, .stateSnapshot(let payload)):
            guard !payload.firmwareVersion.isEmpty else { throw failure(.invalidPayload) }
        case (.protocolError, _, .protocolError(let payload)):
            guard !payload.message.isEmpty else { throw failure(.invalidPayload) }
        default:
            throw failure(.invalidPayload)
        }
    }

    private func validate(_ payload: FujiActionRequestPayload) throws {
        switch payload {
        case .foodSearch(let criteria):
            if let radius = criteria.radiusM, !(100...5_000).contains(radius) { throw failure(.invalidPayload) }
            if let budget = criteria.budgetRMB, !(1...2_000).contains(budget) { throw failure(.invalidPayload) }
            if let avoid = criteria.avoidTerms, avoid.count > 8 { throw failure(.invalidPayload) }
        case .startNavigation(let candidateID, _, let confirmed):
            guard confirmed, !candidateID.isEmpty, candidateID.count <= 96 else { throw failure(.invalidPayload) }
        }
    }

    private func classifyDecodeFailure(_ data: Data) -> FujiProtocolErrorCode {
        guard let object = try? JSONSerialization.jsonObject(with: data) as? [String: Any] else { return .invalidJSON }
        if let version = object["version"] as? Int, version != FujiMessage.supportedVersion { return .unsupportedVersion }
        if let rawType = object["type"] as? String, FujiMessageType(rawValue: rawType) == nil { return .unknownType }
        return .invalidPayload
    }

    private func failure(_ code: FujiProtocolErrorCode) -> FujiMessageValidationError {
        .protocolError(code)
    }
}

@MainActor
final class FujiActionResultCache {
    private struct Entry {
        let requestID: UUID
        let result: FujiActionResultPayload
        let storedAtMS: UInt64
    }

    private let capacity: Int
    private let retentionMS: UInt64
    private var entries: [Entry] = []

    init(capacity: Int = 32, retentionMS: UInt64 = 600_000) {
        self.capacity = capacity
        self.retentionMS = retentionMS
    }

    func result(for requestID: UUID, nowMS: UInt64) -> FujiActionResultPayload? {
        purge(nowMS: nowMS)
        return entries.last(where: { $0.requestID == requestID })?.result
    }

    func store(_ result: FujiActionResultPayload, for requestID: UUID, nowMS: UInt64) {
        purge(nowMS: nowMS)
        entries.removeAll { $0.requestID == requestID }
        entries.append(.init(requestID: requestID, result: result, storedAtMS: nowMS))
        if entries.count > capacity {
            entries.removeFirst(entries.count - capacity)
        }
    }

    func reset() {
        entries.removeAll()
    }

    private func purge(nowMS: UInt64) {
        entries.removeAll { nowMS >= $0.storedAtMS && nowMS - $0.storedAtMS >= retentionMS }
    }
}

struct FujiFrame: Equatable {
    static let headerSize = 14
    static let magic: [UInt8] = [0x46, 0x55]
    static let version: UInt8 = 1
    static let startFlag: UInt8 = 1 << 0
    static let endFlag: UInt8 = 1 << 1

    let flags: UInt8
    let transferID: UInt32
    let chunkIndex: UInt16
    let chunkCount: UInt16
    let totalLength: UInt16
    let payload: Data

    var encoded: Data {
        var data = Data(Self.magic)
        data.append(Self.version)
        data.append(flags)
        data.appendUInt32LE(transferID)
        data.appendUInt16LE(chunkIndex)
        data.appendUInt16LE(chunkCount)
        data.appendUInt16LE(totalLength)
        data.append(payload)
        return data
    }

    init(
        flags: UInt8,
        transferID: UInt32,
        chunkIndex: UInt16,
        chunkCount: UInt16,
        totalLength: UInt16,
        payload: Data
    ) {
        self.flags = flags
        self.transferID = transferID
        self.chunkIndex = chunkIndex
        self.chunkCount = chunkCount
        self.totalLength = totalLength
        self.payload = payload
    }

    init(encoded data: Data) throws {
        guard data.count >= Self.headerSize,
              Array(data.prefix(2)) == Self.magic,
              data[2] == Self.version,
              data[3] & ~(Self.startFlag | Self.endFlag) == 0 else { throw FujiFrameError.invalidHeader }
        flags = data[3]
        transferID = data.uint32LE(at: 4)
        chunkIndex = data.uint16LE(at: 8)
        chunkCount = data.uint16LE(at: 10)
        totalLength = data.uint16LE(at: 12)
        payload = data.dropFirst(Self.headerSize)
        guard chunkCount > 0,
              chunkIndex < chunkCount,
              totalLength <= FujiMessage.maximumJSONBytes,
              (chunkIndex != 0 || flags & Self.startFlag != 0),
              (chunkIndex != chunkCount - 1 || flags & Self.endFlag != 0) else {
            throw FujiFrameError.invalidHeader
        }
    }
}

enum FujiFrameError: Error, Equatable {
    case invalidMTU
    case payloadTooLarge
    case invalidHeader
    case transferInProgress
    case inconsistentTransfer
    case timedOut
}

enum FujiFramer {
    static func frames(for json: Data, mtu: Int, transferID: UInt32) throws -> [Data] {
        guard json.count <= FujiMessage.maximumJSONBytes, json.count <= Int(UInt16.max) else {
            throw FujiFrameError.payloadTooLarge
        }
        let chunkCapacity = mtu - 3 - FujiFrame.headerSize
        guard chunkCapacity > 0 else { throw FujiFrameError.invalidMTU }
        let count = max(1, Int(ceil(Double(json.count) / Double(chunkCapacity))))
        guard count <= Int(UInt16.max) else { throw FujiFrameError.payloadTooLarge }

        return (0..<count).map { index in
            let start = index * chunkCapacity
            let end = min(json.count, start + chunkCapacity)
            var flags: UInt8 = 0
            if index == 0 { flags |= FujiFrame.startFlag }
            if index == count - 1 { flags |= FujiFrame.endFlag }
            return FujiFrame(
                flags: flags,
                transferID: transferID,
                chunkIndex: UInt16(index),
                chunkCount: UInt16(count),
                totalLength: UInt16(json.count),
                payload: json.subdata(in: start..<end)
            ).encoded
        }
    }
}

struct FujiFrameAssembler {
    private struct Transfer {
        let id: UInt32
        let chunkCount: UInt16
        let totalLength: UInt16
        let startedAtMS: UInt64
        var chunks: [UInt16: Data]
    }

    private var transfer: Transfer?
    let timeoutMS: UInt64

    init(timeoutMS: UInt64 = 5_000) {
        self.timeoutMS = timeoutMS
    }

    mutating func accept(_ encodedFrame: Data, nowMS: UInt64) throws -> Data? {
        if let transfer, nowMS >= transfer.startedAtMS && nowMS - transfer.startedAtMS >= timeoutMS {
            self.transfer = nil
            throw FujiFrameError.timedOut
        }

        let frame = try FujiFrame(encoded: encodedFrame)
        if transfer == nil {
            guard frame.flags & FujiFrame.startFlag != 0 else { throw FujiFrameError.invalidHeader }
            transfer = Transfer(
                id: frame.transferID,
                chunkCount: frame.chunkCount,
                totalLength: frame.totalLength,
                startedAtMS: nowMS,
                chunks: [:]
            )
        }
        guard transfer?.id == frame.transferID else { throw FujiFrameError.transferInProgress }
        guard transfer?.chunkCount == frame.chunkCount, transfer?.totalLength == frame.totalLength else {
            self.transfer = nil
            throw FujiFrameError.inconsistentTransfer
        }
        guard transfer?.chunks[frame.chunkIndex] == nil else { throw FujiFrameError.inconsistentTransfer }
        transfer?.chunks[frame.chunkIndex] = frame.payload

        guard let current = transfer, current.chunks.count == Int(current.chunkCount) else { return nil }
        var result = Data()
        for index in 0..<current.chunkCount {
            guard let chunk = current.chunks[index] else { return nil }
            result.append(chunk)
        }
        transfer = nil
        guard result.count == Int(current.totalLength) else { throw FujiFrameError.inconsistentTransfer }
        return result
    }

    mutating func reset() {
        transfer = nil
    }
}

private extension Data {
    mutating func appendUInt16LE(_ value: UInt16) {
        append(UInt8(truncatingIfNeeded: value))
        append(UInt8(truncatingIfNeeded: value >> 8))
    }

    mutating func appendUInt32LE(_ value: UInt32) {
        append(UInt8(truncatingIfNeeded: value))
        append(UInt8(truncatingIfNeeded: value >> 8))
        append(UInt8(truncatingIfNeeded: value >> 16))
        append(UInt8(truncatingIfNeeded: value >> 24))
    }

    func uint16LE(at offset: Int) -> UInt16 {
        UInt16(self[offset]) | UInt16(self[offset + 1]) << 8
    }

    func uint32LE(at offset: Int) -> UInt32 {
        UInt32(self[offset])
            | UInt32(self[offset + 1]) << 8
            | UInt32(self[offset + 2]) << 16
            | UInt32(self[offset + 3]) << 24
    }
}
