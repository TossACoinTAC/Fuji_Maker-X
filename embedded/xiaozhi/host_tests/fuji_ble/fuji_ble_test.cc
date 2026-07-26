#include "fuji_ble_session.h"

#include <fstream>
#include <iostream>
#include <iterator>
#include <optional>
#include <string>
#include <vector>

namespace {

int failures = 0;

#define CHECK(condition)                                                                      \
    do {                                                                                      \
        if (!(condition)) {                                                                   \
            std::cerr << __FILE__ << ':' << __LINE__ << " check failed: " #condition << '\n'; \
            ++failures;                                                                       \
        }                                                                                     \
    } while (false)

std::string ReadFixture(const char* relative_path) {
    std::ifstream stream(std::string(FUJI_FIXTURE_ROOT) + '/' + relative_path, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>());
}

std::vector<std::vector<uint8_t>> Encode(const std::string& json, std::size_t mtu, uint32_t id) {
    std::vector<std::vector<uint8_t>> frames;
    const std::vector<uint8_t> bytes(json.begin(), json.end());
    CHECK(fuji::protocol::EncodeFrames(bytes, mtu, id, &frames) ==
          fuji::protocol::ErrorCode::kNone);
    return frames;
}

fuji::protocol::ErrorCode Feed(fuji::ble::InboundSession* session,
                               const std::vector<std::vector<uint8_t>>& frames, uint64_t first_ms,
                               uint64_t final_ms,
                               std::optional<fuji::ble::ReceivedMessage>* completed) {
    fuji::protocol::ErrorCode error = fuji::protocol::ErrorCode::kNone;
    for (std::size_t index = 0; index < frames.size(); ++index) {
        const uint64_t now = index + 1 == frames.size() ? final_ms : first_ms;
        error = session->AcceptFrame(frames[index], now, completed);
        if (error != fuji::protocol::ErrorCode::kNone) {
            return error;
        }
    }
    return error;
}

void TestValidAndDuplicateAcrossReconnect() {
    fuji::ble::InboundSession session;
    const std::string json = ReadFixture("valid/food_search_result.json");
    const auto frames = Encode(json, 23, 1);
    std::optional<fuji::ble::ReceivedMessage> completed;
    CHECK(Feed(&session, frames, 100, 200, &completed) == fuji::protocol::ErrorCode::kNone);
    CHECK(completed.has_value());
    CHECK(completed->message.direction == fuji::protocol::Direction::kPhoneToDevice);

    session.ResetConnection();
    completed.reset();
    CHECK(Feed(&session, frames, 300, 400, &completed) == fuji::protocol::ErrorCode::kDuplicate);
    session.ResetForRestart();
    CHECK(Feed(&session, frames, 500, 600, &completed) == fuji::protocol::ErrorCode::kNone);
}

void TestDirectionAndDisconnectReset() {
    fuji::ble::InboundSession session;
    const auto wrong_direction = Encode(ReadFixture("valid/food_search_request.json"), 185, 2);
    std::optional<fuji::ble::ReceivedMessage> completed;
    CHECK(Feed(&session, wrong_direction, 100, 101, &completed) ==
          fuji::protocol::ErrorCode::kInvalidPayload);

    const auto valid = Encode(ReadFixture("valid/food_search_result.json"), 23, 3);
    CHECK(valid.size() > 2);
    CHECK(session.AcceptFrame(valid.front(), 200, &completed) == fuji::protocol::ErrorCode::kNone);
    session.ResetConnection();
    CHECK(session.AcceptFrame(valid[1], 201, &completed) ==
          fuji::protocol::ErrorCode::kFragmentError);
}

void TestMessageTtlStartsAtFirstChunk() {
    const std::string json =
        R"({"version":1,"message_id":"aaaaaaaa-0000-4000-8000-000000000099","request_id":"bbbbbbbb-0000-4000-8000-000000000099","direction":"phone_to_device","type":"action_result","ttl_ms":1,"payload":{"action":"food_search","status":"failed","error_code":"no_results"}})";
    const auto frames = Encode(json, 23, 4);
    CHECK(frames.size() > 1);
    fuji::ble::InboundSession session;
    std::optional<fuji::ble::ReceivedMessage> completed;
    CHECK(Feed(&session, frames, 100, 101, &completed) == fuji::protocol::ErrorCode::kExpired);
    CHECK(!session.LastErrorWasReassemblyTimeout());
}

void TestReassemblyTimeoutIsClassifiedSeparately() {
    const auto frames = Encode(ReadFixture("valid/food_search_result.json"), 23, 5);
    CHECK(frames.size() > 1);
    fuji::ble::InboundSession session;
    std::optional<fuji::ble::ReceivedMessage> completed;
    CHECK(session.AcceptFrame(frames.front(), 100, &completed) == fuji::protocol::ErrorCode::kNone);
    CHECK(session.AcceptFrame(frames[1], 5100, &completed) == fuji::protocol::ErrorCode::kExpired);
    CHECK(session.LastErrorWasReassemblyTimeout());
}

}  // namespace

int main() {
    TestValidAndDuplicateAcrossReconnect();
    TestDirectionAndDisconnectReset();
    TestMessageTtlStartsAtFirstChunk();
    TestReassemblyTimeoutIsClassifiedSeparately();
    if (failures != 0) {
        std::cerr << failures << " Fuji BLE host test(s) failed\n";
        return 1;
    }
    std::cout << "Fuji BLE host tests passed\n";
    return 0;
}
