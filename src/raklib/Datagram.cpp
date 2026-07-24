#include "mpmpes/raklib/Datagram.hpp"

#include "mpmpes/binary/BinaryStream.hpp"

namespace mpmpes::raklib {

std::size_t Datagram::length() const {
  std::size_t n = 4;
  for (const auto& p : packets) {
    n += p.totalLength();
  }
  return n;
}

std::string Datagram::encode() const {
  mpmpes::binary::BinaryStream out;
  out.putByte(id);
  out.putLTriad(seq_number);
  for (const auto& p : packets) {
    out.put(p.toBinary(false));
  }
  return out.buffer();
}

bool Datagram::decode(std::string_view buffer) {
  packets.clear();
  if (buffer.size() < 4) return false;
  id = static_cast<std::uint8_t>(buffer[0]);
  seq_number = mpmpes::binary::Binary::readLTriad(buffer.substr(1, 3));
  std::size_t offset = 4;
  while (offset < buffer.size()) {
    auto [pk, used] = EncapsulatedPacket::fromBinary(buffer.substr(offset), false);
    if (used == 0 || pk.buffer.empty()) break;
    offset += used;
    packets.push_back(std::move(pk));
  }
  return true;
}

} // namespace mpmpes::raklib
