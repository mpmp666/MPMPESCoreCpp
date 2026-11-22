// EndFeatures + Xoroshiro — port of ender/EndFeatures.php (self-contained parts).
#include "mpmpes/level/gen/EndFeatures.hpp"

#include "mpmpes/level/Chunk.hpp"
#include "mpmpes/level/gen/EndGenerator.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <limits>
#include "mpmpes/util/Sha256.hpp"

namespace mpmpes::level::gen {
int XoroshiroRandom::dbg_count = 0;

namespace {
constexpr std::uint8_t OBSIDIAN = 49, BEDROCK = 7, FIRE = 51, IRON_BARS = 101, TORCH = 50;
constexpr std::uint8_t END_STONE = 121, STAINED_CLAY = 159, GLOWSTONE = 89;
constexpr std::uint8_t CHORUS_PLANT = 240, CHORUS_FLOWER = 200, END_GATEWAY = 209;
const int HORIZONTAL[4][2] = {{0, -1}, {1, 0}, {0, 1}, {-1, 0}};

void put(level::Chunk& chunk, int x, int y, int z, std::uint8_t id, std::uint8_t meta = 0) {
  if (y < 0 || y >= 128) return;
  if ((x >> 4) != chunk.x() || (z >> 4) != chunk.z()) return;
  // PE 0.14 substitutions (PHP EndFeatures::put)
  if (id == CHORUS_PLANT) { id = STAINED_CLAY; meta = 10; }
  else if (id == CHORUS_FLOWER) { id = STAINED_CLAY; meta = 2; }
  else if (id == END_GATEWAY) { id = GLOWSTONE; meta = 0; }
  chunk.setBlock(x & 15, y, z & 15, id, meta);
}

bool intersects(level::Chunk& c, int minX, int minZ, int maxX, int maxZ) {
  return maxX >= c.x() * 16 && minX <= c.x() * 16 + 15 && maxZ >= c.z() * 16 &&
         minZ <= c.z() * 16 + 15;
}
} // namespace

// ---------------------------------------------------------------- Xoroshiro
static std::int64_t urs(std::int64_t v, int bits) {
  if (bits == 0) return v;
  return static_cast<std::int64_t>((static_cast<std::uint64_t>(v) >> bits) &
                                   (0x7fffffffffffffffLL >> (bits - 1)));
}
static std::int64_t rotl(std::int64_t v, int bits) {
  return static_cast<std::int64_t>((static_cast<std::uint64_t>(v) << bits) |
                                   (static_cast<std::uint64_t>(v) >> (64 - bits)));
}
static std::int64_t mixStafford13(std::int64_t v) {
  v = JavaRandom::multiplyLong(v ^ urs(v, 30), -4658895280553007687LL);
  v = JavaRandom::multiplyLong(v ^ urs(v, 27), -7723592293110705685LL);
  return v ^ urs(v, 31);
}

void XoroshiroRandom::setSeed(std::int64_t seed) {
  std::int64_t lo = seed ^ 7640891576956012809LL;
  std::int64_t hi = JavaRandom::addLong(lo, -7046029254386353131LL);
  lo_ = mixStafford13(lo);
  hi_ = mixStafford13(hi);
  if ((lo_ | hi_) == 0) {
    lo_ = -7046029254386353131LL;
    hi_ = 7640891576956012809LL;
  }
}

std::int64_t XoroshiroRandom::nextRawLong() {
  std::int64_t s0 = lo_, s1 = hi_;
  std::int64_t result = JavaRandom::addLong(rotl(JavaRandom::addLong(s0, s1), 17), s0);
  s1 ^= s0;
  lo_ = rotl(s0, 49) ^ s1 ^ (s1 << 21);
  hi_ = rotl(s1, 28);
  return result;
}

std::int32_t XoroshiroRandom::next(int bits) {
  std::int64_t v = urs(nextRawLong(), 64 - bits);
  return static_cast<std::int32_t>(bits == 32 && v >= 0x80000000LL ? v - 0x100000000LL : v);
}
std::int32_t XoroshiroRandom::nextInt() { return next(32); }
std::int64_t XoroshiroRandom::nextLong() {
  std::int64_t upper = next(32);
  std::int64_t lower = next(32);
  if (lower < 0) upper = (upper - 1) & 0xffffffffLL;
  return (upper << 32) | (lower & 0xffffffffLL);
}
std::int32_t XoroshiroRandom::nextInt(std::int32_t bound) {
  if ((bound & (bound - 1)) == 0)
    return static_cast<std::int32_t>((bound * static_cast<std::int64_t>(next(31))) >> 31);
  std::int32_t sample, mod;
  do {
    sample = next(31);
    mod = sample % bound;
  } while (sample - mod + (bound - 1) >= 0x80000000LL);
  return mod;
}
double XoroshiroRandom::nextDouble() {
  std::int64_t u = next(26), l = next(27);
  return static_cast<double>((u << 27) + l) * 1.1102230246251565E-16;
}

std::int64_t XoroshiroRandom::setDecorationSeed(std::int64_t seed, std::int64_t bx, std::int64_t bz) {
  setSeed(seed);
  std::int64_t xs = nextLong() | 1, zs = nextLong() | 1;
  std::int64_t result = JavaRandom::addLong(JavaRandom::multiplyLong(bx, xs), JavaRandom::multiplyLong(bz, zs)) ^ seed;
  setSeed(result);
  return result;
}
void XoroshiroRandom::setFeatureSeed(std::int64_t decoration_seed, std::int64_t index, std::int64_t step) {
  std::int64_t scaled = (10000 * step) & 0xffffffffLL;
  if (scaled >= 0x80000000LL) scaled -= 0x100000000LL;
  setSeed(JavaRandom::addLong(JavaRandom::addLong(decoration_seed, index), scaled));
}

// ---------------------------------------------------------------- EndFeatures
EndFeatures::EndFeatures(std::int64_t seed, EndTerrainNoise& terrain) : seed_(seed), terrain_(terrain) {
  // biomeZoomSeed = sha256 of seed (first 8 bytes as 2 little-endian uint32)
  // We don't ship sha256 here; the zoom seed only feeds biomeAt() jitter (fiddles).
  // Reproduce a stable 64-bit value via the same xoroshiro stream used elsewhere.
  {
    // sha256(pack('V2', seed & 0xffffffff, (seed >> 32) & 0xffffffff)) -> Vlo/Vhi
    unsigned char pk[8];
    std::uint32_t lo = static_cast<std::uint32_t>(seed & 0xffffffffLL);
    std::uint32_t hi = static_cast<std::uint32_t>((seed >> 32) & 0xffffffffLL);
    for (int i = 0; i < 4; ++i) pk[i] = static_cast<unsigned char>(lo >> (i * 8));
    for (int i = 0; i < 4; ++i) pk[4 + i] = static_cast<unsigned char>(hi >> (i * 8));
    auto hash = util::sha256(std::string_view(reinterpret_cast<const char*>(pk), 8));
    const unsigned char* hb = reinterpret_cast<const unsigned char*>(hash.data());
    std::uint32_t vlo = hb[0] | (hb[1] << 8) | (hb[2] << 16) | (hb[3] << 24);
    std::uint32_t vhi = hb[4] | (hb[5] << 8) | (hb[6] << 16) | (hb[7] << 24);
    biome_zoom_seed_ = (static_cast<std::int64_t>(vhi) << 32) | vlo;
  }
  JavaRandom keyR(seed);
  int spikeKey = static_cast<int>(keyR.nextLong() & 65535);
  JavaRandom random(spikeKey);
  int sizes[10] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9};
  for (int i = 10; i > 1; --i) {
    int j = random.nextInt(i);
    std::swap(sizes[i - 1], sizes[j]);
  }
  for (int i = 0; i < 10; ++i) {
    double angle = 2.0 * (-M_PI + (M_PI / 10) * i);
    int size = sizes[i];
    spikes_.push_back({static_cast<int>(std::floor(42.0 * std::cos(angle))),
                       static_cast<int>(std::floor(42.0 * std::sin(angle))),
                       2 + size / 3, 76 + size * 3, size == 1 || size == 2, size});
  }
  // podiumY = surfaceHeight(0,0)-1 — computed from the terrain grid at chunk 0,0
  {
    double nodes[297];
    terrain_.chunkNodes(0, 0, nodes);
    auto nodeAt = [&](int gx, int gz, int gy) { return nodes[(gx * 3 + gz) * 33 + gy]; };
    int surface = 0;
    for (int y = 127; y >= 0; --y) {
      int gx = 0, gz = 0, gy = y >> 2;
      double fy = (y & 3) / 4.0;
      double n00 = nodeAt(0, 0, gy);
      double d = n00 + fy * (nodeAt(0, 0, gy + 1) - n00);
      // (0,0) column: fx=fz=0 so d is the density
      if (EndTerrainNoise::squeeze(d) > 0.0 || d > 0.0) { surface = y + 1; break; }
      (void)d;
    }
    podium_y_ = std::max(1, surface - 1);
  }
}

