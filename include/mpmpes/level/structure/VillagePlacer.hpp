#pragma once
#include "mpmpes/level/Chunk.hpp"
#include "mpmpes/util/Sha256.hpp"
#include <cstdint>
namespace mpmpes::level::structure {
// NaturalStructures::start village placement: anchor per region, biome/height validation,
// jigsaw pieces placed into the chunk (surface mode).
void placeVillageIntoChunk(level::Chunk& chunk, std::int64_t seed, int regionX, int regionZ);
}
