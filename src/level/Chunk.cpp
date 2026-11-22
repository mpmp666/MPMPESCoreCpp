#include "mpmpes/level/Chunk.hpp"

#include "mpmpes/binary/BinaryStream.hpp"
#include "mpmpes/level/gen/NormalGenerator.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <fstream>

namespace mpmpes::level {
namespace {

void setNibble(std::vector<std::uint8_t>& arr, int index, std::uint8_t nibble) {
  const int i = index >> 1;
  if (i < 0 || static_cast<std::size_t>(i) >= arr.size()) return;
  nibble &= 0x0f;
  if (index & 1) {
    arr[static_cast<std::size_t>(i)] = static_cast<std::uint8_t>((arr[static_cast<std::size_t>(i)] & 0x0f) | (nibble << 4));
  } else {
    arr[static_cast<std::size_t>(i)] = static_cast<std::uint8_t>((arr[static_cast<std::size_t>(i)] & 0xf0) | nibble);
  }
}

std::uint8_t getNibble(const std::vector<std::uint8_t>& arr, int index) {
  const int i = index >> 1;
  if (i < 0 || static_cast<std::size_t>(i) >= arr.size()) return 0;
  if (index & 1) return static_cast<std::uint8_t>((arr[static_cast<std::size_t>(i)] >> 4) & 0x0f);
  return static_cast<std::uint8_t>(arr[static_cast<std::size_t>(i)] & 0x0f);
}

std::uint32_t hash2(int x, int z, std::int32_t seed) {
  std::uint32_t h = static_cast<std::uint32_t>(seed);
  h ^= static_cast<std::uint32_t>(x) * 374761393u;
  h ^= static_cast<std::uint32_t>(z) * 668265263u;
  h = (h ^ (h >> 13)) * 1274126177u;
  return h ^ (h >> 16);
}

} // namespace

Chunk::Chunk(int x, int z) : x_(x), z_(z) {
  blocks_.assign(kBlocksSize, 0);
  data_.assign(kNibbleSize, 0);
  sky_light_.assign(kNibbleSize, 0xff); // full sky light default
  block_light_.assign(kNibbleSize, 0);
  height_map_.assign(kHeightMapSize, 0);
  biome_colors_.assign(kBiomeColorsSize, 0x018DB360); // biome 1 | plains green
}

void Chunk::setBlock(int x, int y, int z, std::uint8_t id, std::uint8_t meta) {
  if (x < 0 || x >= 16 || z < 0 || z >= 16 || y < 0 || y >= kChunkHeight) return;
  const int idx = blockIndex(x, y, z);
  const auto old = blocks_[static_cast<std::size_t>(idx)];
  const auto old_meta = getNibble(data_, idx);
  if (old == id && old_meta == (meta & 0x0f)) return;
  blocks_[static_cast<std::size_t>(idx)] = id;
  setNibble(data_, idx, meta);
  dirty_ = true;
  const int hi = (z << 4) | x;
  if (id != 0) {
    if (height_map_[static_cast<std::size_t>(hi)] < static_cast<std::uint8_t>(y + 1)) {
      height_map_[static_cast<std::size_t>(hi)] = static_cast<std::uint8_t>(y + 1);
    }
  } else if (height_map_[static_cast<std::size_t>(hi)] == static_cast<std::uint8_t>(y + 1)) {
    // top block removed — recompute column height so item drops land correctly
    std::uint8_t h = 0;
    for (int yy = kChunkHeight - 1; yy >= 0; --yy) {
      if (getBlockId(x, yy, z) != 0) {
        h = static_cast<std::uint8_t>(yy + 1);
        break;
      }
    }
    height_map_[static_cast<std::size_t>(hi)] = h;
  }
}

void Chunk::recomputeHeightMap() {
  for (int z = 0; z < 16; ++z) {
    for (int x = 0; x < 16; ++x) {
      std::uint8_t h = 0;
      for (int y = kChunkHeight - 1; y >= 0; --y) {
        if (getBlockId(x, y, z) != 0) {
          h = static_cast<std::uint8_t>(y + 1);
          break;
        }
      }
      height_map_[static_cast<std::size_t>((z << 4) | x)] = h;
    }
  }
}

std::uint8_t Chunk::getBlockId(int x, int y, int z) const {
  if (x < 0 || x >= 16 || z < 0 || z >= 16 || y < 0 || y >= kChunkHeight) return 0;
  return blocks_[static_cast<std::size_t>(blockIndex(x, y, z))];
}

std::uint8_t Chunk::getBlockMeta(int x, int y, int z) const {
  if (x < 0 || x >= 16 || z < 0 || z >= 16 || y < 0 || y >= kChunkHeight) return 0;
  return getNibble(data_, blockIndex(x, y, z));
}

void Chunk::setBiome(int x, int z, std::uint8_t biome_id, std::uint32_t rgb) {
  if (x < 0 || x >= 16 || z < 0 || z >= 16) return;
  const int i = (z << 4) | x;
  biome_colors_[static_cast<std::size_t>(i)] =
      (static_cast<std::uint32_t>(biome_id) << 24) | (rgb & 0x00ffffff);
}

void Chunk::setHeight(int x, int z, std::uint8_t h) {
  if (x < 0 || x >= 16 || z < 0 || z >= 16) return;
  height_map_[static_cast<std::size_t>((z << 4) | x)] = h;
}

bool Chunk::saveToFile(const std::string& path) const {
  std::ofstream out(path, std::ios::binary | std::ios::trunc);
  if (!out) return false;
  // MPC1: magic + cx + cz + blocks + data + height_map + biome_colors
  out.write("MPC1", 4);
  std::int32_t cx = x_, cz = z_;
  out.write(reinterpret_cast<const char*>(&cx), 4);
  out.write(reinterpret_cast<const char*>(&cz), 4);
  out.write(reinterpret_cast<const char*>(blocks_.data()),
            static_cast<std::streamsize>(blocks_.size()));
  out.write(reinterpret_cast<const char*>(data_.data()),
            static_cast<std::streamsize>(data_.size()));
  out.write(reinterpret_cast<const char*>(height_map_.data()),
            static_cast<std::streamsize>(height_map_.size()));
  out.write(reinterpret_cast<const char*>(biome_colors_.data()),
            static_cast<std::streamsize>(biome_colors_.size() * sizeof(std::uint32_t)));
  return static_cast<bool>(out);
}

bool Chunk::loadFromFile(const std::string& path, Chunk& out_chunk) {
  std::ifstream in(path, std::ios::binary);
  if (!in) return false;
  char magic[4]{};
  in.read(magic, 4);
  if (std::memcmp(magic, "MPC1", 4) != 0) return false;
  std::int32_t cx = 0, cz = 0;
  in.read(reinterpret_cast<char*>(&cx), 4);
  in.read(reinterpret_cast<char*>(&cz), 4);
  Chunk c(cx, cz);
  in.read(reinterpret_cast<char*>(c.blocks_.data()),
          static_cast<std::streamsize>(c.blocks_.size()));
  in.read(reinterpret_cast<char*>(c.data_.data()), static_cast<std::streamsize>(c.data_.size()));
  in.read(reinterpret_cast<char*>(c.height_map_.data()),
          static_cast<std::streamsize>(c.height_map_.size()));
  in.read(reinterpret_cast<char*>(c.biome_colors_.data()),
          static_cast<std::streamsize>(c.biome_colors_.size() * sizeof(std::uint32_t)));
  if (!in) return false;
  // sky/block light: full sky default
  c.sky_light_.assign(kNibbleSize, 0xff);
  c.block_light_.assign(kNibbleSize, 0);
  c.setGenerated(true);
  c.clearDirty();
  out_chunk = std::move(c);
  return true;
}

std::uint8_t Chunk::getSkyLight(int x, int y, int z) const {
  if (x < 0 || x >= 16 || z < 0 || z >= 16 || y < 0 || y >= kChunkHeight) return 15;
  return getNibble(sky_light_, blockIndex(x, y, z));
}
void Chunk::setSkyLight(int x, int y, int z, std::uint8_t v) {
  if (x < 0 || x >= 16 || z < 0 || z >= 16 || y < 0 || y >= kChunkHeight) return;
  setNibble(sky_light_, blockIndex(x, y, z), v);
}
std::uint8_t Chunk::getBlockLight(int x, int y, int z) const {
  if (x < 0 || x >= 16 || z < 0 || z >= 16 || y < 0 || y >= kChunkHeight) return 0;
  return getNibble(block_light_, blockIndex(x, y, z));
}
void Chunk::setBlockLight(int x, int y, int z, std::uint8_t v) {
  if (x < 0 || x >= 16 || z < 0 || z >= 16 || y < 0 || y >= kChunkHeight) return;
  setNibble(block_light_, blockIndex(x, y, z), v);
}

nbt::TagPtr Chunk::toNbt() const {
  using nbt::Tag;
  auto lv = Tag::Compound();
  lv->set("xPos", Tag::Int(x_));
  lv->set("zPos", Tag::Int(z_));
  lv->set("LastUpdate", Tag::Long(0));
  lv->set("LightPopulated", Tag::Byte(light_populated_ ? 1 : 0));
  lv->set("TerrainPopulated", Tag::Byte(populated_ ? 1 : 0));
  lv->set("TerrainGenerated", Tag::Byte(generated_ ? 1 : 0));
  lv->set("V", Tag::Byte(1));
  lv->set("InhabitedTime", Tag::Long(0));
  lv->set("Blocks", Tag::ByteArray(std::string(reinterpret_cast<const char*>(blocks_.data()), blocks_.size())));
  lv->set("Data", Tag::ByteArray(std::string(reinterpret_cast<const char*>(data_.data()), data_.size())));
  lv->set("SkyLight", Tag::ByteArray(std::string(reinterpret_cast<const char*>(sky_light_.data()), sky_light_.size())));
  lv->set("BlockLight", Tag::ByteArray(std::string(reinterpret_cast<const char*>(block_light_.data()), block_light_.size())));
  std::vector<std::int32_t> bc(biome_colors_.begin(), biome_colors_.end());
  lv->set("BiomeColors", Tag::IntArray(std::move(bc)));
  std::vector<std::int32_t> hm(height_map_.begin(), height_map_.end());
  lv->set("HeightMap", Tag::IntArray(std::move(hm)));
  // ExtraData: BE int count + (int key, short value)
  std::string ex;
  auto putI = [&](std::uint32_t v) {
    ex.push_back(char(v >> 24)); ex.push_back(char(v >> 16)); ex.push_back(char(v >> 8)); ex.push_back(char(v));
  };
  putI(static_cast<std::uint32_t>(extra_data_.size()));
  for (auto& [k, v] : extra_data_) {
    putI(static_cast<std::uint32_t>(k));
    ex.push_back(char(static_cast<std::uint16_t>(v) >> 8));
    ex.push_back(char(v));
  }
  lv->set("ExtraData", Tag::ByteArray(std::move(ex)));
  return lv;
}

bool Chunk::fromNbt(const nbt::Tag& lv, Chunk& out) {
  Chunk c(static_cast<int>(lv.getInt("xPos")), static_cast<int>(lv.getInt("zPos")));
  auto copyBytes = [](const nbt::TagPtr& t, std::vector<std::uint8_t>& dst) {
    if (!t || t->type != nbt::TAG_ByteArray || t->s.size() != dst.size()) return false;
    std::memcpy(dst.data(), t->s.data(), dst.size());
    return true;
  };
  bool has_blocks = copyBytes(lv.get("Blocks"), c.blocks_);
  if (has_blocks) {
    copyBytes(lv.get("Data"), c.data_);
    if (!copyBytes(lv.get("SkyLight"), c.sky_light_)) c.sky_light_.assign(kNibbleSize, 0xff);
    copyBytes(lv.get("BlockLight"), c.block_light_);
  } else {
    // Anvil-style Sections (Y-major, 16 high, YZX order)
    auto secs = lv.get("Sections");
    if (!secs || secs->type != nbt::TAG_List) return false;
    for (auto& sec : secs->list) {
      if (!sec || sec->type != nbt::TAG_Compound) continue;
      int sy = static_cast<int>(sec->getInt("Y"));
      if (sy < 0 || sy >= 8) continue;
      auto bl = sec->get("Blocks");
      auto da = sec->get("Data");
      auto sl = sec->get("SkyLight");
      auto bk = sec->get("BlockLight");
      if (!bl || bl->s.size() != 4096) continue;
      for (int y = 0; y < 16; ++y)
        for (int z = 0; z < 16; ++z)
          for (int x = 0; x < 16; ++x) {
            int si = (y << 8) | (z << 4) | x;
            int wy = sy * 16 + y;
            int di = blockIndex(x, wy, z);
            c.blocks_[static_cast<std::size_t>(di)] = static_cast<std::uint8_t>(bl->s[static_cast<std::size_t>(si)]);
            auto nib = [&](const nbt::TagPtr& t, std::uint8_t def) -> std::uint8_t {
              if (!t || t->s.size() != 2048) return def;
              std::uint8_t b = static_cast<std::uint8_t>(t->s[static_cast<std::size_t>(si >> 1)]);
              return (si & 1) ? (b >> 4) : (b & 0x0f);
            };
            setNibble(c.data_, di, nib(da, 0));
            setNibble(c.sky_light_, di, nib(sl, 15));
            setNibble(c.block_light_, di, nib(bk, 0));
          }
    }
  }
  auto bc = lv.get("BiomeColors");
  if (bc && bc->type == nbt::TAG_IntArray && bc->ia.size() == 256) {
    for (int i = 0; i < 256; ++i) c.biome_colors_[static_cast<std::size_t>(i)] = static_cast<std::uint32_t>(bc->ia[static_cast<std::size_t>(i)]);
  } else if (auto bio = lv.get("Biomes"); bio && bio->type == nbt::TAG_ByteArray && bio->s.size() == 256) {
    for (int i = 0; i < 256; ++i)
      c.biome_colors_[static_cast<std::size_t>(i)] = (static_cast<std::uint32_t>(static_cast<std::uint8_t>(bio->s[static_cast<std::size_t>(i)])) << 24) | 0x8DB360;
  }
  auto hm = lv.get("HeightMap");
  if (hm && hm->type == nbt::TAG_IntArray && hm->ia.size() == 256) {
    for (int i = 0; i < 256; ++i) c.height_map_[static_cast<std::size_t>(i)] = static_cast<std::uint8_t>(std::clamp(hm->ia[static_cast<std::size_t>(i)], 0, 255));
  } else {
    c.recomputeHeightMap();
  }
  if (auto ex = lv.get("ExtraData"); ex && ex->type == nbt::TAG_ByteArray && ex->s.size() >= 4) {
    const auto& b = ex->s;
    auto rd = [&](std::size_t o) {
      return (std::uint32_t(std::uint8_t(b[o])) << 24) | (std::uint32_t(std::uint8_t(b[o + 1])) << 16) |
             (std::uint32_t(std::uint8_t(b[o + 2])) << 8) | std::uint32_t(std::uint8_t(b[o + 3]));
    };
    std::uint32_t n = rd(0);
    std::size_t o = 4;
    for (std::uint32_t k = 0; k < n && o + 6 <= b.size(); ++k, o += 6) {
      auto key = static_cast<std::int32_t>(rd(o));
      auto val = static_cast<std::int16_t>((std::uint16_t(std::uint8_t(b[o + 4])) << 8) | std::uint8_t(b[o + 5]));
      c.extra_data_.emplace_back(key, val);
    }
  }
  c.light_populated_ = lv.getInt("LightPopulated") > 0;
  if (lv.has("TerrainGenerated")) c.generated_ = lv.getInt("TerrainGenerated") > 0;
  else c.generated_ = lv.getInt("TerrainPopulated") > 0 || has_blocks;
  c.populated_ = lv.getInt("TerrainPopulated", 1) > 0;
  c.dirty_ = false;
  out = std::move(c);
  return true;
}

std::string Chunk::networkPayload() const {
  // ChunkRequestTask::onRun layout
  std::string out;
  out.reserve(static_cast<std::size_t>(kBlocksSize + kNibbleSize * 3 + kHeightMapSize +
                                       kBiomeColorsSize * 4 + 4));
  out.append(reinterpret_cast<const char*>(blocks_.data()), blocks_.size());
  out.append(reinterpret_cast<const char*>(data_.data()), data_.size());
  out.append(reinterpret_cast<const char*>(sky_light_.data()), sky_light_.size());
  out.append(reinterpret_cast<const char*>(block_light_.data()), block_light_.size());
  out.append(reinterpret_cast<const char*>(height_map_.data()), height_map_.size());
  // biome colors as big-endian ints (pack N*)
  for (auto c : biome_colors_) {
    out.push_back(static_cast<char>((c >> 24) & 0xff));
    out.push_back(static_cast<char>((c >> 16) & 0xff));
    out.push_back(static_cast<char>((c >> 8) & 0xff));
    out.push_back(static_cast<char>(c & 0xff));
  }
  // extraData: LInt count + (LInt key, LShort value)
  auto putL = [&](std::uint32_t v) {
    out.push_back(char(v)); out.push_back(char(v >> 8)); out.push_back(char(v >> 16)); out.push_back(char(v >> 24));
  };
  putL(static_cast<std::uint32_t>(extra_data_.size()));
  for (auto& [k, v] : extra_data_) {
    putL(static_cast<std::uint32_t>(k));
    out.push_back(char(v));
    out.push_back(char(static_cast<std::uint16_t>(v) >> 8));
  }
  // no tiles
  return out;
}

Chunk generateFlatChunk(int chunk_x, int chunk_z, std::uint8_t biome) {
  Chunk c(chunk_x, chunk_z);
  constexpr std::uint32_t plains = 0x8DB360;
  for (int z = 0; z < 16; ++z) {
    for (int x = 0; x < 16; ++x) {
      c.setBiome(x, z, biome, plains);
      c.setBlock(x, 0, z, protocol::BLOCK_BEDROCK);
      c.setBlock(x, 1, z, protocol::BLOCK_DIRT);
      c.setBlock(x, 2, z, protocol::BLOCK_DIRT);
      c.setBlock(x, 3, z, protocol::BLOCK_GRASS);
      c.setHeight(x, z, 4);
      // skylight above floor full (already 0xff nibble default)
    }
  }
  c.setGenerated(true);
  return c;
}

Chunk generateVoidChunk(int chunk_x, int chunk_z) {
  Chunk c(chunk_x, chunk_z);
  for (int z = 0; z < 16; ++z)
    for (int x = 0; x < 16; ++x) c.setBiome(x, z, 1, 0x8DB360);
  c.setGenerated(true);
  return c;
}

// Value noise 0..1 from integer lattice (hash2)
float valueNoise2d(int x, int z, std::int32_t seed) {
  return static_cast<float>(hash2(x, z, seed) & 0xffffu) / 65535.f;
}

float smoothNoise2d(float x, float z, std::int32_t seed) {
  const int x0 = static_cast<int>(std::floor(x));
  const int z0 = static_cast<int>(std::floor(z));
  const float fx = x - static_cast<float>(x0);
  const float fz = z - static_cast<float>(z0);
  // smoothstep
  const float ux = fx * fx * (3.f - 2.f * fx);
  const float uz = fz * fz * (3.f - 2.f * fz);
  const float a = valueNoise2d(x0, z0, seed);
  const float b = valueNoise2d(x0 + 1, z0, seed);
  const float c = valueNoise2d(x0, z0 + 1, seed);
  const float d = valueNoise2d(x0 + 1, z0 + 1, seed);
  const float i1 = a + (b - a) * ux;
  const float i2 = c + (d - c) * ux;
  return i1 + (i2 - i1) * uz;
}

float fbm2d(float x, float z, std::int32_t seed, int octaves = 4) {
  float amp = 1.f;
  float freq = 1.f;
  float sum = 0.f;
  float norm = 0.f;
  for (int i = 0; i < octaves; ++i) {
    sum += smoothNoise2d(x * freq, z * freq, seed + i * 131) * amp;
    norm += amp;
    amp *= 0.5f;
    freq *= 2.f;
  }
  return sum / norm;
}

Chunk generateNormalStubChunk(int chunk_x, int chunk_z, std::int32_t seed) {
  Chunk c(chunk_x, chunk_z);
  gen::generateNormalChunk(c, seed);
  c.setGenerated(true);
  c.recomputeHeightMap();
  return c;
}

Chunk generateNetherChunk(int chunk_x, int chunk_z, std::int32_t seed) {
  Chunk c(chunk_x, chunk_z);
  gen::generateNetherChunk(c, seed);
  c.setGenerated(true);
  c.recomputeHeightMap();
  return c;
}

Chunk generateEndChunk(int chunk_x, int chunk_z, std::int32_t seed) {
  Chunk c(chunk_x, chunk_z);
  constexpr std::uint32_t end_color = 0xd6d6a8;

  for (int z = 0; z < 16; ++z) {
    for (int x = 0; x < 16; ++x) {
      const int wx = (chunk_x << 4) + x;
      const int wz = (chunk_z << 4) + z;
      c.setBiome(x, z, 9, end_color);

      // central island + scattered outer islands
      const float dist = std::sqrt(static_cast<float>(wx * wx + wz * wz));
      const float island =
          fbm2d(static_cast<float>(wx) * 0.05f, static_cast<float>(wz) * 0.05f, seed + 3, 4);

      bool solid = false;
      int top = 0;
      if (dist < 48.f) {
        // main island: thicker near center
        const float edge = 1.f - dist / 48.f;
        const int thick = 8 + static_cast<int>(edge * 16.f + island * 6.f);
        const int base = 56 - thick / 2;
        for (int y = base; y < base + thick; ++y) {
          if (y >= 0 && y < 128) {
            c.setBlock(x, y, z, protocol::BLOCK_END_STONE);
            solid = true;
            top = y;
          }
        }
      } else if (island > 0.68f && dist < 220.f) {
        // outer floating islands
        const int thick = 3 + static_cast<int>((island - 0.68f) * 20.f);
        const int base = 48 + static_cast<int>(fbm2d(static_cast<float>(wx) * 0.02f,
                                                     static_cast<float>(wz) * 0.02f, seed + 9,
                                                     2) *
                                               30.f);
        for (int y = base; y < base + thick && y < 120; ++y) {
          if (y >= 0) {
            c.setBlock(x, y, z, protocol::BLOCK_END_STONE);
            solid = true;
            top = y;
          }
        }
      }

      // spawn platform guarantee near 0,0 — larger flat top, open sky, ignore void below
      if (std::abs(wx) <= 8 && std::abs(wz) <= 8) {
        for (int y = 60; y <= 64; ++y) c.setBlock(x, y, z, protocol::BLOCK_END_STONE);
        // clear air above so auto-spawn open_air checks pass
        for (int y = 65; y <= 80; ++y) c.setBlock(x, y, z, 0);
        top = 64;
        solid = true;
      }

      c.setHeight(x, z, static_cast<std::uint8_t>(solid ? top + 1 : 0));
    }
  }
  c.setGenerated(true);
  return c;
}

} // namespace mpmpes::level
