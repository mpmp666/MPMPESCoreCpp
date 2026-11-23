#include "mpmpes/level/structure/LegacyBuilder.hpp"

#include <algorithm>
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
  begin(5, 6, 5, 4);
  fill(0, 0, 0, 4, 4, 4, 24, 1); // sandstone base
  fill(1, 1, 1, 3, 2, 3, 9);     // water
  // rim
  for (int x = 0; x < 5; ++x)
    for (int z = 0; z < 5; ++z) {
      if (x == 0 || x == 4 || z == 0 || z == 4) put(x, 3, z, 24, 0);
    }
  put(2, 4, 2, 24, 1);
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
  // ported in a follow-up pass
}

void LegacyBuilder::swampHut() {
  // ported in a follow-up pass
}

Template LegacyBuilder::build(const std::string& family) {
  cells_.clear();
  tiles_.clear();
  if (family == "desert_pyramid") desertPyramid();
  else if (family == "desert_well") desertWell();
  else if (family == "jungle_temple") jungleTemple();
  else if (family == "swamp_hut") swampHut();
  return finish();
}

} // namespace mpmpes::level::structure
