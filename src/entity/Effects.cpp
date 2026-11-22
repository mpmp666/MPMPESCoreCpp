#include "mpmpes/entity/Effects.hpp"

#include <algorithm>
#include <cctype>

namespace mpmpes::entity {

namespace {
const EffectDef DEFS[] = {
    {1, "speed", false},          {2, "slowness", true},   {3, "haste", false},
    {4, "fatigue", true},         {5, "strength", false},  {6, "healing", false},
    {7, "harming", true},         {8, "jump", false},      {9, "nausea", true},
    {10, "regeneration", false},  {11, "resistance", false}, {12, "fire_resistance", false},
    {13, "water_breathing", false}, {14, "invisibility", false}, {15, "blindness", true},
    {16, "night_vision", false},  {17, "hunger", true},    {18, "weakness", true},
    {19, "poison", true},         {20, "wither", true},    {21, "health_boost", false},
    {22, "absorption", false},    {23, "saturation", false},
};
const std::pair<const char*, int> ALIASES[] = {
    {"swiftness", 3}, {"mining_fatigue", 4}, {"confusion", 9}, {"damage_resistance", 11},
    {"strength", 5}, {"jumpboost", 8}, {"jump_boost", 8}, {"fireresistance", 12},
    {"waterbreathing", 13}, {"nightvision", 16}, {"healthboost", 21},
};
} // namespace

const EffectDef* effectDef(int id) {
  for (const auto& d : DEFS)
    if (d.id == id) return &d;
  return nullptr;
}

const EffectDef* effectByName(std::string_view name) {
  std::string n(name);
  std::transform(n.begin(), n.end(), n.begin(), [](unsigned char c) { return std::tolower(c); });
  std::replace(n.begin(), n.end(), ' ', '_');
  for (const auto& d : DEFS)
    if (n == d.name) return &d;
  for (const auto& [alias, id] : ALIASES)
    if (n == alias) return effectDef(id);
  // numeric id?
  try {
    int id = std::stoi(n);
    return effectDef(id);
  } catch (...) {
    return nullptr;
  }
}

bool effectShouldApply(int id, int amplifier, int duration_left) {
  if (amplifier < 0) amplifier = 0;
  auto interval = [&](int base) { return base >> amplifier; };
  switch (id) {
    case 19: { // poison
      int i = interval(25);
      return i > 0 ? (duration_left % i) == 0 : true;
    }
    case 20: { // wither
      int i = interval(50);
      return i > 0 ? (duration_left % i) == 0 : true;
    }
    case 10: // regeneration
    case 17: { // hunger
      int i = interval(40);
      return i > 0 ? (duration_left % i) == 0 : true;
    }
    case 23: { // saturation
      int i = interval(20);
      return i > 0 ? (duration_left % i) == 0 : true;
    }
    case 6: // healing
    case 7: // harming
      return true;
    default:
      return false;
  }
}

} // namespace mpmpes::entity