void EndFeatures::placeSpike(level::Chunk& chunk, const EndSpike& spike) {
  int cx = spike.centerX, cz = spike.centerZ, radius = spike.radius, height = spike.height;
  int minX = std::max(cx - radius, chunk.x() * 16), maxX = std::min(cx + radius, chunk.x() * 16 + 15);
  int minZ = std::max(cz - radius, chunk.z() * 16), maxZ = std::min(cz + radius, chunk.z() * 16 + 15);
  for (int x = minX; x <= maxX; ++x)
    for (int z = minZ; z <= maxZ; ++z) {
      bool inside = (x - cx) * (x - cx) + (z - cz) * (z - cz) <= radius * radius + 1;
      for (int y = 0; y <= height + 10; ++y) {
        if (inside && y < height) put(chunk, x, y, z, OBSIDIAN);
        else if (y > 65) put(chunk, x, y, z, 0);
      }
    }
  if (spike.guarded) {
    for (int dx = -2; dx <= 2; ++dx)
      for (int dz = -2; dz <= 2; ++dz)
        for (int dy = 0; dy <= 3; ++dy)
          if (std::abs(dx) == 2 || std::abs(dz) == 2 || dy == 3)
            put(chunk, cx + dx, height + dy, cz + dz, IRON_BARS);
  }
  put(chunk, cx, height, cz, BEDROCK);
  put(chunk, cx, height + 1, cz, FIRE);
}

