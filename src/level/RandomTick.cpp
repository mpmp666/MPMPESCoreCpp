// Random block ticks — port of Level::tickChunks + the crop/plant onUpdate rules.
#include "mpmpes/level/Level.hpp"
#include "mpmpes/level/gen/NormalGenerator.hpp"

namespace mpmpes::level::gen { class PmRandom; void growTreeForLevel(Chunk&, PmRandom&, int, int, int, int); }
namespace mpmpes::level {

namespace {
// Crop/plant block ids (0.14): wheat 59, carrot 141, potato 142, beetroot 244,
// pumpkin stem 104, melon stem 105, nether wart 115, cactus 81, sugarcane 83,
// sapling 6, grass 2, mycelium 110, mushrooms 39/40, ice 79, snow layer 78.
constexpr std::uint8_t WHEAT = 59, CARROT = 141, POTATO = 142, BEETROOT = 244;
constexpr std::uint8_t PUMPKIN_STEM = 104, MELON_STEM = 105, NETHER_WART = 115;
constexpr std::uint8_t CACTUS = 81, SUGARCANE = 83, SAPLING = 6, GRASS = 2, DIRT = 3;
constexpr std::uint8_t MYCELIUM = 110, RED_MUSH = 39, BROWN_MUSH = 40, ICE = 79;
constexpr std::uint8_t FARMLAND = 60, SNOW = 78;

bool isCrop(std::uint8_t id) {
  return id == WHEAT || id == CARROT || id == POTATO || id == BEETROOT || id == PUMPKIN_STEM ||
         id == MELON_STEM;
}
} // namespace

bool Level::randomTickBlock(int x, int y, int z) {
  if (y < 0 || y >= 128) return false;
  const std::uint8_t id = getBlockId(x, y, z);
  if (id == 0) return false;
  std::uint8_t meta = getBlockMeta(x, y, z);
  auto& rng = tick_rng_;

  auto below = getBlockId(x, y - 1, z);
  if (isCrop(id)) {
    // Crops::onUpdate: 1/3 chance to grow a stage (cap 7)
    if (meta < 7 && (rng() % 3) == 0) {
      setBlock(x, y, z, id, meta + 1);
      return true;
    }
    return false;
  }
  if (id == NETHER_WART) {
    if (meta < 3 && (rng() % 13) == 0) {
      setBlock(x, y, z, id, meta + 1);
      return true;
    }
    return false;
  }
  if (id == CACTUS) {
    if (below != CACTUS) {
      if (meta == 15) {
        for (int dy = 1; dy < 3; ++dy)
          if (y + dy < 128 && getBlockId(x, y + dy, z) == 0) setBlock(x, y + dy, z, CACTUS, 0);
        setBlock(x, y, z, CACTUS, 0);
        return true;
      }
      setBlock(x, y, z, CACTUS, meta + 1);
      return true;
    }
    return false;
  }
  if (id == SUGARCANE) {
    if (below != SUGARCANE) {
      if (meta == 15) {
        for (int dy = 1; dy < 3; ++dy)
          if (y + dy < 128 && getBlockId(x, y + dy, z) == 0) {
            setBlock(x, y + dy, z, SUGARCANE, 0);
            break;
          }
        setBlock(x, y, z, SUGARCANE, 0);
        return true;
      }
      setBlock(x, y, z, SUGARCANE, meta + 1);
      return true;
    }
    return false;
  }
  if (id == SAPLING) {
    if ((rng() % 7) == 0) {
      if (meta & 8) {
        // grow into a tree (oak/birch/spruce/jungle by meta&7)
        int type = meta & 7;
        setBlock(x, y, z, 0, 0);
        Chunk& chunk = getOrCreateChunk(x >> 4, z >> 4);
        gen::PmRandom treeRng(static_cast<std::int64_t>(rng()));
        gen::growTreeForLevel(chunk, treeRng, x & 15, y, z & 15, type);
        return true;
      }
      setBlock(x, y, z, SAPLING, meta | 8);
      return true;
    }
    return false;
  }
  if (id == GRASS) {
    // Grass spread: 4 attempts to spread to adjacent dirt
    for (int i = 0; i < 4; ++i) {
      int tx = x + static_cast<int>(rng() % 3) - 1;
      int ty = y + static_cast<int>(rng() % 5) - 2;
      int tz = z + static_cast<int>(rng() % 3) - 1;
      if (ty < 0 || ty >= 128) continue;
      if (getBlockId(tx, ty, tz) == DIRT && getBlockId(tx, ty + 1, tz) == 0) {
        setBlock(tx, ty, tz, GRASS, 0);
      }
    }
    return false;
  }
  if (id == MYCELIUM) {
    for (int i = 0; i < 4; ++i) {
      int tx = x + static_cast<int>(rng() % 3) - 1;
      int ty = y + static_cast<int>(rng() % 5) - 2;
      int tz = z + static_cast<int>(rng() % 3) - 1;
      if (ty < 0 || ty >= 128) continue;
      if (getBlockId(tx, ty, tz) == DIRT && getBlockId(tx, ty + 1, tz) == 0)
        setBlock(tx, ty, tz, MYCELIUM, 0);
    }
    return false;
  }
  if (id == RED_MUSH || id == BROWN_MUSH) {
    // mushroom spread to nearby podzol/dirt
    int tx = x + static_cast<int>(rng() % 3) - 1;
    int tz = z + static_cast<int>(rng() % 3) - 1;
    int ty = y + static_cast<int>(rng() % 3) - 1;
    if (ty > 0 && ty < 128 && getBlockId(tx, ty, tz) == 0 &&
        (getBlockId(tx, ty - 1, tz) == 243 || getBlockId(tx, ty - 1, tz) == DIRT))
      setBlock(tx, ty, tz, id, 0);
    return false;
  }
  if (id == ICE) {
    // melt next to non-cold neighbors (PHP: light-based; approximate with air-above melt chance)
    if ((rng() % 8) == 0) {
      setBlock(x, y, z, 9, 0); // still water
      return true;
    }
    return false;
  }
  return false;
}

void Level::randomTickAround(int player_x, int player_z, int radius_chunks, std::mt19937& rng) {
  tick_rng_.seed(rng());
  const int pcx = player_x >> 4, pcz = player_z >> 4;
  // PHP: a few random chunks near the player, 3 random blocks per 16-y band
  for (int i = 0; i < 6; ++i) {
    int cx = pcx + static_cast<int>(rng() % (2 * radius_chunks + 1)) - radius_chunks;
    int cz = pcz + static_cast<int>(rng() % (2 * radius_chunks + 1)) - radius_chunks;
    auto* chunk = getChunk(cx, cz);
    if (!chunk) continue;
    for (int band = 0; band < 8; ++band) {
      for (int k = 0; k < 3; ++k) {
        int x = (cx << 4) + static_cast<int>(rng() & 15);
        int y = (band << 4) + static_cast<int>(rng() & 15);
        int z = (cz << 4) + static_cast<int>(rng() & 15);
        if (randomTickBlock(x, y, z)) {
          if (on_block_update_)
            on_block_update_(x, y, z, getBlockId(x, y, z), getBlockMeta(x, y, z));
        }
      }
    }
  }
}

} // namespace mpmpes::level
