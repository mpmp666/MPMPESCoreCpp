#include "mpmpes/level/Level.hpp"

#include "mpmpes/protocol/Packets.hpp"
#include "mpmpes/util/Logger.hpp"

#include <cstring>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace fs = std::filesystem;

namespace mpmpes::level {

GeneratorType parseGenerator(std::string_view s) {
  if (s == "flat" || s == "FLAT" || s == "2") return GeneratorType::Flat;
  if (s == "void" || s == "VOID") return GeneratorType::Void;
  if (s == "normal" || s == "default" || s == "infinite" || s == "1")
    return GeneratorType::NormalStub;
  if (s == "nether" || s == "hell" || s == "NETHER" || s == "HELL") return GeneratorType::Nether;
  if (s == "end" || s == "ender" || s == "the_end" || s == "END" || s == "ENDER")
    return GeneratorType::End;
  return GeneratorType::Flat;
}

Level::Level(LevelSettings settings) : settings_(std::move(settings)) {
  // dimension hints from generator if unset
  // End uses OVERWORLD on the PE wire (dim=2 crashes 0.14 clients); keep internal flag via generator.
  if (settings_.generator == GeneratorType::Nether) settings_.dimension = protocol::DIMENSION_NETHER;
  if (settings_.generator == GeneratorType::End) settings_.dimension = protocol::DIMENSION_OVERWORLD;

  if (settings_.generator == GeneratorType::Flat) {
    settings_.spawn = {0, 5, 0};
  } else if (settings_.generator == GeneratorType::Void) {
    settings_.spawn = {0, 64, 0};
  } else if (settings_.generator == GeneratorType::Nether) {
    settings_.spawn = {0, 72, 0}; // above lava seas
  } else if (settings_.generator == GeneratorType::End) {
    settings_.spawn = {0, 65, 0}; // central island platform
  } else {
    settings_.spawn = {0, 72, 0}; // normal hills ~ sea+10
  }
}

Chunk Level::generate(int cx, int cz) const {
  switch (settings_.generator) {
    case GeneratorType::Flat:
      return generateFlatChunk(cx, cz);
    case GeneratorType::Void:
      return generateVoidChunk(cx, cz);
    case GeneratorType::NormalStub:
      return generateNormalStubChunk(cx, cz, settings_.seed);
    case GeneratorType::Nether:
      return generateNetherChunk(cx, cz, settings_.seed);
    case GeneratorType::End:
      return generateEndChunk(cx, cz, settings_.seed);
  }
  return generateFlatChunk(cx, cz);
}

std::string Level::chunkPath(int cx, int cz) const {
  // worlds/<name>/chunks/c.cx.cz.mpc
  return data_path_ + "/chunks/c." + std::to_string(cx) + "." + std::to_string(cz) + ".mpc";
}

std::string Level::chestPath() const {
  return data_path_ + "/chests.dat";
}

std::string Level::furnacePath() const {
  return data_path_ + "/furnaces.dat";
}

bool Level::tryLoadChunk(int cx, int cz, Chunk& out) {
  if (data_path_.empty()) return false;
  return Chunk::loadFromFile(chunkPath(cx, cz), out);
}

Chunk& Level::getOrCreateChunk(int cx, int cz) {
  std::lock_guard lock(mu_);
  const auto key = chunkKey(cx, cz);
  auto it = chunks_.find(key);
  if (it != chunks_.end()) return it->second;
  Chunk chunk;
  if (tryLoadChunk(cx, cz, chunk)) {
    auto [ins, _] = chunks_.emplace(key, std::move(chunk));
    return ins->second;
  }
  chunk = generate(cx, cz);
  chunk.markDirty(true); // new terrain — persist on first autosave so edits accumulate
  auto [ins, _] = chunks_.emplace(key, std::move(chunk));
  return ins->second;
}

int Level::saveDirtyChunks(bool force_all) {
  if (data_path_.empty()) return 0;
  std::error_code ec;
  fs::create_directories(data_path_ + "/chunks", ec);
  int n = 0;
  std::lock_guard lock(mu_);
  for (auto& [_, c] : chunks_) {
    if (!force_all && !c.dirty()) continue;
    if (c.saveToFile(chunkPath(c.x(), c.z()))) {
      c.clearDirty();
      ++n;
    }
  }
  return n;
}

Level::ChestInv& Level::getOrCreateChest(int x, int y, int z) {
  std::lock_guard lock(mu_);
  const auto key = blockPosKey(x, y, z);
  auto it = chests_.find(key);
  if (it != chests_.end()) return it->second;
  ChestInv inv;
  inv.x = x;
  inv.y = y;
  inv.z = z;
  inv.slots.assign(27, item::ItemStack::air());
  inv.dirty = true;
  inv.pair_x = static_cast<int>(0x80000000);
  inv.pair_z = static_cast<int>(0x80000000);
  auto [ins, _] = chests_.emplace(key, std::move(inv));
  return ins->second;
}

Level::ChestInv* Level::getChest(int x, int y, int z) {
  std::lock_guard lock(mu_);
  auto it = chests_.find(blockPosKey(x, y, z));
  if (it == chests_.end()) return nullptr;
  return &it->second;
}

void Level::unpairChest(int x, int y, int z) {
  std::lock_guard lock(mu_);
  auto it = chests_.find(blockPosKey(x, y, z));
  if (it == chests_.end()) return;
  const int px = it->second.pair_x;
  const int pz = it->second.pair_z;
  it->second.pair_x = static_cast<int>(0x80000000);
  it->second.pair_z = static_cast<int>(0x80000000);
  it->second.dirty = true;
  if (px != static_cast<int>(0x80000000)) {
    // partner y is same as this chest
    auto pit = chests_.find(blockPosKey(px, y, pz));
    if (pit != chests_.end() && pit->second.pair_x == x && pit->second.pair_z == z) {
      pit->second.pair_x = static_cast<int>(0x80000000);
      pit->second.pair_z = static_cast<int>(0x80000000);
      pit->second.dirty = true;
    }
  }
}

void Level::removeChest(int x, int y, int z) {
  // single lock: unpair partner then erase (avoid nested lock with unpairChest)
  std::lock_guard lock(mu_);
  auto it = chests_.find(blockPosKey(x, y, z));
  if (it != chests_.end()) {
    const int px = it->second.pair_x;
    const int pz = it->second.pair_z;
    const int y0 = it->second.y;
    if (px != static_cast<int>(0x80000000)) {
      auto pit = chests_.find(blockPosKey(px, y0, pz));
      if (pit != chests_.end() && pit->second.pair_x == x && pit->second.pair_z == z) {
        pit->second.pair_x = static_cast<int>(0x80000000);
        pit->second.pair_z = static_cast<int>(0x80000000);
        pit->second.dirty = true;
      }
    }
    chests_.erase(it);
  }
}

std::vector<Level::ChestInv> Level::chestsInChunk(int cx, int cz) const {
  std::lock_guard lock(mu_);
  std::vector<ChestInv> out;
  const int min_x = cx * 16;
  const int max_x = min_x + 15;
  const int min_z = cz * 16;
  const int max_z = min_z + 15;
  for (const auto& [_, c] : chests_) {
    if (c.x >= min_x && c.x <= max_x && c.z >= min_z && c.z <= max_z) out.push_back(c);
  }
  return out;
}

void Level::saveChests() {
  if (data_path_.empty()) return;
  std::error_code ec;
  fs::create_directories(data_path_, ec);
  std::ofstream out(chestPath(), std::ios::binary | std::ios::trunc);
  if (!out) return;
  // CHS2: same as CHST + pairx/pairz after slots (PE double-chest)
  out.write("CHS2", 4);
  std::lock_guard lock(mu_);
  std::uint32_t count = static_cast<std::uint32_t>(chests_.size());
  out.write(reinterpret_cast<const char*>(&count), 4);
  for (auto& [_, c] : chests_) {
    out.write(reinterpret_cast<const char*>(&c.x), 4);
    out.write(reinterpret_cast<const char*>(&c.y), 4);
    out.write(reinterpret_cast<const char*>(&c.z), 4);
    std::uint16_t slots = static_cast<std::uint16_t>(c.slots.size());
    out.write(reinterpret_cast<const char*>(&slots), 2);
    for (const auto& s : c.slots) {
      std::int16_t id = s.id;
      std::uint8_t cnt = s.count;
      std::int16_t dmg = s.damage;
      out.write(reinterpret_cast<const char*>(&id), 2);
      out.write(reinterpret_cast<const char*>(&cnt), 1);
      out.write(reinterpret_cast<const char*>(&dmg), 2);
    }
    out.write(reinterpret_cast<const char*>(&c.pair_x), 4);
    out.write(reinterpret_cast<const char*>(&c.pair_z), 4);
    c.dirty = false;
  }
}

void Level::loadChests() {
  if (data_path_.empty()) return;
  std::ifstream in(chestPath(), std::ios::binary);
  if (!in) return;
  char magic[4]{};
  in.read(magic, 4);
  const bool v2 = std::memcmp(magic, "CHS2", 4) == 0;
  if (!v2 && std::memcmp(magic, "CHST", 4) != 0) return;
  std::uint32_t count = 0;
  in.read(reinterpret_cast<char*>(&count), 4);
  std::lock_guard lock(mu_);
  for (std::uint32_t i = 0; i < count && in; ++i) {
    ChestInv inv;
    in.read(reinterpret_cast<char*>(&inv.x), 4);
    in.read(reinterpret_cast<char*>(&inv.y), 4);
    in.read(reinterpret_cast<char*>(&inv.z), 4);
    std::uint16_t slots = 0;
    in.read(reinterpret_cast<char*>(&slots), 2);
    inv.slots.assign(27, item::ItemStack::air());
    for (std::uint16_t s = 0; s < slots && s < 27; ++s) {
      std::int16_t id = 0;
      std::uint8_t cnt = 0;
      std::int16_t dmg = 0;
      in.read(reinterpret_cast<char*>(&id), 2);
      in.read(reinterpret_cast<char*>(&cnt), 1);
      in.read(reinterpret_cast<char*>(&dmg), 2);
      inv.slots[s] = item::ItemStack::of(id, cnt, dmg);
    }
    if (v2) {
      in.read(reinterpret_cast<char*>(&inv.pair_x), 4);
      in.read(reinterpret_cast<char*>(&inv.pair_z), 4);
    } else {
      inv.pair_x = static_cast<int>(0x80000000);
      inv.pair_z = static_cast<int>(0x80000000);
    }
    inv.dirty = false;
    chests_[blockPosKey(inv.x, inv.y, inv.z)] = std::move(inv);
  }
  util::Logger::instance().info("Loaded ", chests_.size(), " chests for world ", settings_.name,
                                " fmt=", v2 ? "CHS2" : "CHST");
}

Level::FurnaceInv& Level::getOrCreateFurnace(int x, int y, int z) {
  std::lock_guard lock(mu_);
  const auto key = blockPosKey(x, y, z);
  auto it = furnaces_.find(key);
  if (it != furnaces_.end()) return it->second;
  FurnaceInv inv;
  inv.x = x;
  inv.y = y;
  inv.z = z;
  inv.slots.assign(3, item::ItemStack::air());
  inv.dirty = true;
  auto [ins, _] = furnaces_.emplace(key, std::move(inv));
  return ins->second;
}

Level::FurnaceInv* Level::getFurnace(int x, int y, int z) {
  std::lock_guard lock(mu_);
  auto it = furnaces_.find(blockPosKey(x, y, z));
  if (it == furnaces_.end()) return nullptr;
  return &it->second;
}

void Level::removeFurnace(int x, int y, int z) {
  std::lock_guard lock(mu_);
  furnaces_.erase(blockPosKey(x, y, z));
}

void Level::saveFurnaces() {
  if (data_path_.empty()) return;
  std::error_code ec;
  fs::create_directories(data_path_, ec);
  std::ofstream out(furnacePath(), std::ios::binary | std::ios::trunc);
  if (!out) return;
  // FUR2: + burn_time, max_time, cook_time
  out.write("FUR2", 4);
  std::lock_guard lock(mu_);
  std::uint32_t count = static_cast<std::uint32_t>(furnaces_.size());
  out.write(reinterpret_cast<const char*>(&count), 4);
  for (auto& [_, c] : furnaces_) {
    out.write(reinterpret_cast<const char*>(&c.x), 4);
    out.write(reinterpret_cast<const char*>(&c.y), 4);
    out.write(reinterpret_cast<const char*>(&c.z), 4);
    std::uint16_t slots = static_cast<std::uint16_t>(c.slots.size());
    out.write(reinterpret_cast<const char*>(&slots), 2);
    for (const auto& s : c.slots) {
      std::int16_t id = s.id;
      std::uint8_t cnt = s.count;
      std::int16_t dmg = s.damage;
      out.write(reinterpret_cast<const char*>(&id), 2);
      out.write(reinterpret_cast<const char*>(&cnt), 1);
      out.write(reinterpret_cast<const char*>(&dmg), 2);
    }
    out.write(reinterpret_cast<const char*>(&c.burn_time), 2);
    out.write(reinterpret_cast<const char*>(&c.max_time), 2);
    out.write(reinterpret_cast<const char*>(&c.cook_time), 2);
    c.dirty = false;
  }
}

void Level::loadFurnaces() {
  if (data_path_.empty()) return;
  std::ifstream in(furnacePath(), std::ios::binary);
  if (!in) return;
  char magic[4]{};
  in.read(magic, 4);
  const bool v2 = (std::memcmp(magic, "FUR2", 4) == 0);
  if (!v2 && std::memcmp(magic, "FURN", 4) != 0) return;
  std::uint32_t count = 0;
  in.read(reinterpret_cast<char*>(&count), 4);
  std::lock_guard lock(mu_);
  for (std::uint32_t i = 0; i < count && in; ++i) {
    FurnaceInv inv;
    in.read(reinterpret_cast<char*>(&inv.x), 4);
    in.read(reinterpret_cast<char*>(&inv.y), 4);
    in.read(reinterpret_cast<char*>(&inv.z), 4);
    std::uint16_t slots = 0;
    in.read(reinterpret_cast<char*>(&slots), 2);
    inv.slots.assign(3, item::ItemStack::air());
    for (std::uint16_t s = 0; s < slots && s < 3; ++s) {
      std::int16_t id = 0;
      std::uint8_t cnt = 0;
      std::int16_t dmg = 0;
      in.read(reinterpret_cast<char*>(&id), 2);
      in.read(reinterpret_cast<char*>(&cnt), 1);
      in.read(reinterpret_cast<char*>(&dmg), 2);
      inv.slots[s] = item::sanitizeSlot(item::ItemStack::of(id, cnt, dmg));
    }
    if (v2) {
      in.read(reinterpret_cast<char*>(&inv.burn_time), 2);
      in.read(reinterpret_cast<char*>(&inv.max_time), 2);
      in.read(reinterpret_cast<char*>(&inv.cook_time), 2);
      if (inv.burn_time < 0) inv.burn_time = 0;
      if (inv.max_time < 0) inv.max_time = 0;
      if (inv.cook_time < 0) inv.cook_time = 0;
      if (inv.cook_time > 200) inv.cook_time = 200;
    }
    inv.dirty = false;
    furnaces_[blockPosKey(inv.x, inv.y, inv.z)] = std::move(inv);
  }
  util::Logger::instance().info("Loaded ", furnaces_.size(), " furnaces for world ", settings_.name);
}

const Chunk* Level::getChunk(int cx, int cz) const {
  std::lock_guard lock(mu_);
  const auto key = chunkKey(cx, cz);
  auto it = chunks_.find(key);
  if (it == chunks_.end()) return nullptr;
  return &it->second;
}

std::string Level::chunkNetworkPayload(int cx, int cz) {
  // PM ChunkRequestTask: blocks+meta+lights+height+biomes+extraData + LE tile NBT compounds.
  // Without trailing tiles PE 0.14 draws chest as transparent ghost and refuses container UI.
  auto payload = getOrCreateChunk(cx, cz).networkPayload();

  // Prefer known inventories (with pair state); also scan column for any block 54 missing inv.
  std::vector<ChestInv> list = chestsInChunk(cx, cz);
  {
    // ensure every chest block in this chunk has a tile entry
    const int base_x = cx * 16;
    const int base_z = cz * 16;
    for (int lz = 0; lz < 16; ++lz) {
      for (int lx = 0; lx < 16; ++lx) {
        for (int y = 0; y < 128; ++y) {
          const int wx = base_x + lx;
          const int wz = base_z + lz;
          if (getBlockId(wx, y, wz) != 54) continue;
          bool found = false;
          for (const auto& c : list) {
            if (c.x == wx && c.y == y && c.z == wz) {
              found = true;
              break;
            }
          }
          if (!found) {
            // create inv so pair/open work later; tile still needed for render
            auto& inv = getOrCreateChest(wx, y, wz);
            list.push_back(inv);
          }
        }
      }
    }
  }

  for (const auto& chest : list) {
    if (getBlockId(chest.x, chest.y, chest.z) != 54) continue;
    const bool paired =
        chest.isPaired() && getBlockId(chest.pair_x, chest.y, chest.pair_z) == 54;
    payload += protocol::encodeChestSpawnNbt(chest.x, chest.y, chest.z, paired,
                                             paired ? chest.pair_x : 0,
                                             paired ? chest.pair_z : 0);
  }
  return payload;
}

std::uint8_t Level::getBlockId(int x, int y, int z) {
  if (y < 0 || y >= kChunkHeight) return 0;
  const int cx = x >> 4;
  const int cz = z >> 4;
  const int lx = x & 15;
  const int lz = z & 15;
  return getOrCreateChunk(cx, cz).getBlockId(lx, y, lz);
}

std::uint8_t Level::getBlockMeta(int x, int y, int z) {
  if (y < 0 || y >= kChunkHeight) return 0;
  const int cx = x >> 4;
  const int cz = z >> 4;
  return getOrCreateChunk(cx, cz).getBlockMeta(x & 15, y, z & 15);
}

bool Level::setBlock(int x, int y, int z, std::uint8_t id, std::uint8_t meta) {
  if (y < 0 || y >= kChunkHeight) return false;
  // protect bedrock layer
  if (y == 0 && id == 0) return false;
  auto existing = getBlockId(x, y, z);
  if (existing == protocol::BLOCK_BEDROCK && id == 0) return false;
  const int cx = x >> 4;
  const int cz = z >> 4;
  getOrCreateChunk(cx, cz).setBlock(x & 15, y, z & 15, id, meta);
  return true;
}

int Level::highestBlockY(int x, int z) {
  const int cx = x >> 4;
  const int cz = z >> 4;
  auto& chunk = getOrCreateChunk(cx, cz);
  const int lx = x & 15;
  const int lz = z & 15;
  for (int y = kChunkHeight - 1; y >= 0; --y) {
    if (chunk.getBlockId(lx, y, lz) != 0) return y;
  }
  return -1;
}

Level& LevelManager::create(LevelSettings settings) {
  auto name = settings.name;
  auto level = std::make_unique<Level>(std::move(settings));
  auto* ptr = level.get();
  // default data path: worlds/<name>
  ptr->setDataPath(std::string("worlds/") + name);
  std::error_code ec;
  fs::create_directories(ptr->dataPath() + "/chunks", ec);
  ptr->loadChests();
  ptr->loadFurnaces();
  levels_[name] = std::move(level);
  if (!default_) default_ = ptr;
  util::Logger::instance().info("World loaded: ", name, " generator=",
                                static_cast<int>(ptr->generator()),
                                " spawn=", ptr->spawn().x, ",", ptr->spawn().y, ",",
                                ptr->spawn().z, " path=", ptr->dataPath());
  return *ptr;
}

Level* LevelManager::get(std::string_view name) {
  auto it = levels_.find(std::string(name));
  if (it == levels_.end()) return nullptr;
  return it->second.get();
}

Level& LevelManager::getOrCreateDefault(const LevelSettings& defaults) {
  if (default_) return *default_;
  return create(defaults);
}

void LevelManager::tickAll() {
  for (auto& [_, lvl] : levels_) {
    lvl->tickTime();
  }
}

int LevelManager::saveAll(bool force_all) {
  int total = 0;
  for (auto& [name, lvl] : levels_) {
    int n = lvl->saveDirtyChunks(force_all);
    lvl->saveChests();
    lvl->saveFurnaces();
    if (n > 0) {
      util::Logger::instance().info("Saved world ", name, " chunks=", n);
    }
    total += n;
  }
  return total;
}

void LevelManager::loadWorldsFile(std::string_view path, const LevelSettings& defaults) {
  std::ifstream in{std::string(path)};
  if (!in) {
    // create default worlds.txt
    std::ofstream out{std::string(path)};
    if (out) {
      out << "# name:generator:seed\n";
      out << "# generators: flat | void | normal | nether | end\n";
      out << "# End world name should be 'ender' (PM parity)\n";
      out << defaults.name << ":"
          << (defaults.generator == GeneratorType::Flat
                  ? "flat"
                  : (defaults.generator == GeneratorType::Void
                         ? "void"
                         : (defaults.generator == GeneratorType::Nether
                                ? "nether"
                                : (defaults.generator == GeneratorType::End ? "end" : "normal"))))
          << ":" << defaults.seed << "\n";
      out << "nether:nether:1\n";
      out << "ender:end:2\n";
    }
    create(defaults);
    LevelSettings nether = defaults;
    nether.name = "nether";
    nether.generator = GeneratorType::Nether;
    nether.dimension = protocol::DIMENSION_NETHER;
    nether.seed = 1;
    create(nether);
    LevelSettings endw = defaults;
    endw.name = "ender";
    endw.generator = GeneratorType::End;
    endw.dimension = protocol::DIMENSION_OVERWORLD; // PE wire-safe
    endw.seed = 2;
    create(endw);
    return;
  }
  std::string line;
  bool any = false;
  while (std::getline(in, line)) {
    if (line.empty() || line[0] == '#') continue;
    std::istringstream ss(line);
    std::string name, gen, seed_s;
    if (!std::getline(ss, name, ':')) continue;
    if (!std::getline(ss, gen, ':')) gen = "flat";
    if (!std::getline(ss, seed_s, ':')) seed_s = "0";
    LevelSettings s = defaults;
    s.name = name;
    s.generator = parseGenerator(gen);
    try {
      s.seed = std::stoi(seed_s);
    } catch (...) {
      s.seed = 0;
    }
    // dimension from generator or world name
    // PE 0.14: only 0/1 on wire. End worlds (ender/end) stay OVERWORLD dimension id.
    if (s.generator == GeneratorType::Nether || name == "nether")
      s.dimension = protocol::DIMENSION_NETHER;
    else if (s.generator == GeneratorType::End || name == "end" || name == "ender")
      s.dimension = protocol::DIMENSION_OVERWORLD;
    create(s);
    any = true;
  }
  if (!any) create(defaults);
  if (!default_ && !levels_.empty()) default_ = levels_.begin()->second.get();
  // Prefer named default from settings
  if (auto* d = get(defaults.name)) setDefault(*d);
}

} // namespace mpmpes::level