void EndFeatures::placePodium(level::Chunk& chunk) {
  int oy = podium_y_;
  for (int x = -4; x <= 4; ++x)
    for (int z = -4; z <= 4; ++z)
      for (int dy = -1; dy <= 32; ++dy) {
        double d2 = x * x + dy * dy + z * z;
        if (d2 >= 3.5 * 3.5) continue;
        bool insideRim = d2 < 2.5 * 2.5;
        std::uint8_t id;
        if (dy < 0) id = insideRim ? BEDROCK : END_STONE;
        else id = (dy == 0 && !insideRim) ? BEDROCK : 0;
        put(chunk, x, oy + dy, z, id);
      }
  for (int dy = 0; dy < 4; ++dy) put(chunk, 0, oy + dy, 0, BEDROCK);
  const int torches[4][3] = {{1, 0, 1}, {-1, 0, 2}, {0, 1, 3}, {0, -1, 4}};
  for (auto& t : torches) put(chunk, t[0], oy + 2, t[1], TORCH, t[2]);
}

void EndFeatures::placePlatform(level::Chunk& chunk) {
  for (int dx = -2; dx <= 2; ++dx)
    for (int dz = -2; dz <= 2; ++dz)
      for (int dy = -1; dy < 3; ++dy)
        put(chunk, 100 + dx, 49 + dy, dz, dy == -1 ? OBSIDIAN : 0);
}

