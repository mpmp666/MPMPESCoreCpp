// Normal generator chunk pipeline — Normal::generateChunk + populators.
#include "mpmpes/level/Chunk.hpp"
#include "mpmpes/level/gen/NormalGenerator.hpp"

#include <cmath>
#include <cstring>
#include <cstdlib>

namespace mpmpes::level::gen {

namespace {
constexpr int WATER_HEIGHT = 62;
constexpr int SMOOTH_SIZE = 2;

// transparent for the GroundCover top scan (liquids + plants + non-full blocks)
bool isTransparent(std::uint8_t id) {
  switch (id) {
    case 0: case 6: case 8: case 9: case 10: case 11: case 18: case 161: case 26: case 27:
    case 28: case 30: case 31: case 32: case 37: case 38: case 39: case 40: case 50: case 51:
    case 55: case 59: case 63: case 64: case 65: case 66: case 68: case 71: case 75: case 76:
    case 78: case 83: case 85: case 92: case 93: case 94: case 96: case 101: case 102: case 104:
    case 105: case 106: case 107: case 111: case 115: case 116: case 117: case 118: case 119:
    case 120: case 126: case 127: case 131: case 132: case 140: case 141: case 142: case 143:
    case 144: case 145: case 146: case 147: case 148: case 151: case 154: case 167: case 175:
    case 176: case 177: case 183: case 184: case 185: case 186: case 187: case 244: case 245:
    case 246: case 247:
      return true;
    default:
      return false;
  }
}

// not-solid targets leaves can overwrite (PHP !Block::$solid)
bool isSolidBlock(std::uint8_t id) { return !isTransparent(id); }

double kernel(int sx, int sz) {
  // GAUSSIAN_KERNEL, SMOOTH_SIZE=2 (double precision, PHP parity)
  static double k[5][5] = {};
  static bool init = false;
  if (!init) {
    const double bell = 1.0 / SMOOTH_SIZE, height = 2.0 * SMOOTH_SIZE;
    for (int x = -SMOOTH_SIZE; x <= SMOOTH_SIZE; ++x)
      for (int z = -SMOOTH_SIZE; z <= SMOOTH_SIZE; ++z) {
        double bx = bell * x, bz = bell * z;
        k[x + SMOOTH_SIZE][z + SMOOTH_SIZE] = height * std::exp(-(bx * bx + bz * bz) / 2.0);
      }
    init = true;
  }
  return k[sx + SMOOTH_SIZE][sz + SMOOTH_SIZE];
}

// PHP int64 overflow semantics: $hash *= $hash + 223 becomes a float on overflow,
// and later >> on that float goes through zend_dval_to_lval (fmod 2^64).
static std::int64_t phpMulWrap(std::int64_t a, std::int64_t b) {
  __int128 prod = static_cast<__int128>(a) * b;
  if (prod >= INT64_MIN && prod <= INT64_MAX) return static_cast<std::int64_t>(prod);
  double d = static_cast<double>(a) * static_cast<double>(b);
  constexpr double two64 = 4294967296.0 * 4294967296.0;
  double m = std::fmod(d, two64);
  if (m < 0) m += two64;
  return static_cast<std::int64_t>(static_cast<std::uint64_t>(m));
}

// Normal::pickBiome (hash jitter + selector)
int pickBiome(const BiomeSelector& sel, std::int64_t seed, int x, int z) {
  std::int64_t h = static_cast<std::int64_t>(
      static_cast<std::uint64_t>(static_cast<std::int64_t>(x) * 2345803) ^
      static_cast<std::uint64_t>(static_cast<std::int64_t>(z) * 9236449) ^
      static_cast<std::uint64_t>(seed));
  h = phpMulWrap(h, h + 223);
  int xn = static_cast<int>((h >> 20) & 3);
  int zn = static_cast<int>((h >> 22) & 3);
  if (xn == 3) xn = 1;
  if (zn == 3) zn = 1;
  return sel.pick(x + xn - 1, z + zn - 1);
}

struct OreType {
  std::uint8_t id;
  std::uint8_t meta;
  int cluster_count, cluster_size, min_h, max_h;
};
const OreType ORE_TYPES[] = {
    {16, 0, 20, 16, 0, 128},  // coal
    {15, 0, 20, 8, 0, 64},    // iron
    {73, 0, 8, 7, 0, 16},     // redstone
    {21, 0, 1, 6, 0, 32},     // lapis
    {14, 0, 2, 8, 0, 32},     // gold
    {56, 0, 1, 7, 0, 16},     // diamond
    {3, 0, 20, 32, 0, 128},   // dirt
    {1, 1, 20, 32, 0, 128},   // granite
    {1, 3, 20, 32, 0, 128},   // diorite
    {1, 5, 20, 32, 0, 128},   // andesite
    {13, 0, 10, 16, 0, 128},  // gravel
};

void placeOreVein(Chunk& chunk, PmRandom& random, const OreType& type, int x, int y, int z) {
  const int cluster = type.cluster_size;
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
          int my = yy & 0x7f; // PHP y-wrap parity
          auto cur = chunk.getBlockId(lx, my, lz);
          if (cur == 1 || cur == 87) chunk.setBlock(lx, my, lz, type.id, type.meta);
        }
      }
    }
  }
}

