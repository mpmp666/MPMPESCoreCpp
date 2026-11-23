#include "mpmpes/level/structure/VillagePlacer.hpp"
#include "mpmpes/level/structure/Jigsaw.hpp"
#include "mpmpes/level/structure/Repository.hpp"
#include "mpmpes/level/structure/StructurePlacer.hpp"
#include "mpmpes/util/Sha256.hpp"
#include <cmath>
#include <unordered_map>

namespace mpmpes::level::gen { int surfaceHeightAt(std::int64_t, int, int); int pickBiomeOverworld(std::int64_t, int, int); }

namespace mpmpes::level::structure {
namespace {
constexpr int SPACING = 28, SEPARATION = 10;
struct VillagePlan { bool valid = false; int x = 0, y = 0, z = 0; std::vector<Piece> pieces; };
std::unordered_map<std::int64_t, VillagePlan> g_plans;

int shaNum(const std::string& s) {
  auto raw = util::sha256(s);
  char hex[65];
  for (int i = 0; i < 32; ++i) std::snprintf(hex + i * 2, 3, "%02x", (unsigned char)raw[i]);
  hex[64] = 0;
  return static_cast<int>(std::stoul(std::string(hex, 7), nullptr, 16) & 0x7fffffff);
}

const VillagePlan& planFor(std::int64_t seed, int rx, int rz) {
  std::int64_t key = (static_cast<std::int64_t>(rx) << 32) ^ (rz & 0xffffffffLL);
  auto it = g_plans.find(key);
  if (it != g_plans.end()) return it->second;
  VillagePlan plan;
  int hash = shaNum(std::to_string(seed) + ":village:" + std::to_string(rx) + ":" + std::to_string(rz));
  int span = SPACING - SEPARATION;
  int cx = rx * SPACING + hash % span;
  int cz = rz * SPACING + (hash / 97) % span;
  int x = cx * 16 + 8, z = cz * 16 + 8;
  // terrain sample: height + biome + water(62)
  int height = gen::surfaceHeightAt(seed, x, z);
  int biome = gen::pickBiomeOverworld(seed, x, z);
  const int WATER = 62;
  bool valid = (biome == 1 || biome == 2 || biome == 5 || biome == 12) &&
               height >= 61 && WATER <= height && height <= 100;
  if (valid) {
    std::string variant = biome == 2 ? "desert" : biome == 5 ? "taiga" : biome == 12 ? "snowy"
                          : (hash % 5 == 0 ? "savanna" : "plains");
    JigsawLayout layout(hash);
    std::string pool = "village/" + variant + "/town_centers";
    plan.pieces = layout.assemble("village", pool, variant);
    if (!plan.pieces.empty()) {
      plan.valid = true;
      plan.x = x;
      plan.z = z;
      plan.y = height + 1 - StructureRepository::groundLevel(plan.pieces[0].templ);
    }
  }
  return g_plans[key] = plan;
}
} // namespace

void placeVillageIntoChunk(level::Chunk& chunk, std::int64_t seed, int regionX, int regionZ) {
  const auto& plan = planFor(seed, regionX, regionZ);
  if (!plan.valid) return;
  // village reaches up to MAX_SPAN 112 blocks (7 chunks); placePieces clips per-piece
  const int reach = 8;
  if (std::abs(plan.x - (chunk.x() * 16 + 8)) > reach * 16 || std::abs(plan.z - (chunk.z() * 16 + 8)) > reach * 16)
    return;
  placePieces(chunk, plan.pieces, plan.x, plan.y, plan.z, true);
}

} // namespace mpmpes::level::structure
