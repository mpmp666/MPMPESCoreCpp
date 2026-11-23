#include "mpmpes/level/structure/Repository.hpp"

#include "mpmpes/nbt/Tag.hpp" // inflateAny for .json.gz
#include "mpmpes/util/Logger.hpp"

#include <array>
#include <cstring>
#include <filesystem>
#include <fstream>

namespace fs = std::filesystem;

namespace mpmpes::level::structure {

std::string StructureRepository::dir_;
std::unordered_map<std::string, std::array<int, 3>> StructureRepository::sizes_;
std::unordered_map<std::string, std::string> StructureRepository::paths_;
std::unordered_map<std::string, std::shared_ptr<Template>> StructureRepository::cache_;

namespace {
std::string b64decode(std::string_view in) {
  auto val = [](char c) -> int {
    if (c >= 'A' && c <= 'Z') return c - 'A';
    if (c >= 'a' && c <= 'z') return c - 'a' + 26;
    if (c >= '0' && c <= '9') return c - '0' + 52;
    if (c == '+') return 62;
    if (c == '/') return 63;
    return -1;
  };
  std::string out;
  int buf = 0, bits = 0;
  for (char c : in) {
    int v = val(c);
    if (v < 0) continue;
    buf = (buf << 6) | v;
    bits += 6;
    if (bits >= 8) {
      bits -= 8;
      out.push_back(static_cast<char>((buf >> bits) & 0xff));
    }
  }
  return out;
}
} // namespace

bool StructureRepository::init(const std::string& dir) {
  dir_ = dir;
  std::string manifest_path = dir_ + "/manifest.json";
  std::ifstream in(manifest_path, std::ios::binary);
  if (!in) {
    util::Logger::instance().warning("Structure manifest missing: ", manifest_path);
    return false;
  }
  std::string text((std::istreambuf_iterator<char>(in)), {});
  auto root = util::jsonParse(text);
  if (!root) {
    util::Logger::instance().warning("Structure manifest parse failed");
    return false;
  }
  auto* templates = root->get("templates");
  if (!templates) return false;
  for (const auto& [key, entry] : templates->obj()) {
    auto* size = entry->get("size");
    auto* file = entry->get("file") ? entry->get("file") : entry->get("path");
    if (!size) continue;
    std::array<int, 3> sz{0, 0, 0};
    for (int i = 0; i < 3; ++i)
      if (auto* v = size->at(i)) sz[i] = v->asInt();
    sizes_[key] = sz;
    std::string fp = file ? file->asStr() : "";
    if (fp.empty()) fp = key + ".json.gz";
    // keys are like "end_city/base_floor" — files live under dir/end_city/base_floor.json.gz
    paths_[key] = fp;
  }
  util::Logger::instance().info("Structure repository: ", sizes_.size(), " templates from ", dir_);
  return true;
}

bool StructureRepository::has(std::string_view key) { return sizes_.count(std::string(key)) > 0; }

int StructureRepository::sizeOf(std::string_view key, int out[3]) {
  auto it = sizes_.find(std::string(key));
  if (it == sizes_.end()) return 0;
  out[0] = it->second[0];
  out[1] = it->second[1];
  out[2] = it->second[2];
  return 1;
}

const Template* StructureRepository::get(std::string_view key) {
  std::string k(key);
  auto it = cache_.find(k);
  if (it != cache_.end()) return it->second.get();
  auto pit = paths_.find(k);
  if (pit == paths_.end()) return nullptr;
  std::string path = dir_ + "/" + pit->second;
  std::ifstream in(path, std::ios::binary);
  if (!in) {
    util::Logger::instance().warning("Missing structure template: ", path);
    return nullptr;
  }
  std::string gz((std::istreambuf_iterator<char>(in)), {});
  auto raw = nbt::inflateAny(gz);
  if (!raw) {
    util::Logger::instance().warning("Corrupt structure template: ", path);
    return nullptr;
  }
  auto root = util::jsonParse(*raw);
  if (!root) return nullptr;
  auto t = std::make_shared<Template>();
  t->key = k;
  if (auto* sz = root->get("size"))
    for (int i = 0; i < 3; ++i)
      if (auto* v = sz->at(i)) t->size[i] = v->asInt();
  if (auto* b = root->get("blocks")) t->blocks = b64decode(b->asStr());
  if (auto* pal = root->get("palette")) {
    for (const auto& variants : pal->arr()) {
      std::vector<std::pair<int, int>> var;
      for (const auto& rot : variants->arr()) {
        if (rot->isArr() && rot->arr().size() >= 2)
          var.emplace_back(rot->at(0)->asInt(), rot->at(1)->asInt());
      }
      t->palette.push_back(std::move(var));
    }
  }
  if (auto* tiles = root->get("tiles")) {
    for (const auto& tile : tiles->arr()) {
      Template::Tile tile2;
      if (auto* pos = tile->get("pos"))
        for (int i = 0; i < 3; ++i)
          if (auto* v = pos->at(i)) tile2.pos[i] = v->asInt();
      if (auto* name = tile->get("name")) {
        std::string tn = name->asStr();
        tile2.is_chest = tn == "chest" || tn == "Chest" || tn == "minecraft:chest";
      }
      if (auto* loot = tile->get("loot")) tile2.loot_table = loot->asStr();
      if (auto* loot = tile->get("loot_table")) tile2.loot_table = loot->asStr();
      if (auto* nbtj = tile->get("nbt")) {
        // keep raw json for sign text etc.
      }
      t->tiles.push_back(std::move(tile2));
    }
  }
  if (auto* ents = root->get("entities")) {
    for (const auto& e : ents->arr()) {
      Template::Ent en;
      if (auto* pos = e->get("pos"))
        for (int i = 0; i < 3; ++i)
          if (auto* v = pos->at(i)) en.pos[i] = static_cast<float>(v->asNum());
      if (auto* id = e->get("id")) en.id = id->asStr();
      if (auto* nbtE = e->get("nbt")) {
        if (auto* rot = nbtE->get("Rotation"))
          if (auto* r0 = rot->at(0)) en.yaw = static_cast<float>(r0->asNum());
      }
      t->entities.push_back(en);
    }
  }
  cache_[k] = t;
  return t.get();
}

} // namespace mpmpes::level::structure
