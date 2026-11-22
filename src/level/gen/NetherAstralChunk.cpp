// Nether (hell/Nether.php) + Astral (astral/Astral.php) chunk generation.
#include "mpmpes/level/Chunk.hpp"
#include "mpmpes/level/gen/NormalGenerator.hpp"

#include <cmath>
#include <cstdlib>

namespace mpmpes::level::gen {

namespace {
constexpr std::uint8_t NETHERRACK = 87, SOUL_SAND = 88, GRAVEL = 13, STILL_LAVA = 11;
constexpr std::uint8_t LAVA = 10, FIRE = 51, GLOWSTONE = 89, QUARTZ_ORE = 153;
constexpr std::uint8_t BEDROCK = 7, END_STONE = 121;

struct NOreType {
  std::uint8_t id;
  int cluster_count, cluster_size, min_h, max_h;
};

void placeVein87(Chunk& chunk, PmRandom& random, int id, int cluster, int x, int y, int z,
                 bool top_variant /*place into air*/) {
  const bool dbgvein = std::getenv("MP_VEIN_DEBUG") && id == 88;
  // object/NetherOre.php (place into 87) / NetherOreTop.php (place into 0)
  double angle = random.nextFloat() * M_PI;
  double offx = std::cos(angle) * cluster / 8.0;
  double offz = std::sin(angle) * cluster / 8.0;
  double x1 = x + 8 + offx, x2 = x + 8 - offx;
  double z1 = z + 8 + offz, z2 = z + 8 - offz;
  double y1 = y + random.nextBoundedInt(3) + 2;
  double y2 = y + random.nextBoundedInt(3) + 2;
  for (int count = 0; count <= cluster; ++count) {
    double seedX = x1 + (x2 - x1) * count / cluster;
    double seedY = y1 + (y2 - y1) * count / cluster;
    double seedZ = z1 + (z2 - z1) * count / cluster;
    double size = ((std::sin(count * (M_PI / cluster)) + 1) * random.nextFloat() * cluster / 16 + 1) / 2;
    int startX = static_cast<int>(seedX - size), startY = static_cast<int>(seedY - size);
    int startZ = static_cast<int>(seedZ - size);
    int endX = static_cast<int>(seedX + size), endY = static_cast<int>(seedY + size);
    int endZ = static_cast<int>(seedZ + size);
    for (int xx = startX; xx <= endX; ++xx) {
      double sx = (xx + 0.5 - seedX) / size;
      sx *= sx;
      if (sx >= 1) continue;
      for (int yy = startY; yy <= endY; ++yy) {
        double sy = (yy + 0.5 - seedY) / size;
        sy *= sy;
        if (yy <= 0 || sx + sy >= 1) continue;
        for (int zz = startZ; zz <= endZ; ++zz) {
          double sz = (zz + 0.5 - seedZ) / size;
          sz *= sz;
          if (sx + sy + sz >= 1) continue;
          int lx = xx - (chunk.x() << 4), lz = zz - (chunk.z() << 4);
          if (lx < 0 || lx > 15 || lz < 0 || lz > 15) continue;
          int my = yy & 0x7f; // PHP y-wrap parity (SimpleChunkManager & 0x7f)
          auto cur = chunk.getBlockId(lx, my, lz);
          if (top_variant ? (cur == 0) : (cur == NETHERRACK)) {
            chunk.setBlock(lx, my, lz, id, 0);
            if (dbgvein) fprintf(stderr, "vein %d,%d,%d\n", lx, my, lz);
            if (std::getenv("MP_GLOW_DEBUG") && id == 89) fprintf(stderr, "glowblock %d,%d,%d\n", lx, my, lz);
          }
        }
      }
    }
  }
}

int firstAirFromTop(Chunk& chunk, int lx, int lz) {
  for (int y = 127; y >= 0; --y)
    if (chunk.getBlockId(lx, y, lz) == 0) return y + 1;
  return -1;
}
int firstAirFromBottom(Chunk& chunk, int lx, int lz) {
  for (int y = 0; y <= 127; ++y)
    if (chunk.getBlockId(lx, y, lz) == 0) return y;
  return -1;
}
} // namespace

void generateNetherChunk(Chunk& chunk, std::int64_t seed) {
  const int cx = chunk.x(), cz = chunk.z();
  PmRandom rng(seed);
  Simplex noiseBase(rng, 4, 1.0 / 4.0, 1.0 / 64.0);
  rng.setSeed(seed);

  auto noise = fastNoise3D(noiseBase, 16, 128, 16, 4, 8, 4, cx * 16, 0, cz * 16);
  auto noiseAt = [&](int x, int z, int y) {
    return noise[(static_cast<std::size_t>(x) * 17 + z) * 129 + y];
  };
  const int emptyHeight = 64;
  const double emptyAmplitude = 1.0, density = 0.5;
  for (int x = 0; x < 16; ++x)
    for (int z = 0; z < 16; ++z) {
      chunk.setBiome(x, z, 8, 0); // HELL, color 0
      for (int y = 0; y < 128; ++y) {
        if (y == 0 || y == 127) {
          chunk.setBlock(x, y, z, BEDROCK, 0);
          continue;
        }
        double nv = (std::abs(emptyHeight - y) / 64.0) * emptyAmplitude - noiseAt(x, z, y);
        nv -= 1.0 - density;
        if (nv > 0) {
          chunk.setBlock(x, y, z, NETHERRACK, 0);
        } else if (y <= 32) {
          chunk.setBlock(x, y, z, STILL_LAVA, 0);
          if (y + 1 <= 127) chunk.setBlockLight(x, y + 1, z, 15);
        }
      }
    }

  // populateChunk: NetherOre -> NetherGlowStone -> GroundFire -> NetherLava (fresh stream)
  PmRandom prng(chunkSeed(seed, cx, cz));
  const bool dbg = std::getenv("MP_NETHER_DEBUG") != nullptr;
  if (dbg) fprintf(stderr, "NetherOre seed=%lld\n", (long long)prng.debugSeed());
  const NOreType ores[] = {
      {QUARTZ_ORE, 20, 16, 0, 128},
      {SOUL_SAND, 5, 64, 0, 128},
      {GRAVEL, 5, 64, 0, 128},
      {LAVA, 1, 16, 0, 32},
  };
  for (const auto& t : ores) {
    if (std::getenv("MP_NETHER_DEBUG")) fprintf(stderr, "ore id=%d stream pos marker\n", t.id);
    for (int i = 0; i < t.cluster_count; ++i) {
      int x = prng.nextRange(cx << 4, (cx << 4) + 15);
      int y = prng.nextRange(t.min_h, t.max_h);
      int z = prng.nextRange(cz << 4, (cz << 4) + 15);
      int lx = x - (cx << 4), lz = z - (cz << 4);
      if (dbg && t.id == SOUL_SAND) fprintf(stderr, "CENTER %d,%d,%d can=%d\n", x, y, z, (int)(y >= 0 && y <= 127 && chunk.getBlockId(lx, y, lz) == NETHERRACK));
      if (y >= 0 && y <= 127 && chunk.getBlockId(lx, y, lz) == NETHERRACK)
        placeVein87(chunk, prng, t.id, t.cluster_size, x, y, z, false);
    }
  }
  if (dbg) fprintf(stderr, "NetherGlowStone seed=%lld\n", (long long)prng.debugSeed());
  // glowstone: clusterCount 1, clusterSize 20, at first air pocket from top
  for (int i = 0; i < 1; ++i) {
    int x = prng.nextRange(cx << 4, (cx << 4) + 15);
    int z = prng.nextRange(cz << 4, (cz << 4) + 15);
    int lx = x - (cx << 4), lz = z - (cz << 4);
    int y = firstAirFromTop(chunk, lx, lz);
    if (dbg) fprintf(stderr, "cpp glow center=%d,%d,%d\n", x, y, z);
    if (y > 0) placeVein87(chunk, prng, GLOWSTONE, 20, x, y, z, true); // NetherOreTop: into air
  }
  if (dbg) fprintf(stderr, "GroundFire seed=%lld\n", (long long)prng.debugSeed());
  // ground fire: 1-2 on netherrack tops
  {
    int amount = prng.nextRange(0, 2) + 1;
    for (int i = 0; i < amount; ++i) {
      int x = prng.nextRange(cx << 4, (cx << 4) + 15);
      int z = prng.nextRange(cz << 4, (cz << 4) + 15);
      int lx = x - (cx << 4), lz = z - (cz << 4);
      int y = firstAirFromBottom(chunk, lx, lz);
      if (y > 0 && (chunk.getBlockId(lx, y, lz) == 0 || chunk.getBlockId(lx, y, lz) == 78) &&
          chunk.getBlockId(lx, y - 1, lz) == NETHERRACK) {
        chunk.setBlock(lx, y, lz, FIRE, 0);
        chunk.setBlockLight(lx, y, lz, 15);
      }
    }
  }
  if (dbg) fprintf(stderr, "NetherLava seed=%lld\n", (long long)prng.debugSeed());
  // lava springs: 5% of chunks (PHP gate uses mt_rand — separate stream here)
  {
    std::uint32_t gate = static_cast<std::uint32_t>((cx * 734287 + cz * 912271) ^ (seed ^ 0x51de)) * 1103515245 + 12345;
    if (((gate >> 16) % 100) < 5) {
      int amount = prng.nextRange(0, 1) + 0;
      for (int i = 0; i < amount; ++i) {
        int x = prng.nextRange(cx << 4, (cx << 4) + 15);
        int z = prng.nextRange(cz << 4, (cz << 4) + 15);
        int lx = x - (cx << 4), lz = z - (cz << 4);
        int y = firstAirFromBottom(chunk, lx, lz);
        if (y > 0 && y <= 127 && chunk.getBlockId(lx, y, lz) == 0 &&
            chunk.getBlockId(lx, y - 1, lz) == NETHERRACK) {
          chunk.setBlock(lx, y, lz, LAVA, 0);
          chunk.setBlockLight(lx, y, lz, 15);
        }
      }
    }
  }
  chunk.setPopulated(true);
}

void generateAstralChunk(Chunk& chunk, std::int64_t seed) {
  const int cx = chunk.x(), cz = chunk.z();
  PmRandom rng(seed);
  Simplex noiseBase(rng, 4, 1.0 / 4.0, 1.0 / 64.0);
  rng.setSeed(seed);

  auto noise = fastNoise3D(noiseBase, 16, 128, 16, 4, 8, 4, cx * 16, 0, cz * 16);
  auto noiseAt = [&](int x, int z, int y) {
    return noise[(static_cast<std::size_t>(x) * 17 + z) * 129 + y];
  };
  const double density = 0.28;
  const int emptyHeight = 64;

  // glow island for this chunk (deterministic per-chunk stream)
  int glx = -1, glz = -1, glowY = -1, gr = 0;
  {
    PmRandom g((seed ^ 0x9e3779b9LL) + cx * 73856093LL + cz * 19349663LL);
    if (g.nextRange(0, 3) == 0) {
      glx = 3 + g.nextRange(0, 9);
      glz = 3 + g.nextRange(0, 9);
      glowY = 36 + g.nextRange(0, 8);
      gr = 1 + g.nextRange(0, 2);
    }
  }

  for (int x = 0; x < 16; ++x)
    for (int z = 0; z < 16; ++z) {
      chunk.setBiome(x, z, 8, 0); // HELL (same as PHP Astral)
      int gy = (glx >= 0 && std::abs(x - glx) <= gr && std::abs(z - glz) <= gr) ? glowY : -1;
      for (int y = 0; y < 128; ++y) {
        if (y == 0) {
          chunk.setBlock(x, y, z, BEDROCK, 0);
          continue;
        }
        double nv = (std::abs(emptyHeight - y) / 64.0) - noiseAt(x, z, y);
        nv -= 1.0 - density;
        if (nv > 0) {
          chunk.setBlock(x, y, z, END_STONE, 0);
        } else if (gy >= 0 && y >= gy && y <= gy + 1) {
          chunk.setBlock(x, y, z, GLOWSTONE, 0);
          chunk.setBlockLight(x, y, z, 15);
        }
      }
    }
  chunk.setPopulated(true);
}

} // namespace mpmpes::level::gen
