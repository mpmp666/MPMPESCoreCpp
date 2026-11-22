#pragma once

#include "mpmpes/item/Item.hpp"
#include "mpmpes/level/Chunk.hpp"
#include "mpmpes/level/Region.hpp"
#include "mpmpes/nbt/Tag.hpp"

#include <functional>

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
  Astral = 5, // Genisys astral: end-stone floating islands, no pillars
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
  void setSpawn(Vec3i s) { settings_.spawn = s; }
  // Scan nearby terrain and set spawn to center of largest flat plane.
  // Search: 3x3 chunks around (0,0), only columns within max_dist of origin.
  // Returns chosen spawn (also writes settings_.spawn).
  Vec3i findAndSetAutoSpawn(int chunk_radius = 1, int max_dist = 100);
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
  // Safe stand feet Y near column for portal/goto (open air above, no ceiling trap).
  // Returns feet Y, or -1 if none found.
  int safeStandFeetY(int x, int z);

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
  // Try load from disk before generate (called inside getOrCreateChunk, mu_ held)
  bool tryLoadChunk(int cx, int cz, Chunk& out);
  // level.dat (PocketMine McRegion BE gzip NBT "Data" compound)
  bool loadLevelData();
  void saveLevelData();
  bool hasLevelData() const { return level_data_ != nullptr; }
  // True when the world was created this run (no pre-existing level.dat)
  bool isFresh() const { return fresh_; }
  void markFresh(bool v = true) { fresh_ = v; }
  nbt::TagPtr levelData() const { return level_data_; }
  // Import pre-0.6 chests.dat/furnaces.dat/... into region TileEntities
  void migrateLegacyTileFiles();
  // Entities stored in chunk NBT that the runtime has not adopted yet (removed on fetch)
  std::vector<nbt::TagPtr> rawEntitiesInChunk(int cx, int cz);
  // Entities loaded from disk awaiting adoption by the entity manager (server tick).
  std::vector<std::pair<std::int64_t, nbt::TagPtr>> drainPendingEntities();
  // Hook used on save to serialize live entities of a chunk (called under level lock)
  using EntitySaver = std::function<std::vector<nbt::TagPtr>(int cx, int cz)>;
  void setEntitySaver(EntitySaver f) { entity_saver_ = std::move(f); }
  void markEntitiesDirty() { entities_dirty_ = true; }
  RegionCache& region();

  // Weather (Genisys level/weather)
  bool raining() const { return raining_; }
  bool thundering() const { return thundering_; }
  void setRaining(bool v) { raining_ = v; }
  void setThundering(bool v) { thundering_ = v; }
  void setTimeRunning(bool v) { time_running_ = v; }
  bool timeRunning() const { return time_running_; }
  std::int32_t seed() const { return settings_.seed; }

  // Simple chest inventories keyed by block pos
  struct ChestInv {
    int x = 0, y = 0, z = 0;
    std::vector<item::ItemStack> slots; // 27
    bool dirty = false;
    // PE double-chest pair (PM tile pairx/pairz). INT_MIN = unpaired.
    int pair_x = 0x80000000;
    int pair_z = 0x80000000;
    bool isPaired() const { return pair_x != static_cast<int>(0x80000000); }
    std::string custom_name;
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

  // Hopper inventories: 5 slots (PE 0.14 HopperBlockEntity)
  // meta: low 3 bits = facing (0 down, 2 N, 3 S, 4 W, 5 E); bit 0x8 = disabled by redstone
  struct HopperInv {
    int x = 0, y = 0, z = 0;
    std::vector<item::ItemStack> slots; // 5
    int cooldown = 0; // ticks until next transfer (PE ~8)
    bool dirty = false;
  };
  HopperInv& getOrCreateHopper(int x, int y, int z);
  HopperInv* getHopper(int x, int y, int z);
  void removeHopper(int x, int y, int z);
  void saveHoppers();
  void loadHoppers();
  std::string hopperPath() const;
  // Snapshot hoppers in a chunk column (for BlockEntityData after FullChunkData).
  std::vector<HopperInv> hoppersInChunk(int cx, int cz) const;
  std::vector<std::tuple<int, int, int>> hopperPositions() const {
    std::lock_guard lock(mu_);
    std::vector<std::tuple<int, int, int>> out;
    out.reserve(hoppers_.size());
    for (const auto& [_, h] : hoppers_) out.emplace_back(h.x, h.y, h.z);
    return out;
  }

  // Sign tiles (PM tile/Sign: Text1..Text4 + Creator)
  struct SignTile {
    int x = 0, y = 0, z = 0;
    std::string text1, text2, text3, text4;
    std::string creator; // player uuid/name who placed; empty = no edit guard
    bool dirty = false;
  };
  SignTile& getOrCreateSign(int x, int y, int z);
  SignTile* getSign(int x, int y, int z);
  void removeSign(int x, int y, int z);
  void saveSigns();
  void loadSigns();
  std::string signPath() const;
  std::vector<SignTile> signsInChunk(int cx, int cz) const;

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
  std::unordered_map<std::int64_t, HopperInv> hoppers_;
  std::unordered_map<std::int64_t, SignTile> signs_;

  void loadTileNbt(const nbt::TagPtr& t, std::int64_t chunk_key);
  nbt::TagPtr buildChunkNbtLocked(const Chunk& c) const;
  void saveLevelDataLocked();
  std::unique_ptr<RegionCache> region_;
  std::unordered_map<std::int64_t, std::vector<nbt::TagPtr>> raw_tiles_;
  std::unordered_map<std::int64_t, std::vector<nbt::TagPtr>> raw_entities_;
  std::vector<std::pair<std::int64_t, nbt::TagPtr>> pending_entities_;
  nbt::TagPtr level_data_;
  bool fresh_ = false;
  bool raining_ = false;
  bool thundering_ = false;
  EntitySaver entity_saver_;
  bool entities_dirty_ = false;
};

// Item <-> PocketMine NBT (id/Count/Damage/Slot/tag)
nbt::TagPtr itemToNbt(const item::ItemStack& s, int slot = -1);
item::ItemStack itemFromNbt(const nbt::Tag& t);
std::string generatorNameFor(GeneratorType g);

class LevelManager {
public:
  Level& create(LevelSettings settings);
  Level* get(std::string_view name);
  Level& getOrCreateDefault(const LevelSettings& defaults);
  Level* defaultLevel() { return default_; }
  void setDefault(Level& level) { default_ = &level; }

  const std::unordered_map<std::string, std::unique_ptr<Level>>& all() const { return levels_; }

  void tickAll();

  // Save all worlds (dirty chunks + chests/furnaces/signs). Returns total chunks written.
  int saveAll(bool force_all = false);

  // Load simple worlds list: name:type:seed per line in worlds.txt (optional)
  void loadWorldsFile(std::string_view path, const LevelSettings& defaults);

private:
  std::unordered_map<std::string, std::unique_ptr<Level>> levels_;
  Level* default_ = nullptr;
};

GeneratorType parseGenerator(std::string_view s);

} // namespace mpmpes::level
