#pragma once
#include "mpmpes/item/Item.hpp"
#include <cstdint>
namespace mpmpes::item {
// Brewing recipe: input potion meta + ingredient id -> output potion meta (PHP CraftingManager brewing)
struct BrewRecipe { std::int16_t input_meta; std::int16_t ingredient; std::int16_t output_meta; };
// returns output meta or -1
int brewResult(std::int16_t input_meta, std::int16_t ingredient);
}
