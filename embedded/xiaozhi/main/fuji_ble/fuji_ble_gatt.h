#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace fuji::ble::gatt {

inline constexpr int32_t kSchemaVersion = 2;
inline constexpr char kServiceUuid[] = "F157CFF7-0A18-4020-8CC0-CB1A0DA5BC22";
inline constexpr char kCommandUuid[] = "43265B1A-2D59-40A3-BC4D-0E2FA73FFC20";
inline constexpr char kEventUuid[] = "FD1BA046-D0DD-415A-A757-A907E36AB912";
inline constexpr char kStateUuid[] = "6406E94B-8721-4CA7-ACE4-5E67D0AFD1FD";

constexpr uint8_t HexNibble(char value) {
    if (value >= '0' && value <= '9') {
        return static_cast<uint8_t>(value - '0');
    }
    if (value >= 'a' && value <= 'f') {
        return static_cast<uint8_t>(value - 'a' + 10);
    }
    return static_cast<uint8_t>(value - 'A' + 10);
}

template <std::size_t N>
constexpr std::array<uint8_t, 16> UuidToLittleEndian(const char (&uuid)[N]) {
    static_assert(N == 37, "Fuji UUIDs must use canonical 36-character notation");
    std::array<uint8_t, 16> canonical = {};
    std::size_t source = 0;
    for (std::size_t byte = 0; byte < canonical.size(); ++byte) {
        while (uuid[source] == '-') {
            ++source;
        }
        canonical[byte] =
            static_cast<uint8_t>((HexNibble(uuid[source]) << 4) | HexNibble(uuid[source + 1]));
        source += 2;
    }

    std::array<uint8_t, 16> little_endian = {};
    for (std::size_t byte = 0; byte < canonical.size(); ++byte) {
        little_endian[byte] = canonical[canonical.size() - 1 - byte];
    }
    return little_endian;
}

inline constexpr auto kServiceUuidLittleEndian = UuidToLittleEndian(kServiceUuid);
inline constexpr auto kCommandUuidLittleEndian = UuidToLittleEndian(kCommandUuid);
inline constexpr auto kEventUuidLittleEndian = UuidToLittleEndian(kEventUuid);
inline constexpr auto kStateUuidLittleEndian = UuidToLittleEndian(kStateUuid);

static_assert(kEventUuidLittleEndian[0] == 0x12 && kEventUuidLittleEndian[15] == 0xFD);
static_assert(kStateUuidLittleEndian[0] == 0xFD && kStateUuidLittleEndian[15] == 0x64);

}  // namespace fuji::ble::gatt
