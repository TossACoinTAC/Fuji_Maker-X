#include "fuji_protocol.h"

#include <cJSON.h>

#include <algorithm>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <iterator>
#include <optional>
#include <string>
#include <vector>

namespace {

using fuji::protocol::ErrorCode;

int failures = 0;

#define CHECK(condition)                                                                                 \
    do {                                                                                                 \
        if (!(condition)) {                                                                              \
            std::cerr << __FILE__ << ':' << __LINE__ << " check failed: " #condition << '\n';          \
            ++failures;                                                                                  \
        }                                                                                                \
    } while (false)

std::string ReadFile(const std::string& path) {
    std::ifstream stream(path, std::ios::binary);
    if (!stream) {
        std::cerr << "cannot open fixture: " << path << '\n';
        std::exit(2);
    }
    return std::string(std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>());
}

void TestFixtures() {
    const std::string root = FUJI_FIXTURE_ROOT;
    const std::string manifest_json = ReadFile(root + "/manifest.json");
    cJSON* manifest = cJSON_ParseWithLength(manifest_json.data(), manifest_json.size());
    CHECK(cJSON_IsObject(manifest));
    if (!cJSON_IsObject(manifest)) {
        cJSON_Delete(manifest);
        return;
    }

    const cJSON* valid = cJSON_GetObjectItemCaseSensitive(manifest, "valid");
    const cJSON* path = nullptr;
    cJSON_ArrayForEach(path, valid) {
        fuji::protocol::Message message;
        const ErrorCode error = fuji::protocol::ParseAndValidate(ReadFile(root + "/" + path->valuestring), &message);
        if (error != ErrorCode::kNone) {
            std::cerr << path->valuestring << " rejected as " << fuji::protocol::ErrorCodeName(error) << '\n';
            ++failures;
        }
    }

    const cJSON* invalid = cJSON_GetObjectItemCaseSensitive(manifest, "invalid");
    const cJSON* fixture = nullptr;
    cJSON_ArrayForEach(fixture, invalid) {
        const cJSON* fixture_path = cJSON_GetObjectItemCaseSensitive(fixture, "path");
        const cJSON* expected = cJSON_GetObjectItemCaseSensitive(fixture, "error");
        fuji::protocol::Message message;
        const ErrorCode error = fuji::protocol::ParseAndValidate(
            ReadFile(root + "/" + fixture_path->valuestring), &message);
        if (std::string(fuji::protocol::ErrorCodeName(error)) != expected->valuestring) {
            std::cerr << fixture_path->valuestring << " expected " << expected->valuestring << " but got "
                      << fuji::protocol::ErrorCodeName(error) << '\n';
            ++failures;
        }
    }
    cJSON_Delete(manifest);
}

void TestFraming() {
    const std::string json = ReadFile(std::string(FUJI_FIXTURE_ROOT) + "/valid/food_search_request.json");
    const std::vector<uint8_t> input(json.begin(), json.end());
    for (std::size_t mtu : {23U, 185U, 517U}) {
        std::vector<std::vector<uint8_t>> frames;
        CHECK(fuji::protocol::EncodeFrames(input, mtu, static_cast<uint32_t>(mtu), &frames) == ErrorCode::kNone);
        CHECK(!frames.empty());

        std::vector<std::vector<uint8_t>> reordered;
        reordered.push_back(frames.front());
        reordered.insert(reordered.end(), frames.rbegin(), frames.rend() - 1);
        fuji::protocol::FrameAssembler assembler;
        std::optional<std::vector<uint8_t>> completed;
        for (const auto& frame : reordered) {
            CHECK(assembler.Accept(frame, 1000, &completed) == ErrorCode::kNone);
        }
        CHECK(completed.has_value());
        CHECK(completed == input);
    }

    std::vector<std::vector<uint8_t>> frames;
    CHECK(fuji::protocol::EncodeFrames(input, 185, 42, &frames) == ErrorCode::kNone);
    fuji::protocol::FrameAssembler assembler;
    std::optional<std::vector<uint8_t>> completed;
    CHECK(assembler.Accept(frames[0], 100, &completed) == ErrorCode::kNone);
    CHECK(assembler.Accept(frames[1], 5100, &completed) == ErrorCode::kExpired);

    assembler.Reset();
    CHECK(assembler.Accept(frames[0], 100, &completed) == ErrorCode::kNone);
    CHECK(assembler.Accept(frames[0], 101, &completed) == ErrorCode::kFragmentError);
    std::vector<std::vector<uint8_t>> colliding;
    CHECK(fuji::protocol::EncodeFrames(input, 185, 43, &colliding) == ErrorCode::kNone);
    assembler.Reset();
    CHECK(assembler.Accept(frames[0], 102, &completed) == ErrorCode::kNone);
    CHECK(assembler.Accept(colliding[0], 103, &completed) == ErrorCode::kFragmentError);

    std::vector<uint8_t> oversized(fuji::protocol::kMaximumJsonBytes + 1, 0);
    CHECK(fuji::protocol::EncodeFrames(oversized, 517, 1, &frames) == ErrorCode::kPayloadTooLarge);
}

void TestDeduplicationAndReset() {
    fuji::protocol::Message first;
    fuji::protocol::Message second;
    fuji::protocol::Message third;
    CHECK(fuji::protocol::ParseAndValidate(
              ReadFile(std::string(FUJI_FIXTURE_ROOT) + "/valid/food_search_request.json"), &first) == ErrorCode::kNone);
    second = first;
    third = first;
    second.message_id = "aaaaaaaa-0000-4000-8000-000000000001";
    third.message_id = "aaaaaaaa-0000-4000-8000-000000000002";

    fuji::protocol::MessageDeduplicator cache(2);
    CHECK(cache.Accept(first, 100) == ErrorCode::kNone);
    CHECK(cache.Accept(first, 101) == ErrorCode::kDuplicate);
    CHECK(cache.Accept(second, 102) == ErrorCode::kNone);
    CHECK(cache.Accept(third, 103) == ErrorCode::kNone);
    CHECK(cache.Size() == 2);
    CHECK(cache.Accept(first, 104) == ErrorCode::kNone);
    cache.Reset();
    CHECK(cache.Size() == 0);
    CHECK(cache.Accept(first, 105) == ErrorCode::kNone);
}

void TestActionResultCache() {
    fuji::protocol::ActionResultCache cache(2, 600000);
    cache.Store("request-1", "navigation_launched", 100);
    CHECK(cache.Get("request-1", 101) == "navigation_launched");
    cache.Store("request-2", "failed", 102);
    cache.Store("request-3", "cancelled", 103);
    CHECK(!cache.Get("request-1", 104).has_value());
    cache.Store("request-4", "navigation_ready", 200);
    CHECK(!cache.Get("request-4", 600200).has_value());
    cache.Reset();
    CHECK(cache.Size() == 0);
}

}  // namespace

int main() {
    TestFixtures();
    TestFraming();
    TestDeduplicationAndReset();
    TestActionResultCache();
    if (failures != 0) {
        std::cerr << failures << " Fuji protocol host test(s) failed\n";
        return 1;
    }
    std::cout << "Fuji protocol host tests passed\n";
    return 0;
}
