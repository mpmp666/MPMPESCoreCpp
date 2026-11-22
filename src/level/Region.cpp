#include "mpmpes/level/Region.hpp"

#include "mpmpes/nbt/Tag.hpp"

#include <algorithm>
#include <ctime>
#include <filesystem>
#include <unistd.h>
#include <vector>

namespace mpmpes::level {

namespace {
constexpr std::uint32_t kMaxSectorLength = 256u << 12;
std::uint32_t be32(const unsigned char* p) {
  return (std::uint32_t(p[0]) << 24) | (std::uint32_t(p[1]) << 16) | (std::uint32_t(p[2]) << 8) |
         std::uint32_t(p[3]);
}
void putBe32(unsigned char* p, std::uint32_t v) {
  p[0] = static_cast<unsigned char>(v >> 24);
  p[1] = static_cast<unsigned char>(v >> 16);
  p[2] = static_cast<unsigned char>(v >> 8);
  p[3] = static_cast<unsigned char>(v);
}
} // namespace

RegionFile::RegionFile(std::string path, int rx, int rz) : path_(std::move(path)), rx_(rx), rz_(rz) {
  bool exists = std::filesystem::exists(path_);
  if (!exists) {
    std::FILE* t = std::fopen(path_.c_str(), "wb");
    if (t) std::fclose(t);
  }
  fp_ = std::fopen(path_.c_str(), "r+b");
  if (!fp_) return;
  std::fseek(fp_, 0, SEEK_END);
  long sz = std::ftell(fp_);
  if (!exists || sz < 8192) {
    createBlank();
  } else {
    loadLocationTable();
  }
}

RegionFile::~RegionFile() {
  if (fp_) {
    writeLocationTable();
    std::fclose(fp_);
  }
}

void RegionFile::createBlank() {
  std::fseek(fp_, 0, SEEK_SET);
  std::vector<unsigned char> zero(8192, 0);
  std::fwrite(zero.data(), 1, zero.size(), fp_);
  offset_.fill(0);
  sectors_.fill(0);
  timestamp_.fill(0);
  last_sector_ = 1;
  std::fflush(fp_);
}

void RegionFile::loadLocationTable() {
  std::fseek(fp_, 0, SEEK_SET);
  std::vector<unsigned char> buf(8192, 0);
  std::size_t n = std::fread(buf.data(), 1, buf.size(), fp_);
  (void)n;
  last_sector_ = 1;
  for (int i = 0; i < 1024; ++i) {
    std::uint32_t idx = be32(&buf[static_cast<std::size_t>(i) * 4]);
    offset_[i] = idx >> 8;
    sectors_[i] = idx & 0xff;
    timestamp_[i] = be32(&buf[4096 + static_cast<std::size_t>(i) * 4]);
    if (offset_[i] + sectors_[i] > 0 && offset_[i] + sectors_[i] - 1 > last_sector_)
      last_sector_ = offset_[i] + sectors_[i] - 1;
  }
}

void RegionFile::writeLocationTable() {
  if (!fp_) return;
  std::vector<unsigned char> buf(8192, 0);
  for (int i = 0; i < 1024; ++i) {
    putBe32(&buf[static_cast<std::size_t>(i) * 4], (offset_[i] << 8) | (sectors_[i] & 0xff));
    putBe32(&buf[4096 + static_cast<std::size_t>(i) * 4], timestamp_[i]);
  }
  std::fseek(fp_, 0, SEEK_SET);
  std::fwrite(buf.data(), 1, buf.size(), fp_);
  std::fflush(fp_);
}

void RegionFile::writeLocationIndex(int index) {
  unsigned char b[4];
  putBe32(b, (offset_[index] << 8) | (sectors_[index] & 0xff));
  std::fseek(fp_, index * 4, SEEK_SET);
  std::fwrite(b, 1, 4, fp_);
  putBe32(b, timestamp_[index]);
  std::fseek(fp_, 4096 + index * 4, SEEK_SET);
  std::fwrite(b, 1, 4, fp_);
}

bool RegionFile::chunkExists(int lx, int lz) const {
  int i = lx + (lz << 5);
  if (i < 0 || i >= 1024) return false;
  return offset_[i] != 0 && sectors_[i] != 0;
}

std::optional<std::string> RegionFile::readChunkRaw(int lx, int lz) {
  if (!fp_) return std::nullopt;
  int i = lx + (lz << 5);
  if (i < 0 || i >= 1024 || !chunkExists(lx, lz)) return std::nullopt;
  std::fseek(fp_, static_cast<long>(offset_[i]) << 12, SEEK_SET);
  unsigned char hdr[5];
  if (std::fread(hdr, 1, 5, fp_) != 5) return std::nullopt;
  std::uint32_t length = be32(hdr);
  unsigned char compression = hdr[4];
  if (length == 0 || length > kMaxSectorLength) return std::nullopt;
  if (length > (sectors_[i] << 12)) {
    sectors_[i] = length >> 12;
    writeLocationIndex(i);
  } else if (compression != 1 && compression != 2) {
    return std::nullopt;
  }
  std::string data(length - 1, '\0');
  if (std::fread(data.data(), 1, data.size(), fp_) != data.size()) return std::nullopt;
  return data;
}

bool RegionFile::writeChunkRaw(int lx, int lz, const std::string& compressed) {
  if (!fp_) return false;
  std::uint32_t length = static_cast<std::uint32_t>(compressed.size()) + 1;
  if (length + 4 > kMaxSectorLength) return false;
  std::uint32_t sectors = (length + 4 + 4095) / 4096;
  if (sectors > 0xff) return false;
  int i = lx + (lz << 5);
  bool changed = false;
  if (sectors_[i] < sectors) {
    offset_[i] = last_sector_ + 1;
    last_sector_ += sectors;
    changed = true;
  } else if (sectors_[i] != sectors) {
    changed = true;
  }
  sectors_[i] = sectors;
  timestamp_[i] = static_cast<std::uint32_t>(std::time(nullptr));
  std::fseek(fp_, static_cast<long>(offset_[i]) << 12, SEEK_SET);
  unsigned char hdr[5];
  putBe32(hdr, length);
  hdr[4] = 2; // zlib
  std::fwrite(hdr, 1, 5, fp_);
  std::fwrite(compressed.data(), 1, compressed.size(), fp_);
  std::size_t pad = (static_cast<std::size_t>(sectors) << 12) - (compressed.size() + 5);
  static const std::vector<char> zero(4096, 0);
  while (pad > 0) {
    std::size_t k = std::min<std::size_t>(pad, 4096);
    std::fwrite(zero.data(), 1, k, fp_);
    pad -= k;
  }
  (void)changed;
  writeLocationIndex(i); // timestamp always refreshed
  return true;
}

void RegionFile::removeChunk(int lx, int lz) {
  int i = lx + (lz << 5);
  offset_[i] = 0;
  sectors_[i] = 0;
  writeLocationIndex(i);
}

void RegionFile::flush() {
  if (fp_) std::fflush(fp_);
}

int RegionFile::compact() {
  if (!fp_) return 0;
  struct E {
    int idx;
    std::uint32_t off, n;
  };
  std::vector<E> list;
  for (int i = 0; i < 1024; ++i)
    if (offset_[i] && sectors_[i]) list.push_back({i, offset_[i], sectors_[i]});
  std::sort(list.begin(), list.end(), [](const E& a, const E& b) { return a.off < b.off; });
  std::fseek(fp_, 0, SEEK_END);
  long file_size = std::ftell(fp_);
  std::uint32_t end = 2;
  for (auto& e : list) {
    if (e.off < end || (static_cast<long>(e.off + e.n) << 12) > file_size) return 0; // overlap: refuse
    end = e.off + e.n;
  }
  std::uint32_t next = 2;
  for (auto& e : list) {
    std::size_t bytes = static_cast<std::size_t>(e.n) << 12;
    if (e.off != next) {
      std::string buf(bytes, '\0');
      std::fseek(fp_, static_cast<long>(e.off) << 12, SEEK_SET);
      if (std::fread(buf.data(), 1, bytes, fp_) != bytes) return 0;
      std::fseek(fp_, static_cast<long>(next) << 12, SEEK_SET);
      std::fwrite(buf.data(), 1, bytes, fp_);
      offset_[e.idx] = next;
    }
    next += e.n;
  }
  std::fflush(fp_);
  if (ftruncate(fileno(fp_), static_cast<off_t>(next) << 12) != 0) {
  }
  last_sector_ = next - 1;
  writeLocationTable();
  return std::max<int>(0, static_cast<int>((file_size >> 12) - next));
}

RegionFile* RegionCache::get(int cx, int cz, bool create) {
  int rx = cx >> 5, rz = cz >> 5;
  std::int64_t key = (static_cast<std::int64_t>(static_cast<std::uint32_t>(rx)) << 32) |
                     static_cast<std::uint32_t>(rz);
  auto it = files_.find(key);
  if (it != files_.end()) return it->second.get();
  std::string path = dir_ + "/r." + std::to_string(rx) + "." + std::to_string(rz) + "." + ext_;
  if (!create && !std::filesystem::exists(path)) return nullptr;
  std::error_code ec;
  std::filesystem::create_directories(dir_, ec);
  auto rf = std::make_unique<RegionFile>(path, rx, rz);
  if (!rf->ok()) return nullptr;
  auto* raw = rf.get();
  files_.emplace(key, std::move(rf));
  return raw;
}

std::optional<std::string> RegionCache::readChunk(int cx, int cz) {
  std::lock_guard lock(mu_);
  auto* rf = get(cx, cz, false);
  if (!rf) return std::nullopt;
  auto raw = rf->readChunkRaw(cx & 31, cz & 31);
  if (!raw) return std::nullopt;
  return nbt::inflateAny(*raw);
}

bool RegionCache::writeChunk(int cx, int cz, const std::string& nbt_raw, int level) {
  std::lock_guard lock(mu_);
  auto* rf = get(cx, cz, true);
  if (!rf) return false;
  auto z = nbt::zlibCompress(nbt_raw, level);
  if (z.empty()) return false;
  return rf->writeChunkRaw(cx & 31, cz & 31, z);
}

bool RegionCache::hasChunk(int cx, int cz) {
  std::lock_guard lock(mu_);
  auto* rf = get(cx, cz, false);
  return rf && rf->chunkExists(cx & 31, cz & 31);
}

void RegionCache::flushAll() {
  std::lock_guard lock(mu_);
  for (auto& [_, f] : files_) f->flush();
}

void RegionCache::closeAll() {
  std::lock_guard lock(mu_);
  files_.clear();
}

} // namespace mpmpes::level