void EndFeatures::growChorus(level::Chunk& chunk, XoroshiroRandom& random, int x, int y, int z,
                             int ox, int oz, int depth) {
  auto empty = [&](int bx, int by, int bz) {
    if (by < 0 || by > 127) return true;
    if ((bx >> 4) != chunk.x() || (bz >> 4) != chunk.z())
      return !terrain_.solid(bx, by, bz); // PHP draftBlock falls back to terrain density
    return chunk.getBlockId(bx & 15, by, bz & 15) == 0;
  };
  auto allEmptyAround = [&](int bx, int by, int bz, int skipDir = -1) {
    for (int d = 0; d < 4; ++d) {
      if (d == skipDir) continue;
      if (!empty(bx + HORIZONTAL[d][0], by, bz + HORIZONTAL[d][1])) return false;
    }
    return true;
  };
  int height = random.nextInt(4) + 1 + (depth == 0 ? 1 : 0);
  for (int i = 0; i < height; ++i) {
    int ty = y + i + 1;
    if (!allEmptyAround(x, ty, z)) return;
    put(chunk, x, ty, z, CHORUS_PLANT);
    put(chunk, x, ty - 1, z, CHORUS_PLANT);
  }
  bool placedStem = false;
  if (depth < 4) {
    int stems = random.nextInt(4) + (depth == 0 ? 1 : 0);
    for (int i = 0; i < stems; ++i) {
      int dir = random.nextInt(4);
      int dx = HORIZONTAL[dir][0], dz = HORIZONTAL[dir][1];
      int tx = x + dx, ty = y + height, tz = z + dz;
      if (std::abs(tx - ox) < 8 && std::abs(tz - oz) < 8 && empty(tx, ty, tz) &&
          empty(tx, ty - 1, tz) && allEmptyAround(tx, ty, tz, (dir + 2) % 4)) {
        placedStem = true;
        put(chunk, tx, ty, tz, CHORUS_PLANT);
        put(chunk, x, ty, z, CHORUS_PLANT);
        growChorus(chunk, random, tx, ty, tz, ox, oz, depth + 1);
      }
    }
  }
  if (!placedStem) put(chunk, x, y + height, z, CHORUS_FLOWER, 5);
}

static std::int64_t biomeMix(std::int64_t value, std::int64_t coordinate) {
  return JavaRandom::addLong(
      JavaRandom::multiplyLong(value,
                               JavaRandom::addLong(JavaRandom::multiplyLong(value, 6364136223846793005LL),
                                                   1442695040888963407LL)),
      coordinate);
}
static double biomeFiddle(std::int64_t value) {
  return ((static_cast<double>((value >> 24) & 1023) / 1024.0) - 0.5) * 0.9;
}

std::string EndFeatures::biomeAt(int x, int y, int z) {
  int px = (x - 2) >> 2, py = (y - 2) >> 2, pz = (z - 2) >> 2;
  double fx = ((x - 2) & 3) / 4.0, fy = ((y - 2) & 3) / 4.0, fz = ((z - 2) & 3) / 4.0;
  double best = std::numeric_limits<double>::infinity();
  int bestX = px, bestZ = pz;
  for (int i = 0; i < 8; ++i) {
    int ox = (i & 4) == 0 ? 0 : 1, oy = (i & 2) == 0 ? 0 : 1, oz = (i & 1) == 0 ? 0 : 1;
    int qx = px + ox, qz = pz + oz;
    std::int64_t r = biome_zoom_seed_;
    const std::int64_t coords[6] = {qx, (py + oy), qz, qx, (py + oy), qz};
    for (std::int64_t c : coords) r = biomeMix(r, c);
    double dx = fx - ox + biomeFiddle(r);
    r = biomeMix(r, biome_zoom_seed_);
    double dy = fy - oy + biomeFiddle(r);
    r = biomeMix(r, biome_zoom_seed_);
    double dz = fz - oz + biomeFiddle(r);
    double distance = dz * dz + dy * dy + dx * dx;
    if (distance < best) { best = distance; bestX = qx; bestZ = qz; }
  }
  return terrain_.biome(bestX * 4, bestZ * 4);
}