int highestWorkable(Chunk& chunk, int lx, int lz, bool trees = false) {
  for (int y = 127; y >= (trees ? 1 : 0); --y) {
    auto b = chunk.getBlockId(lx, y, lz);
    if (trees) {
      if (b == 3 || b == 2 || b == 243) return y + 1; // dirt/grass/podzol
      if (b != 0 && b != 78) return -1;
    } else {
      if (b != 0 && b != 18 && b != 161 && b != 78) return y == 0 ? -1 : y + 1;
    }
  }
  return -1;
}

void growTree(Chunk& chunk, PmRandom& random, int x, int y, int z, int type);

// Tree base: canPlace + place (Tree.php)
bool treeCanPlace(Chunk& chunk, int x, int y, int z, int height) {
  auto overridable = [](std::uint8_t id) {
    return id == 0 || id == 6 || id == 17 || id == 18 || id == 78 || id == 162 || id == 161;
  };
  int radius = 0;
  for (int yy = 0; yy < height + 3; ++yy) {
    if (yy == 1 || yy == height) ++radius;
    for (int xx = -radius; xx <= radius; ++xx)
      for (int zz = -radius; zz <= radius; ++zz) {
        int lx = x + xx, ly = y + yy, lz = z + zz;
        if (lx < 0 || lx > 15 || lz < 0 || lz > 15 || ly > 127) continue;
        if (!overridable(chunk.getBlockId(lx, ly, lz))) return false;
      }
  }
  return true;
}
void placeTrunk(Chunk& chunk, int x, int y, int z, int height, std::uint8_t wood_meta) {
  int lx = x, lz = z;
  if (lx < 0 || lx > 15 || lz < 0 || lz > 15) return;
  if (y - 1 >= 0) chunk.setBlock(lx, y - 1, lz, 3, 0); // dirt
  auto overridable = [](std::uint8_t id) {
    return id == 0 || id == 6 || id == 17 || id == 18 || id == 78 || id == 162 || id == 161;
  };
  for (int yy = 0; yy < height; ++yy) {
    if (y + yy > 127) break;
    if (overridable(chunk.getBlockId(lx, y + yy, lz))) chunk.setBlock(lx, y + yy, lz, 17, wood_meta);
  }
}
void placeOakLeaves(Chunk& chunk, PmRandom& random, int x, int y, int z, int height, std::uint8_t leaf_meta) {
  if (std::getenv("MP_TREE_DEBUG")) fprintf(stderr, "leaves x=%d y=%d z=%d h=%d\n", x, y, z, height);
  for (int yy = y - 3 + height; yy <= y + height; ++yy) {
    int yOff = yy - (y + height);
    int mid = static_cast<int>(1 - yOff / 2.0);
    for (int xx = x - mid; xx <= x + mid; ++xx) {
      int xOff = std::abs(xx - x);
      for (int zz = z - mid; zz <= z + mid; ++zz) {
        int zOff = std::abs(zz - z);
        if (xOff == mid && zOff == mid && (yOff == 0 || random.nextBoundedInt(2) == 0)) continue;
        if (yy < 0 || yy > 127) continue;
        if (xx < 0 || xx > 15 || zz < 0 || zz > 15) continue; // chunk-local coords
        if (!isSolidBlock(chunk.getBlockId(xx, yy, zz))) chunk.setBlock(xx, yy, zz, 18, leaf_meta);
      }
    }
  }
}

