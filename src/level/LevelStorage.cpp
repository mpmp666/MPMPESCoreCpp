// McRegion-compatible persistence for Level (PocketMine level/format/mcregion).
// Chunks live in worlds/<name>/region/r.X.Z.mcr, tiles inside each chunk's
// TileEntities list, world metadata in level.dat (gzip BE NBT).
#include "mpmpes/level/Level.hpp"
#include "mpmpes/level/Region.hpp"
#include "mpmpes/nbt/Tag.hpp"
#include "mpmpes/util/Logger.hpp"

#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <set>
#include <sstream>

namespace fs = std::filesystem;

namespace mpmpes::level {

using nbt::Tag;
using nbt::TagPtr;

TagPtr itemToNbt(const item::ItemStack& s, int slot) {
  auto t = Tag::Compound();
  t->set("id", Tag::Short(s.id));
  t->set("Count", Tag::Byte(static_cast<std::int8_t>(s.count)));
  t->set("Damage", Tag::Short(s.damage));
  if (slot >= 0) t->set("Slot", Tag::Byte(static_cast<std::int8_t>(slot)));
  if (!s.nbt.empty()) {
    if (auto tag = nbt::read(s.nbt, nbt::Endian::Little); tag && tag->type == nbt::TAG_Compound) t->set("tag", tag);
  }
  return t;
}

item::ItemStack itemFromNbt(const Tag& t) {
  if (!t.has("id") || !t.has("Count")) return item::ItemStack::air();
  item::ItemStack s;
  s.id = static_cast<std::int16_t>(t.getInt("id"));
  s.count = static_cast<std::uint8_t>(std::clamp<std::int64_t>(t.getInt("Count"), 0, 255));
  s.damage = static_cast<std::int16_t>(t.getInt("Damage"));
  if (auto tag = t.get("tag"); tag && tag->type == nbt::TAG_Compound && !tag->comp.empty())
    s.nbt = nbt::write(*tag, "", nbt::Endian::Little);
  return s;
}

static TagPtr itemsList(const std::vector<item::ItemStack>& slots) {
  auto l = Tag::List(nbt::TAG_Compound);
  for (std::size_t i = 0; i < slots.size(); ++i) {
    if (slots[i].empty()) continue;
    l->list.push_back(itemToNbt(slots[i], static_cast<int>(i)));
  }
  return l;
}

static void readItems(const Tag& tile, std::vector<item::ItemStack>& slots) {
  auto items = tile.get("Items");
  if (!items || items->type != nbt::TAG_List) return;
  for (auto& it : items->list) {
    if (!it || it->type != nbt::TAG_Compound) continue;
    auto slot = it->getInt("Slot", -1);
    if (slot < 0 || slot >= static_cast<std::int64_t>(slots.size())) continue;
    slots[static_cast<std::size_t>(slot)] = item::sanitizeSlot(itemFromNbt(*it));
  }
}

static TagPtr tileBase(const char* id, int x, int y, int z) {
  auto t = Tag::Compound();
  t->set("id", Tag::String(id));
  t->set("x", Tag::Int(x));
  t->set("y", Tag::Int(y));
  t->set("z", Tag::Int(z));
  return t;
}

RegionCache& Level::region() {
  if (!region_) region_ = std::make_unique<RegionCache>(data_path_ + "/region", "mcr");
  return *region_;
}

bool Level::tryLoadChunk(int cx, int cz, Chunk& out) {
  // Caller holds mu_.
  if (data_path_.empty()) return false;
  auto raw = region().readChunk(cx, cz);
  if (raw) {
    auto root = nbt::read(*raw, nbt::Endian::Big);
    TagPtr lv = root ? root->get("Level") : nullptr;
    if (lv && lv->type == nbt::TAG_Compound && Chunk::fromNbt(*lv, out)) {
      const auto ck = chunkKey(cx, cz);
      raw_tiles_.erase(ck);
      raw_entities_.erase(ck);
      if (auto tiles = lv->get("TileEntities"); tiles && tiles->type == nbt::TAG_List) {
        for (auto& t : tiles->list) {
          if (!t || t->type != nbt::TAG_Compound) continue;
          loadTileNbt(t, ck);
        }
      }
      if (auto ents = lv->get("Entities"); ents && ents->type == nbt::TAG_List) {
        for (auto& e : ents->list)
          if (e && e->type == nbt::TAG_Compound) {
            raw_entities_[ck].push_back(e);
            pending_entities_.emplace_back(ck, e);
          }
      }
      if (!out.generated()) return false; // empty placeholder: regenerate
      return true;
    }
    util::Logger::instance().warning("Corrupted chunk ", cx, ",", cz, " in ", name(), "; regenerating");
  }
  // Legacy MPMPESCoreCpp per-chunk files (pre-0.6); migrated on next save.
  if (Chunk::loadFromFile(chunkPath(cx, cz), out)) {
    out.markDirty(true);
    return true;
  }
  return false;
}

void Level::loadTileNbt(const TagPtr& t, std::int64_t ck) {
  const std::string id = t->getString("id");
  const int x = static_cast<int>(t->getInt("x"));
  const int y = static_cast<int>(t->getInt("y"));
  const int z = static_cast<int>(t->getInt("z"));
  const auto key = blockPosKey(x, y, z);
  if (id == "Chest") {
    ChestInv inv;
    inv.x = x; inv.y = y; inv.z = z;
    inv.slots.assign(27, item::ItemStack::air());
    readItems(*t, inv.slots);
    if (t->has("pairx") && t->has("pairz")) {
      inv.pair_x = static_cast<int>(t->getInt("pairx"));
      inv.pair_z = static_cast<int>(t->getInt("pairz"));
    }
    inv.custom_name = t->getString("CustomName");
    chests_[key] = std::move(inv);
  } else if (id == "Furnace") {
    FurnaceInv inv;
    inv.x = x; inv.y = y; inv.z = z;
    inv.slots.assign(3, item::ItemStack::air());
    readItems(*t, inv.slots);
    inv.burn_time = static_cast<std::int16_t>(std::max<std::int64_t>(0, t->getInt("BurnTime")));
    inv.cook_time = static_cast<std::int16_t>(std::clamp<std::int64_t>(t->getInt("CookTime"), 0, 200));
    inv.max_time = static_cast<std::int16_t>(std::max<std::int64_t>(0, t->getInt("MaxTime")));
    furnaces_[key] = std::move(inv);
  } else if (id == "Hopper") {
    HopperInv inv;
    inv.x = x; inv.y = y; inv.z = z;
    inv.slots.assign(5, item::ItemStack::air());
    readItems(*t, inv.slots);
    inv.cooldown = static_cast<int>(std::max<std::int64_t>(0, t->getInt("TransferCooldown")));
    hoppers_[key] = std::move(inv);
  } else if (id == "Sign") {
    SignTile s;
    s.x = x; s.y = y; s.z = z;
    s.text1 = t->getString("Text1");
    s.text2 = t->getString("Text2");
    s.text3 = t->getString("Text3");
    s.text4 = t->getString("Text4");
    s.creator = t->getString("Creator");
    signs_[key] = std::move(s);
  } else {
    // Preserve any tile we do not model yet (FlowerPot, Skull, ItemFrame, ...).
    raw_tiles_[ck].push_back(t);
  }
}

TagPtr Level::buildChunkNbtLocked(const Chunk& c) const {
  auto lv = c.toNbt();
  const int min_x = c.x() * 16, min_z = c.z() * 16;
  auto inChunk = [&](int x, int z) { return x >= min_x && x < min_x + 16 && z >= min_z && z < min_z + 16; };
  auto tiles = Tag::List(nbt::TAG_Compound);
  for (const auto& [_, ch] : chests_) {
    if (!inChunk(ch.x, ch.z)) continue;
    auto t = tileBase("Chest", ch.x, ch.y, ch.z);
    t->set("Items", itemsList(ch.slots));
    if (ch.isPaired()) {
      t->set("pairx", Tag::Int(ch.pair_x));
      t->set("pairz", Tag::Int(ch.pair_z));
    }
    if (!ch.custom_name.empty()) t->set("CustomName", Tag::String(ch.custom_name));
    tiles->list.push_back(t);
  }
  for (const auto& [_, f] : furnaces_) {
    if (!inChunk(f.x, f.z)) continue;
    auto t = tileBase("Furnace", f.x, f.y, f.z);
    t->set("Items", itemsList(f.slots));
    t->set("BurnTime", Tag::Short(f.burn_time));
    t->set("CookTime", Tag::Short(f.cook_time));
    t->set("MaxTime", Tag::Short(f.max_time));
    int ticks = f.max_time > 0 ? static_cast<int>(std::ceil(f.burn_time * 200.0 / f.max_time)) : 0;
    t->set("BurnTicks", Tag::Short(static_cast<std::int16_t>(ticks)));
    tiles->list.push_back(t);
  }
  for (const auto& [_, h] : hoppers_) {
    if (!inChunk(h.x, h.z)) continue;
    auto t = tileBase("Hopper", h.x, h.y, h.z);
    t->set("Items", itemsList(h.slots));
    t->set("TransferCooldown", Tag::Int(h.cooldown));
    tiles->list.push_back(t);
  }
  for (const auto& [_, s] : signs_) {
    if (!inChunk(s.x, s.z)) continue;
    auto t = tileBase("Sign", s.x, s.y, s.z);
    t->set("Text1", Tag::String(s.text1));
    t->set("Text2", Tag::String(s.text2));
    t->set("Text3", Tag::String(s.text3));
    t->set("Text4", Tag::String(s.text4));
    if (!s.creator.empty()) t->set("Creator", Tag::String(s.creator));
    tiles->list.push_back(t);
  }
  const auto ck = chunkKey(c.x(), c.z());
  if (auto it = raw_tiles_.find(ck); it != raw_tiles_.end())
    for (auto& t : it->second) tiles->list.push_back(t);
  lv->set("TileEntities", tiles);
  auto ents = Tag::List(nbt::TAG_Compound);
  if (entity_saver_)
    for (auto& e : entity_saver_(c.x(), c.z()))
      if (e) ents->list.push_back(e);
  // entities loaded from disk but not yet adopted stay in raw form
  if (auto it = raw_entities_.find(ck); it != raw_entities_.end())
    for (auto& e : it->second) ents->list.push_back(e);
  lv->set("Entities", ents);
  lv->set("TileTicks", Tag::List(nbt::TAG_Compound));
  return lv;
}

int Level::saveDirtyChunks(bool force_all) {
  if (data_path_.empty()) return 0;
  std::error_code ec;
  fs::create_directories(data_path_ + "/region", ec);
  // Chunks owning dirty tiles must be rewritten (load them if migrated from legacy files)
  std::set<std::int64_t> tile_dirty;
  {
    std::lock_guard lock(mu_);
    auto mark = [&](int x, int z, bool& d) {
      if (d) tile_dirty.insert(chunkKey(x >> 4, z >> 4));
      d = false;
    };
    for (auto& [_, t] : chests_) mark(t.x, t.z, t.dirty);
    for (auto& [_, t] : furnaces_) mark(t.x, t.z, t.dirty);
    for (auto& [_, t] : hoppers_) mark(t.x, t.z, t.dirty);
    for (auto& [_, t] : signs_) mark(t.x, t.z, t.dirty);
  }
  for (auto ck : tile_dirty) {
    int cx = static_cast<int>(ck >> 32), cz = static_cast<int>(static_cast<std::int32_t>(ck & 0xffffffff));
    getOrCreateChunk(cx, cz);
  }
  int n = 0;
  std::lock_guard lock(mu_);
  for (auto& [key, c] : chunks_) {
    if (!force_all && !c.dirty() && !tile_dirty.count(key) && !entity_saver_) continue;
    if (!force_all && !c.dirty() && !tile_dirty.count(key) && entity_saver_ && !entities_dirty_) continue;
    auto lv = buildChunkNbtLocked(c);
    auto root = Tag::Compound();
    root->set("Level", lv);
    if (region().writeChunk(c.x(), c.z(), nbt::write(*root, "", nbt::Endian::Big), 7)) {
      c.clearDirty();
      ++n;
    }
  }
  entities_dirty_ = false;
  region().flushAll();
  saveLevelDataLocked();
  return n;
}

std::string generatorNameFor(GeneratorType g) {
  switch (g) {
    case GeneratorType::Flat: return "flat";
    case GeneratorType::Void: return "void";
    case GeneratorType::NormalStub: return "normal";
    case GeneratorType::Nether: return "nether";
    case GeneratorType::End: return "ender";
    case GeneratorType::Astral: return "astral";
  }
  return "normal";
}

bool Level::loadLevelData() {
  std::ifstream in(data_path_ + "/level.dat", std::ios::binary);
  if (!in) return false;
  std::stringstream ss;
  ss << in.rdbuf();
  auto root = nbt::readCompressed(ss.str(), nbt::Endian::Big);
  if (!root) return false;
  auto data = root->get("Data");
  if (!data || data->type != nbt::TAG_Compound) return false;
  level_data_ = data;
  settings_.seed = static_cast<std::int32_t>(data->getInt("RandomSeed", settings_.seed));
  settings_.spawn = {static_cast<int>(data->getInt("SpawnX", settings_.spawn.x)),
                     static_cast<int>(data->getInt("SpawnY", settings_.spawn.y)),
                     static_cast<int>(data->getInt("SpawnZ", settings_.spawn.z))};
  time_ = static_cast<std::int32_t>(data->getInt("Time", 0) % 24000);
  auto gen = data->getString("generatorName");
  if (!gen.empty()) {
    settings_.generator = parseGenerator(gen);
    if (settings_.generator == GeneratorType::Nether) settings_.dimension = protocol::DIMENSION_NETHER;
  }
  settings_.preset = data->getString("generatorOptions", settings_.preset);
  if (data->has("raining")) raining_ = data->getInt("raining") != 0;
  if (data->has("thundering")) thundering_ = data->getInt("thundering") != 0;
  return true;
}

void Level::saveLevelDataLocked() {
  if (data_path_.empty()) return;
  if (!level_data_) level_data_ = Tag::Compound();
  auto& d = *level_data_;
  auto now_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::system_clock::now().time_since_epoch()).count();
  if (!d.has("hardcore")) d.set("hardcore", Tag::Byte(0));
  d.set("initialized", Tag::Byte(1));
  if (!d.has("GameType")) d.set("GameType", Tag::Int(settings_.gamemode));
  if (!d.has("generatorVersion")) d.set("generatorVersion", Tag::Int(1));
  d.set("SpawnX", Tag::Int(settings_.spawn.x));
  d.set("SpawnY", Tag::Int(settings_.spawn.y));
  d.set("SpawnZ", Tag::Int(settings_.spawn.z));
  d.set("version", Tag::Int(19133));
  d.set("DayTime", Tag::Int(time_));
  d.set("LastPlayed", Tag::Long(now_ms));
  d.set("RandomSeed", Tag::Long(settings_.seed));
  if (!d.has("SizeOnDisk")) d.set("SizeOnDisk", Tag::Long(0));
  d.set("Time", Tag::Long(time_));
  d.set("generatorName", Tag::String(generatorNameFor(settings_.generator)));
  d.set("generatorOptions", Tag::String(settings_.preset));
  d.set("LevelName", Tag::String(settings_.name));
  d.set("raining", Tag::Byte(raining_ ? 1 : 0));
  d.set("thundering", Tag::Byte(thundering_ ? 1 : 0));
  if (!d.has("GameRules")) d.set("GameRules", Tag::Compound());
  auto root = Tag::Compound();
  root->set("Data", level_data_);
  auto buf = nbt::writeGzip(*root, "", nbt::Endian::Big);
  std::string tmp = data_path_ + "/level.dat.tmp";
  {
    std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
    if (!out) return;
    out.write(buf.data(), static_cast<std::streamsize>(buf.size()));
  }
  std::error_code ec;
  fs::rename(tmp, data_path_ + "/level.dat", ec);
}