void EndFeatures::gateways(level::Chunk& chunk) {
  bool near = false;
  for (int cx = chunk.x() - 1; cx <= chunk.x() + 1 && !near; ++cx)
    for (int cz = chunk.z() - 1; cz <= chunk.z() + 1 && !near; ++cz)
      if (terrain_.biome(cx * 16, cz * 16) == "end_highlands") near = true;
  if (!near) return;
  XoroshiroRandom deco(0);
  auto dseed = deco.setDecorationSeed(seed_, chunk.x() * 16, chunk.z() * 16);
  XoroshiroRandom random(0);
  random.setFeatureSeed(dseed, 0, 4);
  float threshold = 1.0f / 700.0f;
  if (!(random.nextFloat() < threshold)) return;
  int x = chunk.x() * 16 + random.nextInt(16);
  int z = chunk.z() * 16 + random.nextInt(16);
  int lx = x & 15, lz = z & 15;
  int surface = -1;
  for (int yy = 127; yy >= 0; --yy)
    if (chunk.getBlockId(lx, yy, lz) != 0) { surface = yy; break; }
  if (surface <= 0) return;
  int y = surface + 1 + 3 + random.nextInt(7);
  if (biomeAt(x, y, z) != "end_highlands") return;
  for (int dx = -1; dx <= 1; ++dx)
    for (int dz = -1; dz <= 1; ++dz)
      for (int dy = -2; dy <= 2; ++dy) {
        std::uint8_t id;
        if (dx == 0 && dy == 0 && dz == 0) id = END_GATEWAY;
        else if (dy == 0) id = 0;
        else if (dy == 2 || dy == -2) id = (dx == 0 && dz == 0) ? BEDROCK : 0;
        else id = (dx == 0 || dz == 0) ? BEDROCK : 0;
        put(chunk, x + dx, y + dy, z + dz, id);
      }
}

void EndFeatures::chorusPlants(level::Chunk& chunk) {
  // PHP: plans for all 3x3 neighbors; each plan's chorusBlocks apply to this chunk.
  for (int ncx = chunk.x() - 1; ncx <= chunk.x() + 1; ++ncx) {
    for (int ncz = chunk.z() - 1; ncz <= chunk.z() + 1; ++ncz) {
      // nearBiome(ncx, ncz, end_highlands)
      bool near = false;
      for (int ax = ncx - 1; ax <= ncx + 1 && !near; ++ax)
        for (int az = ncz - 1; az <= ncz + 1 && !near; ++az)
          if (terrain_.biome(ax * 16, az * 16) == "end_highlands") near = true;
      if (!near) continue;
      XoroshiroRandom deco(0);
      auto dseed = deco.setDecorationSeed(seed_, ncx * 16, ncz * 16);
      XoroshiroRandom random(0);
      random.setFeatureSeed(dseed, 0, 9);
      int count = random.nextInt(5);
      for (int i = 0; i < count; ++i) {
        int x = ncx * 16 + random.nextInt(16);
        int z = ncz * 16 + random.nextInt(16);
        // draftSurface on terrain+islands: use this chunk if in range else terrain density
        auto surfaceBlock = [&](int bx, int by, int bz) -> std::uint8_t {
          if (by < 0 || by > 127) return 0;
          if ((bx >> 4) == chunk.x() && (bz >> 4) == chunk.z())
            return chunk.getBlockId(bx & 15, by, bz & 15);
          return terrain_.solid(bx, by, bz) ? END_STONE : 0;
        };
        int y = 0;
        for (int yy = 127; yy >= 0; --yy)
          if (surfaceBlock(x, yy, z) != 0) { y = yy + 1; break; }
        if (y <= 0 || y > 127) continue;
        if (biomeAt(x, y, z) != "end_highlands") continue;
        if (surfaceBlock(x, y, z) != 0 || surfaceBlock(x, y - 1, z) != END_STONE) continue;
        put(chunk, x, y, z, CHORUS_PLANT);
        growChorus(chunk, random, x, y, z, x, z, 0);
      }
    }
  }
}


