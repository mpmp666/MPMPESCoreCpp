#pragma once
// Structure resources — port of StructureRepository (manifest/templates/rotate/bounds).

#include "mpmpes/util/Json.hpp"

#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace mpmpes::level::structure {

struct Template {
  std::string key;
  int size[3];           // sx, sy, sz
  // blocks: base64-decoded records (vx,vy,vz,vp little-endian uint16 x4)
  std::string blocks;    // raw bytes
  // palette[paletteIndex][rotation] = (blockId, meta)
  std::vector<std::vector<std::pair<int, int>>> palette;
  struct Tile {
    int pos[3];
    std::string nbt_json; // raw tile NBT json (kept; tiles that are chests get loot)
    bool is_chest = false;
    std::string loot_table;
  };
  std::vector<Tile> tiles;
  struct Marker {
    int pos[3];
    std::string metadata;
  };
  std::vector<Marker> markers;
  struct Ent {
    float pos[3];
    std::string id;
    float yaw = 0;
  };
  std::vector<Ent> entities;
};

class StructureRepository {
public:
  static bool init(const std::string& dir); // loads manifest.json
  static bool has(std::string_view key);
  static const Template* get(std::string_view key); // lazy-loads + caches
  static int sizeOf(std::string_view key, int out[3]);

  static std::pair<int, int> rotate(int x, int z, int rotation) {
    switch (rotation & 3) {
      case 1: return {-z, x};
      case 2: return {-x, -z};
      case 3: return {z, -x};
      default: return {x, z};
    }
  }
  static std::array<int, 6> bounds(const std::vector<struct Piece>& pieces);

private:
  static std::string dir_;
  static std::unordered_map<std::string, std::array<int, 3>> sizes_; // key -> size
  static std::unordered_map<std::string, std::string> paths_;        // key -> file
  static std::unordered_map<std::string, std::shared_ptr<Template>> cache_;
};

struct Piece {
  std::string templ;
  int x, y, z;
  int rotation;
  bool ignore_air;
  int generation_depth;
  int box[6];
};

} // namespace mpmpes::level::structure
