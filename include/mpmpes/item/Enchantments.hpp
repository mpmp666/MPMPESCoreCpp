#pragma once
// Enchantments — port of pocketmine\item\enchantment\Enchantment (DATA table).

#include <cstdint>
#include <optional>
#include <string_view>
#include <vector>

namespace mpmpes::item {

struct EnchantDef {
  int id;
  const char* name;
  int max_level;
  int weight;     // rarity weight
  int min_lo, min_step_lo, max_lo, max_step_hi; // enchantability range params
  std::uint32_t slots;   // supported (command/anvil)
  std::uint32_t primary; // enchanting-table primary
};

const EnchantDef* enchantDef(int id);
const EnchantDef* enchantByName(std::string_view name);
int enchantMaxLevel(int id);

// Item ench NBT (tag "ench": list of {id short, lvl short})
struct EnchantEntry {
  int id;
  int level;
};
std::vector<EnchantEntry> getEnchantments(std::string_view item_nbt_le);
std::string setEnchantment(std::string_view item_nbt_le, int id, int level); // returns new LE NBT
int enchantLevel(std::string_view item_nbt_le, int id);

} // namespace mpmpes::item