void Level::saveLevelData() {
  std::lock_guard lock(mu_);
  saveLevelDataLocked();
}

void Level::migrateLegacyTileFiles() {
  // Pre-0.6 builds stored tiles in side files; read once, mark dirty so they move into
  // region TileEntities, then rename the files so they are not re-imported.
  loadChests();
  loadFurnaces();
  loadHoppers();
  loadSigns();
  bool any = false;
  {
    std::lock_guard lock(mu_);
    for (auto& [_, t] : chests_) t.dirty = any = true;
    for (auto& [_, t] : furnaces_) t.dirty = any = true;
    for (auto& [_, t] : hoppers_) t.dirty = any = true;
    for (auto& [_, t] : signs_) t.dirty = any = true;
  }
  if (!any) return;
  saveDirtyChunks(false);
  std::error_code ec;
  for (auto p : {chestPath(), furnacePath(), hopperPath(), signPath()})
    if (fs::exists(p)) fs::rename(p, p + ".migrated", ec);
  util::Logger::instance().info("Migrated legacy tile files of world ", name(), " into region format");
}

std::vector<TagPtr> Level::rawEntitiesInChunk(int cx, int cz) {
  std::lock_guard lock(mu_);
  auto it = raw_entities_.find(chunkKey(cx, cz));
  if (it == raw_entities_.end()) return {};
  auto v = std::move(it->second);
  raw_entities_.erase(it);
  return v;
}

std::vector<std::pair<std::int64_t, nbt::TagPtr>> Level::drainPendingEntities() {
  std::lock_guard lock(mu_);
  auto out = std::move(pending_entities_);
  pending_entities_.clear();
  for (auto& [ck, tag] : out) {
    auto it = raw_entities_.find(ck);
    if (it == raw_entities_.end()) continue;
    auto& v = it->second;
    for (auto e = v.begin(); e != v.end(); ++e) {
      if (*e == tag) {
        v.erase(e);
        break;
      }
    }
    if (v.empty()) raw_entities_.erase(it);
  }
  return out;
}

} // namespace mpmpes::level

