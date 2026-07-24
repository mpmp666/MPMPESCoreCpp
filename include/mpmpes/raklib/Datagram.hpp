#pragma once

#include "mpmpes/raklib/EncapsulatedPacket.hpp"

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace mpmpes::raklib {

// RakLib DATA_PACKET (0x80-0x8f): pid | LTriad seq | encapsulated...
struct Datagram {
  std::uint8_t id = 0x84; // DATA_PACKET_4
  std::uint32_t seq_number = 0;
  std::vector<EncapsulatedPacket> packets;
  double send_time = 0.0;

  std::string encode() const;
  bool decode(std::string_view buffer);
  std::size_t length() const;
};

} // namespace mpmpes::raklib
