#include "mpmpes/item/Brewing.hpp"

namespace mpmpes::item {
namespace {
// potion meta: WATER_BOTTLE 0, AWKWARD 4, THICK 3, MUNDANE_EXTENDED 2, MUNDANE 1, ...
// {input_meta, ingredient_id, output_meta}
const BrewRecipe RECIPES[] = {
    {0, 372, 4},    // water + nether_wart -> awkward
    {4, 353, 14},   // awkward + sugar -> speed
    {14, 331, 15},  // speed + redstone -> speed_T
    {14, 348, 16},  // speed + glowstone -> speed_II
    {0, 348, 3},    // water + glowstone -> thick
    {0, 331, 2},    // water + redstone -> mundane_extended
    {0, 376, 34},   // water + fermented_eye -> weakness
    {0, 370, 1},    // water + ghast_tear -> mundane
    {0, 382, 1}, {0, 377, 1}, {0, 378, 1}, {0, 353, 1}, {0, 375, 1}, {0, 414, 1}, // water + X -> mundane
    {1, 376, 34},   // mundane + fse -> weakness
    {3, 376, 34},   // thick + fse -> weakness
    {2, 376, 35},   // mundane_ext + fse -> weakness_T
    {34, 331, 35},  // weakness + redstone -> weakness_T
    {4, 370, 28},   // awkward + ghast_tear -> regeneration
    {28, 331, 29},  // regen + redstone
    {28, 348, 30},  // regen + glowstone
    {4, 377, 31},   // awkward + blaze_powder -> strength
    {31, 331, 32}, {31, 348, 33},
    {4, 375, 25},   // awkward + spider_eye -> poison
    {25, 331, 26}, {25, 348, 27},
    {4, 382, 21},   // awkward + glistering_melon -> healing
    {21, 348, 22},
    {4, 462, 19},   // awkward + puffer_fish -> water_breathing
    {19, 331, 20},
    {19, 376, 23},  // water_breathing + fse -> harming
    {21, 376, 23}, {25, 376, 23}, // healing/poison + fse -> harming
    {23, 348, 24},  // harming + glowstone -> harming_II
    {22, 376, 24}, {26, 376, 24},
    {4, 378, 12},   // awkward + magma_cream -> fire_resistance
    {12, 331, 13},
    {4, 414, 9},    // awkward + rabbit_foot -> leaping
    {9, 331, 10}, {9, 348, 11},
    {12, 376, 17}, {14, 376, 17}, {9, 376, 17}, // fire/speed/leaping + fse -> slowness
    {13, 376, 18}, {10, 376, 18}, {15, 376, 18},
    {17, 331, 18},  // slowness + redstone -> slowness_T
    {4, 396, 5},    // awkward + golden_carrot -> night_vision
    {5, 331, 6},
    {5, 376, 7},    // night_vision + fse -> invisibility
    {7, 331, 8},    // invisibility + redstone
    {6, 376, 8},    // night_vision_T + fse -> invisibility_T
};
}
int brewResult(std::int16_t input_meta, std::int16_t ingredient) {
  for (const auto& r : RECIPES)
    if (r.input_meta == input_meta && r.ingredient == ingredient) return r.output_meta;
  return -1;
}
} // namespace mpmpes::item
