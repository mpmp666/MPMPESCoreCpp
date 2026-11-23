// End city placement — EndCityGenerator (candidate/plan) + StructurePlacer::chunk.
#include "mpmpes/level/Chunk.hpp"
#include "mpmpes/level/structure/StructurePlacer.hpp"
#include "mpmpes/level/structure/EndCityLayout.hpp"
#include "mpmpes/level/structure/Repository.hpp"

#include <cmath>
#include <algorithm>
#include <functional>
#include <map>
#include <unordered_map>

namespace mpmpes::level::structure {

std::vector<PendingLootChest> g_pending_loot_chests;

int placePieces(level::Chunk& chunk, const std::vector<Piece>& pieces, int originX, int originY,
                int originZ, bool foundations);

namespace {
constexpr int SPACING = 20, SEPARATION = 11;
constexpr std::int64_t SALT = 10387313;
} // namespace

// EndCityGenerator::candidate
CityAnchor cityCandidate(std::int64_t seed, int rx, int rz) {
  std::int64_t pseed = gen::JavaRandom::addLong(
      gen::JavaRandom::addLong(gen::JavaRandom::multiplyLong(rx, 341873128712LL),
                               gen::JavaRandom::multiplyLong(rz, 132897987541LL)),
      gen::JavaRandom::addLong(seed, SALT));
  gen::JavaRandom random(pseed);
  int chunkX = rx * SPACING + (random.nextInt(9) + random.nextInt(9)) / 2;
  int chunkZ = rz * SPACING + (random.nextInt(9) + random.nextInt(9)) / 2;
  return {chunkX * 16 + 7, chunkZ * 16 + 7, chunkX, chunkZ, pseed};
}

// Write a city's pieces that intersect this chunk. surfaceHeight: (x,z)->top solid y.
namespace {
struct CityPlan {
  bool valid = false;
  int x = 0, y = 0, z = 0;
  std::vector<Piece> pieces;
};
std::unordered_map<std::int64_t, CityPlan> g_city_plans;
} // namespace

void placeCityIntoChunk(level::Chunk& chunk, std::int64_t seed, int regionX, int regionZ,
                        gen::EndTerrainNoise& terrain,
                        const std::function<int(int, int)>& surfaceHeight) {
  const std::int64_t rkey = (static_cast<std::int64_t>(regionX) << 32) ^ (regionZ & 0xffffffffLL);
  auto cit = g_city_plans.find(rkey);
  if (cit != g_city_plans.end()) {
    if (!cit->second.valid) return;
    placePieces(chunk, cit->second.pieces, cit->second.x, cit->second.y, cit->second.z, false);
    return;
  }
  auto anchor = cityCandidate(seed, regionX, regionZ);
  // PHP endForChunk: skip anchors > 18 chunks away (reach = ceil(272/16)+1 = 18)
  if (std::abs(anchor.chunkX - chunk.x()) > 18 || std::abs(anchor.chunkZ - chunk.z()) > 18) return;
  // plan(): source seed + rotation
  gen::JavaRandom random0(seed);
  std::int64_t xScale = random0.nextLong();
  std::int64_t zScale = random0.nextLong();
  std::int64_t sourceSeed = gen::JavaRandom::multiplyLong(anchor.chunkX, xScale) ^
                            gen::JavaRandom::multiplyLong(anchor.chunkZ, zScale) ^ seed;
  gen::JavaRandom random(sourceSeed);
  int rotation = random.nextInt(4);
  int dx = (rotation == 1 || rotation == 2) ? -5 : 5;
  int dz = (rotation == 2 || rotation == 3) ? -5 : 5;
  int y = std::min(std::initializer_list<int>{surfaceHeight(anchor.x, anchor.z) - 1,
                    surfaceHeight(anchor.x, anchor.z + dz) - 1,
                    surfaceHeight(anchor.x + dx, anchor.z) - 1,
                    surfaceHeight(anchor.x + dx, anchor.z + dz) - 1});
  if (y < 60) { g_city_plans[rkey] = CityPlan{}; return; }
  // biome eligibility (cities only in end_highlands)
  if (terrain.biome(anchor.x, anchor.z) != "end_highlands") { g_city_plans[rkey] = CityPlan{}; return; }
  EndCityLayout layout(random);
  auto pieces = layout.buildAtRotation(rotation);
  CityPlan plan;
  plan.valid = true;
  plan.x = anchor.x;
  plan.y = y;
  plan.z = anchor.z;
  plan.pieces = pieces;
  g_city_plans[rkey] = plan;
  placePieces(chunk, plan.pieces, plan.x, plan.y, plan.z, false);
}

// StructurePlacer::chunk — write pieces into the chunk with rotation, tiles, foundations.
int placePieces(level::Chunk& chunk, const std::vector<Piece>& pieces, int originX, int originY,
                int originZ, bool foundations) {
  const int minX = chunk.x() * 16, minZ = chunk.z() * 16;
  int count = 0;
  for (const auto& piece : pieces) {
    int size[3];
    if (!StructureRepository::sizeOf(piece.templ, size)) continue;
    int rotation = piece.rotation & 3;
    int px = originX + piece.x, py = originY + piece.y, pz = originZ + piece.z;
    auto [ddx, ddz] = StructureRepository::rotate(size[0] - 1, size[2] - 1, rotation);
    if (px + std::max(0, ddx) < minX || px + std::min(0, ddx) > minX + 15 ||
        pz + std::max(0, ddz) < minZ || pz + std::min(0, ddz) > minZ + 15)
      continue;
    const Template* templ = StructureRepository::get(piece.templ);
    if (!templ) continue;
    const std::string& data = templ->blocks;
    // foundation columns: lowest solid block per (x,z) for ground-fill (PHP StructurePlacer)
    std::map<int, std::pair<int, int>> foundationColumns; // (x<<4|z) -> (y, id)
    for (std::size_t off = 0; off + 8 <= data.size(); off += 8) {
      int rx = (std::uint8_t)data[off] | ((std::uint8_t)data[off + 1] << 8);
      int ry = (std::uint8_t)data[off + 2] | ((std::uint8_t)data[off + 3] << 8);
      int rz = (std::uint8_t)data[off + 4] | ((std::uint8_t)data[off + 5] << 8);
      int pi = (std::uint8_t)data[off + 6] | ((std::uint8_t)data[off + 7] << 8);
      auto [bx, bz] = StructureRepository::rotate(rx, rz, rotation);
      int lx = bx + px - minX;
      int lz = bz + pz - minZ;
      int by = py + ry;
      if (lx < 0 || lx > 15 || lz < 0 || lz > 15) continue;
      if (by < 0 || by > 127) continue;
      if (pi < 0 || pi >= static_cast<int>(templ->palette.size())) continue;
      const auto& variants = templ->palette[pi];
      if (variants.empty()) continue;
      auto [id, meta] = variants[std::min(rotation, static_cast<int>(variants.size()) - 1)];
      if (id < 0) continue;
      if (id == 0 && piece.ignore_air) continue;
      chunk.setBlock(lx, by, lz, static_cast<std::uint8_t>(id), static_cast<std::uint8_t>(meta));
      ++count;
      if (foundations && piece.y <= 1 && id != 0) {
        static const int skip[] = {8, 9, 10, 11, 18, 30, 31, 37, 38, 50, 51, 78, 85, 106, 171};
        bool skipB = false;
        for (int sid : skip) if (id == sid) { skipB = true; break; }
        if (!skipB) {
          int key = (lx << 4) | lz;
          auto it = foundationColumns.find(key);
          if (it == foundationColumns.end() || by < it->second.first)
            foundationColumns[key] = {by, id};
        }
      }
    }
    // foundation fill (PHP): from lowest block down to terrain (max 18), cobblestone or end_stone
    if (foundations) {
      for (const auto& [key, col] : foundationColumns) {
        int cy = col.first, cid = col.second;
        if (cy > py + 3) continue;
        int fx = key >> 4, fz = key & 15;
        for (int below = cy - 1; below >= std::max(1, cy - 18); --below) {
          auto cur = chunk.getBlockId(fx, below, fz);
          if (cur != 0 && cur != 8 && cur != 9 && cur != 31 && cur != 78) break;
          chunk.setBlock(fx, below, fz, cid == 121 ? 121 : 4, 0);
          ++count;
        }
      }
    }
    // markers: "Chest" markers place a chest block with a loot table (StructurePlacer)
    bool isEndCity = piece.templ.rfind("end_city/", 0) == 0;
    for (const auto& marker : templ->markers) {
      if (!isEndCity) continue;
      const std::string& md = marker.metadata;
      if (md != "Chest" && md != "chest") continue;
      auto [mx, mz] = StructureRepository::rotate(marker.pos[0], marker.pos[2], rotation);
      int wx = px + mx, wy = py + marker.pos[1] - 1, wz = pz + mz; // below the marker
      if ((wx >> 4) != chunk.x() || (wz >> 4) != chunk.z() || wy < 1 || wy > 126) continue;
      int lx = wx & 15, lz = wz & 15;
      auto cur = chunk.getBlockId(lx, wy, lz);
      if (cur != 0 && cur != 54) continue;
      // facing: default north (5), rotated by piece rotation
      static const int facing_map[4] = {5, 3, 4, 2}; // north->east->south->west per rotation
      int meta = facing_map[rotation & 3];
      chunk.setBlock(lx, wy, lz, 54, static_cast<std::uint8_t>(meta));
      g_pending_loot_chests.push_back({wx, wy, wz, "minecraft:chests/end_city_treasure"});
      ++count;
    }
  }
  return count;
}

} // namespace mpmpes::level::structure
