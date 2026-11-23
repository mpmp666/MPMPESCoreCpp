#pragma once
#include "mpmpes/level/Chunk.hpp"
#include "mpmpes/level/gen/EndGenerator.hpp"
#include <cstdint>
#include <string>
namespace mpmpes::level::structure {
// NaturalStructures::start/forChunk — all structure families, one placement service.
// dimension: 0 overworld, 1 nether, 2 end
void placeStructuresIntoChunk(level::Chunk& chunk, std::int64_t seed, int dimension, int chunkX, int chunkZ);
}
