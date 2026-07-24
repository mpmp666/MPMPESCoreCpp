#include "mpmpes/level/Chunk.hpp"

#include "mpmpes/binary/BinaryStream.hpp"

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
  // extraData: LInt count = 0
  out.push_back(0);
  out.push_back(0);
  out.push_back(0);
  out.push_back(0);
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
  constexpr std::uint32_t plains = 0x8DB360;
  constexpr std::uint32_t forest = 0x056621;
  constexpr int sea = 62;

  for (int z = 0; z < 16; ++z) {
    for (int x = 0; x < 16; ++x) {
      const int wx = (chunk_x << 4) + x;
      const int wz = (chunk_z << 4) + z;

      // multi-octave hills: base ~58-78, peaks a bit higher
      const float n = fbm2d(static_cast<float>(wx) * 0.02f, static_cast<float>(wz) * 0.02f, seed, 5);
      int height = 58 + static_cast<int>(n * 28.f); // ~58-86
      // occasional peaks
      const float ridge = fbm2d(static_cast<float>(wx) * 0.008f, static_cast<float>(wz) * 0.008f,
                                seed + 99, 3);
      if (ridge > 0.72f) height += static_cast<int>((ridge - 0.72f) * 40.f);
      height = std::clamp(height, 8, 120);

      const bool forest_biome = fbm2d(static_cast<float>(wx) * 0.01f,
                                      static_cast<float>(wz) * 0.01f, seed + 7, 2) > 0.55f;
      c.setBiome(x, z, forest_biome ? 4 : 1, forest_biome ? forest : plains);

      for (int y = 0; y <= height; ++y) {
        if (y == 0) {
          c.setBlock(x, y, z, protocol::BLOCK_BEDROCK);
        } else if (y < height - 4) {
          std::uint8_t id = protocol::BLOCK_STONE;
          // sparse ores in stone
          const auto oh = hash2(wx * 31 + y, wz * 17 + y * 3, seed + 3);
          if (y < 16 && (oh % 48) == 0) id = protocol::BLOCK_DIAMOND_ORE;
          else if (y < 32 && (oh % 24) == 0) id = protocol::BLOCK_GOLD_ORE;
          else if (y < 64 && (oh % 12) == 0) id = protocol::BLOCK_IRON_ORE;
          else if ((oh % 8) == 0) id = protocol::BLOCK_COAL_ORE;
          else if ((oh % 40) == 1) id = protocol::BLOCK_GRAVEL;
          c.setBlock(x, y, z, id);
        } else if (y < height) {
          c.setBlock(x, y, z, protocol::BLOCK_DIRT);
        } else {
          // surface: sand near sea level low areas, else grass
          if (height <= sea && height < 64)
            c.setBlock(x, y, z, protocol::BLOCK_SAND);
          else
            c.setBlock(x, y, z, protocol::BLOCK_GRASS);
        }
      }
      // water fill if below sea
      if (height < sea) {
        for (int y = height + 1; y <= sea; ++y) {
          c.setBlock(x, y, z, protocol::BLOCK_WATER);
        }
      }
      c.setHeight(x, z, static_cast<std::uint8_t>(std::max(height, sea) + 1));
    }
  }

  // sparse oak trees (2x2 trunk + leaf blob) — only on grass, not too dense
  for (int z = 2; z < 14; ++z) {
    for (int x = 2; x < 14; ++x) {
      const int wx = (chunk_x << 4) + x;
      const int wz = (chunk_z << 4) + z;
      const auto th = hash2(wx, wz, seed + 42);
      if ((th % 55) != 0) continue;
      // find surface grass
      int surface = -1;
      for (int y = 120; y >= 1; --y) {
        const auto id = c.getBlockId(x, y, z);
        if (id == protocol::BLOCK_GRASS) {
          surface = y;
          break;
        }
        if (id != 0 && id != protocol::BLOCK_LEAVES && id != protocol::BLOCK_LOG) break;
      }
      if (surface < 8 || surface > 100) continue;
      const int trunk_h = 4 + static_cast<int>(th % 3); // 4-6
      for (int ty = 1; ty <= trunk_h; ++ty) {
        c.setBlock(x, surface + ty, z, protocol::BLOCK_LOG);
      }
      const int top = surface + trunk_h;
      for (int ly = top - 2; ly <= top + 1; ++ly) {
        const int r = (ly >= top) ? 1 : 2;
        for (int dz = -r; dz <= r; ++dz) {
          for (int dx = -r; dx <= r; ++dx) {
            if (dx == 0 && dz == 0 && ly <= top) continue;
            if (std::abs(dx) == r && std::abs(dz) == r && (th & 1)) continue;
            const int lx = x + dx;
            const int lz = z + dz;
            if (lx < 0 || lx >= 16 || lz < 0 || lz >= 16) continue;
            if (c.getBlockId(lx, ly, lz) == 0) c.setBlock(lx, ly, lz, protocol::BLOCK_LEAVES);
          }
        }
      }
    }
  }

  c.setGenerated(true);
  return c;
}

