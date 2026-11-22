#include "mpmpes/item/Enchantments.hpp"

#include "mpmpes/nbt/Tag.hpp"

#include <algorithm>
#include <cctype>

namespace mpmpes::item {

namespace {
// [name, max, weight, minLo, minStep, maxLo, maxStep, slots(supported), slots(primary)]
// SLOT_* bit layout matches PHP (none/all/armor/head/torso/legs/feet/sword/bow/...)
constexpr std::uint32_t S_NONE = 0, S_ALL = 0x3fff, S_ARMOR = 0xf, S_HEAD = 1, S_TORSO = 2,
                        S_LEGS = 4, S_FEET = 8, S_SWORD = 0x10, S_BOW = 0x20, S_HOE = 0x40,
                        S_SHEARS = 0x80, S_FLINT = 0x100, S_AXE = 0x200, S_PICKAXE = 0x400,
                        S_SHOVEL = 0x800, S_FISHING_ROD = 0x1000, S_CARROT_STICK = 0x2000;
constexpr std::uint32_t S_DIG = S_AXE | S_PICKAXE | S_SHOVEL; // PHP SLOT_DIG = 0xE00
const EnchantDef DEFS[] = {
    {0, "protection", 4, 10, 1, 11, 12, 11, S_ARMOR, S_ARMOR},
    {1, "fire_protection", 4, 5, 10, 8, 18, 8, S_ARMOR, S_ARMOR},
    {2, "feather_falling", 4, 5, 5, 6, 11, 6, S_FEET, S_FEET},
    {3, "blast_protection", 4, 2, 5, 8, 13, 8, S_ARMOR, S_ARMOR},
    {4, "projectile_protection", 4, 5, 3, 6, 9, 6, S_ARMOR, S_ARMOR},
    {5, "thorns", 3, 1, 10, 20, 60, 20, S_ARMOR, S_TORSO},
    {6, "respiration", 3, 2, 10, 10, 40, 10, S_HEAD, S_HEAD},
    {7, "depth_strider", 3, 2, 10, 10, 25, 10, S_FEET, S_FEET},
    {8, "aqua_affinity", 1, 2, 1, 0, 41, 0, S_HEAD, S_HEAD},
    {9, "sharpness", 5, 10, 1, 11, 21, 11, S_SWORD | S_AXE, S_SWORD},
    {10, "smite", 5, 5, 5, 8, 25, 8, S_SWORD | S_AXE, S_SWORD},
    {11, "bane_of_arthropods", 5, 5, 5, 8, 25, 8, S_SWORD | S_AXE, S_SWORD},
    {12, "knockback", 2, 5, 5, 20, 55, 20, S_SWORD, S_SWORD},
    {13, "fire_aspect", 2, 2, 10, 20, 60, 20, S_SWORD, S_SWORD},
    {14, "looting", 3, 2, 15, 9, 65, 9, S_SWORD, S_SWORD},
    {15, "efficiency", 5, 10, 1, 10, 51, 10, S_DIG | S_HOE | S_SHEARS, S_DIG | S_HOE | S_SHEARS},
    {16, "silk_touch", 1, 1, 15, 0, 65, 0, S_DIG | S_HOE, S_DIG | S_HOE},
    {17, "unbreaking", 3, 5, 5, 8, 55, 8, S_ALL, S_ALL},
    {18, "fortune", 3, 2, 15, 9, 65, 9, S_DIG | S_HOE, S_DIG | S_HOE},
    {19, "power", 5, 10, 1, 10, 16, 10, S_BOW, S_BOW},
    {20, "punch", 2, 2, 12, 20, 37, 20, S_BOW, S_BOW},
    {21, "flame", 1, 2, 20, 0, 50, 0, S_BOW, S_BOW},
    {22, "infinity", 1, 1, 20, 0, 50, 0, S_BOW, S_BOW},
    {23, "luck_of_the_sea", 3, 2, 15, 9, 65, 9, S_FISHING_ROD, S_FISHING_ROD},
    {24, "lure", 3, 2, 15, 9, 65, 9, S_FISHING_ROD, S_FISHING_ROD},
};
// legacy TYPE_ suffixes and common aliases
const std::pair<const char*, int> ALIASES[] = {
    {"armor_protection", 0}, {"protection", 0}, {"fire_protection", 1}, {"fall_protection", 2},
    {"feather_fall", 2}, {"explosion_protection", 3}, {"projectile_protection", 4},
    {"water_breathing", 6}, {"water_speed", 7}, {"water_affinity", 8}, {"bane", 11},
    {"fire_aspect", 13}, {"fireaspect", 13}, {"silk_touch", 16}, {"silktouch", 16},
    {"durability", 17}, {"luck_of_the_sea", 23}, {"luck", 23},
};
} // namespace

const EnchantDef* enchantDef(int id) {
  for (const auto& d : DEFS)
    if (d.id == id) return &d;
  return nullptr;
}

int enchantMaxLevel(int id) {
  auto* d = enchantDef(id);
  return d ? d->max_level : 0;
}

const EnchantDef* enchantByName(std::string_view name) {
  std::string n(name);
  std::transform(n.begin(), n.end(), n.begin(), [](unsigned char c) { return std::tolower(c); });
  if (n.rfind("minecraft:", 0) == 0) n = n.substr(10);
  for (const auto& d : DEFS)
    if (n == d.name) return &d;
  // legacy TYPE_WEAPON_SHARPNESS style
  std::string upper = n;
  std::transform(upper.begin(), upper.end(), upper.begin(), ::toupper);
  static const std::pair<const char*, int> legacy[] = {
      {"TYPE_ARMOR_PROTECTION", 0}, {"TYPE_ARMOR_FIRE_PROTECTION", 1},
      {"TYPE_ARMOR_FALL_PROTECTION", 2}, {"TYPE_ARMOR_EXPLOSION_PROTECTION", 3},
      {"TYPE_ARMOR_PROJECTILE_PROTECTION", 4}, {"TYPE_ARMOR_THORNS", 5},
      {"TYPE_WATER_BREATHING", 6}, {"TYPE_WATER_SPEED", 7}, {"TYPE_WATER_AFFINITY", 8},
      {"TYPE_WEAPON_SHARPNESS", 9}, {"TYPE_WEAPON_SMITE", 10}, {"TYPE_WEAPON_ARTHROPODS", 11},
      {"TYPE_WEAPON_KNOCKBACK", 12}, {"TYPE_WEAPON_FIRE_ASPECT", 13}, {"TYPE_WEAPON_LOOTING", 14},
      {"TYPE_MINING_EFFICIENCY", 15}, {"TYPE_MINING_SILK_TOUCH", 16}, {"TYPE_MINING_DURABILITY", 17},
      {"TYPE_MINING_FORTUNE", 18}, {"TYPE_BOW_POWER", 19}, {"TYPE_BOW_KNOCKBACK", 20},
      {"TYPE_BOW_FLAME", 21}, {"TYPE_BOW_INFINITY", 22}, {"TYPE_FISHING_FORTUNE", 23},
      {"TYPE_FISHING_LURE", 24},
  };
  for (const auto& [k, id] : legacy)
    if (upper == k) return enchantDef(id);
  for (const auto& [k, id] : ALIASES)
    if (n == k) return enchantDef(id);
  try {
    return enchantDef(std::stoi(n));
  } catch (...) {
    return nullptr;
  }
}

std::vector<EnchantEntry> getEnchantments(std::string_view item_nbt_le) {
  std::vector<EnchantEntry> out;
  if (item_nbt_le.empty()) return out;
  auto tag = nbt::read(item_nbt_le, nbt::Endian::Little);
  if (!tag || tag->type != nbt::TAG_Compound) return out;
  auto ench = tag->get("ench");
  if (!ench || ench->type != nbt::TAG_List) return out;
  for (auto& e : ench->list) {
    if (!e || e->type != nbt::TAG_Compound) continue;
    out.push_back({static_cast<int>(e->getInt("id")), static_cast<int>(e->getInt("lvl"))});
  }
  return out;
}

std::string setEnchantment(std::string_view item_nbt_le, int id, int level) {
  nbt::TagPtr tag;
  if (!item_nbt_le.empty()) tag = nbt::read(item_nbt_le, nbt::Endian::Little);
  if (!tag || tag->type != nbt::TAG_Compound) tag = nbt::Tag::Compound();
  auto ench = tag->get("ench");
  if (!ench || ench->type != nbt::TAG_List) {
    ench = nbt::Tag::List(nbt::TAG_Compound);
    tag->set("ench", ench);
  }
  bool found = false;
  for (auto& e : ench->list) {
    if (!e || e->type != nbt::TAG_Compound) continue;
    if (e->getInt("id") == id) {
      if (!found) {
        e->set("id", nbt::Tag::Short(static_cast<std::int16_t>(id)));
        e->set("lvl", nbt::Tag::Short(static_cast<std::int16_t>(level)));
        found = true;
      } else {
        e = nullptr; // drop duplicate
      }
    }
  }
  ench->list.erase(std::remove(ench->list.begin(), ench->list.end(), nullptr), ench->list.end());
  if (!found) {
    auto c = nbt::Tag::Compound();
    c->set("id", nbt::Tag::Short(static_cast<std::int16_t>(id)));
    c->set("lvl", nbt::Tag::Short(static_cast<std::int16_t>(level)));
    ench->list.push_back(c);
  }
  return nbt::write(*tag, "", nbt::Endian::Little);
}

int enchantLevel(std::string_view item_nbt_le, int id) {
  for (auto& e : getEnchantments(item_nbt_le))
    if (e.id == id) return e.level;
  return 0;
}

} // namespace mpmpes::item