void EndFeatures::smallIslands(level::Chunk& chunk) {
  // nearBiome(chunk, small_end_islands)
  bool near = false;
  for (int cx = chunk.x() - 1; cx <= chunk.x() + 1 && !near; ++cx)
    for (int cz = chunk.z() - 1; cz <= chunk.z() + 1 && !near; ++cz)
      if (terrain_.biome(cx * 16, cz * 16) == "small_end_islands") near = true;
  if (!near) return;
  // 3x3 neighborhood merge (islands from neighbor chunks cross borders)
  for (int ncx = chunk.x() - 1; ncx <= chunk.x() + 1; ++ncx) {
    for (int ncz = chunk.z() - 1; ncz <= chunk.z() + 1; ++ncz) {
      bool nnear = false;
      for (int ax = ncx - 1; ax <= ncx + 1 && !nnear; ++ax)
        for (int az = ncz - 1; az <= ncz + 1 && !nnear; ++az)
          if (terrain_.biome(ax * 16, az * 16) == "small_end_islands") nnear = true;
      if (!nnear) continue;
      XoroshiroRandom deco(0);
      auto dseed = deco.setDecorationSeed(seed_, ncx * 16, ncz * 16);
      XoroshiroRandom random(0);
      random.setFeatureSeed(dseed, 0, 0);
      float threshold = 1.0f / 14.0f;
      float rf = random.nextFloat();
      if (!(rf < threshold)) continue;
      int count = random.nextInt(4) < 3 ? 1 : 2;
      for (int i = 0; i < count; ++i) {
        int x = ncx * 16 + random.nextInt(16);
        int z = ncz * 16 + random.nextInt(16);
        int y = 55 + random.nextInt(16);
        if (biomeAt(x, y, z) != "small_end_islands") continue;
        double size = random.nextInt(3) + 4.0;
        for (int dy = 0; size > 0.5; --dy) {
          for (int dx = (int)std::floor(-size); dx <= (int)std::ceil(size); ++dx)
            for (int dz = (int)std::floor(-size); dz <= (int)std::ceil(size); ++dz) {
              if (dx * dx + dz * dz > (size + 1.0) * (size + 1.0)) continue;
              int yy = y + dy;
              if (yy < 0 || yy > 127) continue;
              if (((x + dx) >> 4) != chunk.x() || ((z + dz) >> 4) != chunk.z()) continue;
              put(chunk, x + dx, yy, z + dz, END_STONE);
            }
          size -= random.nextInt(2) + 0.5;
        }
      }
    }
  }
}

void EndFeatures::populate(level::Chunk& chunk) {
  // PHP EndFeatures::populate order: islands -> gateways -> spikes -> chorus -> podium -> platform
  smallIslands(chunk);
  gateways(chunk);
  for (const auto& spike : spikes_) {
    int extent = std::max(spike.radius, spike.guarded ? 2 : 0);
    if (!intersects(chunk, spike.centerX - extent, spike.centerZ - extent, spike.centerX + extent,
                    spike.centerZ + extent))
      continue;
    placeSpike(chunk, spike);
  }
  chorusPlants(chunk);
  if (intersects(chunk, -4, -4, 4, 4)) placePodium(chunk);
  if (intersects(chunk, 98, -2, 102, 2)) placePlatform(chunk);
}

} // namespace mpmpes::level::gen
