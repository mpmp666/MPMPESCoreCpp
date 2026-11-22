#pragma once
// Cross-version wire translation — port of PocketMine network/MultiProtocol.php.
//
// The server core speaks MCPE 0.14 (protocol 70) internally. Every outgoing
// native buffer is translated to the client's "wire family" right before
// RakNet encapsulation, and every incoming family packet is rewritten into the
// native 0.14 layout before dispatch, so game code never sees other versions.
//
// Families (identical numbering to the PHP core):
//   0 = 0.14.x (45/46/60/70)       native, 0x8e marker
//   1 = 0.15.x (81..89)            remapped ids, 0xfe marker
//   2 = 0.13.x (37..39)            trimmed tails, bare
//   3 = 0.16.x (90..99)            full re-encode (varints), 0xfe marker
//   4 = 0.12.x (34)                trimmed tails + PlayerList/Craft layout, bare
//   5 = 0.11.x (21..33)            old ids, short slots, bare, raw batches

#include <array>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace mpmpes::protocol::mp {

inline constexpr int PROTOCOL_0_11_MIN = 21;
inline constexpr int PROTOCOL_0_11_MAX = 33;
inline constexpr int PROTOCOL_0_12 = 34;
inline constexpr int PROTOCOL_0_13 = 37;
inline constexpr int PROTOCOL_0_13_LAST = 39;
inline constexpr int PROTOCOL_0_15 = 81;
inline constexpr int PROTOCOL_0_16 = 90;

// Native virtual packet ids that only exist on 0.16 (PHP 0xec/0xee/0xef)
inline constexpr std::uint8_t RESOURCE_PACKS_INFO_PACKET = 0xec;
inline constexpr std::uint8_t AVAILABLE_COMMANDS_PACKET = 0xee;
inline constexpr std::uint8_t COMMAND_STEP_PACKET = 0xef;

enum Family : int { F014 = 0, F015 = 1, F013 = 2, F016 = 3, F012 = 4, F011 = 5 };

int wireFamily(int protocol);
inline bool is011(int p) { return p >= PROTOCOL_0_11_MIN && p <= PROTOCOL_0_11_MAX; }
inline bool is012(int p) { return p == PROTOCOL_0_12; }
inline bool is013(int p) { return p >= PROTOCOL_0_13 && p <= PROTOCOL_0_13_LAST; }
inline bool is015(int p) { return p >= PROTOCOL_0_15 && p < PROTOCOL_0_16; }
inline bool is016(int p) { return p >= PROTOCOL_0_16; }
inline bool isJwt(int p) { return p >= PROTOCOL_0_15; }
// Accepted login protocols (PHP Player LOGIN check)
bool isAccepted(int protocol);

// Encapsulation prefix ("\x8e", "\xfe" or "")
std::string_view encapPrefix(int family);

// Native 0.14 buffer -> zero or more family buffers ([pid][payload], no prefix)
std::vector<std::string> translateOutgoing(int family, std::string_view native);

// Build a family batch packet (wrapper included) from translated inner buffers.
// Returns the encoded batch buffer (no encapsulation prefix).
std::string buildBatch(int family, const std::vector<std::string>& inner, int compression_level);

// Incoming family packet ([pid][payload], marker already stripped) -> native buffers.
// Batches are unpacked recursively; returns native inner packets in order.
std::vector<std::string> translateIncoming(int family, std::string_view buf);

// Packet id remaps (exposed for tests)
std::optional<std::uint8_t> toClientPid15(std::uint8_t native);
std::optional<std::uint8_t> toServerPid15(std::uint8_t wire);
std::optional<std::uint8_t> toClientPid11(std::uint8_t native);
std::optional<std::uint8_t> toServerPid11(std::uint8_t wire);

// Creative / recipe item filters for old clients
bool isUnknownToOldProtocol(int item_id);
bool isOldProtocolCreativeItem(int item_id);
bool is012ProtocolCreativeItem(int item_id);
bool is011CreativeItem(int item_id, int meta);

// Native 0.16-only packets (encoded by the core, translated for family 3; dropped elsewhere)
std::string encodeResourcePacksInfo();
std::string encodeAvailableCommands(std::string_view commands_json, std::string_view unknown = "");

// 0.11 AddPlayer needs the skin; native AddPlayer has none. (eid -> skin, slim)
using SkinLookup = std::function<void(std::int64_t eid, std::string& skin, bool& slim)>;
void setSkinLookup(SkinLookup f);
// 0.16 StartGame carries difficulty + world name (MOTD) that native StartGame lacks.
void setStartGame16Info(int difficulty, std::string world_name);

// JWT login (0.15/0.16): parse [protocol int][(0.16: edition byte + uvarint) | int len][zlib].
struct JwtLogin {
  bool ok = false;
  int protocol = 0;
  std::string username;
  std::array<std::uint8_t, 16> uuid{};
  std::int64_t client_id = 0;
  std::string server_address;
  std::string skin_id;
  std::string skin;
};
JwtLogin decodeJwtLogin(std::string_view after_pid);

} // namespace mpmpes::protocol::mp