void growTree(Chunk& chunk, PmRandom& random, int x, int y, int z, int type) {
  // Tree::growTree — random order matches PHP exactly:
  // (birch: big check first) canPlace(default height) THEN placeObject draws heights.
  if (type == 1) { // spruce
    if (!treeCanPlace(chunk, x, y, z, 10)) return; // default spruce height 10
    int height = random.nextBoundedInt(4) + 6;
    int topSize = height - (1 + random.nextBoundedInt(2));
    int lRadius = 2 + random.nextBoundedInt(2);
    placeTrunk(chunk, x, y, z, height - random.nextBoundedInt(3), 1);
    int radius = random.nextBoundedInt(2);
    int maxR = 1, minR = 0;
    for (int yy = 0; yy <= topSize; ++yy) {
      int yyy = y + height - yy;
      for (int xx = x - radius; xx <= x + radius; ++xx) {
        int xOff = std::abs(xx - x);
        for (int zz = z - radius; zz <= z + radius; ++zz) {
          int zOff = std::abs(zz - z);
          if (xOff == radius && zOff == radius && radius > 0) continue;
          if (yyy < 0 || yyy > 127) continue;
          if (xx < 0 || xx > 15 || zz < 0 || zz > 15) continue;
          if (!isSolidBlock(chunk.getBlockId(xx, yyy, zz))) chunk.setBlock(xx, yyy, zz, 18, 1);
        }
      }
      if (radius >= maxR) {
        radius = minR;
        minR = 1;
        if (++maxR > lRadius) maxR = lRadius;
      } else {
        ++radius;
      }
    }
    return;
  }
  if (type == 2) { // birch
    bool big = random.nextBoundedInt(39) == 0; // drawn in growTree BEFORE canPlace
    if (!treeCanPlace(chunk, x, y, z, 7)) return;
    int height = random.nextBoundedInt(3) + 5 + (big ? 5 : 0);
    placeTrunk(chunk, x, y, z, height - 1, 2);
    placeOakLeaves(chunk, random, x, y, z, height, 2);
    return;
  }
  if (type == 3) { // jungle (approximate canopy; jungle wood)
    if (!treeCanPlace(chunk, x, y, z, 8)) return;
    placeTrunk(chunk, x, y, z, 7, 3);
    placeOakLeaves(chunk, random, x, y, z, 8, 3);
    return;
  }
  // oak: canPlace with default height 7, then draw real height
  if (!treeCanPlace(chunk, x, y, z, 7)) return;
  int height = random.nextBoundedInt(3) + 4;
  placeTrunk(chunk, x, y, z, height - 1, 0);
  placeOakLeaves(chunk, random, x, y, z, height, 0);
}

struct Pop {
  std::uint8_t kind; // 0 tree,1 tallgrass,2 flower,3 sugarcane,4 tallsugarcane,5 cactus,6 tallcacti,7 deadbush,8 lilypad,9 waterpit,10 mossstone
  int base, rand;
  int arg = 0;                       // tree type / flower variant list index
  std::vector<std::pair<int, int>> flower_types;
};

