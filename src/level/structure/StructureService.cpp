#include "mpmpes/level/structure/StructureService.hpp"
#include "mpmpes/level/structure/Jigsaw.hpp"
#include "mpmpes/level/structure/EndCityLayout.hpp"
#include "mpmpes/level/structure/Repository.hpp"
#include "mpmpes/level/structure/StructurePlacer.hpp"
#include "mpmpes/util/Sha256.hpp"
#include <algorithm>
#include <cmath>
#include <unordered_map>
#include <cstdio>

namespace mpmpes::level::gen {
int surfaceHeightAt(std::int64_t, int, int);
int pickBiomeOverworld(std::int64_t, int, int);
int surfaceHeightNether(std::int64_t, int, int);
}

namespace mpmpes::level::structure {
namespace {
struct Def {
  int spacing, separation;
  std::string mode; // surface, ocean, buried, underground, nether, end
  std::vector<int> biomes;
};
// NaturalStructures::definitions (overworld)
const std::vector<std::pair<std::string, Def>> DEFS = {
    {"village", {28, 10, "surface", {1, 2, 5, 12}}},
    {"pillager_outpost", {32, 12, "surface", {1, 2, 4, 5, 12, 27}}},
    {"desert_pyramid", {28, 10, "surface", {2}}},
    {"desert_well", {20, 8, "surface", {2}}},
    {"jungle_temple", {28, 10, "surface", {4, 27}}},
    {"swamp_hut", {28, 10, "surface", {6}}},
    {"igloo", {28, 10, "surface", {5, 12}}},
    {"ruined_portal", {28, 10, "surface", {}}},
    {"shipwreck", {24, 8, "ocean", {0, 7}}},
    {"underwater_ruin", {20, 8, "ocean", {0, 7}}},
    {"ocean_monument", {32, 12, "ocean", {0}}},
    {"buried_treasure", {20, 8, "buried", {0, 7, 2}}},
};

int shaNum(const std::string& s) {
  auto raw = util::sha256(s);
  char hex[65];
  for (int i = 0; i < 32; ++i) std::snprintf(hex + i * 2, 3, "%02x", (unsigned char)raw[i]);
  hex[64] = 0;
  return static_cast<int>(std::stoul(std::string(hex, 7), nullptr, 16) & 0x7fffffff);
}

struct Plan {
  bool valid = false;
  int x = 0, y = 0, z = 0;
  std::vector<Piece> pieces;
  int reach = 8;
  bool foundations = false;
};
std::unordered_map<std::int64_t, Plan> g_plans;

const Plan& planFor(std::int64_t seed, const std::string& family, const Def& def, int rx, int rz) {
  std::int64_t key = ((static_cast<std::int64_t>(rx) << 32) ^ (rz & 0xffffffffLL)) ^
                       (static_cast<std::int64_t>(std::hash<std::string>{}(family)) << 1);
  auto it = g_plans.find(key);
  if (it != g_plans.end()) return it->second;
  Plan plan;
  int hash = shaNum(std::to_string(seed) + ":" + family + ":" + std::to_string(rx) + ":" + std::to_string(rz));
  int span = def.spacing - def.separation;
  int cx = rx * def.spacing + hash % span;
  int cz = rz * def.spacing + (hash / 97) % span;
  int x = cx * 16 + 8, z = cz * 16 + 8;
  int height = gen::surfaceHeightAt(seed, x, z);
  int biome = gen::pickBiomeOverworld(seed, x, z);
  const int WATER = 62;
  bool valid = def.biomes.empty() || std::find(def.biomes.begin(), def.biomes.end(), biome) != def.biomes.end();
  if (def.mode == "surface" && (height < 61 || WATER > height || height > 100)) valid = false;
  if (def.mode == "ocean" && (WATER < 55 || height > WATER - 5)) valid = false;
  if (valid) {
    std::string variant = family == "village"
        ? (biome == 2 ? "desert" : biome == 5 ? "taiga" : biome == 12 ? "snowy" : (hash % 5 == 0 ? "savanna" : "plains"))
        : "";
    JigsawLayout layout(hash);
    std::string pool;
    if (family == "village") pool = "village/" + variant + "/town_centers";
    else if (family == "pillager_outpost") pool = "pillager_outpost/base_plates";
    else if (family == "ancient_city") pool = "ancient_city/city_center";
    else if (family == "trail_ruins") pool = "trail_ruins/tower";
    if (family == "igloo") plan.pieces = layout.igloo(variant);
    else if (!pool.empty()) plan.pieces = layout.assemble(family, pool, variant);
    else plan.pieces = layout.standalone(family, variant);
    if (!plan.pieces.empty()) {
      plan.valid = true;
      plan.x = x;
      plan.z = z;
      plan.foundations = (def.mode == "surface");
      if (def.mode == "ocean")
        plan.y = std::max(4, height + 1) - 0 - StructureRepository::groundLevel(plan.pieces[0].templ);
      else
        plan.y = height + 1 - StructureRepository::groundLevel(plan.pieces[0].templ);
    }
  }
  return g_plans[key] = plan;
}
} // namespace

void placeStructuresIntoChunk(level::Chunk& chunk, std::int64_t seed, int dimension, int chunkX,
                              int chunkZ) {
  if (dimension == 2) return; // end handled by EndGenerator's city placement
  if (dimension != 0) return; // nether structures TODO
  for (const auto& [family, def] : DEFS) {
    const int reach = 8;
    auto floordiv = [](int a, int b) { return a >= 0 ? a / b : -((-a + b - 1) / b); };
    const int minRX = floordiv(chunkX - reach, def.spacing) - 1;
    const int maxRX = floordiv(chunkX + reach, def.spacing);
    const int minRZ = floordiv(chunkZ - reach, def.spacing) - 1;
    const int maxRZ = floordiv(chunkZ + reach, def.spacing);
    for (int rx = minRX; rx <= maxRX; ++rx)
      for (int rz = minRZ; rz <= maxRZ; ++rz) {
        const auto& plan = planFor(seed, family, def, rx, rz);
        if (!plan.valid) continue;
        if (std::abs(plan.x - (chunkX * 16 + 8)) > reach * 16 ||
            std::abs(plan.z - (chunkZ * 16 + 8)) > reach * 16)
          continue;
        placePieces(chunk, plan.pieces, plan.x, plan.y, plan.z, plan.foundations);
      }
  }
}

} // namespace mpmpes::level::structure
