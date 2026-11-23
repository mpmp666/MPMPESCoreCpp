#pragma once
// Deterministic structure loot — StructureTiles::loot (LCG + weighted pools).

#include "mpmpes/util/Sha256.hpp"

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace mpmpes::level::structure {

struct LootItem {
  int id;
  int meta;
  int count;
};

// Generate end_city_treasure loot for a chest at (x,y,z) in a world with `seed`.
// PHP seeds by hash('sha256', seed:x:y:z) first 7 hex, then an LCG.
std::vector<LootItem> endCityLoot(std::int64_t seed, int x, int y, int z);

// sha256(seed:x:y:z) first-7-hex as int (PHP loot hash)
inline int lootHash(std::int64_t seed, int x, int y, int z) {
  std::string s = std::to_string(seed) + ":" + std::to_string(x) + ":" + std::to_string(y) + ":" +
                  std::to_string(z);
  auto h = util::sha256(s);
  int v = 0;
  for (int i = 0; i < 4; ++i) v = (v << 8) | (std::uint8_t)h[i]; // first bytes as hex int
  return v & 0x7fffffff;
}

} // namespace mpmpes::level::structure
