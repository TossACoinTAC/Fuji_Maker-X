#include "fuji_ble_session.h"

namespace fuji::ble {

protocol::ErrorCode InboundSession::AcceptFrame(const std::vector<uint8_t>& bytes, uint64_t now_ms,
                                                std::optional<ReceivedMessage>* completed) {
    last_error_was_reassembly_timeout_ = false;
    if (completed == nullptr) {
        return protocol::ErrorCode::kInvalidPayload;
    }
    completed->reset();

    protocol::Frame frame;
    const protocol::ErrorCode frame_error = protocol::DecodeFrame(bytes, &frame);
    if (frame_error != protocol::ErrorCode::kNone) {
        return frame_error;
    }
    if ((frame.flags & 0x01U) != 0) {
        transfer_started_at_ms_ = now_ms;
    }

    std::optional<std::vector<uint8_t>> payload;
    const protocol::ErrorCode assembly_error = assembler_.Accept(bytes, now_ms, &payload);
    if (assembly_error != protocol::ErrorCode::kNone) {
        last_error_was_reassembly_timeout_ = assembly_error == protocol::ErrorCode::kExpired;
        transfer_started_at_ms_.reset();
        return assembly_error;
    }
    if (!payload.has_value()) {
        return protocol::ErrorCode::kNone;
    }

    const uint64_t started_at_ms = transfer_started_at_ms_.value_or(now_ms);
    transfer_started_at_ms_.reset();
    std::string json(payload->begin(), payload->end());
    protocol::Message message;
    const protocol::ErrorCode parse_error = protocol::ParseAndValidate(json, &message);
    if (parse_error != protocol::ErrorCode::kNone) {
        return parse_error;
    }
    if (message.direction != protocol::Direction::kPhoneToDevice) {
        return protocol::ErrorCode::kInvalidPayload;
    }
    if (now_ms >= started_at_ms &&
        now_ms - started_at_ms >= static_cast<uint64_t>(message.ttl_ms)) {
        return protocol::ErrorCode::kExpired;
    }
    const protocol::ErrorCode duplicate_error = deduplicator_.Accept(message, now_ms);
    if (duplicate_error != protocol::ErrorCode::kNone) {
        return duplicate_error;
    }

    *completed = ReceivedMessage{std::move(message), std::move(json), now_ms};
    return protocol::ErrorCode::kNone;
}

void InboundSession::ResetConnection() {
    assembler_.Reset();
    transfer_started_at_ms_.reset();
    last_error_was_reassembly_timeout_ = false;
}

void InboundSession::ResetForRestart() {
    ResetConnection();
    deduplicator_.Reset();
}

}  // namespace fuji::ble
