#pragma once

#include <cstddef>
#include <cstdint>
#include <deque>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace fuji::protocol {

constexpr int kVersion = 1;
constexpr std::size_t kMaximumJsonBytes = 8192;
constexpr int kMinimumTtlMs = 1;
constexpr int kMaximumTtlMs = 120000;
constexpr std::size_t kFrameHeaderSize = 14;
constexpr uint64_t kReassemblyTimeoutMs = 5000;

enum class ErrorCode {
    kNone,
    kInvalidJson,
    kUnsupportedVersion,
    kUnknownType,
    kInvalidPayload,
    kPayloadTooLarge,
    kFragmentError,
    kDuplicate,
    kExpired,
};

const char* ErrorCodeName(ErrorCode code);

enum class Direction { kDeviceToPhone, kPhoneToDevice };
enum class MessageType {
    kCapabilityReport,
    kActionRequest,
    kActionResult,
    kCancel,
    kStateSnapshot,
    kProtocolError,
};
enum class Action { kNone, kFoodSearch, kStartNavigation };

struct Message {
    int version = 0;
    std::string message_id;
    std::optional<std::string> request_id;
    Direction direction = Direction::kDeviceToPhone;
    MessageType type = MessageType::kProtocolError;
    Action action = Action::kNone;
    int ttl_ms = 0;
};

ErrorCode ParseAndValidate(const std::string& json, Message* message);

class MessageDeduplicator {
public:
    explicit MessageDeduplicator(std::size_t capacity = 128, uint64_t retention_ms = 600000);
    ErrorCode Accept(const Message& message, uint64_t received_at_ms);
    void Reset();
    std::size_t Size() const { return entries_.size(); }

private:
    struct Entry {
        std::string message_id;
        uint64_t received_at_ms;
    };

    void Purge(uint64_t now_ms);

    std::size_t capacity_;
    uint64_t retention_ms_;
    std::deque<Entry> entries_;
};

class ActionResultCache {
public:
    explicit ActionResultCache(std::size_t capacity = 32, uint64_t retention_ms = 600000);
    void Store(const std::string& request_id, const std::string& encoded_result, uint64_t now_ms);
    std::optional<std::string> Get(const std::string& request_id, uint64_t now_ms);
    void Reset();
    std::size_t Size() const { return entries_.size(); }

private:
    struct Entry {
        std::string request_id;
        std::string encoded_result;
        uint64_t stored_at_ms;
    };

    void Purge(uint64_t now_ms);

    std::size_t capacity_;
    uint64_t retention_ms_;
    std::deque<Entry> entries_;
};

struct Frame {
    uint8_t flags = 0;
    uint32_t transfer_id = 0;
    uint16_t chunk_index = 0;
    uint16_t chunk_count = 0;
    uint16_t total_length = 0;
    std::vector<uint8_t> payload;
};

ErrorCode EncodeFrames(
    const std::vector<uint8_t>& json,
    std::size_t att_mtu,
    uint32_t transfer_id,
    std::vector<std::vector<uint8_t>>* frames);
ErrorCode DecodeFrame(const std::vector<uint8_t>& bytes, Frame* frame);

class FrameAssembler {
public:
    explicit FrameAssembler(uint64_t timeout_ms = kReassemblyTimeoutMs) : timeout_ms_(timeout_ms) {}
    ErrorCode Accept(
        const std::vector<uint8_t>& bytes,
        uint64_t now_ms,
        std::optional<std::vector<uint8_t>>* completed);
    void Reset();

private:
    struct Transfer {
        uint32_t id;
        uint16_t chunk_count;
        uint16_t total_length;
        uint64_t started_at_ms;
        std::map<uint16_t, std::vector<uint8_t>> chunks;
    };

    uint64_t timeout_ms_;
    std::optional<Transfer> transfer_;
};

}  // namespace fuji::protocol