Chunk generateNetherChunk(int chunk_x, int chunk_z, std::int32_t seed) {
  Chunk c(chunk_x, chunk_z);
  constexpr std::uint32_t hell = 0xbf3b3b; // reddish
  constexpr int ceil_y = 127;
  constexpr int lava_y = 31;
  // Fixed spawn island around world (0,0): solid plateau above lava so players can land.
  constexpr int island_r = 20;       // radius in blocks
  constexpr int island_top = 72;     // surface Y (feet at 73)

  for (int z = 0; z < 16; ++z) {
    for (int x = 0; x < 16; ++x) {
      const int wx = (chunk_x << 4) + x;
      const int wz = (chunk_z << 4) + z;
      c.setBiome(x, z, 8, hell); // hell biome id (approx)

      // floor / ceiling bedrock
      c.setBlock(x, 0, z, protocol::BLOCK_BEDROCK);
      c.setBlock(x, ceil_y, z, protocol::BLOCK_BEDROCK);

      const int dist2 = wx * wx + wz * wz;
      const bool on_spawn_island = dist2 <= island_r * island_r;

      // netherrack terrain density (caves via noise threshold)
      const float floor_n =
          fbm2d(static_cast<float>(wx) * 0.04f, static_cast<float>(wz) * 0.04f, seed, 4);
      const float ceil_n =
          fbm2d(static_cast<float>(wx) * 0.03f + 50.f, static_cast<float>(wz) * 0.03f + 50.f,
                seed + 11, 3);
      // Raise floors so some land pokes above lava seas (was often fully submerged).
      int floor_h = 18 + static_cast<int>(floor_n * 40.f); // ~18-58
      int ceil_h = ceil_y - 4 - static_cast<int>(ceil_n * 20.f); // ~103-123
      floor_h = std::clamp(floor_h, 4, 64);
      // Keep ceiling high enough that floors never seal into solid columns.
      ceil_h = std::clamp(ceil_h, 100, ceil_y - 2);

      if (on_spawn_island) {
        // Soft edge: full height to island_r-4, then slope down toward lava.
        const int dist = static_cast<int>(std::sqrt(static_cast<float>(dist2)));
        int top = island_top;
        if (dist > island_r - 4) {
          top = island_top - (dist - (island_r - 4)) * 3;
          if (top < lava_y + 2) top = lava_y + 2;
        }
        floor_h = top;
        // Guarantee tall open air above the forced spawn island (no ceiling trap).
        if (ceil_h < floor_h + 24) ceil_h = floor_h + 24;
        if (ceil_h > ceil_y - 2) ceil_h = ceil_y - 2;
      } else if (ceil_h <= floor_h + 8) {
        // Non-island: keep at least 8 air between floor top and ceiling bottom.
        ceil_h = std::min(ceil_y - 2, floor_h + 16);
      }

      for (int y = 1; y < ceil_y; ++y) {
        if (y <= floor_h || y >= ceil_h) {
          // occasional gravel / glowstone pockets near surfaces
          const auto nh = hash2(wx + y * 3, wz - y * 7, seed + 5);
          if (y > 4 && y < floor_h && (nh % 40) == 0)
            c.setBlock(x, y, z, protocol::BLOCK_GRAVEL);
          else if ((y == floor_h || y == ceil_h) && (nh % 90) == 0)
            c.setBlock(x, y, z, protocol::BLOCK_GLOWSTONE);
          else
            c.setBlock(x, y, z, protocol::BLOCK_NETHERRACK);
        } else if (y <= lava_y && !on_spawn_island) {
          // No lava under the forced spawn island.
          c.setBlock(x, y, z, protocol::BLOCK_LAVA);
        }
        // else air (open cavern)
      }

      // hanging glowstone blobs under ceiling
      const auto gh = hash2(wx, wz, seed + 88);
      if ((gh % 70) == 0 && ceil_h > lava_y + 10) {
        const int gy = ceil_h - 1 - static_cast<int>(gh % 4);
        for (int dy = 0; dy < 3; ++dy) {
          if (gy - dy > lava_y) c.setBlock(x, gy - dy, z, protocol::BLOCK_GLOWSTONE);
        }
      }

      c.setHeight(x, z, static_cast<std::uint8_t>(floor_h + 1));
    }
  }
  c.setGenerated(true);
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
