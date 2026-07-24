#include "mpmpes/raklib/EncapsulatedPacket.hpp"

#include "mpmpes/binary/BinaryStream.hpp"

#include <cmath>

namespace mpmpes::raklib {
namespace {
namespace Binary = mpmpes::binary::Binary;
using mpmpes::binary::StreamError;
} // namespace

std::pair<EncapsulatedPacket, std::size_t> EncapsulatedPacket::fromBinary(
    std::string_view binary, bool internal) {
  EncapsulatedPacket packet;
  if (binary.empty()) {
    return {packet, 0};
  }

  try {
    const auto flags = static_cast<std::uint8_t>(binary[0]);
    packet.reliability = static_cast<std::uint8_t>((flags & 0b11100000) >> 5);
    packet.has_split = (flags & 0b00010000) != 0;

    std::size_t offset = 0;
    std::size_t length = 0;

    if (internal) {
      if (binary.size() < 9) return {packet, 0};
      length = static_cast<std::size_t>(Binary::readInt(binary.substr(1, 4)));
      packet.identifier_ack = Binary::readInt(binary.substr(5, 4));
      offset = 9;
    } else {
      if (binary.size() < 3) return {packet, 0};
      const auto bits = Binary::readShort(binary.substr(1, 2));
      length = static_cast<std::size_t>(std::ceil(bits / 8.0));
      offset = 3;
      packet.identifier_ack = std::nullopt;
    }

    const auto rel = packet.reliability;
    if (rel > 0) {
      if (rel >= 2 && rel != 5) {
        if (binary.size() < offset + 3) return {packet, 0};
        packet.message_index = Binary::readLTriad(binary.substr(offset, 3));
        offset += 3;
      }
      if (rel <= 4 && rel != 2) {
        if (binary.size() < offset + 4) return {packet, 0};
        packet.order_index = Binary::readLTriad(binary.substr(offset, 3));
        offset += 3;
        packet.order_channel = static_cast<std::uint8_t>(binary[offset++]);
      }
    }

    if (packet.has_split) {
      if (binary.size() < offset + 10) return {packet, 0};
      packet.split_count = Binary::readInt(binary.substr(offset, 4));
      offset += 4;
      packet.split_id = Binary::readShort(binary.substr(offset, 2));
      offset += 2;
      packet.split_index = Binary::readInt(binary.substr(offset, 4));
      offset += 4;
    }

    if (binary.size() < offset + length) return {packet, 0};
    packet.buffer = std::string(binary.substr(offset, length));
    offset += length;
    return {std::move(packet), offset};
  } catch (const StreamError&) {
    return {EncapsulatedPacket{}, 0};
  }
}

std::size_t EncapsulatedPacket::totalLength() const {
  return 3 + buffer.size() + (message_index ? 3 : 0) + (order_index ? 4 : 0) +
         (has_split ? 10 : 0);
}

std::string EncapsulatedPacket::toBinary(bool internal) const {
  mpmpes::binary::BinaryStream out;
  const std::uint8_t flags = static_cast<std::uint8_t>(
      (reliability << 5) | (has_split ? 0b00010000 : 0));
  out.putByte(flags);

  if (internal) {
    out.putInt(static_cast<std::int32_t>(buffer.size()));
    out.putInt(identifier_ack.value_or(0));
  } else {
    out.putShort(static_cast<std::uint16_t>(buffer.size() << 3));
  }

  if (reliability > 0) {
    if (reliability >= 2 && reliability != 5) {
      out.putLTriad(message_index.value_or(0));
    }
    if (reliability <= 4 && reliability != 2) {
      out.putLTriad(order_index.value_or(0));
      out.putByte(order_channel.value_or(0));
    }
  }

  if (has_split) {
    out.putInt(split_count.value_or(0));
    out.putShort(split_id.value_or(0));
    out.putInt(split_index.value_or(0));
  }

  out.put(buffer);
  return out.buffer();
}

} // namespace mpmpes::raklib
