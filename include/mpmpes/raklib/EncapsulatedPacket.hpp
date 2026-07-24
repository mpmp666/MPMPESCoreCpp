#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

namespace mpmpes::raklib {

struct EncapsulatedPacket {
  std::uint8_t reliability = 0;
  bool has_split = false;
  std::optional<std::uint32_t> message_index;
  std::optional<std::uint32_t> order_index;
  std::optional<std::uint8_t> order_channel;
  std::optional<std::int32_t> split_count;
  std::optional<std::uint16_t> split_id;
  std::optional<std::int32_t> split_index;
  std::string buffer;
  bool need_ack = false;
  std::optional<std::int32_t> identifier_ack;

  // Parse from binary. Returns bytes consumed from input, or 0 on failure.
  static std::pair<EncapsulatedPacket, std::size_t> fromBinary(std::string_view binary,
                                                               bool internal = false);

  std::string toBinary(bool internal = false) const;
  std::size_t totalLength() const;
};

} // namespace mpmpes::raklib
