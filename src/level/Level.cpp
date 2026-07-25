#include "mpmpes/level/Level.hpp"

#include "mpmpes/protocol/Info.hpp"
#include "mpmpes/protocol/Packets.hpp"
#include "mpmpes/util/Logger.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <queue>
#include <sstream>
#include <vector>

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

std::string Level::signPath() const {
  return data_path_ + "/signs.dat";
}

std::string Level::hopperPath() const {
  return data_path_ + "/hoppers.dat";
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
  bool loaded = false;
  if (tryLoadChunk(cx, cz, chunk)) {
    loaded = true;
  } else {
    chunk = generate(cx, cz);
    chunk.markDirty(true); // new terrain — persist on first autosave so edits accumulate
  }
  auto [ins, _] = chunks_.emplace(key, std::move(chunk));
  // Register orphan hopper tiles under the same lock (hoppers.dat may miss blocks that
  // still exist in chunk terrain). Do NOT call getOrCreateHopper here — it re-locks mu_.
  if (loaded) {
    const int base_x = cx * 16;
    const int base_z = cz * 16;
    auto& c = ins->second;
    for (int lz = 0; lz < 16; ++lz) {
      for (int lx = 0; lx < 16; ++lx) {
        for (int y = 0; y < kChunkHeight; ++y) {
          if (c.getBlockId(lx, y, lz) != protocol::BLOCK_HOPPER) continue;
          const int wx = base_x + lx;
          const int wz = base_z + lz;
          const auto hkey = blockPosKey(wx, y, wz);
          if (hoppers_.find(hkey) != hoppers_.end()) continue;
          HopperInv inv;
          inv.x = wx;
          inv.y = y;
          inv.z = wz;
          inv.slots.assign(5, item::ItemStack::air());
          inv.cooldown = 0;
          inv.dirty = true;
          hoppers_.emplace(hkey, std::move(inv));
        }
      }
    }
  }
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

Level::HopperInv& Level::getOrCreateHopper(int x, int y, int z) {
  std::lock_guard lock(mu_);
  const auto key = blockPosKey(x, y, z);
  auto it = hoppers_.find(key);
  if (it != hoppers_.end()) return it->second;
  HopperInv inv;
  inv.x = x;
  inv.y = y;
  inv.z = z;
  inv.slots.assign(5, item::ItemStack::air());
  inv.cooldown = 0;
  inv.dirty = true;
  auto [ins, _] = hoppers_.emplace(key, std::move(inv));
  return ins->second;
}

Level::HopperInv* Level::getHopper(int x, int y, int z) {
  std::lock_guard lock(mu_);
  auto it = hoppers_.find(blockPosKey(x, y, z));
  if (it == hoppers_.end()) return nullptr;
  return &it->second;
}

void Level::removeHopper(int x, int y, int z) {
  std::lock_guard lock(mu_);
  hoppers_.erase(blockPosKey(x, y, z));
}

std::vector<Level::HopperInv> Level::hoppersInChunk(int cx, int cz) const {
  std::lock_guard lock(mu_);
  std::vector<HopperInv> out;
  const int min_x = cx * 16;
  const int max_x = min_x + 15;
  const int min_z = cz * 16;
  const int max_z = min_z + 15;
  for (const auto& [_, h] : hoppers_) {
    if (h.x >= min_x && h.x <= max_x && h.z >= min_z && h.z <= max_z) out.push_back(h);
  }
  return out;
}

void Level::saveHoppers() {
  if (data_path_.empty()) return;
  std::error_code ec;
  fs::create_directories(data_path_, ec);
  std::ofstream out(hopperPath(), std::ios::binary | std::ios::trunc);
  if (!out) return;
  // HOP1: xyz + 5 slots
  out.write("HOP1", 4);
  std::lock_guard lock(mu_);
  std::uint32_t count = static_cast<std::uint32_t>(hoppers_.size());
  out.write(reinterpret_cast<const char*>(&count), 4);
  for (auto& [_, c] : hoppers_) {
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
    out.write(reinterpret_cast<const char*>(&c.cooldown), 4);
    c.dirty = false;
  }
}

void Level::loadHoppers() {
  if (data_path_.empty()) return;
  std::ifstream in(hopperPath(), std::ios::binary);
  if (!in) return;
  char magic[4]{};
  in.read(magic, 4);
  if (std::memcmp(magic, "HOP1", 4) != 0) return;
  std::uint32_t count = 0;
  in.read(reinterpret_cast<char*>(&count), 4);
  std::lock_guard lock(mu_);
  for (std::uint32_t i = 0; i < count && in; ++i) {
    HopperInv inv;
    in.read(reinterpret_cast<char*>(&inv.x), 4);
    in.read(reinterpret_cast<char*>(&inv.y), 4);
    in.read(reinterpret_cast<char*>(&inv.z), 4);
    std::uint16_t slots = 0;
    in.read(reinterpret_cast<char*>(&slots), 2);
    inv.slots.assign(5, item::ItemStack::air());
    for (std::uint16_t s = 0; s < slots && s < 5; ++s) {
      std::int16_t id = 0;
      std::uint8_t cnt = 0;
      std::int16_t dmg = 0;
      in.read(reinterpret_cast<char*>(&id), 2);
      in.read(reinterpret_cast<char*>(&cnt), 1);
      in.read(reinterpret_cast<char*>(&dmg), 2);
      inv.slots[s] = item::sanitizeSlot(item::ItemStack::of(id, cnt, dmg));
    }
    in.read(reinterpret_cast<char*>(&inv.cooldown), 4);
    if (inv.cooldown < 0) inv.cooldown = 0;
    inv.dirty = false;
    hoppers_[blockPosKey(inv.x, inv.y, inv.z)] = std::move(inv);
  }
  util::Logger::instance().info("Loaded ", hoppers_.size(), " hoppers for world ", settings_.name);
}

Level::SignTile& Level::getOrCreateSign(int x, int y, int z) {
  std::lock_guard lock(mu_);
  const auto key = blockPosKey(x, y, z);
  auto it = signs_.find(key);
  if (it != signs_.end()) return it->second;
  SignTile t;
  t.x = x;
  t.y = y;
  t.z = z;
  t.dirty = true;
  auto [ins, _] = signs_.emplace(key, std::move(t));
  return ins->second;
}

Level::SignTile* Level::getSign(int x, int y, int z) {
  std::lock_guard lock(mu_);
  auto it = signs_.find(blockPosKey(x, y, z));
  if (it == signs_.end()) return nullptr;
  return &it->second;
}

void Level::removeSign(int x, int y, int z) {
  std::lock_guard lock(mu_);
  signs_.erase(blockPosKey(x, y, z));
}

std::vector<Level::SignTile> Level::signsInChunk(int cx, int cz) const {
  std::lock_guard lock(mu_);
  std::vector<SignTile> out;
  const int min_x = cx * 16;
  const int max_x = min_x + 15;
  const int min_z = cz * 16;
  const int max_z = min_z + 15;
  for (const auto& [_, s] : signs_) {
    if (s.x >= min_x && s.x <= max_x && s.z >= min_z && s.z <= max_z) out.push_back(s);
  }
  return out;
}

void Level::saveSigns() {
  if (data_path_.empty()) return;
  std::error_code ec;
  fs::create_directories(data_path_, ec);
  std::ofstream out(signPath(), std::ios::binary | std::ios::trunc);
  if (!out) return;
  // SGN1: count + per sign: xyz + 4 text strings (u16le len + bytes) + creator string
  out.write("SGN1", 4);
  std::lock_guard lock(mu_);
  std::uint32_t count = static_cast<std::uint32_t>(signs_.size());
  out.write(reinterpret_cast<const char*>(&count), 4);
  auto writeStr = [&](const std::string& s) {
    std::uint16_t n = static_cast<std::uint16_t>(std::min<std::size_t>(s.size(), 65535));
    out.write(reinterpret_cast<const char*>(&n), 2);
    if (n) out.write(s.data(), n);
  };
  for (auto& [_, s] : signs_) {
    out.write(reinterpret_cast<const char*>(&s.x), 4);
    out.write(reinterpret_cast<const char*>(&s.y), 4);
    out.write(reinterpret_cast<const char*>(&s.z), 4);
    writeStr(s.text1);
    writeStr(s.text2);
    writeStr(s.text3);
    writeStr(s.text4);
    writeStr(s.creator);
    s.dirty = false;
  }
}

void Level::loadSigns() {
  if (data_path_.empty()) return;
  std::ifstream in(signPath(), std::ios::binary);
  if (!in) return;
  char magic[4]{};
  in.read(magic, 4);
  if (std::memcmp(magic, "SGN1", 4) != 0) return;
  std::uint32_t count = 0;
  in.read(reinterpret_cast<char*>(&count), 4);
  auto readStr = [&]() -> std::string {
    std::uint16_t n = 0;
    in.read(reinterpret_cast<char*>(&n), 2);
    if (!in || n == 0) return {};
    std::string s(n, '\0');
    in.read(s.data(), n);
    return s;
  };
  std::lock_guard lock(mu_);
  for (std::uint32_t i = 0; i < count && in; ++i) {
    SignTile t;
    in.read(reinterpret_cast<char*>(&t.x), 4);
    in.read(reinterpret_cast<char*>(&t.y), 4);
    in.read(reinterpret_cast<char*>(&t.z), 4);
    t.text1 = readStr();
    t.text2 = readStr();
    t.text3 = readStr();
    t.text4 = readStr();
    t.creator = readStr();
    t.dirty = false;
    signs_[blockPosKey(t.x, t.y, t.z)] = std::move(t);
  }
  util::Logger::instance().info("Loaded ", signs_.size(), " signs for world ", settings_.name);
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

  // Hopper tiles in this chunk (PE 0.14 needs trailing Hopper NBT or client treats as ghost)
  std::vector<HopperInv> hop_list = hoppersInChunk(cx, cz);
  {
    const int base_x = cx * 16;
    const int base_z = cz * 16;
    for (int lz = 0; lz < 16; ++lz) {
      for (int lx = 0; lx < 16; ++lx) {
        for (int y = 0; y < 128; ++y) {
          const int wx = base_x + lx;
          const int wz = base_z + lz;
          if (getBlockId(wx, y, wz) != protocol::BLOCK_HOPPER) continue;
          bool found = false;
          for (const auto& h : hop_list) {
            if (h.x == wx && h.y == y && h.z == wz) {
              found = true;
              break;
            }
          }
          if (!found) {
            auto& inv = getOrCreateHopper(wx, y, wz);
            hop_list.push_back(inv);
          }
        }
      }
    }
  }
  for (const auto& hop : hop_list) {
    if (getBlockId(hop.x, hop.y, hop.z) != protocol::BLOCK_HOPPER) continue;
    payload += protocol::encodeHopperSpawnNbt(hop.x, hop.y, hop.z);
  }

  // Sign tiles in this chunk (PM FullChunk trailing tile NBT)
  for (const auto& sign : signsInChunk(cx, cz)) {
    const auto bid = getBlockId(sign.x, sign.y, sign.z);
    if (bid != protocol::BLOCK_SIGN_POST && bid != protocol::BLOCK_WALL_SIGN) continue;
    payload += protocol::encodeSignSpawnNbt(sign.x, sign.y, sign.z, sign.text1, sign.text2,
                                            sign.text3, sign.text4);
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

namespace {

// Non-solid / unsafe surface tops for spawn (fluids, leaves, plants, fire, portal...)
bool isNonSolidBlock(std::uint8_t id) {
  if (id == 0) return true;
  // fluids (flowing + still)
  if (id == protocol::BLOCK_WATER || id == 9) return true;
  if (id == protocol::BLOCK_LAVA || id == 11) return true;
  if (id == protocol::BLOCK_LEAVES) return true;
  // tall grass / flowers / sapling / fire / snow layer / portal-ish / vines
  if (id == 6 || id == 31 || id == 37 || id == 38 || id == 39 || id == 40) return true;
  if (id == 51 || id == 78 || id == 90 || id == 106 || id == 111 || id == 175) return true;
  return false;
}

bool isFluidBlock(std::uint8_t id) {
  return id == protocol::BLOCK_WATER || id == 9 || id == protocol::BLOCK_LAVA || id == 11;
}

// True open air cell (not fluid / not "non-solid plant" clutter for clearance).
bool isClearAir(std::uint8_t id) {
  return id == 0 || id == protocol::BLOCK_LEAVES || id == 6 || id == 31 || id == 37 || id == 38 ||
         id == 39 || id == 40 || id == 78 || id == 106 || id == 111 || id == 175;
}

// Surface Y (block under feet) suitable for player spawn, or -1.
// Rules:
// - only floors in [y_lo, y_hi]
// - ignore void columns (no solid in band)
// - require open_air continuous clear cells above the surface (not just 2 headroom)
// - never stand on bedrock / under a ceiling ledge (top-down 2-air trap)
int surfaceYForSpawn(Level& lvl, int x, int z, int y_lo, int y_hi, int open_air) {
  if (open_air < 3) open_air = 3;
  const int y_min = std::max(1, y_lo);
  const int y_max = std::min({y_hi, kChunkHeight - 1 - open_air});
  if (y_max < y_min) return -1;

  // Scan top-down inside the band only (ignore void below y_lo and roof above y_hi).
  for (int y = y_max; y >= y_min; --y) {
    const auto id = lvl.getBlockId(x, y, z);
    if (isNonSolidBlock(id) || isFluidBlock(id)) continue;
    if (id == protocol::BLOCK_BEDROCK) continue;

    // Feet/head + open sky: y+1 .. y+open_air must be clear (no solid ceiling ledge).
    bool open = true;
    for (int dy = 1; dy <= open_air; ++dy) {
      const auto a = lvl.getBlockId(x, y + dy, z);
      if (!isClearAir(a) || isFluidBlock(a)) {
        open = false;
        break;
      }
    }
    if (!open) continue;

    // Prefer real ground tops: block below should be solid/support, or y is band floor.
    // (Skips single floating blobs under ceiling when possible.)
    if (y > y_min) {
      const auto below = lvl.getBlockId(x, y - 1, z);
      if (isNonSolidBlock(below) || isFluidBlock(below)) {
        // Allow thin end-island tops only if open_air is already large.
        // Still accept: floating end platforms are legitimate.
      }
    }
    return y;
  }
  return -1;
}

std::uint8_t platformBlockFor(GeneratorType gen) {
  switch (gen) {
    case GeneratorType::Nether:
      return protocol::BLOCK_NETHERRACK;
    case GeneratorType::End:
      return protocol::BLOCK_END_STONE;
    default:
      return protocol::BLOCK_GRASS;
  }
}

Vec3i carveSpawnPlatform(Level& lvl, GeneratorType gen, int feet_y_hint) {
  const int px = 0;
  const int pz = 0;
  int py = feet_y_hint - 1;
  if (gen == GeneratorType::Nether) py = 72;
  else if (gen == GeneratorType::End) py = 64;
  else if (gen == GeneratorType::Flat) py = 3;
  else if (gen == GeneratorType::NormalStub) py = 68;
  if (py < 1) py = 64;
  if (py > kChunkHeight - 12) py = kChunkHeight - 12;
  const auto floor_id = platformBlockFor(gen);
  for (int dz = -2; dz <= 2; ++dz) {
    for (int dx = -2; dx <= 2; ++dx) {
      lvl.setBlock(px + dx, py, pz + dz, floor_id, 0);
      // Clear a tall open column so nether ceiling cannot trap the player.
      for (int dy = 1; dy <= 10; ++dy) {
        lvl.setBlock(px + dx, py + dy, pz + dz, 0, 0);
      }
    }
  }
  return {px, py + 1, pz};
}

} // namespace

int Level::safeStandFeetY(int x, int z) {
  int y_lo = 2;
  int y_hi = 100;
  int open_air = 8;
  switch (settings_.generator) {
    case GeneratorType::Flat:
      y_lo = 1;
      y_hi = 20;
      open_air = 4;
      break;
    case GeneratorType::Nether:
      y_lo = 34; // above lava sea
      y_hi = 90; // well below bedrock / hanging ceiling
      open_air = 12;
      break;
    case GeneratorType::End:
      y_lo = 40; // ignore void layer under floating islands
      y_hi = 90;
      open_air = 10;
      break;
    case GeneratorType::Void:
      y_lo = 1;
      y_hi = kChunkHeight - 3;
      open_air = 4;
      break;
    default: // normal hills
      y_lo = 40;
      y_hi = 110;
      open_air = 8;
      break;
  }
  const int sy = surfaceYForSpawn(*this, x, z, y_lo, y_hi, open_air);
  if (sy < 1) return -1;
  return sy + 1; // feet on top of surface block
}

Vec3i Level::findAndSetAutoSpawn(int chunk_radius, int max_dist) {
  // Default spawn remains whatever constructor set, until we find something better.
  Vec3i best = settings_.spawn;
  if (chunk_radius < 0) chunk_radius = 0;
  if (max_dist < 1) max_dist = 1;

  // Comfortable surface-Y band (avoids nether ceiling / void roof).
  // open_air: continuous clear blocks above surface so the plane is "open sky", not under ceiling.
  int y_lo = 2;
  int y_hi = 100;
  int open_air = 8;
  switch (settings_.generator) {
    case GeneratorType::Flat:
      y_lo = 1;
      y_hi = 20;
      open_air = 4;
      break;
    case GeneratorType::Nether:
      y_lo = 34; // above lava sea; ignore void/lava shell below
      y_hi = 90; // never accept ceiling underside (~103-127)
      open_air = 12;
      break;
    case GeneratorType::End:
      y_lo = 40; // ignore void under floating islands
      y_hi = 90;
      open_air = 10;
      break;
    case GeneratorType::Void:
      y_lo = 1;
      y_hi = kChunkHeight - 3;
      open_air = 4;
      break;
    default: // normal
      y_lo = 40;
      y_hi = 110;
      open_air = 8;
      break;
  }

  // Symmetric scan around (0,0):
  // - load (2*r+1)^2 chunks (default 3x3)
  // - only columns with |x|,|z| <= lim and x^2+z^2 <= max_dist^2
  // lim keeps the box centered so superflat centroid lands on origin.
  const int chunk_extent = chunk_radius * 16 + 15; // e.g. r=1 → 31
  const int lim = std::min(max_dist, chunk_extent);
  const int min_x = -lim;
  const int max_x = lim;
  const int min_z = -lim;
  const int max_z = lim;
  const int w = max_x - min_x + 1;
  const int h = max_z - min_z + 1;
  if (w <= 0 || h <= 0) return best;

  // Preload chunks covering the symmetric AABB (may be slightly >3x3 when lim=31: cx -2..1).
  const int min_cx = min_x >> 4;
  const int max_cx = max_x >> 4;
  const int min_cz = min_z >> 4;
  const int max_cz = max_z >> 4;
  for (int cz = min_cz; cz <= max_cz; ++cz) {
    for (int cx = min_cx; cx <= max_cx; ++cx) {
      (void)getOrCreateChunk(cx, cz);
    }
  }

  std::vector<int> surf(static_cast<std::size_t>(w * h), -1);
  auto idx = [w](int lx, int lz) { return lz * w + lx; };
  const int max_dist2 = max_dist * max_dist;

  int standable = 0;
  for (int z = min_z; z <= max_z; ++z) {
    for (int x = min_x; x <= max_x; ++x) {
      if (x * x + z * z > max_dist2) continue;
      const int sy = surfaceYForSpawn(*this, x, z, y_lo, y_hi, open_air);
      if (sy < 1) continue;
      surf[static_cast<std::size_t>(idx(x - min_x, z - min_z))] = sy;
      ++standable;
    }
  }

  // 4-connected flood fill: same surface Y = same flat plane.
  std::vector<std::uint8_t> seen(static_cast<std::size_t>(w * h), 0);
  int best_area = 0;
  long long best_center_dist2 = 0x7fffffffffffffffLL;
  int best_cx = best.x;
  int best_cz = best.z;
  int best_sy = best.y > 0 ? best.y - 1 : 4;

  for (int lz = 0; lz < h; ++lz) {
    for (int lx = 0; lx < w; ++lx) {
      const int i0 = idx(lx, lz);
      if (seen[static_cast<std::size_t>(i0)]) continue;
      const int y0 = surf[static_cast<std::size_t>(i0)];
      if (y0 < 1) {
        seen[static_cast<std::size_t>(i0)] = 1;
        continue;
      }

      int area = 0;
      long long sum_x = 0;
      long long sum_z = 0;
      int min_bx = lx, max_bx = lx, min_bz = lz, max_bz = lz;
      bool has_origin = false;
      std::queue<std::pair<int, int>> q;
      q.push({lx, lz});
      seen[static_cast<std::size_t>(i0)] = 1;

      while (!q.empty()) {
        const auto [cx, cz] = q.front();
        q.pop();
        const int ii = idx(cx, cz);
        if (surf[static_cast<std::size_t>(ii)] != y0) continue;
        ++area;
        const int wx = min_x + cx;
        const int wz = min_z + cz;
        sum_x += wx;
        sum_z += wz;
        if (wx == 0 && wz == 0) has_origin = true;
        if (cx < min_bx) min_bx = cx;
        if (cx > max_bx) max_bx = cx;
        if (cz < min_bz) min_bz = cz;
        if (cz > max_bz) max_bz = cz;

        static const int ox[4] = {1, -1, 0, 0};
        static const int oz[4] = {0, 0, 1, -1};
        for (int d = 0; d < 4; ++d) {
          const int nx = cx + ox[d];
          const int nz = cz + oz[d];
          if (nx < 0 || nz < 0 || nx >= w || nz >= h) continue;
          const int ni = idx(nx, nz);
          if (seen[static_cast<std::size_t>(ni)]) continue;
          if (surf[static_cast<std::size_t>(ni)] != y0) continue;
          seen[static_cast<std::size_t>(ni)] = 1;
          q.push({nx, nz});
        }
      }

      if (area <= 0) continue;

      // Plane center = centroid of member columns; snap to a real plane cell.
      // If (0,0) is on this plane, use origin (still "on the plane", best default).
      int use_x = 0;
      int use_z = 0;
      if (has_origin) {
        use_x = 0;
        use_z = 0;
      } else {
        const int mid_x = static_cast<int>(sum_x / area);
        const int mid_z = static_cast<int>(sum_z / area);
        use_x = mid_x;
        use_z = mid_z;
        const int clx = mid_x - min_x;
        const int clz = mid_z - min_z;
        bool ok = clx >= 0 && clz >= 0 && clx < w && clz < h &&
                  surf[static_cast<std::size_t>(idx(clx, clz))] == y0;
        if (!ok) {
          int nearest_d2 = 0x7fffffff;
          ok = false;
          for (int zz = min_bz; zz <= max_bz; ++zz) {
            for (int xx = min_bx; xx <= max_bx; ++xx) {
              if (surf[static_cast<std::size_t>(idx(xx, zz))] != y0) continue;
              const int wx = min_x + xx;
              const int wz = min_z + zz;
              const int ddx = wx - mid_x;
              const int ddz = wz - mid_z;
              const int d2 = ddx * ddx + ddz * ddz;
              if (d2 < nearest_d2) {
                nearest_d2 = d2;
                use_x = wx;
                use_z = wz;
                ok = true;
              }
            }
          }
        }
        if (!ok) continue;
      }

      const long long center_dist2 =
          static_cast<long long>(use_x) * use_x + static_cast<long long>(use_z) * use_z;
      // Largest area wins; tie-break: spawn closer to (0,0).
      if (area > best_area || (area == best_area && center_dist2 < best_center_dist2)) {
        best_area = area;
        best_center_dist2 = center_dist2;
        best_cx = use_x;
        best_cz = use_z;
        best_sy = y0;
      }
    }
  }

  // Need a real platform (nether lava seas often have only 1-cell "planes").
  constexpr int kMinPlaneArea = 9;
  if (best_area >= kMinPlaneArea) {
    best.x = best_cx;
    best.y = best_sy + 1; // feet on top of surface block
    best.z = best_cz;
    settings_.spawn = best;
    util::Logger::instance().info(
        "Auto-spawn ", settings_.name, ": plane_area=", best_area, " at ", best.x, ",",
        best.y, ",", best.z, " (symmetric lim=", lim, ", max_dist=", max_dist,
        ", chunks ", (max_cx - min_cx + 1), "x", (max_cz - min_cz + 1), ")");
    return best;
  }

  // Fallback: 5x5 safe platform at origin.
  best = carveSpawnPlatform(*this, settings_.generator, settings_.spawn.y);
  settings_.spawn = best;
  util::Logger::instance().warning(
      "Auto-spawn ", settings_.name, ": no usable plane (best_area=", best_area,
      ", standable=", standable, "); built 5x5 platform at ", best.x, ",", best.y, ",",
      best.z);
  return best;
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
  ptr->loadHoppers();
  ptr->loadSigns();
  // Auto spawn: largest flat plane in 3x3 chunks around 0,0 (columns within 100 of origin).
  // Flat worlds still scan (fast); result ~0,5,0. Normal/nether/end get a real platform.
  try {
    ptr->findAndSetAutoSpawn(/*chunk_radius=*/1, /*max_dist=*/100);
  } catch (...) {
    util::Logger::instance().warning("Auto-spawn failed for ", name, "; using default spawn");
  }
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
    lvl->saveHoppers();
    lvl->saveSigns();
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
