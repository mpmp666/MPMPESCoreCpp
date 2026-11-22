#pragma once
// MCPE-26.2-style End terrain (ender/EndTerrainNoise stack): JavaRandom,
// ImprovedNoise, SimplexNoise, BlendedNoise, EndIslands, EndChunkGrid.

#include <cmath>
#include <cstdint>
#include <memory>

namespace mpmpes::level { class Chunk; }
#include <vector>

namespace mpmpes::level::gen {

// pocketmine ender noise\JavaRandom — java.util.Random LCG, exact 48-bit arithmetic
class JavaRandom {
public:
  explicit JavaRandom(std::int64_t seed = 0) { setSeed(seed); }
  void setSeed(std::int64_t seed) { state_ = (seed ^ 0x5deece66dLL) & 0xffffffffffffLL; }
  std::int32_t next(int bits);
  std::int32_t nextInt() { return next(32); }
  std::int32_t nextInt(std::int32_t bound);
  std::int64_t nextLong();
  bool nextBoolean() { return next(1) != 0; }
  double nextDouble();
  double nextFloat() { return next(24) * 5.960464477539063E-8; }
  void consumeCount(int n) { for (int i = 0; i < n; ++i) next(32); }

  static std::int64_t addLong(std::int64_t a, std::int64_t b) { return a + b; }
  static std::int64_t multiplyLong(std::int64_t a, std::int64_t b) {
    return static_cast<std::int64_t>(static_cast<std::uint64_t>(a) * static_cast<std::uint64_t>(b));
  }

private:
  std::int64_t state_ = 0;
};

class ImprovedNoise {
public:
  explicit ImprovedNoise(JavaRandom& random);
  double noise(double x, double y, double z, double yScale = 0.0, double yFudge = 0.0) const;

private:
  int p_[256];
  double xo_, yo_, zo_;
};

class EndSimplexNoise {
public:
  explicit EndSimplexNoise(JavaRandom& random);
  double getValue(double x, double y) const;

private:
  int p_[256];
  double xo_, yo_, zo_;
  double f2_, g2_;
};

class BlendedNoise {
public:
  explicit BlendedNoise(JavaRandom& random);
  double compute(int x, int y, int z) const;

private:
  std::vector<ImprovedNoise> min_limit_, max_limit_, main_;
};

class EndIslands {
public:
  explicit EndIslands(std::int64_t seed);
  double heightValue(int sx, int sz);
  double height(int bx, int bz) { return heightValue(floorDiv(bx, 8), floorDiv(bz, 8)); }
  double compute(int bx, int bz);

  static int floorDiv(int a, int b) { return a >= 0 ? a / b : (a - b + 1) / b; }

private:
  std::unique_ptr<EndSimplexNoise> noise_;
};

class EndTerrainNoise {
public:
  explicit EndTerrainNoise(std::int64_t seed);
  double density(int x, int y, int z);
  // lattice nodes for a chunk: gx,gz in 0..2, gy 0..32 -> 297 values
  void chunkNodes(int cx, int cz, double out[297]);
  static double squeeze(double v) {
    double c = std::max(-1.0, std::min(1.0, v));
    return c / 2.0 - c * c * c / 24.0;
  }

private:
  std::unique_ptr<BlendedNoise> base_;
  EndIslands islands_;
};

void generateEndTerrainChunk(level::Chunk& chunk, std::int64_t seed);

} // namespace mpmpes::level::gen
