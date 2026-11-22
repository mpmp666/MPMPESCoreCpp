#pragma once
// End features (ender/EndFeatures.php): obsidian spikes, exit podium, spawn
// platform, chorus plants, small end islands. End cities live in the structure
// system (separate milestone).

#include <cstdint>
#include <string>
#include <vector>

namespace mpmpes::level { class Chunk; }

namespace mpmpes::level::gen {

class XoroshiroRandom {
public:
  explicit XoroshiroRandom(std::int64_t seed) { setSeed(seed); }
  void setSeed(std::int64_t seed);
  std::int64_t nextRawLong();
  std::int32_t next(int bits);
  std::int32_t nextInt();
  std::int64_t nextLong();
  std::int32_t nextInt(std::int32_t bound);
  static int dbg_count;
  std::int32_t nextIntCounted(std::int32_t bound) { ++dbg_count; return nextInt(bound); }
  double nextFloat() { return next(24) * 5.960464477539063E-8; }
  double nextDouble();
  bool nextBoolean() { return next(1) != 0; }
  std::int64_t setDecorationSeed(std::int64_t seed, std::int64_t bx, std::int64_t bz);
  void setFeatureSeed(std::int64_t decoration_seed, std::int64_t index, std::int64_t step);

private:
  std::int64_t lo_ = 0, hi_ = 0;
};

struct EndSpike {
  int centerX, centerZ, radius, height;
  bool guarded;
  int size;
};

class EndFeatures {
public:
  EndFeatures(std::int64_t seed, class EndTerrainNoise& terrain);
  // Apply to a generated chunk (only blocks inside the chunk are written).
  void populate(level::Chunk& chunk);

  const std::vector<EndSpike>& spikes() const { return spikes_; }
  int podiumY() const { return podium_y_; }

private:
  std::int64_t seed_;
  EndTerrainNoise& terrain_;
  std::vector<EndSpike> spikes_;
  int podium_y_ = 48;
  std::int64_t biome_zoom_seed_ = 0;

  void placeSpike(level::Chunk& chunk, const EndSpike& spike);
  void placePodium(level::Chunk& chunk);
  void placePlatform(level::Chunk& chunk);
  void gateways(level::Chunk& chunk);
  void chorusPlants(level::Chunk& chunk);
  void smallIslands(level::Chunk& chunk);
  std::string biomeAt(int x, int y, int z);
  void growChorus(level::Chunk& chunk, XoroshiroRandom& random, int x, int y, int z, int ox, int oz, int depth);
};

} // namespace mpmpes::level::gen
