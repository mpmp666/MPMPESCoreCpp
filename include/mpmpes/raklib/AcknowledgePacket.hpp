#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace mpmpes::raklib {

// ACK (0xc0) / NACK (0xa0) payload — mirrors PHP AcknowledgePacket.
struct AcknowledgePacket {
  std::uint8_t id = 0; // 0xc0 or 0xa0
  std::vector<std::uint32_t> packets;

  std::string encode() const;
  bool decode(std::string_view buffer);
};

} // namespace mpmpes::raklib
