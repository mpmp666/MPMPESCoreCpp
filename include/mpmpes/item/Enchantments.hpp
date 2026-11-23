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
  int anvil_cost; // DATA[7] — anvil combine cost per level
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

// Enchanting-table offer generation (EnchantmentLevelTable::generateOffers)
int getEnchantAbility(int item_id, int meta);
struct EnchantOffer {
  std::vector<EnchantEntry> enchantments;
  int cost; // level cost shown on the button (option+1 resource cost is separate)
};
// 3 offers for an item + bookshelf count with a per-player seed
std::vector<EnchantOffer> generateEnchantOffers(int item_id, int meta, int bookshelves, std::int64_t seed);
// supported-by-item check for an enchantment on the enchanting table (primary slot)
bool canEnchantItemTable(int item_id, const EnchantDef& def);

} // namespace mpmpes::item
