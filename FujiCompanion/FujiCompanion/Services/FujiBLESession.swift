import Foundation

@MainActor
final class FujiBLESession {
    static let reassemblyTimeoutMS: UInt64 = 5_000

    private var assembler = FujiFrameAssembler()
    private let validator: FujiMessageValidator
    private var transferStartedAtMS: UInt64?

    init(validator: FujiMessageValidator? = nil) {
        self.validator = validator ?? FujiMessageValidator()
    }

    var transferDeadlineMS: UInt64? {
        transferStartedAtMS.map { $0 + Self.reassemblyTimeoutMS }
    }

    func accept(_ encodedFrame: Data, nowMS: UInt64) throws -> FujiMessage? {
        do {
            let frame = try FujiFrame(encoded: encodedFrame)
            if frame.flags & FujiFrame.startFlag != 0 {
                transferStartedAtMS = nowMS
            }
            guard let json = try assembler.accept(encodedFrame, nowMS: nowMS) else {
                return nil
            }

            let receivedAtMS = transferStartedAtMS ?? nowMS
            transferStartedAtMS = nil
            let validated = try validator.decode(
                json,
                receivedAtMS: receivedAtMS,
                evaluatedAtMS: nowMS
            )
            guard validated.message.direction == .deviceToPhone else {
                throw FujiMessageValidationError.protocolError(.invalidPayload)
            }
            return validated.message
        } catch {
            assembler.reset()
            transferStartedAtMS = nil
            throw error
        }
    }

    func frames(
        for message: FujiMessage,
        maximumWriteValueLength: Int,
        transferID: UInt32
    ) throws -> [Data] {
        guard message.direction == .phoneToDevice else {
            throw FujiMessageValidationError.protocolError(.invalidPayload)
        }
        _ = try FujiMessageValidator(capacity: 1).validate(message, receivedAtMS: 0)
        let json = try JSONEncoder().encode(message)
        guard json.count <= FujiMessage.maximumJSONBytes else {
            throw FujiMessageValidationError.protocolError(.payloadTooLarge)
        }

        // CoreBluetooth reports ATT payload bytes, while FujiFramer accepts the
        // full ATT MTU and subtracts the three-byte ATT write overhead itself.
        return try FujiFramer.frames(
            for: json,
            mtu: maximumWriteValueLength + 3,
            transferID: transferID
        )
    }

    func resetConnection() {
        assembler.reset()
        transferStartedAtMS = nil
    }

    func expireTransferIfNeeded(at nowMS: UInt64) -> Bool {
        guard let transferStartedAtMS,
              nowMS >= transferStartedAtMS,
              nowMS - transferStartedAtMS >= Self.reassemblyTimeoutMS else {
            return false
        }
        resetConnection()
        return true
    }

    func resetForRestart() {
        resetConnection()
        validator.reset()
    }
}
