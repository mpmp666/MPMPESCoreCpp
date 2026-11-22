#pragma once
// Recipe registry + native CraftingData builder, loaded from the generated
// RecipesData.hpp (PocketMine recipes.json). Mirrors CraftingManager.

#include "mpmpes/item/Item.hpp"
#include "mpmpes/item/RecipesData.hpp"

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace mpmpes::item {

struct RecipeEntry {
  int type = 0; // 0 shapeless, 1 shaped, 2 furnace, 3 furnace data
  int w = 0, h = 0;
  ItemStack result;
  std::vector<ItemStack> ings; // shaped: native wire order (z<w, x<h => z*h+x)
  std::vector<ItemStack> grid; // shaped: real grid, row-major h*w (air-filled)
  std::int32_t furnace_input = 0;
  std::array<std::uint8_t, 16> uuid{};
};

class RecipeRegistry {
public:
  const std::vector<RecipeEntry>& entries() const { return entries_; }
  const RecipeEntry* findByUuid(const std::array<std::uint8_t, 16>& uuid) const;
  const RecipeEntry* findFurnace(std::int16_t id, std::int16_t damage) const;

  // Native 0.14 CraftingData buffers
  const std::string& nativeFull() const { return native_full_; }
  const std::string& nativeOld() const { return native_old_; }   // 0.13
  const std::string& native012() const { return native_012_; }   // 0.12

  // Validate a craft against inputs (shape or multiset) and consume.
  // Returns result stack if craftable.
  std::optional<ItemStack> tryCraft(const RecipeEntry& recipe,
                                    const std::vector<ItemStack>& input,
                                    std::vector<ItemStack>& inventory);

  static RecipeRegistry& instance() {
    static RecipeRegistry r;
    return r;
  }

private:
  RecipeRegistry();
  void buildNative();
  std::vector<RecipeEntry> entries_;
  std::unordered_map<std::string, const RecipeEntry*> by_uuid_;
  std::unordered_map<std::int64_t, const RecipeEntry*> furnace_;
  std::string native_full_, native_old_, native_012_;
};

} // namespace mpmpes::item
