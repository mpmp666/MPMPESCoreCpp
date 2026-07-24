#pragma once

#include "mpmpes/protocol/Info.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace mpmpes::level {

// MCPE 0.14 column chunk: 16x16x128
inline constexpr int kChunkWidth = 16;
inline constexpr int kChunkHeight = 128;
inline constexpr int kBlocksSize = kChunkWidth * kChunkWidth * kChunkHeight; // 32768
inline constexpr int kNibbleSize = kBlocksSize / 2;                          // 16384
inline constexpr int kHeightMapSize = 256;
inline constexpr int kBiomeColorsSize = 256; // ints

inline int blockIndex(int x, int y, int z) {
  // PM mcregion: (x << 11) | (z << 7) | y
  return (x << 11) | (z << 7) | y;
}

class Chunk {
public:
  Chunk(int x = 0, int z = 0);

  int x() const { return x_; }
  int z() const { return z_; }

  void setBlock(int x, int y, int z, std::uint8_t id, std::uint8_t meta = 0);
  std::uint8_t getBlockId(int x, int y, int z) const;
  std::uint8_t getBlockMeta(int x, int y, int z) const;

  void setBiome(int x, int z, std::uint8_t biome_id, std::uint32_t rgb = 0x8DB360);
  void setHeight(int x, int z, std::uint8_t h);
  void recomputeHeightMap();

  // Network payload for FullChunkDataPacket (ChunkRequestTask layout)
  std::string networkPayload() const;

  bool generated() const { return generated_; }
  void setGenerated(bool v = true) { generated_ = v; }

  bool dirty() const { return dirty_; }
  void markDirty(bool v = true) { dirty_ = v; }
  void clearDirty() { dirty_ = false; }

  // Disk: simple binary format "MPC1" + blocks + data + height_map + biome_colors
  bool saveToFile(const std::string& path) const;
  static bool loadFromFile(const std::string& path, Chunk& out);

private:
  int x_ = 0;
  int z_ = 0;
  bool generated_ = false;
  bool dirty_ = false;
  std::vector<std::uint8_t> blocks_;     // 32768
  std::vector<std::uint8_t> data_;       // 16384 nibbles
  std::vector<std::uint8_t> sky_light_;  // 16384
  std::vector<std::uint8_t> block_light_;// 16384
  std::vector<std::uint8_t> height_map_; // 256
  std::vector<std::uint32_t> biome_colors_; // 256
};

// Flat: "2;7,2x3,2;1;" → bedrock, 2 dirt, grass; biome plains
Chunk generateFlatChunk(int chunk_x, int chunk_z, std::uint8_t biome = 1);

// Void: empty air
Chunk generateVoidChunk(int chunk_x, int chunk_z);

// Overworld hills: multi-octave height, dirt/grass, basic ores, sparse trees
Chunk generateNormalStubChunk(int chunk_x, int chunk_z, std::int32_t seed);

// Nether: bedrock floor/ceiling, netherrack, lava seas, glowstone blobs
Chunk generateNetherChunk(int chunk_x, int chunk_z, std::int32_t seed);

// End: floating end-stone islands around origin
Chunk generateEndChunk(int chunk_x, int chunk_z, std::int32_t seed);

} // namespace mpmpes::level
