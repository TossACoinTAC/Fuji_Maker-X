#pragma once

#include "fuji_protocol.h"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace fuji::ble {

struct ReceivedMessage {
    protocol::Message message;
    std::string json;
    uint64_t received_at_ms = 0;
};

// Owns the phone-to-device transfer and wire-message lifetime for one runtime.
// Connection reset deliberately preserves message de-duplication across reconnects.
class InboundSession {
public:
    protocol::ErrorCode AcceptFrame(const std::vector<uint8_t>& frame, uint64_t now_ms,
                                    std::optional<ReceivedMessage>* completed);
    void ResetConnection();
    void ResetForRestart();
    bool LastErrorWasReassemblyTimeout() const { return last_error_was_reassembly_timeout_; }

private:
    protocol::FrameAssembler assembler_;
    protocol::MessageDeduplicator deduplicator_;
    std::optional<uint64_t> transfer_started_at_ms_;
    bool last_error_was_reassembly_timeout_ = false;
};

}  // namespace fuji::ble