std::vector<Pop> popsForBiome(int biome_id) {
  // Exact PHP runtime order (Biome::register auto-appends a default Flower LAST
  // when the biome has no explicit Flower populator).
  std::vector<Pop> v;
  auto flowerDefault = [&] { v.push_back(Pop{2, 8, 0, 0, {{37, 0}, {38, 0}}}); };
  switch (biome_id) {
    case 0: // ocean
    case 7: // river
      v.push_back(Pop{3, 6, 0});
      v.push_back(Pop{4, 60, 0});
      v.push_back(Pop{1, 5, 0});
      flowerDefault();
      break;
    case 1: // plains (flower explicit, 4th)
      v.push_back(Pop{3, 6, 0});
      v.push_back(Pop{4, 60, 0});
      v.push_back(Pop{1, 25, 0});
      v.push_back(Pop{2, 2, 0, 0, {{37, 0}, {38, 0}, {38, 3}, {38, 4}, {38, 5}, {38, 6}, {38, 7}, {38, 8}}});
      v.push_back(Pop{9, 9999, 0});
      v.push_back(Pop{8, 8, 0});
      break;
    case 2: // desert
      v.push_back(Pop{5, 2, 0});
      v.push_back(Pop{6, 60, 0});
      v.push_back(Pop{7, 2, 0});
      flowerDefault();
      break;
    case 3: // mountains
    case 20: // small mountains
      v.push_back(Pop{0, 1, 0});
      v.push_back(Pop{1, 6, 0});
      flowerDefault();
      break;
    case 4: // forest
      v.push_back(Pop{0, 5, 0});
      v.push_back(Pop{1, 3, 0});
      flowerDefault();
      break;
    case 27: // birch forest
      v.push_back(Pop{0, 5, 2});
      v.push_back(Pop{1, 3, 0});
      flowerDefault();
      break;
    case 5: // taiga
      v.push_back(Pop{0, 10, 1});
      v.push_back(Pop{10, 1, 0});
      flowerDefault();
      break;
    case 6: // swamp (flower explicit, 1st)
      v.push_back(Pop{2, 8, 0, 0, {{38, 1}}});
      v.push_back(Pop{8, 4, 0});
      break;
    case 12: // ice plains
      v.push_back(Pop{1, 5, 0});
      flowerDefault();
      break;
    default: break;
  }
  return v;
}

bool findWater(Chunk& chunk, int x, int y, int z) {
  int count = 0;
  for (int i = x - 4; i < x + 4; ++i)
    for (int j = z - 4; j < z + 4; ++j) {
      int lx = i - (chunk.x() << 4), lz = j - (chunk.z() << 4);
      if (lx < 0 || lx > 15 || lz < 0 || lz > 15) continue;
      auto b = chunk.getBlockId(lx, y, lz);
      if (b == 8 || b == 9) ++count;
      if (count > 10) return true;
    }
  return false;
}

