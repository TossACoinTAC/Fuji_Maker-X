#include "fuji_protocol.h"

#include <cJSON.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cstring>
#include <limits>

namespace fuji::protocol {
namespace {

constexpr uint8_t kStartFlag = 1U << 0;
constexpr uint8_t kEndFlag = 1U << 1;

bool IsUuid(const char* value) {
    if (value == nullptr || std::strlen(value) != 36) {
        return false;
    }
    constexpr std::array<int, 4> kHyphens = {8, 13, 18, 23};
    for (int index = 0; index < 36; ++index) {
        const bool hyphen = std::find(kHyphens.begin(), kHyphens.end(), index) != kHyphens.end();
        if ((hyphen && value[index] != '-') || (!hyphen && !std::isxdigit(static_cast<unsigned char>(value[index])))) {
            return false;
        }
    }
    return true;
}

const cJSON* Item(const cJSON* object, const char* name) {
    return cJSON_GetObjectItemCaseSensitive(object, name);
}

bool IsIntegerInRange(const cJSON* item, int minimum, int maximum) {
    return cJSON_IsNumber(item) && item->valuedouble == static_cast<double>(item->valueint) &&
           item->valueint >= minimum && item->valueint <= maximum;
}

bool IsNonEmptyString(const cJSON* item, std::size_t maximum) {
    return cJSON_IsString(item) && item->valuestring != nullptr && item->valuestring[0] != '\0' &&
           std::strlen(item->valuestring) <= maximum;
}

bool IsUuidItem(const cJSON* item) {
    return cJSON_IsString(item) && IsUuid(item->valuestring);
}

bool ParseDirection(const cJSON* item, Direction* direction) {
    if (!cJSON_IsString(item)) {
        return false;
    }
    if (std::strcmp(item->valuestring, "device_to_phone") == 0) {
        *direction = Direction::kDeviceToPhone;
        return true;
    }
    if (std::strcmp(item->valuestring, "phone_to_device") == 0) {
        *direction = Direction::kPhoneToDevice;
        return true;
    }
    return false;
}

bool ParseMessageType(const cJSON* item, MessageType* type) {
    if (!cJSON_IsString(item)) {
        return false;
    }
    constexpr std::array<std::pair<const char*, MessageType>, 6> kTypes = {{
        {"capability_report", MessageType::kCapabilityReport},
        {"action_request", MessageType::kActionRequest},
        {"action_result", MessageType::kActionResult},
        {"cancel", MessageType::kCancel},
        {"state_snapshot", MessageType::kStateSnapshot},
        {"protocol_error", MessageType::kProtocolError},
    }};
    for (const auto& [name, value] : kTypes) {
        if (std::strcmp(item->valuestring, name) == 0) {
            *type = value;
            return true;
        }
    }
    return false;
}

bool ParseAction(const cJSON* item, Action* action) {
    if (!cJSON_IsString(item)) {
        return false;
    }
    if (std::strcmp(item->valuestring, "food_search") == 0) {
        *action = Action::kFoodSearch;
        return true;
    }
    if (std::strcmp(item->valuestring, "start_navigation") == 0) {
        *action = Action::kStartNavigation;
        return true;
    }
    return false;
}

bool ValidStringArray(const cJSON* item, std::size_t maximum_count, std::size_t maximum_length) {
    if (!cJSON_IsArray(item) || static_cast<std::size_t>(cJSON_GetArraySize(item)) > maximum_count) {
        return false;
    }
    const cJSON* child = nullptr;
    cJSON_ArrayForEach(child, item) {
        if (!IsNonEmptyString(child, maximum_length)) {
            return false;
        }
    }
    return true;
}

bool ValidateCapability(const cJSON* payload) {
    const cJSON* versions = Item(payload, "protocol_versions");
    const cJSON* firmware = Item(payload, "firmware_version");
    const cJSON* build = Item(payload, "build_id");
    const cJSON* maximum = Item(payload, "max_payload_bytes");
    const cJSON* capabilities = Item(payload, "capabilities");
    if (!cJSON_IsArray(versions) || cJSON_GetArraySize(versions) < 1 || !IsNonEmptyString(firmware, 64) ||
        !IsNonEmptyString(build, 96) || !IsIntegerInRange(maximum, 8192, 8192) ||
        !ValidStringArray(capabilities, 64, 64)) {
        return false;
    }
    bool supports_v1 = false;
    const cJSON* version = nullptr;
    cJSON_ArrayForEach(version, versions) {
        if (!cJSON_IsNumber(version) || version->valuedouble != static_cast<double>(version->valueint)) {
            return false;
        }
        supports_v1 = supports_v1 || version->valueint == 1;
    }
    return supports_v1;
}

bool ValidateCriteria(const cJSON* criteria) {
    if (!cJSON_IsObject(criteria)) {
        return false;
    }
    const cJSON* radius = Item(criteria, "radius_m");
    const cJSON* budget = Item(criteria, "budget_rmb");
    const cJSON* avoid = Item(criteria, "avoid_terms");
    return (radius == nullptr || IsIntegerInRange(radius, 100, 5000)) &&
           (budget == nullptr || IsIntegerInRange(budget, 1, 2000)) &&
           (avoid == nullptr || ValidStringArray(avoid, 8, 40));
}

bool ValidateActionRequest(const cJSON* payload, Action* action) {
    if (!cJSON_IsObject(payload) || !ParseAction(Item(payload, "action"), action)) {
        return false;
    }
    if (*action == Action::kFoodSearch) {
        return ValidateCriteria(Item(payload, "criteria"));
    }
    const cJSON* candidate = Item(payload, "candidate_id");
    const cJSON* parent = Item(payload, "parent_request_id");
    const cJSON* confirmation = Item(payload, "confirmation");
    return IsNonEmptyString(candidate, 96) && IsUuidItem(parent) && cJSON_IsString(confirmation) &&
           std::strcmp(confirmation->valuestring, "confirmed") == 0;
}

bool ValidResultStatus(const cJSON* status) {
    if (!cJSON_IsString(status)) {
        return false;
    }
    constexpr std::array<const char*, 7> kStatuses = {
        "accepted", "in_progress", "needs_confirmation", "succeeded", "failed", "cancelled", "expired"};
    return std::any_of(kStatuses.begin(), kStatuses.end(), [status](const char* value) {
        return std::strcmp(status->valuestring, value) == 0;
    });
}

bool ValidErrorCode(const cJSON* error) {
    if (!cJSON_IsString(error)) {
        return false;
    }
    constexpr std::array<const char*, 19> kCodes = {
        "invalid_json", "unsupported_version", "unknown_type", "invalid_payload", "payload_too_large",
        "fragment_error", "duplicate", "expired", "cancelled", "disconnected", "bluetooth_unauthorized",
        "location_permission_denied", "location_unavailable", "no_results", "private_route_unavailable",
        "route_lost", "foreground_required", "map_launch_failed", "internal_error"};
    return std::any_of(kCodes.begin(), kCodes.end(), [error](const char* value) {
        return std::strcmp(error->valuestring, value) == 0;
    });
}

bool ValidateCandidates(const cJSON* candidates) {
    if (!cJSON_IsArray(candidates) || cJSON_GetArraySize(candidates) > 3) {
        return false;
    }
    const cJSON* candidate = nullptr;
    cJSON_ArrayForEach(candidate, candidates) {
        if (!cJSON_IsObject(candidate) || !IsNonEmptyString(Item(candidate, "candidate_id"), 96) ||
            !IsNonEmptyString(Item(candidate, "name"), 80) || !IsNonEmptyString(Item(candidate, "reason"), 160)) {
            return false;
        }
    }
    return true;
}

bool ValidateActionResult(const cJSON* payload, Action* action) {
    if (!cJSON_IsObject(payload) || !ParseAction(Item(payload, "action"), action) ||
        !ValidResultStatus(Item(payload, "status"))) {
        return false;
    }
    const cJSON* candidates = Item(payload, "candidates");
    const cJSON* navigation = Item(payload, "navigation_state");
    const cJSON* error = Item(payload, "error_code");
    const cJSON* message = Item(payload, "message");
    if (candidates != nullptr && !ValidateCandidates(candidates)) {
        return false;
    }
    if (navigation != nullptr && (!cJSON_IsString(navigation) ||
        (std::strcmp(navigation->valuestring, "navigation_ready") != 0 &&
         std::strcmp(navigation->valuestring, "navigation_launched") != 0))) {
        return false;
    }
    return (error == nullptr || ValidErrorCode(error)) &&
           (message == nullptr || (cJSON_IsString(message) && std::strlen(message->valuestring) <= 240));
}

bool ValidateSnapshot(const cJSON* payload) {
    const cJSON* state = Item(payload, "device_state");
    if (!cJSON_IsObject(payload) || !cJSON_IsString(state) || !cJSON_IsBool(Item(payload, "earphones_verified")) ||
        !IsNonEmptyString(Item(payload, "firmware_version"), 64) ||
        !ValidStringArray(Item(payload, "capabilities"), 64, 64)) {
        return false;
    }
    constexpr std::array<const char*, 8> kStates = {
        "idle", "listening", "thinking", "speaking", "success", "error", "offline", "muted"};
    if (!std::any_of(kStates.begin(), kStates.end(), [state](const char* value) {
            return std::strcmp(state->valuestring, value) == 0;
        })) {
        return false;
    }
    const cJSON* active = Item(payload, "active_request_id");
    return active == nullptr || IsUuidItem(active);
}

void Append16(std::vector<uint8_t>* output, uint16_t value) {
    output->push_back(static_cast<uint8_t>(value));
    output->push_back(static_cast<uint8_t>(value >> 8));
}

void Append32(std::vector<uint8_t>* output, uint32_t value) {
    output->push_back(static_cast<uint8_t>(value));
    output->push_back(static_cast<uint8_t>(value >> 8));
    output->push_back(static_cast<uint8_t>(value >> 16));
    output->push_back(static_cast<uint8_t>(value >> 24));
}

uint16_t Read16(const std::vector<uint8_t>& input, std::size_t offset) {
    return static_cast<uint16_t>(input[offset]) | static_cast<uint16_t>(input[offset + 1]) << 8;
}

uint32_t Read32(const std::vector<uint8_t>& input, std::size_t offset) {
    return static_cast<uint32_t>(input[offset]) | static_cast<uint32_t>(input[offset + 1]) << 8 |
           static_cast<uint32_t>(input[offset + 2]) << 16 | static_cast<uint32_t>(input[offset + 3]) << 24;
}

}  // namespace

const char* ErrorCodeName(ErrorCode code) {
    switch (code) {
        case ErrorCode::kNone: return "none";
        case ErrorCode::kInvalidJson: return "invalid_json";
        case ErrorCode::kUnsupportedVersion: return "unsupported_version";
        case ErrorCode::kUnknownType: return "unknown_type";
        case ErrorCode::kInvalidPayload: return "invalid_payload";
        case ErrorCode::kPayloadTooLarge: return "payload_too_large";
        case ErrorCode::kFragmentError: return "fragment_error";
        case ErrorCode::kDuplicate: return "duplicate";
        case ErrorCode::kExpired: return "expired";
    }
    return "internal_error";
}

ErrorCode ParseAndValidate(const std::string& json, Message* message) {
    if (message == nullptr) {
        return ErrorCode::kInvalidPayload;
    }
    if (json.size() > kMaximumJsonBytes) {
        return ErrorCode::kPayloadTooLarge;
    }
    cJSON* root = cJSON_ParseWithLength(json.data(), json.size());
    if (!cJSON_IsObject(root)) {
        cJSON_Delete(root);
        return ErrorCode::kInvalidJson;
    }

    const cJSON* version = Item(root, "version");
    if (!cJSON_IsNumber(version) || version->valuedouble != static_cast<double>(version->valueint)) {
        cJSON_Delete(root);
        return ErrorCode::kInvalidPayload;
    }
    if (version->valueint != kVersion) {
        cJSON_Delete(root);
        return ErrorCode::kUnsupportedVersion;
    }

    Message parsed;
    parsed.version = version->valueint;
    const cJSON* message_id = Item(root, "message_id");
    const cJSON* request_id = Item(root, "request_id");
    const cJSON* ttl = Item(root, "ttl_ms");
    const cJSON* payload = Item(root, "payload");
    if (!IsUuidItem(message_id) || !ParseDirection(Item(root, "direction"), &parsed.direction) ||
        !IsIntegerInRange(ttl, kMinimumTtlMs, kMaximumTtlMs) || !cJSON_IsObject(payload)) {
        cJSON_Delete(root);
        return ErrorCode::kInvalidPayload;
    }
    parsed.message_id = message_id->valuestring;
    parsed.ttl_ms = ttl->valueint;
    if (request_id != nullptr) {
        if (!IsUuidItem(request_id)) {
            cJSON_Delete(root);
            return ErrorCode::kInvalidPayload;
        }
        parsed.request_id = request_id->valuestring;
    }
    if (!ParseMessageType(Item(root, "type"), &parsed.type)) {
        const cJSON* type = Item(root, "type");
        const bool was_string = cJSON_IsString(type);
        cJSON_Delete(root);
        return was_string ? ErrorCode::kUnknownType : ErrorCode::kInvalidPayload;
    }

    bool valid = false;
    switch (parsed.type) {
        case MessageType::kCapabilityReport:
            valid = ValidateCapability(payload);
            break;
        case MessageType::kActionRequest:
            valid = parsed.request_id.has_value() && parsed.direction == Direction::kDeviceToPhone &&
                    ValidateActionRequest(payload, &parsed.action);
            break;
        case MessageType::kActionResult:
            valid = parsed.request_id.has_value() && parsed.direction == Direction::kPhoneToDevice &&
                    ValidateActionResult(payload, &parsed.action);
            break;
        case MessageType::kCancel: {
            const cJSON* target = Item(payload, "target_request_id");
            const cJSON* reason = Item(payload, "reason");
            valid = parsed.request_id.has_value() && IsUuidItem(target) &&
                    *parsed.request_id == target->valuestring &&
                    (reason == nullptr || (cJSON_IsString(reason) && std::strlen(reason->valuestring) <= 160));
            break;
        }
        case MessageType::kStateSnapshot:
            valid = parsed.direction == Direction::kDeviceToPhone && ValidateSnapshot(payload);
            break;
        case MessageType::kProtocolError:
            valid = ValidErrorCode(Item(payload, "error_code")) && IsNonEmptyString(Item(payload, "message"), 240);
            break;
    }
    cJSON_Delete(root);
    if (!valid) {
        return ErrorCode::kInvalidPayload;
    }
    *message = std::move(parsed);
    return ErrorCode::kNone;
}

MessageDeduplicator::MessageDeduplicator(std::size_t capacity, uint64_t retention_ms)
    : capacity_(capacity), retention_ms_(retention_ms) {}

ErrorCode MessageDeduplicator::Accept(const Message& message, uint64_t received_at_ms) {
    Purge(received_at_ms);
    const auto duplicate = std::find_if(entries_.begin(), entries_.end(), [&message](const Entry& entry) {
        return entry.message_id == message.message_id;
    });
    if (duplicate != entries_.end()) {
        return ErrorCode::kDuplicate;
    }
    entries_.push_back({message.message_id, received_at_ms});
    while (entries_.size() > capacity_) {
        entries_.pop_front();
    }
    return ErrorCode::kNone;
}

void MessageDeduplicator::Reset() {
    entries_.clear();
}

void MessageDeduplicator::Purge(uint64_t now_ms) {
    while (!entries_.empty() && now_ms >= entries_.front().received_at_ms &&
           now_ms - entries_.front().received_at_ms >= retention_ms_) {
        entries_.pop_front();
    }
}

ActionResultCache::ActionResultCache(std::size_t capacity, uint64_t retention_ms)
    : capacity_(capacity), retention_ms_(retention_ms) {}

void ActionResultCache::Store(const std::string& request_id, const std::string& encoded_result, uint64_t now_ms) {
    Purge(now_ms);
    entries_.erase(std::remove_if(entries_.begin(), entries_.end(), [&request_id](const Entry& entry) {
        return entry.request_id == request_id;
    }), entries_.end());
    entries_.push_back({request_id, encoded_result, now_ms});
    while (entries_.size() > capacity_) {
        entries_.pop_front();
    }
}

std::optional<std::string> ActionResultCache::Get(const std::string& request_id, uint64_t now_ms) {
    Purge(now_ms);
    const auto entry = std::find_if(entries_.rbegin(), entries_.rend(), [&request_id](const Entry& value) {
        return value.request_id == request_id;
    });
    return entry == entries_.rend() ? std::nullopt : std::optional<std::string>(entry->encoded_result);
}

void ActionResultCache::Reset() {
    entries_.clear();
}

void ActionResultCache::Purge(uint64_t now_ms) {
    while (!entries_.empty() && now_ms >= entries_.front().stored_at_ms &&
           now_ms - entries_.front().stored_at_ms >= retention_ms_) {
        entries_.pop_front();
    }
}

ErrorCode EncodeFrames(
    const std::vector<uint8_t>& json,
    std::size_t att_mtu,
    uint32_t transfer_id,
    std::vector<std::vector<uint8_t>>* frames) {
    if (frames == nullptr || json.size() > kMaximumJsonBytes || json.size() > std::numeric_limits<uint16_t>::max()) {
        return ErrorCode::kPayloadTooLarge;
    }
    if (att_mtu <= 3 + kFrameHeaderSize) {
        return ErrorCode::kFragmentError;
    }
    const std::size_t capacity = att_mtu - 3 - kFrameHeaderSize;
    const std::size_t count = std::max<std::size_t>(1, (json.size() + capacity - 1) / capacity);
    if (count > std::numeric_limits<uint16_t>::max()) {
        return ErrorCode::kPayloadTooLarge;
    }
    frames->clear();
    for (std::size_t index = 0; index < count; ++index) {
        const std::size_t start = index * capacity;
        const std::size_t end = std::min(json.size(), start + capacity);
        std::vector<uint8_t> frame;
        frame.reserve(kFrameHeaderSize + end - start);
        frame.push_back('F');
        frame.push_back('U');
        frame.push_back(1);
        frame.push_back((index == 0 ? kStartFlag : 0) | (index + 1 == count ? kEndFlag : 0));
        Append32(&frame, transfer_id);
        Append16(&frame, static_cast<uint16_t>(index));
        Append16(&frame, static_cast<uint16_t>(count));
        Append16(&frame, static_cast<uint16_t>(json.size()));
        frame.insert(frame.end(), json.begin() + start, json.begin() + end);
        frames->push_back(std::move(frame));
    }
    return ErrorCode::kNone;
}

ErrorCode DecodeFrame(const std::vector<uint8_t>& bytes, Frame* frame) {
    if (frame == nullptr || bytes.size() < kFrameHeaderSize || bytes[0] != 'F' || bytes[1] != 'U' || bytes[2] != 1) {
        return ErrorCode::kFragmentError;
    }
    Frame parsed;
    parsed.flags = bytes[3];
    parsed.transfer_id = Read32(bytes, 4);
    parsed.chunk_index = Read16(bytes, 8);
    parsed.chunk_count = Read16(bytes, 10);
    parsed.total_length = Read16(bytes, 12);
    if ((parsed.flags & ~(kStartFlag | kEndFlag)) != 0 || parsed.chunk_count == 0 ||
        parsed.chunk_index >= parsed.chunk_count || parsed.total_length > kMaximumJsonBytes ||
        (parsed.chunk_index == 0 && (parsed.flags & kStartFlag) == 0) ||
        (parsed.chunk_index + 1 == parsed.chunk_count && (parsed.flags & kEndFlag) == 0)) {
        return ErrorCode::kFragmentError;
    }
    parsed.payload.assign(bytes.begin() + kFrameHeaderSize, bytes.end());
    *frame = std::move(parsed);
    return ErrorCode::kNone;
}

ErrorCode FrameAssembler::Accept(
    const std::vector<uint8_t>& bytes,
    uint64_t now_ms,
    std::optional<std::vector<uint8_t>>* completed) {
    if (completed == nullptr) {
        return ErrorCode::kFragmentError;
    }
    completed->reset();
    if (transfer_.has_value() && now_ms >= transfer_->started_at_ms &&
        now_ms - transfer_->started_at_ms >= timeout_ms_) {
        transfer_.reset();
        return ErrorCode::kExpired;
    }

    Frame frame;
    ErrorCode error = DecodeFrame(bytes, &frame);
    if (error != ErrorCode::kNone) {
        return error;
    }
    if (!transfer_.has_value()) {
        if ((frame.flags & kStartFlag) == 0) {
            return ErrorCode::kFragmentError;
        }
        transfer_ = Transfer{frame.transfer_id, frame.chunk_count, frame.total_length, now_ms, {}};
    }
    if (transfer_->id != frame.transfer_id || transfer_->chunk_count != frame.chunk_count ||
        transfer_->total_length != frame.total_length) {
        return ErrorCode::kFragmentError;
    }
    if (transfer_->chunks.find(frame.chunk_index) != transfer_->chunks.end()) {
        return ErrorCode::kFragmentError;
    }
    transfer_->chunks.emplace(frame.chunk_index, std::move(frame.payload));
    if (transfer_->chunks.size() != transfer_->chunk_count) {
        return ErrorCode::kNone;
    }

    std::vector<uint8_t> result;
    result.reserve(transfer_->total_length);
    for (uint16_t index = 0; index < transfer_->chunk_count; ++index) {
        auto item = transfer_->chunks.find(index);
        if (item == transfer_->chunks.end()) {
            return ErrorCode::kNone;
        }
        result.insert(result.end(), item->second.begin(), item->second.end());
    }
    transfer_.reset();
    if (result.size() != frame.total_length) {
        return ErrorCode::kFragmentError;
    }
    *completed = std::move(result);
    return ErrorCode::kNone;
}

void FrameAssembler::Reset() {
    transfer_.reset();
}

}  // namespace fuji::protocol
