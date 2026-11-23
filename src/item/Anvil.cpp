#include "mpmpes/item/Anvil.hpp"

#include "mpmpes/item/Enchantments.hpp"
#include "mpmpes/nbt/Tag.hpp"

#include <algorithm>
#include <cstdio>
#include <cmath>

namespace mpmpes::item {

namespace {
nbt::TagPtr readNbt(const ItemStack& item) {
  if (item.nbt.empty()) return nullptr;
  auto t = nbt::read(item.nbt, nbt::Endian::Little);
  if (!t || t->type != nbt::TAG_Compound) return nullptr;
  return t;
}
ItemStack writeNbt(ItemStack item, const nbt::TagPtr& tag) {
  item.nbt = nbt::write(*tag, "", nbt::Endian::Little);
  return item;
}
bool isRepairMaterial(const ItemStack& input, const ItemStack& addition) {
  int id = input.id;
  // armor tier repair material (PHP isRepairMaterial)
  switch (id) {
    case 298: case 299: case 300: case 301: return addition.id == 334; // leather
    case 302: case 303: case 304: case 305: return addition.id == 265; // chain->iron (PHP: iron)
    case 306: case 307: case 308: case 309: return addition.id == 265; // iron
    case 310: case 311: case 312: case 313: return addition.id == 264; // diamond
    case 314: case 315: case 316: case 317: return addition.id == 266; // gold
  }
  // tools by tier material
  switch (id) {
    case 268: case 269: case 270: case 271: case 290: return addition.id == 5;   // wood->planks
    case 272: case 273: case 274: case 275: case 291: return addition.id == 4;   // stone->cobble
    case 256: case 257: case 258: case 259: case 267: return addition.id == 265; // iron
    case 276: case 277: case 278: case 279: case 293: return addition.id == 264; // diamond
    case 280: case 281: case 282: case 283: case 294: return addition.id == 266; // gold
  }
  return false;
}
bool canEnchantItemId(int item_id, int enchant_id) {
  const EnchantDef* d = enchantDef(enchant_id);
  if (!d) return false;
  return canEnchantItemTable(item_id, *d);
}
} // namespace

int getRepairCost(const ItemStack& item) {
  auto t = readNbt(item);
  if (!t) return 0;
  return std::max(0, static_cast<int>(t->getInt("RepairCost", 0)));
}

ItemStack setRepairCost(const ItemStack& item, int cost) {
  auto t = readNbt(item);
  if (!t) t = nbt::Tag::Compound();
  t->set("RepairCost", nbt::Tag::Int(cost));
  return writeNbt(item, t);
}

int increasedRepairCost(int cost) { return static_cast<int>(std::min<std::int64_t>(std::max(0, cost) * 2LL + 1, 0x7fffffff)); }

std::string getCustomName(const ItemStack& item) {
  auto t = readNbt(item);
  if (!t) return "";
  return t->getString("customName", "");
}
bool hasCustomName(const ItemStack& item) { return !getCustomName(item).empty(); }
ItemStack setCustomName(ItemStack item, const std::string& name) {
  auto t = readNbt(item);
  if (!t) t = nbt::Tag::Compound();
  if (name.empty()) t->remove("customName");
  else t->set("customName", nbt::Tag::String(name));
  return writeNbt(item, t);
}

std::optional<AnvilResult> anvilCombine(const ItemStack& input, const ItemStack& addition,
                                        const std::string* custom_name, bool creative) {
  if (input.empty() || input.count != 1) return std::nullopt;
  ItemStack result = input;
  int price = 0, consumed = 0, renameCost = 0;
  int tax = getRepairCost(input) + getRepairCost(addition);
  int priorWork = std::max(getRepairCost(input), getRepairCost(addition));
  bool usingBook = addition.id == 403; // ENCHANTED_BOOK
  auto enchants = getEnchantments(input.nbt);

  const int maxDamage = maxDurability(input.id);
  if (!addition.empty()) {
    if (maxDamage > 0 && isRepairMaterial(input, addition)) {
      int damage = std::max(0, static_cast<int>(input.damage));
      int unit = maxDamage / 4;
      if (damage <= 0 || unit <= 0) return std::nullopt;
      while (damage > 0 && consumed < addition.count) {
        damage = std::max(0, damage - unit);
        ++consumed;
        ++price;
      }
      result.damage = static_cast<std::int16_t>(damage);
    } else {
      if (!usingBook && (input.id != addition.id || maxDamage <= 0)) return std::nullopt;
      if (addition.count != 1) return std::nullopt;
      if (!usingBook && maxDamage > 0) {
        int remaining = maxDamage - input.damage + maxDamage - addition.damage + (maxDamage * 12) / 100;
        int damage = std::max(0, maxDamage - remaining);
        if (damage < input.damage) {
          result.damage = static_cast<std::int16_t>(damage);
          price += 2;
        }
      }
      auto incoming = getEnchantments(addition.nbt);
      int accepted = 0, rejected = 0;
      for (const auto& ie : incoming) {
        if (ie.id < 0 || ie.level <= 0) { ++rejected; continue; }
        int current = 0;
        int index = -1;
        for (std::size_t i = 0; i < enchants.size(); ++i)
          if (enchants[i].id == ie.id) { current = enchants[i].level; index = static_cast<int>(i); break; }
        const EnchantDef* def = enchantDef(ie.id);
        if (!def) { ++rejected; continue; }
        int level = std::min(def->max_level, current == ie.level ? current + 1 : std::max(current, ie.level));
        bool compatible = creative || input.id == 340 || canEnchantItemId(input.id, ie.id);
        for (const auto& ex : enchants)
          if (ex.id != ie.id) {
            // incompatibility check (same exclusive group)
            static const int excl[][4] = {{0, 1, 3, 4}, {9, 10, 11, -1}, {16, 18, -1, -1}};
            for (auto& g : excl) {
              bool ai = false, bi = false;
              for (int k = 0; k < 4 && g[k] >= 0; ++k) {
                if (g[k] == ex.id) ai = true;
                if (g[k] == ie.id) bi = true;
              }
              if (ai && bi) { compatible = false; ++price; }
            }
          }
        if (!compatible) { ++rejected; continue; }
        EnchantEntry updated{ie.id, level};
        if (index < 0) enchants.push_back(updated);
        else enchants[static_cast<std::size_t>(index)] = updated;
        int fee = def->anvil_cost;
        if (usingBook) fee = std::max(1, fee / 2);
        price += fee * level;
        ++accepted;
      }
      if (rejected > 0 && accepted == 0) return std::nullopt;
      consumed = 1;
    }
  }

  if (custom_name != nullptr) {
    std::string name = *custom_name;
    if (name.size() > 50) return std::nullopt;
    if (name.find_first_not_of(" \t\r\n") == std::string::npos) {
      if (hasCustomName(input)) {
        result = setCustomName(result, "");
        renameCost = 1;
        ++price;
      }
    } else if (name != getCustomName(input)) {
      result = setCustomName(result, name);
      renameCost = 1;
      ++price;
    }
  }

  if (price <= 0) return std::nullopt;
  bool onlyRename = renameCost > 0 && renameCost == price;
  long long cost = std::min<long long>(0x7fffffff, static_cast<long long>(tax) + price);
  if (onlyRename) {
    cost = std::min<long long>(39, cost);
    consumed = 0;
  }
  if (cost >= 40 && !creative) return std::nullopt;
  if (!onlyRename)
    result = setRepairCost(result, increasedRepairCost(priorWork));
  else if (priorWork > 0)
    result = setRepairCost(result, priorWork);

  // apply enchantments to result NBT
  std::string nbt = result.nbt;
  // rebuild ench list: clear then set each
  {
    auto t = readNbt(result);
    if (!t) t = nbt::Tag::Compound();
    t->remove("ench");
    nbt = nbt::write(*t, "", nbt::Endian::Little);
  }
  for (const auto& e : enchants) nbt = setEnchantment(nbt, e.id, e.level);
  result.nbt = nbt;
  // enchanted book special: a plain book with enchants becomes ENCHANTED_BOOK (403)
  if (result.id == 340 && !enchants.empty()) result.id = 403;

  return AnvilResult{result, static_cast<int>(cost), consumed, onlyRename};
}

} // namespace mpmpes::item
