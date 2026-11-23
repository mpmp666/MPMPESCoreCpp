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

// ---------------------------------------------------------------- enchant offers
#include "mpmpes/level/gen/EndGenerator.hpp" // JavaRandom

namespace mpmpes::item {
namespace {
int armorTier(int id) {
  switch (id) {
    case 298: case 299: case 300: case 301: return 1; // leather
    case 302: case 303: case 304: case 305: return 2; // chain
    case 306: case 307: case 308: case 309: return 3; // iron
    case 310: case 311: case 312: case 313: return 5; // diamond
    case 314: case 315: case 316: case 317: return 4; // gold
  }
  return 0;
}
int toolTier(int id) {
  switch (id) {
    case 270: case 271: case 272: case 273: case 274: return 1; // wood
    case 275: case 276: case 277: case 278: case 279: return 2; // stone
    case 256: case 257: case 258: case 259: case 267: return 3; // iron
    case 254: case 255: case 250: case 251: case 252: case 253: return 5; // diamond tools
    case 280: case 281: case 282: case 283: case 284: return 4; // gold
  }
  return 0;
}
// getEnchantAbility (PHP Enchantment::getEnchantAbility)
int abilityFor(int id) {
  if (id == 340 || id == 261 || id == 346) return 1; // book/bow/fishing_rod
  int at = armorTier(id);
  if (at) {
    static const int vals[] = {0, 15, 12, 9, 25, 10}; // leather/gold/chain/iron/diamond
    static const int v2[] = {0, 15, 25, 12, 9, 10};
    return v2[at];
  }
  // tools: sword/axe/pickaxe/shovel/hoe by tier
  switch (id) {
    // wooden 15, stone 5, iron 14, diamond 10, gold 22
    case 268: case 269: case 270: case 271: case 290: return 15; // wood sword/axe/pick/shovel/hoe
    case 272: case 273: case 274: case 275: case 291: return 5;  // stone
    case 256: case 257: case 258: case 259: case 267: return 14; // iron
    case 277: case 278: case 279: case 276: case 293: return 10; // diamond
    case 280: case 281: case 282: case 283: case 294: return 22; // gold
  }
  return 0;
}
std::uint32_t itemSlotFor(int id) {
  // map item id to the SLOT_* bit(s) it can receive
  if (id == 340) return S_ALL; // book accepts all (enchanted book)
  if (id == 261) return S_BOW;
  if (id == 346) return S_FISHING_ROD;
  int at = armorTier(id);
  if (at) {
    // which armor slot: helmet 298/302/306/310/314, chest 299/303/307/311/315, legs 300/304/308/312/316, boots 301/305/309/313/317
    switch (id) {
      case 298: case 302: case 306: case 310: case 314: return S_HEAD;
      case 299: case 303: case 307: case 311: case 315: return S_TORSO;
      case 300: case 304: case 308: case 312: case 316: return S_LEGS;
      case 301: case 305: case 309: case 313: case 317: return S_FEET;
    }
  }
  switch (id) {
    case 267: case 268: case 272: case 276: case 283: return S_SWORD;
    case 269: case 273: case 258: case 277: case 281: return S_AXE;
    case 270: case 274: case 257: case 278: case 282: return S_PICKAXE;
    case 271: case 275: case 256: case 279: case 280: return S_SHOVEL;
    case 290: case 291: case 292: case 293: case 294: return S_HOE;
  }
  return 0;
}
bool compatible(int a, int b) {
  if (a == b) return false;
  static const int excl[][4] = {{0, 1, 3, 4}, {9, 10, 11, -1}, {16, 18, -1, -1}};
  for (auto& g : excl) {
    bool ai = false, bi = false;
    for (int i = 0; i < 4 && g[i] >= 0; ++i) {
      if (g[i] == a) ai = true;
      if (g[i] == b) bi = true;
    }
    if (ai && bi) return false;
  }
  return true;
}
int enchantCost(level::gen::JavaRandom& random, int slot, int bookshelves, int ability) {
  bookshelves = std::max(0, std::min(15, bookshelves));
  int selected = random.nextInt(8) + 1 + (bookshelves >> 1) + random.nextInt(bookshelves + 1);
  if (slot == 0) return std::max(selected / 3, 1);
  return slot == 1 ? (selected * 2) / 3 + 1 : std::max(selected, bookshelves * 2);
}
float f32(double v) { return static_cast<float>(v); }

std::vector<EnchantEntry> selectEnchantments(int item_id, int cost, level::gen::JavaRandom& random) {
  int ability = abilityFor(item_id);
  if (ability <= 0 || cost <= 0) return {};
  cost += 1 + random.nextInt(ability / 4 + 1) + random.nextInt(ability / 4 + 1);
  float span = f32(f32(f32(random.nextFloat() + random.nextFloat()) - 1.0f) * f32(0.15f));
  cost = std::max(1, (int)std::floor(f32(cost + f32(cost * span)) + 0.5f));
  // possible: canEnchantItemTable + ability range covers cost
  std::uint32_t slot = itemSlotFor(item_id);
  std::vector<const EnchantDef*> possible;
  for (const auto& d : DEFS) {
    // applicable if the enchantment's primary slot overlaps the item, or it's universal (S_ALL)
    if (!(d.primary & slot) && d.primary != S_ALL) continue;
    // level range for this cost: choose level 1..max where [min,max] covers cost
    bool ok = false;
    for (int lvl = 1; lvl <= d.max_level; ++lvl) {
      int mn = d.min_lo + (lvl - 1) * d.min_step_lo;
      int mx = d.max_lo + (lvl - 1) * d.max_step_hi;
      if (cost >= mn && cost <= mx) { ok = true; break; }
    }
    if (ok) possible.push_back(&d);
  }
  if (possible.empty()) return {};
  auto choose = [&](const std::vector<const EnchantDef*>& pool) -> const EnchantDef* {
    int total = 0;
    for (auto* d : pool) total += d->weight;
    int pick = random.nextInt(total);
    for (auto* d : pool) {
      pick -= d->weight;
      if (pick < 0) return d;
    }
    return pool.back();
  };
  std::vector<EnchantEntry> result;
  const EnchantDef* first = choose(possible);
  // level for the chosen: the highest level whose min <= cost
  auto levelFor = [&](const EnchantDef* d, int c) {
    int lvl = 1;
    for (int l = 1; l <= d->max_level; ++l) {
      int mn = d->min_lo + (l - 1) * d->min_step_lo;
      int mx = d->max_lo + (l - 1) * d->max_step_hi;
      if (c >= mn && c <= mx) lvl = l;
    }
    return lvl;
  };
  result.push_back({first->id, levelFor(first, cost)});
  while (random.nextInt(50) <= cost) {
    int last = result.back().id;
    std::vector<const EnchantDef*> filtered;
    for (auto* d : possible)
      if (compatible(last, d->id)) filtered.push_back(d);
    if (filtered.empty()) break;
    const EnchantDef* nxt = choose(filtered);
    result.push_back({nxt->id, levelFor(nxt, cost)});
    cost /= 2;
  }
  return result;
}
} // namespace

int getEnchantAbility(int item_id, int meta) { return abilityFor(item_id); }
bool canEnchantItemTable(int item_id, const EnchantDef& def) {
  std::uint32_t slot = itemSlotFor(item_id);
  return (def.primary & slot) != 0 || (def.primary & S_ALL) != 0;
}

std::vector<EnchantOffer> generateEnchantOffers(int item_id, int meta, int bookshelves,
                                                std::int64_t seed) {
  std::vector<EnchantOffer> out;
  if (abilityFor(item_id) <= 0) return out;
  level::gen::JavaRandom random(seed);
  int costs[3];
  for (int slot = 0; slot < 3; ++slot) {
    int cost = enchantCost(random, slot, bookshelves, abilityFor(item_id));
    costs[slot] = cost >= slot + 1 ? cost : 0;
  }
  for (int slot = 0; slot < 3; ++slot) {
    random.setSeed(seed + slot);
    auto list = costs[slot] > 0 ? selectEnchantments(item_id, costs[slot], random) : std::vector<EnchantEntry>{};
    if (item_id == 340 && list.size() > 1)
      list.erase(list.begin() + random.nextInt(static_cast<int>(list.size())));
    out.push_back({list, list.empty() ? 0 : costs[slot]});
  }
  return out;
}

} // namespace mpmpes::item
