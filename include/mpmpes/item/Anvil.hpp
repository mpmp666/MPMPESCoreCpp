#pragma once
// Anvil combine — AnvilInventory::calculateResult (26.2 AnvilMenu.createResult).

#include "mpmpes/item/Item.hpp"

#include <optional>
#include <string>

namespace mpmpes::item {

struct AnvilResult {
  ItemStack item;
  int cost;        // levels required
  int consumed;    // addition items consumed
  bool rename_only;
};

// input + addition -> combined result (repair material, item+item, enchant merge, rename).
// Returns nullopt when the combination is invalid or unaffordable.
std::optional<AnvilResult> anvilCombine(const ItemStack& input, const ItemStack& addition,
                                        const std::string* custom_name, bool creative);

// RepairCost NBT helpers (prior-work penalty)
int getRepairCost(const ItemStack& item);
ItemStack setRepairCost(const ItemStack& item, int cost);
int increasedRepairCost(int cost);

// custom name NBT helpers
std::string getCustomName(const ItemStack& item);
ItemStack setCustomName(ItemStack item, const std::string& name);
bool hasCustomName(const ItemStack& item);

} // namespace mpmpes::item
