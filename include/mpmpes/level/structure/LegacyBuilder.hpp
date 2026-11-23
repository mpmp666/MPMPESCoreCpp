#pragma once
// Procedural legacy structures — LegacyStructures builder (cells -> template).

#include "mpmpes/level/structure/Repository.hpp"

#include <map>
#include <vector>

namespace mpmpes::level::structure {

class LegacyBuilder {
public:
  explicit LegacyBuilder(std::int64_t seed) { random_ = (seed & 0x7fffffff) ? (seed & 0x7fffffff) : 1; }

  // build a template for a family (desert_pyramid, desert_well, jungle_temple, swamp_hut, ...)
  Template build(const std::string& family);

private:
  std::int64_t random_;
  std::map<int, int> cells_; // (y<<14)|(z<<7)|x -> (id<<4)|meta
  std::vector<Template::Tile> tiles_;
  int size_[3] = {0, 0, 0};
  int ground_level_ = 0;

  void begin(int w, int h, int l, int ground);
  int next(int bound);
  void put(int x, int y, int z, int id, int meta = 0);
  void fill(int x0, int y0, int z0, int x1, int y1, int z1, int id, int meta = 0);
  void box(int x0, int y0, int z0, int x1, int y1, int z1, int id, int meta = 0, int inside = 0);
  void weather(int id);
  void chest(int x, int y, int z, int facing, const std::string& loot = "temple");
  void dispenser(int x, int y, int z, int facing);
  void spawner(int x, int y, int z, int entityId);
  std::vector<std::array<int, 4>> loot(const std::string& kind);
  Template finish();

  void desertPyramid();
  void desertWell();
  void jungleTemple();
  void swampHut();
  void buriedTreasure();
  void bonusDungeon();
};

// meta rotation for stairs/etc (StructureRepository::rotateMeta)
int rotateMeta(int id, int meta);

} // namespace mpmpes::level::structure