void runPop(Chunk& chunk, PmRandom& random, const Pop& pop) {
  std::uint32_t flower_rng = static_cast<std::uint32_t>((chunk.x() * 734287 + chunk.z() * 912271) ^ 0x5eed);
  int amount = random.nextRange(0, pop.rand + 1) + pop.base;
  for (int i = 0; i < amount; ++i) {
    int x = random.nextRange(chunk.x() << 4, (chunk.x() << 4) + 15);
    int z = random.nextRange(chunk.z() << 4, (chunk.z() << 4) + 15);
    int lx = x - (chunk.x() << 4), lz = z - (chunk.z() << 4);
    if (pop.kind == 9) { // water pit: scan from y=61
      int y = -1;
      for (int yy = 61; yy >= 0; --yy) {
        auto b = chunk.getBlockId(lx, yy, lz);
        if (b != 0 && b != 18 && b != 161 && b != 78) { y = yy + 1; break; }
      }
      if (y <= 0) continue;
      auto at = chunk.getBlockId(lx, y, lz);
      // PHP: (air or grass) and dirt at same y — matches reference quirk
      if ((at == 0 || at == 2) && chunk.getBlockId(lx, y, lz) == 3) chunk.setBlock(lx, y, lz, 9, 8);
      continue;
    }
    int y = highestWorkable(chunk, lx, lz, pop.kind == 0);
    if (y == -1 || y > 127) continue;
    auto at = chunk.getBlockId(lx, y, lz);
    auto below = y > 0 ? chunk.getBlockId(lx, y - 1, lz) : 0;
    switch (pop.kind) {
      case 0:
        if (std::getenv("MP_TREE_DEBUG")) {
          // count blocks before/after to see if the tree placed
          int before = 0;
          for (int x2 = 0; x2 < 16; ++x2) for (int z2 = 0; z2 < 16; ++z2) for (int y2 = 0; y2 < 128; ++y2) if (chunk.getBlockId(x2, y2, z2) == 17) ++before;
          growTree(chunk, random, lx, y, lz, pop.arg);
          int after = 0;
          for (int x2 = 0; x2 < 16; ++x2) for (int z2 = 0; z2 < 16; ++z2) for (int y2 = 0; y2 < 128; ++y2) if (chunk.getBlockId(x2, y2, z2) == 17) ++after;
          fprintf(stderr, "tree try x=%d z=%d y=%d placed=%d\n", lx, lz, y, after > before);
        } else {
          growTree(chunk, random, lx, y, lz, pop.arg);
        }
        break;
      case 1: // tall grass
        if ((at == 0 || at == 78) && below == 2) chunk.setBlock(lx, y, lz, 31, 1);
        break;
      case 2: { // flower
        if ((at == 0 || at == 78) && below == 2) {
          const auto& types = pop.flower_types;
          // PHP Flower uses global mt_rand (NOT the shared PM stream) for the type —
          // keep the PM stream aligned by drawing from a separate LCG.
          flower_rng = flower_rng * 1103515245 + 12345;
          auto& t = types[static_cast<std::size_t>((flower_rng >> 16) % types.size())];
          chunk.setBlock(lx, y, lz, static_cast<std::uint8_t>(t.first), static_cast<std::uint8_t>(t.second));
        }
        break;
      }
      case 3: // sugarcane
        if (at == 0 && (below == 12 || below == 2) && findWater(chunk, x, y - 1, z))
          chunk.setBlock(lx, y, lz, 83, 1);
        break;
      case 4: // tall sugarcane
        if (at == 0 && below == 83) chunk.setBlock(lx, y, lz, 83, 1);
        break;
      case 5: // cactus
        if (at == 0 && below == 12) chunk.setBlock(lx, y, lz, 81, 1);
        break;
      case 6: // tall cacti
        if (at == 0 && below == 81) chunk.setBlock(lx, y, lz, 81, 1);
        break;
      case 7: // dead bush
        if ((at == 0 || at == 78) && below == 12) chunk.setBlock(lx, y, lz, 32, 1);
        break;
      case 8: // lily pad
        if ((at == 0 || at == 78) && below == 9) chunk.setBlock(lx, y, lz, 111, 1);
        break;
      case 10: // moss stone
        if ((at == 0 || at == 78) && below == 243) chunk.setBlock(lx, y, lz, 48, 1);
        break;
    }
  }
}
} // namespace

