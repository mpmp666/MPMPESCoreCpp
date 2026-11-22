#pragma once
// McRegion (.mcr) / Anvil (.mca) region file I/O — byte-compatible with
// PocketMine level/format/mcregion/RegionLoader.php.

#include <array>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>

namespace mpmpes::level {

class RegionFile {
public:
  RegionFile(std::string path, int rx, int rz);
  ~RegionFile();
  RegionFile(const RegionFile&) = delete;
  RegionFile& operator=(const RegionFile&) = delete;

  bool ok() const { return fp_ != nullptr; }
  // local chunk coords 0..31; returns compressed payload (without length/compression header)
  std::optional<std::string> readChunkRaw(int lx, int lz);
  // Write already zlib-compressed payload
  bool writeChunkRaw(int lx, int lz, const std::string& compressed);
  bool chunkExists(int lx, int lz) const;
  void removeChunk(int lx, int lz);
  void flush();
  // Compact sectors (PHP doSlowCleanUp/cleanGarbage); returns freed sectors
  int compact();

private:
  void loadLocationTable();
  void writeLocationTable();
  void writeLocationIndex(int index);
  void createBlank();

  std::string path_;
  int rx_, rz_;
  std::FILE* fp_ = nullptr;
  std::array<std::uint32_t, 1024> offset_{};   // sector offset
  std::array<std::uint32_t, 1024> sectors_{};  // sector count
  std::array<std::uint32_t, 1024> timestamp_{};
  std::uint32_t last_sector_ = 1;
};

// Caches open region files per directory
class RegionCache {
public:
  explicit RegionCache(std::string region_dir, std::string ext = "mcr")
      : dir_(std::move(region_dir)), ext_(std::move(ext)) {}
  RegionFile* get(int cx, int cz, bool create);
  std::optional<std::string> readChunk(int cx, int cz);       // decompressed NBT bytes
  bool writeChunk(int cx, int cz, const std::string& nbt_raw, int level = 7);
  bool hasChunk(int cx, int cz);
  void flushAll();
  void closeAll();
  const std::string& dir() const { return dir_; }
  const std::string& ext() const { return ext_; }

private:
  std::string dir_;
  std::string ext_;
  std::mutex mu_;
  std::unordered_map<std::int64_t, std::unique_ptr<RegionFile>> files_;
};

} // namespace mpmpes::level
