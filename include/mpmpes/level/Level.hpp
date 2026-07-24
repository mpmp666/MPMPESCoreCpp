#pragma once

#include "mpmpes/item/Item.hpp"
#include "mpmpes/level/Chunk.hpp"

#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <unordered_map>
#include <tuple>
#include <utility>
#include <vector>

namespace mpmpes::level {

enum class GeneratorType {
  Flat = 0,
  Void = 1,
  NormalStub = 2, // overworld infinite-ish hills
  Nether = 3,
  End = 4,
};

struct Vec3i {
  int x = 0;
  int y = 0;
  int z = 0;
};

struct LevelSettings {
  std::string name = "world";
  GeneratorType generator = GeneratorType::Flat;
  std::int32_t seed = 0;
  std::uint8_t dimension = 0; // 0 overworld
  int gamemode = 0;
  Vec3i spawn{0, 5, 0}; // flat surface ~ y=4 + 1
  std::string preset = "2;7,2x3,2;1;"; // for future preset parser
};

inline std::int64_t chunkKey(int x, int z) {
  return (static_cast<std::int64_t>(static_cast<std::uint32_t>(x)) << 32) |
         static_cast<std::uint32_t>(z);
}

class Level {
public:
  explicit Level(LevelSettings settings);

  const LevelSettings& settings() const { return settings_; }
  const std::string& name() const { return settings_.name; }
  GeneratorType generator() const { return settings_.generator; }
  std::uint8_t dimension() const { return settings_.dimension; }
  // PE 0.14 wire dimension: only 0/1. End worlds report OVERWORLD on the protocol.
  std::uint8_t protocolDimension() const {
    if (settings_.dimension == protocol::DIMENSION_NETHER) return protocol::DIMENSION_NETHER;
    return protocol::DIMENSION_OVERWORLD;
  }
  Vec3i spawn() const { return settings_.spawn; }
  std::int32_t time() const { return time_; }
  void setTime(std::int32_t t) { time_ = t; }
  void tickTime() {
    if (time_running_) time_ = (time_ + 1) % 24000;
  }

  // Get or generate chunk (thread-safe enough for single network thread)
  Chunk& getOrCreateChunk(int cx, int cz);
  const Chunk* getChunk(int cx, int cz) const;

  std::string chunkNetworkPayload(int cx, int cz);

  // World block access (creates chunk if needed)
  std::uint8_t getBlockId(int x, int y, int z);
  std::uint8_t getBlockMeta(int x, int y, int z);
  // returns false if rejected (bedrock / OOB)
  bool setBlock(int x, int y, int z, std::uint8_t id, std::uint8_t meta = 0);
  // highest solid block Y at column, or -1 if empty
  int highestBlockY(int x, int z);

  int generatorIdForStartGame() const {
    switch (settings_.generator) {
      case GeneratorType::Flat:
      case GeneratorType::Void:
        return protocol::GENERATOR_FLAT;
      case GeneratorType::NormalStub:
      case GeneratorType::Nether:
      case GeneratorType::End:
        return protocol::GENERATOR_INFINITE;
    }
    return protocol::GENERATOR_FLAT;
  }

  std::size_t loadedChunks() const { return chunks_.size(); }

  // Persistence root: worlds/<name>/
  void setDataPath(std::string path) { data_path_ = std::move(path); }
  const std::string& dataPath() const { return data_path_; }
  std::string chunkPath(int cx, int cz) const;
  std::string chestPath() const;

  // Save dirty chunks (+ optional force all). Returns number of chunks written.
  int saveDirtyChunks(bool force_all = false);
  // Try load from disk before generate (called inside getOrCreateChunk)
  bool tryLoadChunk(int cx, int cz, Chunk& out);