void generateNormalChunk(Chunk& chunk, std::int64_t seed) {
  const int cx = chunk.x(), cz = chunk.z();
  PmRandom rng(seed);
  Simplex noiseBase(rng, 4, 1.0 / 4.0, 1.0 / 32.0);
  rng.setSeed(seed);
  BiomeSelector selector(rng);

  // terrain
  auto noise = fastNoise3D(noiseBase, 16, 128, 16, 4, 8, 4, cx * 16, 0, cz * 16);
  auto noiseAt = [&](int x, int z, int y) {
    return noise[(static_cast<std::size_t>(x) * 17 + z) * 129 + y];
  };
  for (int x = 0; x < 16; ++x)
    for (int z = 0; z < 16; ++z) {
      const int wx = cx * 16 + x, wz = cz * 16 + z;
      const int biome_id = pickBiome(selector, seed, wx, wz);
      double minSum = 0, maxSum = 0, weightSum = 0;
      double cr = 0, cg = 0, cb = 0;
      for (int sx = -SMOOTH_SIZE; sx <= SMOOTH_SIZE; ++sx)
        for (int sz = -SMOOTH_SIZE; sz <= SMOOTH_SIZE; ++sz) {
          const BiomeDef& adj = biomeDef((sx == 0 && sz == 0) ? biome_id : pickBiome(selector, seed, wx + sx, wz + sz));
          double w = kernel(sx, sz);
          minSum += (adj.min_y - 1) * w;
          maxSum += adj.max_y * w;
          cr += ((adj.color >> 16) & 0xff) * ((adj.color >> 16) & 0xff) * w;
          cg += ((adj.color >> 8) & 0xff) * ((adj.color >> 8) & 0xff) * w;
          cb += (adj.color & 0xff) * (adj.color & 0xff) * w;
          weightSum += w;
        }
      minSum /= weightSum;
      maxSum /= weightSum;
      std::uint32_t col = (static_cast<std::uint32_t>(std::sqrt(cr / weightSum)) << 16) |
                          (static_cast<std::uint32_t>(std::sqrt(cg / weightSum)) << 8) |
                          static_cast<std::uint32_t>(std::sqrt(cb / weightSum));
      chunk.setBiome(x, z, static_cast<std::uint8_t>(biome_id), col);
      bool solidLand = false;
      for (int y = 127; y >= 0; --y) {
        if (y == 0) {
          chunk.setBlock(x, y, z, 7, 0);
          continue;
        }
        double noiseAdj = 2 * ((maxSum - y) / (maxSum - minSum)) - 1;
        double caveLevel = minSum - 10;
        double distAbove = std::max(0.0, y - caveLevel);
        noiseAdj = std::min(noiseAdj, 0.4 + distAbove / 10);
        double nv = noiseAt(x, z, y) + noiseAdj;
        if (nv > 0) {
          chunk.setBlock(x, y, z, 1, 0);
          solidLand = true;
        } else if (y <= WATER_HEIGHT && !solidLand) {
          chunk.setBlock(x, y, z, 9, 0);
        }
      }
    }

  if (std::getenv("MP_TERRAIN_ONLY")) { chunk.recomputeHeightMap(); return; }
  // ---- generation populator: GroundCover (reseeded per chunk)
  PmRandom grng(chunkSeed(seed, cx, cz));
  (void)grng; // GroundCover uses no randomness; reseed for stream parity
  for (int x = 0; x < 16; ++x)
    for (int z = 0; z < 16; ++z) {
      const BiomeDef& b = biomeDef(chunk.getBiomeId(x, z));
      if (b.cover.empty()) continue;
      int diffY = isSolidBlock(b.cover[0].first) ? 0 : 1;
      int y = 127;
      for (; y > 0; --y) {
        auto id = chunk.getBlockId(x, y, z);
        if (id != 0 && !isTransparent(id)) break;
      }
      int startY = std::min(127, y + diffY);
      int endY = startY - static_cast<int>(b.cover.size());
      for (int yy = startY; yy > endY && yy >= 0; --yy) {
        auto [cid, cmeta] = b.cover[static_cast<std::size_t>(startY - yy)];
        if (chunk.getBlockId(x, yy, z) == 0 && isSolidBlock(cid)) break;
        if (yy <= WATER_HEIGHT && cid == 2 && chunk.getBlockId(x, yy + 1, z) == 9) cid = 3;
        chunk.setBlock(x, yy, z, cid, cmeta);
      }
    }

  // ---- populate stage (same seed value, fresh stream): ores then biome populators
  PmRandom prng(chunkSeed(seed, cx, cz));
  for (const auto& type : ORE_TYPES) {
    for (int i = 0; i < type.cluster_count; ++i) {
      int x = prng.nextRange(cx << 4, (cx << 4) + 15);
      int y = prng.nextRange(type.min_h, type.max_h);
      int z = prng.nextRange(cz << 4, (cz << 4) + 15);
      int lx = x - (cx << 4), lz = z - (cz << 4);
      auto cur = chunk.getBlockId(lx, y, lz);
      if (cur == 1 || cur == 87) placeOreVein(chunk, prng, type, x, y, z);
    }
  }
  if (!std::getenv("MP_NO_POPS")) {
    int center_biome = chunk.getBiomeId(7, 7);
    int only = std::getenv("MP_ONLY_POP") ? atoi(std::getenv("MP_ONLY_POP")) : -1;
    for (const auto& pop : popsForBiome(center_biome))
      if (only < 0 || pop.kind == only) runPop(chunk, prng, pop);
  }
  chunk.setPopulated(true);
}

} // namespace mpmpes::level::gen
