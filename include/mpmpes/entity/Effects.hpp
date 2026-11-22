#pragma once
// Potion effects — port of pocketmine\entity\Effect (ids, tick cadence, attribute hooks).

#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>

namespace mpmpes::entity {

struct EffectDef {
  int id;
  const char* name;
  bool bad;
};

const EffectDef* effectDef(int id);
const EffectDef* effectByName(std::string_view name);

struct ActiveEffect {
  int id = 0;
  int amplifier = 0;
  int duration = 0; // ticks remaining
  bool particles = true;
};

// Tick cadence (PHP Effect::canTick): returns true when this effect applies this tick
bool effectShouldApply(int id, int amplifier, int duration_left);

} // namespace mpmpes::entity
