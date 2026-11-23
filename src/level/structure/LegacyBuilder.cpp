#include "mpmpes/level/structure/LegacyBuilder.hpp"

#include <algorithm>
#include <array>
#include <cstring>

namespace mpmpes::level::structure {

void LegacyBuilder::begin(int w, int h, int l, int ground) {
  size_[0] = w;
  size_[1] = h;
  size_[2] = l;
  ground_level_ = ground;
}

int LegacyBuilder::next(int bound) {
  random_ = (random_ * 1103515245 + 12345) & 0x7fffffff;
  return static_cast<int>((random_ >> 8) & 0x7fffff) % bound;
}

void LegacyBuilder::put(int x, int y, int z, int id, int meta) {
  if (x < 0 || y < 0 || z < 0 || x >= size_[0] || y >= size_[1] || z >= size_[2]) return;
  cells_[(y << 14) | (z << 7) | x] = (id << 4) | meta;
}

void LegacyBuilder::fill(int x0, int y0, int z0, int x1, int y1, int z1, int id, int meta) {
  for (int y = y0; y <= y1; ++y)
    for (int z = z0; z <= z1; ++z)
      for (int x = x0; x <= x1; ++x) put(x, y, z, id, meta);
}

void LegacyBuilder::box(int x0, int y0, int z0, int x1, int y1, int z1, int id, int meta,
                        int inside) {
  for (int y = y0; y <= y1; ++y)
    for (int z = z0; z <= z1; ++z)
      for (int x = x0; x <= x1; ++x) {
        bool edge = x == x0 || x == x1 || y == y0 || y == y1 || z == z0 || z == z1;
        put(x, y, z, edge ? id : inside, edge ? meta : 0);
      }
}

void LegacyBuilder::weather(int id) {
  for (auto& [key, state] : cells_) {
    if ((state >> 4) != id) continue;
    int roll = next(10);
    if (id == 4 && roll < 5) cells_[key] = 48 << 4;
    else if (id == 98 && roll < 4) cells_[key] = (98 << 4) | (roll < 2 ? 1 : 2);
  }
}

void LegacyBuilder::chest(int x, int y, int z, int facing, const std::string& kind) {
  put(x, y, z, 54, facing);
  // PHP loot() exact LCG consumption: 3+next(3), then per slot next(count(table)) + next(range)
  static const std::unordered_map<std::string, std::vector<std::array<int, 3>>> tables = {
      {"temple", {{265, 1, 5}, {266, 1, 4}, {264, 1, 2}, {352, 3, 8}, {287, 2, 6}, {367, 3, 8}, {388, 1, 3}, {322, 1, 1}}},
      {"library", {{340, 2, 6}, {339, 3, 8}, {345, 1, 1}, {331, 2, 6}, {266, 1, 3}}},
      {"mine", {{265, 2, 6}, {263, 3, 8}, {66, 4, 12}, {297, 1, 4}, {264, 1, 2}, {362, 2, 5}, {361, 2, 5}}},
      {"fortress", {{266, 1, 5}, {265, 1, 4}, {264, 1, 2}, {259, 1, 1}, {377, 2, 5}, {372, 2, 7}, {329, 1, 1}}},
      {"treasure", {{264, 1, 3}, {266, 3, 8}, {265, 3, 9}, {388, 2, 5}, {267, 1, 1}, {297, 2, 5}}},
      {"dungeon", {{329, 1, 1}, {265, 1, 4}, {297, 1, 4}, {331, 2, 6}, {289, 2, 6}, {287, 2, 6}, {322, 1, 1}}},
  };
  auto it = tables.find(kind);
  const auto& table = it != tables.end() ? it->second : tables.at("temple");
  int count = 3 + next(3);
  for (int slot = 0; slot < count; ++slot) {
    const auto& entry = table[next(static_cast<int>(table.size()))];
    (void)next(entry[2] - entry[1] + 1);
  }
  Template::Tile tile;
  tile.pos[0] = x;
  tile.pos[1] = y;
  tile.pos[2] = z;
  tile.is_chest = true;
  tile.loot_table = "legacy:" + kind;
  tiles_.push_back(tile);
}

void LegacyBuilder::dispenser(int x, int y, int z, int facing) {
  put(x, y, z, 23, facing);
  Template::Tile tile;
  tile.pos[0] = x;
  tile.pos[1] = y;
  tile.pos[2] = z;
  tiles_.push_back(tile);
}

void LegacyBuilder::spawner(int x, int y, int z, int entityId) {
  put(x, y, z, 52);
  (void)next(200); // PHP spawner Delay = 100 + next(200)
  Template::Tile tile;
  tile.pos[0] = x;
  tile.pos[1] = y;
  tile.pos[2] = z;
  tiles_.push_back(tile);
}

int rotateMeta(int id, int meta) {
  auto in = [](int id, std::initializer_list<int> set) {
    for (int s : set)
      if (s == id) return true;
    return false;
  };
  if (in(id, {53, 67, 108, 109, 114, 128, 134, 135, 136, 156, 163, 164, 180})) {
    static const int m[] = {2, 3, 1, 0};
    return (meta & 12) | m[meta & 3];
  }
  if (in(id, {23, 54, 61, 62, 68, 125, 146, 154})) {
    switch (meta & 7) {
      case 2: return (meta & 8) | 5;
      case 5: return (meta & 8) | 3;
      case 3: return (meta & 8) | 4;
      case 4: return (meta & 8) | 2;
      default: return (meta & 8) | (meta & 7);
    }
  }
  if (id == 65) {
    switch (meta) {
      case 2: return 5;
      case 5: return 3;
      case 3: return 4;
      case 4: return 2;
      default: return meta;
    }
  }
  if (in(id, {50, 75, 76})) {
    switch (meta & 7) {
      case 1: return (meta & 8) | 3;
      case 3: return (meta & 8) | 2;
      case 2: return (meta & 8) | 4;
      case 4: return (meta & 8) | 1;
      default: return (meta & 8) | (meta & 7);
    }
  }
  if (in(id, {77, 143})) {
    switch (meta & 7) {
      case 2: return (meta & 8) | 4;
      case 4: return (meta & 8) | 3;
      case 3: return (meta & 8) | 5;
      case 5: return (meta & 8) | 2;
      default: return (meta & 8) | (meta & 7);
    }
  }
  if (in(id, {64, 71, 193, 194, 195, 196, 197}))
    return (meta & 8) ? meta : ((meta & 12) | ((meta + 1) & 3));
  if (id == 66) {
    static const int m[] = {1, 0, 5, 4, 2, 3, 7, 8, 9, 6};
    return meta < 10 ? m[meta] : meta;
  }
  if (in(id, {27, 28, 126})) {
    static const int m[] = {1, 0, 5, 4, 2, 3};
    return (meta & 8) | ((meta & 7) < 6 ? m[meta & 7] : (meta & 7));
  }
  if (in(id, {96, 167})) {
    static const int m[] = {2, 3, 1, 0};
    return (meta & 12) | m[meta & 3];
  }
  if (in(id, {86, 91, 93, 94, 107, 183, 184, 185, 186, 187})) {
    static const int m[] = {1, 2, 3, 0};
    return (meta & 12) | m[meta & 3];
  }
  if (in(id, {120, 127, 131})) return (meta & 12) | ((meta + 1) & 3);
  if (in(id, {68, 63})) {
    switch (meta) {
      case 2: return 5;
      case 5: return 3;
      case 3: return 4;
      case 4: return 2;
      default: return meta;
    }
  }
  return meta;
}

Template LegacyBuilder::finish() {
  Template t;
  std::memcpy(t.size, size_, sizeof(size_));
  // palette + blockData
  std::map<int, int> indices;
  std::string data;
  for (const auto& [key, state] : cells_) {
    if (!indices.count(state)) {
      indices[state] = static_cast<int>(t.palette.size());
      int id = state >> 4, meta = state & 15;
      std::vector<std::pair<int, int>> rotations;
      for (int r = 0; r < 4; ++r) {
        rotations.push_back({id, meta});
        meta = rotateMeta(id, meta);
      }
      t.palette.push_back(rotations);
    }
    int x = key & 127, y = key >> 14, z = (key >> 7) & 127, pi = indices[state];
    for (int v : {x, y, z, pi}) {
      data.push_back(static_cast<char>(v & 0xff));
      data.push_back(static_cast<char>((v >> 8) & 0xff));
    }
  }
  t.blocks = std::move(data);
  t.tiles = tiles_;
  t.key = "legacy";
  return t;
}

void LegacyBuilder::desertWell() {
  begin(5, 6, 5, 0);
  fill(0, 0, 0, 4, 1, 4, 24);
  fill(0, 2, 0, 4, 2, 4, 24);
  fill(1, 2, 1, 3, 2, 3, 0);
  put(2, 1, 2, 9);
  for (auto& p : {std::pair{1, 2}, {3, 2}, {2, 1}, {2, 3}}) put(p.first, 1, p.second, 9);
  for (auto& p : {std::pair{0, 2}, {4, 2}, {2, 0}, {2, 4}}) put(p.first, 2, p.second, 44, 1);
  for (int x : {1, 3})
    for (int z : {1, 3}) fill(x, 2, z, x, 4, z, 24);
  fill(1, 5, 1, 3, 5, 3, 44, 1);
  put(2, 5, 2, 24);
}

void LegacyBuilder::desertPyramid() {
  begin(21, 30, 21, 18);
  fill(0, 14, 0, 20, 18, 20, 12);
  fill(7, 4, 7, 13, 17, 13, 24, 2);
  fill(8, 5, 8, 12, 17, 12, 0);
  for (int step = 1; step <= 9; ++step) {
    fill(step, 18 + step, step, 20 - step, 18 + step, 20 - step, 24);
    if (step < 9) fill(step + 1, 18 + step, step + 1, 19 - step, 18 + step, 19 - step, 0);
  }
  for (int x : {0, 16}) {
    box(x, 18, 0, x + 4, 27, 4, 24);
    fill(x + 1, 28, 1, x + 3, 28, 3, 24, 2);
    put(x + 2, 28, 0, 128, 2);
    put(x + 2, 28, 4, 128, 3);
    put(x, 28, 2, 128, 0);
    put(x + 4, 28, 2, 128, 1);
    for (int y : {20, 21, 23, 25}) put(x + 2, y, 0, 159, 1);
    for (int y : {18, 20}) {
      fill(x + 1, y, 0, x + 3, y, 0, 159, 1);
      put(x + 2, y, 0, 24, 1);
    }
    fill(x + 1, 22, 0, x + 3, 22, 0, 24, 2);
  }
  box(8, 18, 0, 12, 22, 5, 24);
  fill(9, 19, 0, 11, 21, 7, 0);
  fill(4, 19, 2, 8, 20, 2, 0);
  fill(12, 19, 2, 16, 20, 2, 0);
  fill(8, 22, 0, 12, 24, 0, 24, 2);
  put(9, 23, 0, 159, 1);
  put(10, 23, 0, 24, 1);
  put(11, 23, 0, 159, 1);
  for (int x : {8, 12})
    for (int z : {8, 12}) fill(x, 19, z, x, 21, z, 24, 2);
  for (int d = -3; d <= 3; ++d) {
    put(10 + d, 18, 10, 159, 1);
    put(10, 18, 10 + d, 159, 1);
  }
  for (auto& p : {std::pair{9, 9}, {11, 9}, {9, 11}, {11, 11}}) put(p.first, 18, p.second, 159, 1);
  box(7, 1, 7, 11, 4, 13, 4, 0, 0);
  fill(8, 2, 8, 10, 4, 12, 0);
  fill(7, 2, 9, 7, 3, 11, 0);
  fill(11, 2, 9, 11, 3, 11, 0);
  fill(8, 2, 7, 10, 3, 7, 0);
  fill(8, 2, 13, 10, 3, 13, 0);
  put(9, 1, 3, 4);
  put(9, 1, 4, 4);
  put(9, 1, 5, 4);
  put(9, 1, 6, 4);
  put(8, 1, 6, 0);
  put(7, 1, 6, 0);
  put(7, 1, 7, 0);
  put(9, 2, 3, 23);
  put(9, 2, 3, 23);
  put(9, 2, 3, 23);
  chest(8, 1, 3, 2);
  chest(9, 1, 10, 3);
  fill(10, 21, 3, 10, 22, 3, 0);
  fill(3, 21, 10, 3, 22, 10, 0);
  fill(17, 21, 10, 17, 22, 10, 0);
  fill(10, 21, 17, 10, 22, 17, 0);
  for (int step = 0; step < 7; ++step) {
    fill(9, 17 + step, 6 + step, 11, 17 + step, 6 + step, 24);
    fill(9, 18 + step, 6 + step, 11, 24, 6 + step, 0);
  }
}

void LegacyBuilder::jungleTemple() {
  begin(12, 15, 15, 4);
  fill(0, 0, 0, 11, 4, 14, 4);
  fill(3, 3, 0, 8, 4, 0, 0);
  fill(4, 5, 0, 7, 6, 2, 0);
  fill(4, 5, 2, 7, 5, 4, 48);
  box(2, 5, 2, 9, 6, 12, 4);
  fill(3, 5, 3, 8, 6, 11, 0);
  box(1, 7, 1, 10, 10, 13, 4);
  fill(2, 7, 2, 9, 7, 12, 4);
  fill(3, 11, 3, 8, 11, 11, 4);
  fill(4, 12, 4, 7, 12, 10, 4);
  fill(5, 13, 6, 6, 13, 8, 4);
  for (int x : {2, 9})
    for (int z : {2, 12}) fill(x, 11, z, x, 13, z, 4);
  for (int z : {3, 5, 7, 9, 11}) {
    fill(0, 7, z, 0, 9, z, 4);
    fill(11, 7, z, 11, 9, z, 4);
  }
  fill(4, 8, 1, 7, 9, 1, 0);
  fill(4, 8, 13, 7, 9, 13, 0);
  for (int x : {3, 8}) {
    fill(x, 8, 1, x, 9, 1, 0);
    fill(x, 8, 13, x, 9, 13, 0);
  }
  fill(5, 5, 2, 6, 6, 3, 0);
  fill(5, 8, 1, 6, 9, 1, 0);
  fill(5, 7, 1, 6, 7, 1, 0);
  fill(5, 7, 1, 6, 7, 13, 0);
  for (int x = 4; x <= 7; ++x) put(x, 4, 0, 67, 2);
  for (int x : {4, 7})
    for (int step = 0; step < 3; ++step) {
      fill(x, 5, 8 + step, x, 5 + step, 8 + step, 4);
      put(x, 5 + step, 8 + step, 67, 2);
      fill(x, 6 + step, 8 + step, x, 9, 8 + step, 0);
    }
  put(5, 13, 6, 67, 2);
  put(6, 13, 6, 67, 2);
  put(5, 13, 8, 67, 3);
  put(6, 13, 8, 67, 3);
  fill(1, 1, 1, 3, 3, 13, 0);
  fill(1, 1, 1, 9, 3, 5, 0);
  fill(1, 1, 12, 10, 3, 13, 0);
  fill(7, 1, 8, 10, 3, 13, 0);
  for (int step = 0; step < 4; ++step) {
    int y = 4 - step;
    int z = 6 + step;
    fill(5, y + 1, z, 6, 6, z, 0);
    fill(5, y - 1, z, 6, y - 1, z, 4);
    fill(5, y, z, 6, y, z, 67, 3);
  }
  fill(1, 1, 1, 3, 3, 13, 0);
  fill(1, 1, 1, 9, 3, 5, 0);
  fill(1, 1, 12, 10, 3, 13, 0);
  fill(7, 1, 8, 10, 3, 13, 0);
  fill(7, 0, 2, 10, 0, 5, 48);
  fill(7, 1, 2, 7, 3, 5, 0);
  chest(8, 1, 3, 2);
  fill(8, 0, 9, 10, 0, 11, 48);
  fill(8, 1, 10, 10, 3, 11, 0);
  chest(9, 1, 10, 4);
  fill(7, 1, 1, 7, 1, 5, 4);
  put(7, 2, 1, 131, 2);
  put(7, 2, 5, 131, 0);
  fill(7, 2, 2, 7, 2, 4, 132);
  put(7, 3, 1, 4);
  put(7, 3, 5, 4);
  fill(3, 0, 2, 4, 0, 4, 48);
  dispenser(3, 1, 3, 4);
  fill(6, 1, 12, 6, 3, 14, 4);
  fill(6, 1, 13, 6, 2, 13, 0);
  put(7, 2, 12, 69, 1);
  put(7, 2, 13, 69, 1);
  put(7, 2, 14, 69, 1);
  weather(4);
}

void LegacyBuilder::buriedTreasure() {
  begin(5, 5, 5, 0);
  fill(0, 0, 0, 4, 0, 4, 24);
  fill(0, 1, 0, 4, 4, 4, 12);
  put(1, 1, 1, 24);
  put(3, 1, 3, 24);
  chest(2, 1, 2, 2, "treasure");
}

void LegacyBuilder::bonusDungeon() {
  bool wide = next(2) == 0;
  int width = wide ? 11 : 9;
  begin(width, 7, 11, 0);
  box(0, 0, 0, width - 1, 6, 10, 4);
  fill(1, 1, 1, width - 2, 1, 9, 4);
  fill(1, 2, 1, width - 2, 5, 9, 0);
  fill(3, 2, 0, 4, 4, 0, 0);
  fill(width - 5, 2, 10, width - 4, 4, 10, 0);
  weather(4);
  static const int spawnerIds[] = {32, 32, 34, 35};
  spawner(width / 2, 2, 5, spawnerIds[next(4)]);
  chest(1, 2, 2, 5, "dungeon");
  chest(width - 2, 2, 8, 4, "dungeon");
}

void LegacyBuilder::door(int x, int y, int z, int facing, bool iron) {
  // PHP door(): bottom at y (facing), top at y+1 (meta 8)
  int id = iron ? 71 : 64;
  put(x, y, z, id, facing);
  put(x, y + 1, z, id, 8);
}

void LegacyBuilder::stronghold() {
  begin(53, 15, 51, 0);
  box(22, 1, 0, 30, 8, 10, 98);
  box(24, 1, 9, 28, 6, 35, 98);
  box(19, 1, 14, 33, 7, 22, 98);
  box(12, 1, 16, 40, 6, 20, 98);
  fill(25, 2, 0, 27, 4, 35, 0);
  fill(12, 2, 17, 40, 4, 19, 0);
  fill(20, 2, 15, 32, 6, 21, 0);
  for (int x : {20, 32}) {
    fill(x, 2, 15, x, 6, 15, 98, 3);
    fill(x, 2, 21, x, 6, 21, 98, 3);
  }
  fill(24, 6, 15, 28, 6, 21, 44, 5);
  fill(20, 6, 17, 32, 6, 19, 44, 5);
  for (int z : {7, 12, 24, 30}) {
    put(25, 4, z, 50, 1);
    put(27, 4, z, 50, 2);
  }
  door(26, 2, 1, 1);
  box(0, 1, 12, 13, 12, 28, 98);
  fill(12, 2, 17, 13, 4, 19, 0);
  for (int z = 13; z <= 27; ++z) {
    bool pier = ((z - 13) % 4) == 0;
    for (int x : {1, 12}) {
      fill(x, 2, z, x, 5, z, pier ? 5 : 47);
      fill(x, 8, z, x, 10, z, pier ? 5 : 47);
    }
    if (pier) {
      put(2, 4, z, 50, 1);
      put(11, 4, z, 50, 2);
    }
  }
  fill(12, 2, 17, 13, 4, 19, 0);
  for (int x : {4, 8})
    for (int z : {15, 21}) fill(x, 2, z, x + 1, 4, z + 3, 47);
  fill(1, 7, 13, 3, 7, 27, 5);
  fill(10, 7, 13, 12, 7, 27, 5);
  fill(4, 7, 13, 9, 7, 15, 5);
  fill(4, 7, 25, 9, 7, 27, 5);
  fill(3, 8, 16, 3, 8, 24, 85);
  fill(10, 8, 16, 10, 8, 24, 85);
  fill(4, 8, 15, 9, 8, 15, 85);
  fill(4, 8, 25, 9, 8, 25, 85);
  fill(2, 2, 26, 2, 7, 26, 65, 5);
  put(2, 7, 26, 0);
  fill(6, 9, 19, 7, 9, 20, 85);
  put(6, 8, 19, 50, 5);
  put(7, 8, 20, 50, 5);
  chest(9, 2, 26, 2, "library");
  chest(11, 8, 14, 3, "library");
  for (auto& p : {std::array<int,3>{2, 11, 15}, std::array<int,3>{10, 11, 24}, std::array<int,3>{5, 6, 17}, std::array<int,3>{8, 5, 24}}) {
    if (next(3) != 0) put(p[0], p[1], p[2], 30);
  }
  box(39, 1, 12, 52, 7, 26, 98);
  fill(39, 2, 17, 40, 4, 19, 0);
  for (int z : {14, 18, 22}) {
    fill(46, 2, z, 46, 5, z + 2, 101);
    fill(47, 1, z + 3, 51, 6, z + 3, 98);
    door(46, 2, z + 1, 0, true);
    put(45, 2, z + 1, 70);
  }
  chest(41, 2, 24, 2);
  box(20, 1, 34, 32, 10, 50, 98);
  fill(25, 2, 34, 27, 4, 35, 0);
  for (int x : {22, 30}) {
    fill(x, 2, 38, x, 4, 44, 101);
    fill(x, 1, 37, x, 1, 47, 11);
  }
  fill(24, 1, 41, 28, 1, 45, 11);
  fill(24, 2, 41, 28, 3, 45, 98);
  fill(25, 3, 42, 27, 3, 44, 49);
  for (int d = 0; d < 3; ++d) {
    put(25 + d, 4, 41, 120, 0);
    put(25 + d, 4, 45, 120, 2);
    put(24, 4, 42 + d, 120, 3);
    put(28, 4, 42 + d, 120, 1);
  }
  fill(25, 2, 38, 27, 2, 38, 109, 2);
  fill(25, 2, 39, 27, 2, 40, 98);
  fill(25, 3, 39, 27, 3, 39, 109, 2);
  spawner(26, 2, 37, 39);
  for (int z : {37, 41, 45, 48}) {
    put(21, 6, z, 101);
    put(31, 6, z, 101);
  }
  weather(98);
}

void LegacyBuilder::swampHut() {
  begin(7, 9, 9, 5);
  fill(1, 4, 1, 5, 4, 7, 5, 1);
  fill(2, 4, 0, 4, 4, 0, 5, 1);
  fill(1, 7, 2, 5, 7, 7, 5, 1);
  fill(2, 5, 2, 3, 6, 2, 5, 1);
  fill(1, 5, 3, 1, 6, 6, 5, 1);
  fill(5, 5, 3, 5, 6, 6, 5, 1);
  fill(2, 5, 7, 4, 6, 7, 5, 1);
  fill(2, 5, 3, 4, 6, 6, 0);
  for (int x : {1, 5})
    for (int z : {2, 7}) fill(x, 0, z, x, 6, z, 17);
  put(1, 5, 1, 85);
  put(5, 5, 1, 85);
  fill(1, 3, 0, 2, 3, 0, 0);
  fill(4, 3, 0, 5, 3, 0, 0);
  put(2, 6, 2, 85);
  put(2, 6, 2, 85);
  put(3, 6, 7, 85);
  put(1, 6, 4, 0);
  put(5, 6, 4, 0);
  put(5, 6, 5, 0);
  put(1, 5, 5, 140);
  put(3, 5, 6, 58);
  put(4, 5, 6, 118, 6);
  fill(0, 7, 1, 6, 7, 1, 134, 2);
  fill(0, 7, 8, 6, 7, 8, 134, 3);
  fill(0, 7, 2, 0, 7, 7, 134, 0);
  fill(6, 7, 2, 6, 7, 7, 134, 1);
  fill(1, 8, 2, 5, 8, 7, 158, 1);
}

Template LegacyBuilder::build(const std::string& family) {
  cells_.clear();
  tiles_.clear();
  if (family == "desert_pyramid") desertPyramid();
  else if (family == "desert_well") desertWell();
  else if (family == "jungle_temple") jungleTemple();
  else if (family == "swamp_hut") swampHut();
  else if (family == "buried_treasure") buriedTreasure();
  else if (family == "bonus_dungeon") bonusDungeon();
  else if (family == "stronghold") stronghold();
  return finish();
}

} // namespace mpmpes::level::structure
