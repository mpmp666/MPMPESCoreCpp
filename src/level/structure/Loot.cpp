#include "mpmpes/level/structure/Loot.hpp"

#include <algorithm>
#include <cstdio>

namespace mpmpes::level::structure {

namespace {
// PE 0.14 ids for end-city loot (horse armor / smithing template absent -> skip)
struct Entry {
  int id;
  int meta;
  int weight;
  int count_min, count_max; // 0,0 -> count 1 (no set_count)
};
const Entry POOL[] = {
    {264, 0, 5, 2, 7},   // diamond
    {265, 0, 10, 4, 8},  // iron_ingot
    {266, 0, 15, 2, 7},  // gold_ingot
    {388, 0, 2, 2, 6},   // emerald
    {458, 0, 5, 1, 10},  // beetroot_seeds
    {329, 0, 3, 0, 0},   // saddle
    {0, 0, 1, 0, 0},     // copper_horse_armor (absent in 0.14)
    {0, 0, 1, 0, 0},     // iron_horse_armor (absent)
    {0, 0, 1, 0, 0},     // golden_horse_armor (absent)
    {0, 0, 1, 0, 0},     // diamond_horse_armor (absent)
    {276, 0, 3, 0, 0},   // diamond_sword
    {0, 0, 3, 0, 0},     // diamond_spear (absent)
    {313, 0, 3, 0, 0},   // diamond_boots
    {311, 0, 3, 0, 0},   // diamond_chestplate
    {312, 0, 3, 0, 0},   // diamond_leggings
    {310, 0, 3, 0, 0},   // diamond_helmet
    {278, 0, 3, 0, 0},   // diamond_pickaxe
    {277, 0, 3, 0, 0},   // diamond_shovel
    {267, 0, 3, 0, 0},   // iron_sword
    {309, 0, 3, 0, 0},   // iron_boots
    {307, 0, 3, 0, 0},   // iron_chestplate
    {308, 0, 3, 0, 0},   // iron_leggings
    {306, 0, 3, 0, 0},   // iron_helmet
    {257, 0, 3, 0, 0},   // iron_pickaxe
    {256, 0, 3, 0, 0},   // iron_shovel
};

struct Lcg {
  int seed;
  int next(int bound) {
    seed = static_cast<int>((static_cast<std::int64_t>(seed) * 1103515245LL + 12345) & 0x7fffffffLL);
    return (seed >> 8) % std::max(1, bound);
  }
};
} // namespace

std::vector<LootItem> endCityLoot(std::int64_t seed, int x, int y, int z) {
  // PHP: $hash = (int) hexdec(substr(hash('sha256', seed:x:y:z), 0, 7))
  std::string s = std::to_string(seed) + ":" + std::to_string(x) + ":" + std::to_string(y) + ":" +
                  std::to_string(z);
  auto raw = util::sha256(s);
  char hex[65];
  for (int i = 0; i < 32; ++i) std::snprintf(hex + i * 2, 3, "%02x", (unsigned char)raw[i]);
  hex[64] = 0;
  int hseed = static_cast<int>(std::stoul(std::string(hex, 7), nullptr, 16) & 0x7fffffff);

  Lcg lcg{hseed};
  // pool rolls: uniform 2..6
  int rolls = 2 + lcg.next(5);
  int total = 0;
  for (const auto& e : POOL) total += e.weight;
  std::vector<LootItem> out;
  for (int i = 0; i < rolls && static_cast<int>(out.size()) < 27; ++i) {
    int choice = lcg.next(total);
    const Entry* picked = nullptr;
    for (const auto& e : POOL) {
      choice -= e.weight;
      if (choice < 0) {
        picked = &e;
        break;
      }
    }
    if (!picked) continue;
    if (picked->id == 0) continue; // unmapped modern item: roll consumed, no item
    int count = 1;
    if (picked->count_max > 0) count = picked->count_min + lcg.next(picked->count_max - picked->count_min + 1);
    // merge into an existing stack of the same id (PHP keeps separate slots; merge for compactness)
    bool merged = false;
    for (auto& it : out)
      if (it.id == picked->id) {
        it.count = std::min(64, it.count + count);
        merged = true;
        break;
      }
    if (!merged) out.push_back({picked->id, picked->meta, count});
  }
  return out;
}

} // namespace mpmpes::level::structure
