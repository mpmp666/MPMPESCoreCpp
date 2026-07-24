#pragma once

#include <cstdint>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace mpmpes::binary {

class StreamError : public std::runtime_error {
public:
  using std::runtime_error::runtime_error;
};

// PocketMine / RakLib style buffer cursor (network big-endian by default).
class BinaryStream {
public:
  BinaryStream() = default;
  explicit BinaryStream(std::string buffer, std::size_t offset = 0);

  void setBuffer(std::string buffer, std::size_t offset = 0);
  const std::string& buffer() const { return buffer_; }
  std::string& buffer() { return buffer_; }
  std::size_t offset() const { return offset_; }
  void setOffset(std::size_t o) { offset_ = o; }
  bool feof() const { return offset_ >= buffer_.size(); }
  void reset() { buffer_.clear(); offset_ = 0; }

  // ---- read ----
  std::string get(std::size_t len);
  std::string getRemaining();
  std::uint8_t getByte();
  bool getBool();
  std::uint16_t getShort();
  std::int16_t getSignedShort();
  std::uint16_t getLShort();
  std::int16_t getSignedLShort();
  std::uint32_t getTriad();
  std::uint32_t getLTriad();
  std::int32_t getInt();
  std::int32_t getLInt();
  std::int64_t getLong();
  std::int64_t getLLong();
  float getFloat();
  float getLFloat();
  double getDouble();
  double getLDouble();
  std::string getString(); // BE u16 length + bytes (RakLib)

  // ---- write ----
  void put(std::string_view data);
  void putByte(std::uint8_t v);
  void putBool(bool v);
  void putShort(std::uint16_t v);
  void putLShort(std::uint16_t v);
  void putTriad(std::uint32_t v);
  void putLTriad(std::uint32_t v);
  void putInt(std::int32_t v);
  void putLInt(std::int32_t v);
  void putLong(std::int64_t v);
  void putLLong(std::int64_t v);
  void putFloat(float v);
  void putLFloat(float v);
  void putDouble(double v);
  void putLDouble(double v);
  void putString(std::string_view s);

private:
  void need(std::size_t n) const;

  std::string buffer_;
  std::size_t offset_ = 0;
};

// Static helpers (same endian rules as PHP raklib\Binary).
namespace Binary {
  std::uint32_t readTriad(std::string_view s);
  std::string writeTriad(std::uint32_t v);
  std::uint32_t readLTriad(std::string_view s);
  std::string writeLTriad(std::uint32_t v);

  std::uint16_t readShort(std::string_view s);
  std::int16_t readSignedShort(std::string_view s);
  std::string writeShort(std::uint16_t v);
  std::uint16_t readLShort(std::string_view s);
  std::string writeLShort(std::uint16_t v);

  std::int32_t readInt(std::string_view s);
  std::string writeInt(std::int32_t v);
  std::int32_t readLInt(std::string_view s);
  std::string writeLInt(std::int32_t v);

  std::int64_t readLong(std::string_view s);
  std::string writeLong(std::int64_t v);
  std::int64_t readLLong(std::string_view s);
  std::string writeLLong(std::int64_t v);

  float readFloat(std::string_view s);
  std::string writeFloat(float v);
  float readLFloat(std::string_view s);
  std::string writeLFloat(float v);
}

} // namespace mpmpes::binary