  // Simple chest inventories keyed by block pos
  struct ChestInv {
    int x = 0, y = 0, z = 0;
    std::vector<item::ItemStack> slots; // 27
    bool dirty = false;
    // PE double-chest pair (PM tile pairx/pairz). INT_MIN = unpaired.
    int pair_x = 0x80000000;
    int pair_z = 0x80000000;
    bool isPaired() const { return pair_x != static_cast<int>(0x80000000); }
  };
  ChestInv& getOrCreateChest(int x, int y, int z);
  ChestInv* getChest(int x, int y, int z);
  void removeChest(int x, int y, int z);
  void saveChests();
  void loadChests();
  // Snapshot chests in a chunk column (for BlockEntityData after FullChunkData).
  std::vector<ChestInv> chestsInChunk(int cx, int cz) const;
  // Clear pair links for this chest and its partner (call before erase).
  void unpairChest(int x, int y, int z);

  // Furnace inventories: 3 slots (0 smelt, 1 fuel, 2 result) — same on-disk map as chests (FURN magic)
  struct FurnaceInv {
    int x = 0, y = 0, z = 0;
    std::vector<item::ItemStack> slots; // 3: 0 smelt, 1 fuel, 2 result
    std::int16_t burn_time = 0;   // ticks remaining fuel
    std::int16_t max_time = 0;    // last fuel max burn
    std::int16_t cook_time = 0;   // 0..200
    bool dirty = false;           // need disk save
    bool need_push_slots = false; // server-side slot change → push to open viewers (NOT client echo)
    std::int16_t last_sent_cook = -1;
    std::int16_t last_sent_burn = -1;
    // v0.4.12: ticks after open to skip SetData/SetSlot (filled open SetContent crashes PE 0.14)
    std::int16_t open_ui_grace = 0;
  };
  // Snapshot furnace block positions (no callback under lock — avoid deadlock with setBlock)
  std::vector<std::tuple<int, int, int>> furnacePositions() const {
    std::lock_guard lock(mu_);
    std::vector<std::tuple<int, int, int>> out;
    out.reserve(furnaces_.size());
    for (const auto& [_, f] : furnaces_) out.emplace_back(f.x, f.y, f.z);
    return out;
  }
  FurnaceInv& getOrCreateFurnace(int x, int y, int z);
  FurnaceInv* getFurnace(int x, int y, int z);
  void removeFurnace(int x, int y, int z);
  void saveFurnaces();
  void loadFurnaces();
  std::string furnacePath() const;

private:
  Chunk generate(int cx, int cz) const;
  static std::int64_t blockPosKey(int x, int y, int z) {
    return (static_cast<std::int64_t>(static_cast<std::uint32_t>(x)) << 40) |
           (static_cast<std::int64_t>(static_cast<std::uint32_t>(y) & 0xff) << 32) |
           static_cast<std::uint32_t>(z);
  }

  LevelSettings settings_;
  std::int32_t time_ = 0;
  bool time_running_ = true;
  mutable std::mutex mu_;
  std::unordered_map<std::int64_t, Chunk> chunks_;
  std::string data_path_; // worlds/<name>
  std::unordered_map<std::int64_t, ChestInv> chests_;
  std::unordered_map<std::int64_t, FurnaceInv> furnaces_;
};

class LevelManager {
public:
  Level& create(LevelSettings settings);
  Level* get(std::string_view name);
  Level& getOrCreateDefault(const LevelSettings& defaults);
  Level* defaultLevel() { return default_; }
  void setDefault(Level& level) { default_ = &level; }

  const std::unordered_map<std::string, std::unique_ptr<Level>>& all() const { return levels_; }

  void tickAll();

  // Save all worlds (dirty chunks + chests). Returns total chunks written.
  int saveAll(bool force_all = false);

  // Load simple worlds list: name:type:seed per line in worlds.txt (optional)
  void loadWorldsFile(std::string_view path, const LevelSettings& defaults);

private:
  std::unordered_map<std::string, std::unique_ptr<Level>> levels_;
  Level* default_ = nullptr;
};

GeneratorType parseGenerator(std::string_view s);

} // namespace mpmpes::level
