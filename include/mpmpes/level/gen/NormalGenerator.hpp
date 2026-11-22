#pragma once
// PocketMine terrain primitives, ported 1:1 so worlds generate identically to
// the PHP core for the same seed:
//   utils/Random, level/generator/noise/{Noise,Perlin,Simplex},
//   level/generator/biome/BiomeSelector, normal/Normal generator + populators.

#include <cstdint>
#include <vector>

namespace mpmpes::level { class Chunk; }

namespace mpmpes::level::gen {

// pocketmine\utils\Random (NOT mt19937 — PM's crc32+LCG)
class PmRandom {
public:
  explicit PmRandom(std::int64_t seed = -1) { setSeed(seed); }
  void setSeed(std::int64_t seed);
  std::int32_t nextInt() { return nextSignedInt() & 0x7fffffff; }
  std::int32_t nextSignedInt();
  double nextFloat() { return static_cast<double>(nextInt()) / 2147483647.0; }
  bool nextBoolean() { return (nextSignedInt() & 1) == 0; }
  int nextRange(int start, int end) { return start + (nextInt() % (end + 1 - start)); }
  int nextBoundedInt(int bound) { return nextInt() % bound; }

  std::int64_t debugSeed() const { return seed_; }
private:
  std::int64_t seed_ = 0;
};

class Simplex {
public:
  Simplex(PmRandom& random, int octaves, double persistence, double expansion = 1.0);
  // normalized in [-1, 1] when normalized=true
  double noise2D(double x, double z, bool normalized = false) const;
  double noise3D(double x, double y, double z, bool normalized = false) const;

private:
  double getNoise2D(double x, double y) const;
  double getNoise3D(double x, double y, double z) const;
  int perm_[512]{};
  double offset_x_ = 0, offset_y_ = 0, offset_z_ = 0;
  int octaves_;
  double persistence_;
  double expansion_;
};

// Generator::getFastNoise3D — sampled grid + trilinear fill, layout [x][z][y]
std::vector<double> fastNoise3D(const Simplex& noise, int x_size, int y_size, int z_size,
                                int xs, int ys, int zs, int base_x, int base_y, int base_z);

struct BiomeDef {
  int id;
  int min_y, max_y;
  std::uint32_t color; // rgb
  std::vector<std::pair<std::uint8_t, std::uint8_t>> cover; // (id, meta) top-down
};

const BiomeDef& biomeDef(int id);

// 0xdeadbeef ^ (chunkX << 8) ^ chunkZ ^ seed, wrapped like PHP pack("N", ...)
inline std::int64_t chunkSeed(std::int64_t seed, int cx, int cz) {
  std::uint64_t v = 0xdeadbeefULL ^ (static_cast<std::uint64_t>(static_cast<std::int64_t>(cx)) << 8) ^
                    static_cast<std::uint64_t>(static_cast<std::int64_t>(cz)) ^
                    static_cast<std::uint64_t>(seed);
  return static_cast<std::int64_t>(v & 0xffffffffULL);
}

void generateNormalChunk(Chunk& chunk, std::int64_t seed);
void generateNetherChunk(Chunk& chunk, std::int64_t seed);
void generateAstralChunk(Chunk& chunk, std::int64_t seed);
// in EndGenerator.cpp


// BiomeSelector: temperature/rainfall simplex pair + 64x64 lookup map
class BiomeSelector {
public:
  explicit BiomeSelector(PmRandom& random);
  int pick(int x, int z) const; // biome id
private:
  Simplex temperature_;
  Simplex rainfall_;
  std::uint8_t map_[64 * 64]{};
};

} // namespace mpmpes::level::gen
