#include "mpmpes/binary/BinaryStream.hpp"

#include <cstring>
#include <utility>

namespace mpmpes::binary {
namespace {

template <typename T>
T bit_cast_from(std::string_view s) {
  if (s.size() < sizeof(T)) {
    throw StreamError("not enough bytes for bit_cast");
  }
  T v{};
  std::memcpy(&v, s.data(), sizeof(T));
  return v;
}

std::string reverse_bytes(std::string_view s) {
  return std::string(s.rbegin(), s.rend());
}

bool host_is_big_endian() {
  const std::uint16_t x = 0x0102;
  return *reinterpret_cast<const std::uint8_t*>(&x) == 0x01;
}

} // namespace

BinaryStream::BinaryStream(std::string buffer, std::size_t offset)
    : buffer_(std::move(buffer)), offset_(offset) {}

void BinaryStream::setBuffer(std::string buffer, std::size_t offset) {
  buffer_ = std::move(buffer);
  offset_ = offset;
}

void BinaryStream::need(std::size_t n) const {
  if (offset_ + n > buffer_.size()) {
    throw StreamError("BinaryStream underflow");
  }
}

std::string BinaryStream::get(std::size_t len) {
  need(len);
  std::string out = buffer_.substr(offset_, len);
  offset_ += len;
  return out;
}

std::string BinaryStream::getRemaining() {
  if (offset_ >= buffer_.size()) {
    return {};
  }
  std::string out = buffer_.substr(offset_);
  offset_ = buffer_.size();
  return out;
}

std::uint8_t BinaryStream::getByte() {
  need(1);
  return static_cast<std::uint8_t>(buffer_[offset_++]);
}

bool BinaryStream::getBool() { return getByte() != 0; }

std::uint16_t BinaryStream::getShort() {
  return Binary::readShort(get(2));
}

std::int16_t BinaryStream::getSignedShort() {
  return Binary::readSignedShort(get(2));
}

std::uint16_t BinaryStream::getLShort() {
  return Binary::readLShort(get(2));
}

std::int16_t BinaryStream::getSignedLShort() {
  auto u = Binary::readLShort(get(2));
  return static_cast<std::int16_t>(u);
}

std::uint32_t BinaryStream::getTriad() {
  return Binary::readTriad(get(3));
}

std::uint32_t BinaryStream::getLTriad() {
  return Binary::readLTriad(get(3));
}

std::int32_t BinaryStream::getInt() {
  return Binary::readInt(get(4));
}

std::int32_t BinaryStream::getLInt() {
  return Binary::readLInt(get(4));
}

std::int64_t BinaryStream::getLong() {
  return Binary::readLong(get(8));
}

std::int64_t BinaryStream::getLLong() {
  return Binary::readLLong(get(8));
}

float BinaryStream::getFloat() {
  return Binary::readFloat(get(4));
}

float BinaryStream::getLFloat() {
  return Binary::readLFloat(get(4));
}

double BinaryStream::getDouble() {
  auto s = get(8);
  if (host_is_big_endian()) {
    return bit_cast_from<double>(s);
  }
  auto r = reverse_bytes(s);
  return bit_cast_from<double>(r);
}

double BinaryStream::getLDouble() {
  auto s = get(8);
  if (host_is_big_endian()) {
    auto r = reverse_bytes(s);
    return bit_cast_from<double>(r);
  }
  return bit_cast_from<double>(s);
}

std::string BinaryStream::getString() {
  auto len = getShort();
  return get(len);
}

void BinaryStream::put(std::string_view data) {
  buffer_.append(data.data(), data.size());
}

void BinaryStream::putByte(std::uint8_t v) {
  buffer_.push_back(static_cast<char>(v));
}

void BinaryStream::putBool(bool v) { putByte(v ? 1 : 0); }

void BinaryStream::putShort(std::uint16_t v) { put(Binary::writeShort(v)); }
void BinaryStream::putLShort(std::uint16_t v) { put(Binary::writeLShort(v)); }
void BinaryStream::putTriad(std::uint32_t v) { put(Binary::writeTriad(v)); }
void BinaryStream::putLTriad(std::uint32_t v) { put(Binary::writeLTriad(v)); }
void BinaryStream::putInt(std::int32_t v) { put(Binary::writeInt(v)); }
void BinaryStream::putLInt(std::int32_t v) { put(Binary::writeLInt(v)); }
void BinaryStream::putLong(std::int64_t v) { put(Binary::writeLong(v)); }
void BinaryStream::putLLong(std::int64_t v) { put(Binary::writeLLong(v)); }
void BinaryStream::putFloat(float v) { put(Binary::writeFloat(v)); }
void BinaryStream::putLFloat(float v) { put(Binary::writeLFloat(v)); }

void BinaryStream::putDouble(double v) {
  char raw[8];
  std::memcpy(raw, &v, 8);
  if (host_is_big_endian()) {
    put(std::string_view(raw, 8));
  } else {
    put(reverse_bytes(std::string_view(raw, 8)));
  }
}

void BinaryStream::putLDouble(double v) {
  char raw[8];
  std::memcpy(raw, &v, 8);
  if (host_is_big_endian()) {
    put(reverse_bytes(std::string_view(raw, 8)));
  } else {
    put(std::string_view(raw, 8));
  }
}

void BinaryStream::putString(std::string_view s) {
  if (s.size() > 0xFFFF) {
    throw StreamError("string too long for u16 length");
  }
  putShort(static_cast<std::uint16_t>(s.size()));
  put(s);
}

// ---- Binary static ----

std::uint32_t Binary::readTriad(std::string_view s) {
  if (s.size() < 3) throw StreamError("triad short");
  return (static_cast<std::uint32_t>(static_cast<std::uint8_t>(s[0])) << 16) |
         (static_cast<std::uint32_t>(static_cast<std::uint8_t>(s[1])) << 8) |
         (static_cast<std::uint32_t>(static_cast<std::uint8_t>(s[2])));
}

std::string Binary::writeTriad(std::uint32_t v) {
  std::string o(3, '\0');
  o[0] = static_cast<char>((v >> 16) & 0xFF);
  o[1] = static_cast<char>((v >> 8) & 0xFF);
  o[2] = static_cast<char>(v & 0xFF);
  return o;
}

std::uint32_t Binary::readLTriad(std::string_view s) {
  if (s.size() < 3) throw StreamError("ltriad short");
  return (static_cast<std::uint32_t>(static_cast<std::uint8_t>(s[0]))) |
         (static_cast<std::uint32_t>(static_cast<std::uint8_t>(s[1])) << 8) |
         (static_cast<std::uint32_t>(static_cast<std::uint8_t>(s[2])) << 16);
}

std::string Binary::writeLTriad(std::uint32_t v) {
  std::string o(3, '\0');
  o[0] = static_cast<char>(v & 0xFF);
  o[1] = static_cast<char>((v >> 8) & 0xFF);
  o[2] = static_cast<char>((v >> 16) & 0xFF);
  return o;
}

std::uint16_t Binary::readShort(std::string_view s) {
  if (s.size() < 2) throw StreamError("short short");
  return static_cast<std::uint16_t>(
      (static_cast<std::uint16_t>(static_cast<std::uint8_t>(s[0])) << 8) |
      static_cast<std::uint16_t>(static_cast<std::uint8_t>(s[1])));
}

std::int16_t Binary::readSignedShort(std::string_view s) {
  return static_cast<std::int16_t>(readShort(s));
}

std::string Binary::writeShort(std::uint16_t v) {
  std::string o(2, '\0');
  o[0] = static_cast<char>((v >> 8) & 0xFF);
  o[1] = static_cast<char>(v & 0xFF);
  return o;
}

std::uint16_t Binary::readLShort(std::string_view s) {
  if (s.size() < 2) throw StreamError("lshort short");
  return static_cast<std::uint16_t>(
      static_cast<std::uint16_t>(static_cast<std::uint8_t>(s[0])) |
      (static_cast<std::uint16_t>(static_cast<std::uint8_t>(s[1])) << 8));
}

std::string Binary::writeLShort(std::uint16_t v) {
  std::string o(2, '\0');
  o[0] = static_cast<char>(v & 0xFF);
  o[1] = static_cast<char>((v >> 8) & 0xFF);
  return o;
}

std::int32_t Binary::readInt(std::string_view s) {
  if (s.size() < 4) throw StreamError("int short");
  std::uint32_t u =
      (static_cast<std::uint32_t>(static_cast<std::uint8_t>(s[0])) << 24) |
      (static_cast<std::uint32_t>(static_cast<std::uint8_t>(s[1])) << 16) |
      (static_cast<std::uint32_t>(static_cast<std::uint8_t>(s[2])) << 8) |
      (static_cast<std::uint32_t>(static_cast<std::uint8_t>(s[3])));
  return static_cast<std::int32_t>(u);
}

std::string Binary::writeInt(std::int32_t v) {
  auto u = static_cast<std::uint32_t>(v);
  std::string o(4, '\0');
  o[0] = static_cast<char>((u >> 24) & 0xFF);
  o[1] = static_cast<char>((u >> 16) & 0xFF);
  o[2] = static_cast<char>((u >> 8) & 0xFF);
  o[3] = static_cast<char>(u & 0xFF);
  return o;
}

std::int32_t Binary::readLInt(std::string_view s) {
  if (s.size() < 4) throw StreamError("lint short");
  std::uint32_t u =
      (static_cast<std::uint32_t>(static_cast<std::uint8_t>(s[0]))) |
      (static_cast<std::uint32_t>(static_cast<std::uint8_t>(s[1])) << 8) |
      (static_cast<std::uint32_t>(static_cast<std::uint8_t>(s[2])) << 16) |
      (static_cast<std::uint32_t>(static_cast<std::uint8_t>(s[3])) << 24);
  return static_cast<std::int32_t>(u);
}

std::string Binary::writeLInt(std::int32_t v) {
  auto u = static_cast<std::uint32_t>(v);
  std::string o(4, '\0');
  o[0] = static_cast<char>(u & 0xFF);
  o[1] = static_cast<char>((u >> 8) & 0xFF);
  o[2] = static_cast<char>((u >> 16) & 0xFF);
  o[3] = static_cast<char>((u >> 24) & 0xFF);
  return o;
}

std::int64_t Binary::readLong(std::string_view s) {
  if (s.size() < 8) throw StreamError("long short");
  std::uint64_t u = 0;
  for (int i = 0; i < 8; ++i) {
    u = (u << 8) | static_cast<std::uint8_t>(s[static_cast<std::size_t>(i)]);
  }
  return static_cast<std::int64_t>(u);
}

std::string Binary::writeLong(std::int64_t v) {
  auto u = static_cast<std::uint64_t>(v);
  std::string o(8, '\0');
  for (int i = 7; i >= 0; --i) {
    o[static_cast<std::size_t>(i)] = static_cast<char>(u & 0xFF);
    u >>= 8;
  }
  return o;
}

std::int64_t Binary::readLLong(std::string_view s) {
  auto r = reverse_bytes(s.substr(0, 8));
  return readLong(r);
}

std::string Binary::writeLLong(std::int64_t v) {
  return reverse_bytes(writeLong(v));
}

float Binary::readFloat(std::string_view s) {
  if (s.size() < 4) throw StreamError("float short");
  if (host_is_big_endian()) {
    return bit_cast_from<float>(s);
  }
  auto r = reverse_bytes(s.substr(0, 4));
  return bit_cast_from<float>(r);
}

std::string Binary::writeFloat(float v) {
  char raw[4];
  std::memcpy(raw, &v, 4);
  if (host_is_big_endian()) {
    return std::string(raw, 4);
  }
  return reverse_bytes(std::string_view(raw, 4));
}

float Binary::readLFloat(std::string_view s) {
  if (s.size() < 4) throw StreamError("lfloat short");
  if (host_is_big_endian()) {
    auto r = reverse_bytes(s.substr(0, 4));
    return bit_cast_from<float>(r);
  }
  return bit_cast_from<float>(s);
}

std::string Binary::writeLFloat(float v) {
  char raw[4];
  std::memcpy(raw, &v, 4);
  if (host_is_big_endian()) {
    return reverse_bytes(std::string_view(raw, 4));
  }
  return std::string(raw, 4);
}

} // namespace mpmpes::binary
