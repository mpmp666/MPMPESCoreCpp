#include "mpmpes/raklib/AcknowledgePacket.hpp"

#include "mpmpes/binary/BinaryStream.hpp"

#include <algorithm>

namespace mpmpes::raklib {
namespace {
using mpmpes::binary::BinaryStream;
using mpmpes::binary::StreamError;
} // namespace

std::string AcknowledgePacket::encode() const {
  BinaryStream out;
  out.putByte(id);

  auto sorted = packets;
  std::sort(sorted.begin(), sorted.end());
  // dedupe
  sorted.erase(std::unique(sorted.begin(), sorted.end()), sorted.end());

  std::string payload;
  std::uint16_t records = 0;

  if (!sorted.empty()) {
    std::size_t pointer = 1;
    std::uint32_t start = sorted[0];
    std::uint32_t last = sorted[0];

    auto flush_single = [&](std::uint32_t v) {
      payload.push_back('\x01');
      payload += mpmpes::binary::Binary::writeLTriad(v);
      ++records;
    };
    auto flush_range = [&](std::uint32_t a, std::uint32_t b) {
      payload.push_back('\x00');
      payload += mpmpes::binary::Binary::writeLTriad(a);
      payload += mpmpes::binary::Binary::writeLTriad(b);
      ++records;
    };

    while (pointer < sorted.size()) {
      const auto current = sorted[pointer++];
      const auto diff = current - last;
      if (diff == 1) {
        last = current;
      } else if (diff > 1) {
        if (start == last) {
          flush_single(start);
        } else {
          flush_range(start, last);
        }
        start = last = current;
      }
    }
    if (start == last) {
      flush_single(start);
    } else {
      flush_range(start, last);
    }
  }

  out.putShort(records);
  out.put(payload);
  return out.buffer();
}

bool AcknowledgePacket::decode(std::string_view buffer) {
  packets.clear();
  if (buffer.empty()) return false;
  try {
    BinaryStream in{std::string(buffer)};
    id = in.getByte();
    const auto count = in.getShort();
    std::size_t cnt = 0;
    for (std::uint16_t i = 0; i < count && !in.feof() && cnt < 4096; ++i) {
      if (in.getByte() == 0) {
        auto start = in.getLTriad();
        auto end = in.getLTriad();
        if (end < start) return false;
        if ((end - start) > 512) end = start + 512;
        for (auto c = start; c <= end; ++c) {
          packets.push_back(c);
          ++cnt;
        }
      } else {
        packets.push_back(in.getLTriad());
        ++cnt;
      }
    }
    return true;
  } catch (const StreamError&) {
    return false;
  }
}

} // namespace mpmpes::raklib
