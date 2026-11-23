#pragma once
#include "mpmpes/level/Chunk.hpp"
#include "mpmpes/level/gen/EndGenerator.hpp"
#include "mpmpes/level/structure/Repository.hpp"
#include <functional>
#include <vector>
namespace mpmpes::level::structure {
// Write pieces into a chunk with rotation (StructurePlacer::chunk)
int placePieces(level::Chunk& chunk, const std::vector<Piece>& pieces, int originX, int originY,
                int originZ, bool foundations);
struct CityAnchor { int x, z, chunkX, chunkZ; std::int64_t placement_seed; };
CityAnchor cityCandidate(std::int64_t seed, int rx, int rz);
// End city: candidate anchor + plan + place (EndCityGenerator)
void placeCityIntoChunk(level::Chunk& chunk, std::int64_t seed, int regionX, int regionZ,
                        gen::EndTerrainNoise& terrain,
                        const std::function<int(int, int)>& surfaceHeight);
}
