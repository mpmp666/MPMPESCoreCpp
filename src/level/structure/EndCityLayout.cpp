#include "mpmpes/level/structure/EndCityLayout.hpp"

#include <algorithm>
#include <cstring>

namespace mpmpes::level::structure {

namespace {
constexpr int MAX_GEN_DEPTH = 8;
const int TOWER_BRIDGES[4][4] = {{0, 1, -1, 0}, {1, 6, -1, 1}, {3, 0, -1, 5}, {2, 5, -1, 6}};
const int FAT_TOWER_BRIDGES[4][4] = {{0, 4, -1, 0}, {1, 12, -1, 4}, {3, 0, -1, 8}, {2, 8, -1, 12}};
} // namespace

EndCityLayout::EndCityLayout(gen::JavaRandom random) : random_(random) {}

Piece EndCityLayout::makePiece(const std::string& name, int x, int y, int z, int rotation,
                               bool overwrite, int gen_depth) {
  std::string key = "end_city/" + name;
  int size[3] = {0, 0, 0};
  StructureRepository::sizeOf(key, size);
  int w = size[0], h = size[1], l = size[2];
  auto [dx, dz] = StructureRepository::rotate(w - 1, l - 1, rotation);
  Piece p;
  p.templ = key;
  p.x = x;
  p.y = y;
  p.z = z;
  p.rotation = rotation & 3;
  p.ignore_air = !overwrite;
  p.generation_depth = gen_depth;
  p.box[0] = x + std::min(0, dx);
  p.box[1] = y;
  p.box[2] = z + std::min(0, dz);
  p.box[3] = x + std::max(0, dx);
  p.box[4] = y + h - 1;
  p.box[5] = z + std::max(0, dz);
  return p;
}

Piece EndCityLayout::relative(const Piece& parent, const std::string& name, int x, int y, int z,
                              int rotation, bool overwrite) {
  auto [rx, rz] = StructureRepository::rotate(x, z, parent.rotation);
  return makePiece(name, parent.x + rx, parent.y + y, parent.z + rz, rotation, overwrite);
}

std::vector<Piece> EndCityLayout::buildAtRotation(int rotation) {
  ship_created_ = false;
  std::vector<Piece> pieces;
  Piece last = add(pieces, makePiece("base_floor", 0, 0, 0, rotation, true));
  last = add(pieces, relative(last, "second_floor_1", -1, 0, -1, rotation, false));
  last = add(pieces, relative(last, "third_floor_1", -1, 4, -1, rotation, false));
  last = add(pieces, relative(last, "third_roof", -1, 8, -1, rotation, true));
  recursiveChildren("tower", 1, last, nullptr, pieces);
  return pieces;
}

const Piece* EndCityLayout::findCollision(const std::vector<Piece>& pieces, const int b[6]) {
  for (const auto& piece : pieces) {
    const auto& a = piece.box;
    if (a[3] >= b[0] && a[0] <= b[3] && a[4] >= b[1] && a[1] <= b[4] && a[5] >= b[2] && a[2] <= b[5])
      return &piece;
  }
  return nullptr;
}

bool EndCityLayout::recursiveChildren(const std::string& generator, int depth, const Piece& parent,
                                      const int* offset, std::vector<Piece>& pieces) {
  if (depth > MAX_GEN_DEPTH) return false;
  std::vector<Piece> children;
  bool generated;
  if (generator == "house") generated = house(depth, parent, offset, children);
  else if (generator == "tower") generated = tower(depth, parent, children);
  else if (generator == "bridge") generated = bridge(depth, parent, children);
  else if (generator == "fat_tower") generated = fatTower(depth, parent, children);
  else return false;
  if (!generated) return false;
  int tag = random_.nextInt();
  for (auto& child : children) {
    child.generation_depth = tag;
    const Piece* collision = findCollision(pieces, child.box);
    if (collision && collision->generation_depth != parent.generation_depth) return false;
  }
  for (auto& c : children) pieces.push_back(std::move(c));
  return true;
}

bool EndCityLayout::house(int depth, const Piece& parent, const int* offset,
                          std::vector<Piece>& pieces) {
  if (depth > MAX_GEN_DEPTH) return false;
  if (!offset) return false;
  int r = parent.rotation;
  Piece last = add(pieces, relative(parent, "base_floor", offset[0], offset[1], offset[2], r, true));
  int floors = random_.nextInt(3);
  if (floors == 0) {
    add(pieces, relative(last, "base_roof", -1, 4, -1, r, true));
  } else if (floors == 1) {
    last = add(pieces, relative(last, "second_floor_2", -1, 0, -1, r, false));
    last = add(pieces, relative(last, "second_roof", -1, 8, -1, r, false));
    recursiveChildren("tower", depth + 1, last, nullptr, pieces);
  } else {
    last = add(pieces, relative(last, "second_floor_2", -1, 0, -1, r, false));
    last = add(pieces, relative(last, "third_floor_2", -1, 4, -1, r, false));
    last = add(pieces, relative(last, "third_roof", -1, 8, -1, r, true));
    recursiveChildren("tower", depth + 1, last, nullptr, pieces);
  }
  return true;
}

bool EndCityLayout::tower(int depth, const Piece& parent, std::vector<Piece>& pieces) {
  int r = parent.rotation;
  int tbx = 3 + random_.nextInt(2);
  int tbz = 3 + random_.nextInt(2);
  Piece last = add(pieces, relative(parent, "tower_base", tbx, -3, tbz, r, true));
  last = add(pieces, relative(last, "tower_piece", 0, 7, 0, r, true));
  Piece bridgePiece;
  bool haveBridge = random_.nextInt(3) == 0;
  if (haveBridge) bridgePiece = last;
  int height = 1 + random_.nextInt(3);
  for (int i = 0; i < height; ++i) {
    last = add(pieces, relative(last, "tower_piece", 0, 4, 0, r, true));
    if (i < height - 1 && random_.nextBoolean()) {
      bridgePiece = last;
      haveBridge = true;
    }
  }
  if (haveBridge) {
    for (const auto& tb : TOWER_BRIDGES) {
      if (random_.nextBoolean()) {
        int turn = tb[0], x = tb[1], y = tb[2], z = tb[3];
        Piece start = add(pieces, relative(bridgePiece, "bridge_end", x, y, z, (r + turn) & 3, true));
        recursiveChildren("bridge", depth + 1, start, nullptr, pieces);
      }
    }
    add(pieces, relative(last, "tower_top", -1, 4, -1, r, true));
  } else {
    if (depth != 7) return recursiveChildren("fat_tower", depth + 1, last, nullptr, pieces);
    add(pieces, relative(last, "tower_top", -1, 4, -1, r, true));
  }
  return true;
}

bool EndCityLayout::bridge(int depth, const Piece& parent, std::vector<Piece>& pieces) {
  int r = parent.rotation;
  int length = random_.nextInt(4) + 1;
  Piece last = add(pieces, relative(parent, "bridge_piece", 0, 0, -4, r, true));
  last.generation_depth = -1;
  int nextY = 0;
  for (int i = 0; i < length; ++i) {
    if (random_.nextBoolean()) {
      last = add(pieces, relative(last, "bridge_piece", 0, nextY, -4, r, true));
      nextY = 0;
    } else {
      if (random_.nextBoolean()) {
        last = add(pieces, relative(last, "bridge_steep_stairs", 0, nextY, -4, r, true));
      } else {
        last = add(pieces, relative(last, "bridge_gentle_stairs", 0, nextY, -8, r, true));
      }
      nextY = 4;
    }
  }
  if (!ship_created_ && random_.nextInt(10 - depth) == 0) {
    int ship_x = -8 + random_.nextInt(8);
    int ship_z = -70 + random_.nextInt(10);
    add(pieces, relative(last, "ship", ship_x, nextY, ship_z, r, true));
    ship_created_ = true;
  } else {
    const int off[3] = {-3, nextY + 1, -11};
    if (!recursiveChildren("house", depth + 1, last, off, pieces)) return false;
  }
  last = add(pieces, relative(last, "bridge_end", 4, nextY, 0, (r + 2) & 3, true));
  last.generation_depth = -1;
  return true;
}

bool EndCityLayout::fatTower(int depth, const Piece& parent, std::vector<Piece>& pieces) {
  int r = parent.rotation;
  Piece last = add(pieces, relative(parent, "fat_tower_base", -3, 4, -3, r, true));
  last = add(pieces, relative(last, "fat_tower_middle", 0, 4, 0, r, true));
  for (int i = 0; i < 2 && random_.nextInt(3) != 0; ++i) {
    last = add(pieces, relative(last, "fat_tower_middle", 0, 8, 0, r, true));
    for (const auto& tb : FAT_TOWER_BRIDGES) {
      if (random_.nextBoolean()) {
        int turn = tb[0], x = tb[1], y = tb[2], z = tb[3];
        Piece start = add(pieces, relative(last, "bridge_end", x, y, z, (r + turn) & 3, true));
        recursiveChildren("bridge", depth + 1, start, nullptr, pieces);
      }
    }
  }
  add(pieces, relative(last, "fat_tower_top", -2, 8, -2, r, true));
  return true;
}

} // namespace mpmpes::level::structure
