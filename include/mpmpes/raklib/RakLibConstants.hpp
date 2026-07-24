#pragma once

#include <cstddef>
#include <cstdint>
#include <string_view>

namespace mpmpes::raklib {

// Mirrors PHP raklib\RakLib
inline constexpr int PROTOCOL = 6;
// NOTE: offline magic starts with 0x00 — must use explicit length (not strlen).
inline constexpr char MAGIC_BYTES[16] = {
    '\x00', '\xff', '\xff', '\x00', '\xfe', '\xfe', '\xfe', '\xfe',
    '\xfd', '\xfd', '\xfd', '\xfd', '\x12', '\x34', '\x56', '\x78'};
inline constexpr std::string_view MAGIC{MAGIC_BYTES, 16};

// Offline message IDs
inline constexpr std::uint8_t ID_UNCONNECTED_PING = 0x01;
inline constexpr std::uint8_t ID_UNCONNECTED_PING_OPEN_CONNECTIONS = 0x02;
inline constexpr std::uint8_t ID_OPEN_CONNECTION_REQUEST_1 = 0x05;
inline constexpr std::uint8_t ID_OPEN_CONNECTION_REPLY_1 = 0x06;
inline constexpr std::uint8_t ID_OPEN_CONNECTION_REQUEST_2 = 0x07;
inline constexpr std::uint8_t ID_OPEN_CONNECTION_REPLY_2 = 0x08;
inline constexpr std::uint8_t ID_CONNECTION_REQUEST = 0x09;
inline constexpr std::uint8_t ID_UNCONNECTED_PONG = 0x1c;
inline constexpr std::uint8_t ID_ADVERTISE_SYSTEM = 0x1d;

// Connected datagram IDs (0x80-0x8f range used for DATA_PACKET_*)
inline constexpr std::uint8_t ID_DATA_PACKET_0 = 0x80;
inline constexpr std::uint8_t ID_NACK = 0xa0;
inline constexpr std::uint8_t ID_ACK = 0xc0;

// MCPE 0.14 protocol (pocketmine\network\protocol\Info)
namespace mcpe {
inline constexpr int CURRENT_PROTOCOL = 70;
inline constexpr std::uint8_t LOGIN_PACKET = 0x8f;
inline constexpr std::uint8_t PLAY_STATUS_PACKET = 0x90;
inline constexpr std::uint8_t DISCONNECT_PACKET = 0x91;
inline constexpr std::uint8_t BATCH_PACKET = 0x92;
inline constexpr std::uint8_t TEXT_PACKET = 0x93;
inline constexpr std::uint8_t SET_TIME_PACKET = 0x94;
inline constexpr std::uint8_t START_GAME_PACKET = 0x95;
} // namespace mcpe

std::string_view magic();

} // namespace mpmpes::raklib
