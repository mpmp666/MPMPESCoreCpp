#pragma once

#include "mpmpes/binary/BinaryStream.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace mpmpes::item {

// Protocol-70 slot (BinaryStream putSlot/getSlot)
struct ItemStack {
  std::int16_t id = 0;
  std::uint8_t count = 0;
  std::int16_t damage = 0;
  std::string nbt; // compound tag bytes (often empty)

  bool empty() const { return id <= 0 || count == 0; }
  bool sameType(const ItemStack& o) const {
    return id == o.id && damage == o.damage;
  }
  bool equalsStack(const ItemStack& o) const {
    return sameType(o) && count == o.count;
  }
  static ItemStack air() { return {}; }
  static ItemStack of(std::int16_t id, std::uint8_t count = 1, std::int16_t damage = 0) {
    ItemStack s;
    s.id = id;
    s.count = count;
    s.damage = damage;
    return s;
  }
};

// Clamp slot fields so PE clients never see garbage ids/counts (client crash risk).
inline ItemStack sanitizeSlot(ItemStack s) {
  if (s.id <= 0 || s.count == 0) return ItemStack::air();
  // 0.14 item/block ids are small; reject wild values from corrupt disk
  if (s.id > 512) return ItemStack::air();
  if (s.count > 64) s.count = 64;
  if (s.damage < 0) s.damage = 0;
  // drop oversized NBT (we never write NBT for furnace yet)
  if (s.nbt.size() > 4096) s.nbt.clear();
  return s;
}

inline void putSlot(binary::BinaryStream& out, const ItemStack& item) {
  const ItemStack s = sanitizeSlot(item);
  if (s.empty()) {
    out.putShort(0);
    return;
  }
  out.putShort(static_cast<std::uint16_t>(s.id));
  out.putByte(s.count);
  out.putShort(static_cast<std::uint16_t>(s.damage));
  out.putLShort(static_cast<std::uint16_t>(s.nbt.size()));
  if (!s.nbt.empty()) out.put(s.nbt);
}

inline ItemStack getSlot(binary::BinaryStream& in) {
  ItemStack s;
  s.id = in.getSignedShort();
  if (s.id <= 0) return ItemStack::air();
  s.count = in.getByte();
  s.damage = static_cast<std::int16_t>(in.getShort());
  auto nbt_len = in.getLShort();
  if (nbt_len > 0) s.nbt = in.get(nbt_len);
  return s;
}

// Common IDs (0.14)
namespace ids {
inline constexpr std::int16_t AIR = 0;
inline constexpr std::int16_t STONE = 1;
inline constexpr std::int16_t GRASS = 2;
inline constexpr std::int16_t DIRT = 3;
inline constexpr std::int16_t COBBLESTONE = 4;
inline constexpr std::int16_t PLANKS = 5;
inline constexpr std::int16_t SAPLING = 6;
inline constexpr std::int16_t BEDROCK = 7;
inline constexpr std::int16_t SAND = 12;
inline constexpr std::int16_t GRAVEL = 13;
inline constexpr std::int16_t LOG = 17;
inline constexpr std::int16_t LEAVES = 18;
inline constexpr std::int16_t GLASS = 20;
inline constexpr std::int16_t LAPIS_ORE = 21;
inline constexpr std::int16_t SANDSTONE = 24;
inline constexpr std::int16_t WOOL = 35;
inline constexpr std::int16_t TORCH = 50;
inline constexpr std::int16_t CHEST = 54;
inline constexpr std::int16_t WORKBENCH = 58;
inline constexpr std::int16_t FURNACE = 61;
inline constexpr std::int16_t LADDER = 65;
inline constexpr std::int16_t COBBLE_STAIRS = 67;
inline constexpr std::int16_t GLOWSTONE = 89;
inline constexpr std::int16_t STONE_BRICK = 98;
inline constexpr std::int16_t IRON_ORE = 15;
inline constexpr std::int16_t COAL_ORE = 16;
inline constexpr std::int16_t GOLD_ORE = 14;
inline constexpr std::int16_t DIAMOND_ORE = 56;
inline constexpr std::int16_t OBSIDIAN = 49;
inline constexpr std::int16_t NETHERRACK = 87;
inline constexpr std::int16_t QUARTZ = 155;
inline constexpr std::int16_t IRON_BLOCK = 42;
inline constexpr std::int16_t GOLD_BLOCK = 41;
inline constexpr std::int16_t DIAMOND_BLOCK = 57;
inline constexpr std::int16_t BOOKSHELF = 47;
inline constexpr std::int16_t TNT = 46;
inline constexpr std::int16_t BRICK = 45;
inline constexpr std::int16_t CLAY = 82;
inline constexpr std::int16_t FENCE = 85;
inline constexpr std::int16_t WATER = 8;
inline constexpr std::int16_t LAVA = 10;
inline constexpr std::int16_t ICE = 79;
inline constexpr std::int16_t SNOW = 80;
inline constexpr std::int16_t CACTUS = 81;
inline constexpr std::int16_t MELON_BLOCK = 103;
inline constexpr std::int16_t PUMPKIN = 86;
inline constexpr std::int16_t MYCELIUM = 110;
inline constexpr std::int16_t END_STONE = 121;
// items
inline constexpr std::int16_t IRON_SHOVEL = 256;
inline constexpr std::int16_t IRON_PICKAXE = 257;
inline constexpr std::int16_t IRON_AXE = 258;
inline constexpr std::int16_t FLINT_STEEL = 259;
inline constexpr std::int16_t APPLE = 260;
inline constexpr std::int16_t BOW = 261;
inline constexpr std::int16_t ARROW = 262;
inline constexpr std::int16_t COAL = 263;
inline constexpr std::int16_t DIAMOND = 264;
inline constexpr std::int16_t IRON_INGOT = 265;
inline constexpr std::int16_t GOLD_INGOT = 266;
inline constexpr std::int16_t IRON_SWORD = 267;
inline constexpr std::int16_t WOODEN_SWORD = 268;
inline constexpr std::int16_t WOODEN_SHOVEL = 269;
inline constexpr std::int16_t WOODEN_PICKAXE = 270;
inline constexpr std::int16_t WOODEN_AXE = 271;
inline constexpr std::int16_t STONE_SWORD = 272;
inline constexpr std::int16_t STONE_SHOVEL = 273;
inline constexpr std::int16_t STONE_PICKAXE = 274;
inline constexpr std::int16_t STONE_AXE = 275;
inline constexpr std::int16_t DIAMOND_SWORD = 276;
inline constexpr std::int16_t DIAMOND_SHOVEL = 277;
inline constexpr std::int16_t DIAMOND_PICKAXE = 278;
inline constexpr std::int16_t DIAMOND_AXE = 279;
inline constexpr std::int16_t GOLDEN_SWORD = 283;
inline constexpr std::int16_t GOLDEN_SHOVEL = 284;
inline constexpr std::int16_t GOLDEN_PICKAXE = 285;
inline constexpr std::int16_t GOLDEN_AXE = 286;
inline constexpr std::int16_t STICK = 280;
inline constexpr std::int16_t BOWL = 281;
inline constexpr std::int16_t MUSHROOM_STEW = 282;
inline constexpr std::int16_t STRING = 287;
inline constexpr std::int16_t FEATHER = 288;
inline constexpr std::int16_t GUNPOWDER = 289;
inline constexpr std::int16_t WHEAT = 296;
inline constexpr std::int16_t BREAD = 297;
inline constexpr std::int16_t LEATHER_HELMET = 298;
inline constexpr std::int16_t CHAIN_CHESTPLATE = 303;
inline constexpr std::int16_t IRON_HELMET = 306;
inline constexpr std::int16_t IRON_CHESTPLATE = 307;
inline constexpr std::int16_t IRON_LEGGINGS = 308;
inline constexpr std::int16_t IRON_BOOTS = 309;
inline constexpr std::int16_t DIAMOND_HELMET = 310;
inline constexpr std::int16_t DIAMOND_CHESTPLATE = 311;
inline constexpr std::int16_t FLINT = 318;
inline constexpr std::int16_t RAW_PORKCHOP = 319;
inline constexpr std::int16_t COOKED_PORKCHOP = 320;
inline constexpr std::int16_t PAINTING = 321;
inline constexpr std::int16_t GOLDEN_APPLE = 322;
inline constexpr std::int16_t SIGN = 323;
inline constexpr std::int16_t WOODEN_DOOR = 324;
inline constexpr std::int16_t BUCKET = 325;
inline constexpr std::int16_t WATER_BUCKET = 326;
inline constexpr std::int16_t LAVA_BUCKET = 327;
inline constexpr std::int16_t MINECART = 328;
inline constexpr std::int16_t SADDLE = 329;
inline constexpr std::int16_t IRON_DOOR = 330;
inline constexpr std::int16_t REDSTONE = 331;
inline constexpr std::int16_t SNOWBALL = 332;
inline constexpr std::int16_t BOAT = 333;
inline constexpr std::int16_t LEATHER = 334;
inline constexpr std::int16_t BRICK_ITEM = 336;
inline constexpr std::int16_t CLAY_BALL = 337;
inline constexpr std::int16_t SUGARCANE = 338;
inline constexpr std::int16_t PAPER = 339;
inline constexpr std::int16_t BOOK = 340;
inline constexpr std::int16_t SLIMEBALL = 341;
inline constexpr std::int16_t EGG = 344;
inline constexpr std::int16_t COMPASS = 345;
inline constexpr std::int16_t FISHING_ROD = 346;
inline constexpr std::int16_t CLOCK = 347;
inline constexpr std::int16_t GLOWSTONE_DUST = 348;
inline constexpr std::int16_t RAW_FISH = 349;
inline constexpr std::int16_t COOKED_FISH = 350;
inline constexpr std::int16_t DYE = 351;
inline constexpr std::int16_t BONE = 352;
inline constexpr std::int16_t SUGAR = 353;
inline constexpr std::int16_t CAKE = 354;
inline constexpr std::int16_t BED = 355;
inline constexpr std::int16_t COOKIE = 357;
inline constexpr std::int16_t SHEARS = 359;
inline constexpr std::int16_t MELON = 360;
inline constexpr std::int16_t PUMPKIN_SEEDS = 361;
inline constexpr std::int16_t MELON_SEEDS = 362;
inline constexpr std::int16_t RAW_BEEF = 363;
inline constexpr std::int16_t STEAK = 364;
inline constexpr std::int16_t RAW_CHICKEN = 365;
inline constexpr std::int16_t COOKED_CHICKEN = 366;
inline constexpr std::int16_t ROTTEN_FLESH = 367;
inline constexpr std::int16_t BLAZE_ROD = 369;
inline constexpr std::int16_t GHAST_TEAR = 370;
inline constexpr std::int16_t GOLD_NUGGET = 371;
inline constexpr std::int16_t NETHER_WART = 372;
inline constexpr std::int16_t POTION = 373;
inline constexpr std::int16_t GLASS_BOTTLE = 374;
inline constexpr std::int16_t SPIDER_EYE = 375;
inline constexpr std::int16_t FERMENTED_SPIDER_EYE = 376;
inline constexpr std::int16_t BLAZE_POWDER = 377;
inline constexpr std::int16_t MAGMA_CREAM = 378;
inline constexpr std::int16_t BREWING_STAND = 379;
inline constexpr std::int16_t CAULDRON = 380;
inline constexpr std::int16_t ENDER_EYE = 381;
inline constexpr std::int16_t SPECKLED_MELON = 382;
inline constexpr std::int16_t SPAWN_EGG = 383;
inline constexpr std::int16_t EXPERIENCE_BOTTLE = 384;
inline constexpr std::int16_t FIRE_CHARGE = 385;
inline constexpr std::int16_t EMERALD = 388;
inline constexpr std::int16_t ITEM_FRAME = 389;
inline constexpr std::int16_t FLOWER_POT = 390;
inline constexpr std::int16_t CARROT = 391;
inline constexpr std::int16_t POTATO = 392;
inline constexpr std::int16_t BAKED_POTATO = 393;
inline constexpr std::int16_t POISONOUS_POTATO = 394;
inline constexpr std::int16_t GOLDEN_CARROT = 396;
inline constexpr std::int16_t PUMPKIN_PIE = 400;
inline constexpr std::int16_t NETHER_BRICK = 405;
inline constexpr std::int16_t NETHER_QUARTZ = 406;
} // namespace ids

// Subset creative inventory for 0.14 clients
inline std::vector<ItemStack> creativeItems() {
  using namespace ids;
  std::vector<ItemStack> v;
  auto add = [&](std::int16_t id, std::int16_t dmg = 0) { v.push_back(ItemStack::of(id, 1, dmg)); };
  // blocks
  add(STONE);
  add(GRASS);
  add(DIRT);
  add(COBBLESTONE);
  add(PLANKS, 0);
  add(PLANKS, 1);
  add(PLANKS, 2);
  add(PLANKS, 3);
  add(SAPLING);
  add(SAND);
  add(GRAVEL);
  add(GOLD_ORE);
  add(IRON_ORE);
  add(COAL_ORE);
  add(LOG, 0);
  add(LOG, 1);
  add(LOG, 2);
  add(LOG, 3);
  add(LEAVES);
  add(GLASS);
  add(LAPIS_ORE);
  add(SANDSTONE);
  add(WOOL, 0);
  add(WOOL, 1);
  add(WOOL, 14);
  add(WOOL, 15);
  add(TORCH);
  add(CHEST);
  add(WORKBENCH);
  // v0.4.14: furnace removed (PE 0.14 open crash); not in creative
  add(LADDER);
  add(COBBLE_STAIRS);
  add(DIAMOND_ORE);
  add(DIAMOND_BLOCK);
  add(IRON_BLOCK);
  add(GOLD_BLOCK);
  add(OBSIDIAN);
  add(GLOWSTONE);
  add(STONE_BRICK);
  add(NETHERRACK);
  add(QUARTZ);
  add(BOOKSHELF);
  add(TNT);
  add(BRICK);
  add(CLAY);
  add(FENCE);
  add(ICE);
  add(SNOW);
  add(CACTUS);
  add(MELON_BLOCK);
  add(PUMPKIN);
  add(MYCELIUM);
  add(END_STONE);
  // tools / items
  add(WOODEN_SWORD);
  add(WOODEN_PICKAXE);
  add(WOODEN_AXE);
  add(WOODEN_SHOVEL);
  add(STONE_SWORD);
  add(STONE_PICKAXE);
  add(STONE_AXE);
  add(IRON_SWORD);
  add(IRON_PICKAXE);
  add(IRON_AXE);
  add(IRON_SHOVEL);
  add(DIAMOND_SWORD);
  add(DIAMOND_PICKAXE);
  add(BOW);
  add(ARROW);
  add(STICK);
  add(COAL);
  add(DIAMOND);
  add(IRON_INGOT);
  add(GOLD_INGOT);
  add(BREAD);
  add(APPLE);
  add(GOLDEN_APPLE);
  add(RAW_PORKCHOP);
  add(COOKED_PORKCHOP);
  add(RAW_BEEF);
  add(STEAK);
  add(RAW_CHICKEN);
  add(COOKED_CHICKEN);
  add(BUCKET);
  add(WATER_BUCKET);
  add(LAVA_BUCKET);
  add(FLINT_STEEL);
  add(SHEARS);
  add(BED);
  add(SIGN);
  // Spawn eggs (PM creative order subset; meta = network entity type)
  // Place near tools so they are easier to find than buried at list end.
  add(SPAWN_EGG, 13); // sheep
  add(SPAWN_EGG, 12); // pig
  add(SPAWN_EGG, 11); // cow
  add(SPAWN_EGG, 10); // chicken
  add(SPAWN_EGG, 14); // wolf
  add(SPAWN_EGG, 15); // villager
  add(SPAWN_EGG, 16); // mooshroom
  add(SPAWN_EGG, 32); // zombie
  add(SPAWN_EGG, 33); // creeper
  add(SPAWN_EGG, 34); // skeleton
  add(SPAWN_EGG, 35); // spider
  return v;
}

// True if item appears in the creative palette (0.14 creative pick allow-list)
inline bool isCreativeItem(const ItemStack& it) {
  if (it.empty()) return true;
  for (const auto& c : creativeItems()) {
    if (c.id == it.id && c.damage == it.damage) return true;
  }
  return false;
}

// Default survival hotbar starter
inline std::vector<ItemStack> starterInventory(bool creative) {
  std::vector<ItemStack> slots(36, ItemStack::air());
  if (creative) {
    slots[0] = ItemStack::of(ids::STONE_PICKAXE);
    slots[1] = ItemStack::of(ids::STONE);
    slots[2] = ItemStack::of(ids::DIRT);
    slots[3] = ItemStack::of(ids::PLANKS);
    slots[4] = ItemStack::of(ids::TORCH, 64);
    slots[5] = ItemStack::of(ids::SHEARS);
    slots[6] = ItemStack::of(ids::SPAWN_EGG, 16, 13); // sheep
    slots[7] = ItemStack::of(ids::SPAWN_EGG, 16, 12); // pig
    slots[8] = ItemStack::of(ids::DIAMOND_SWORD);
  } else {
    slots[0] = ItemStack::of(ids::WOODEN_PICKAXE);
    slots[1] = ItemStack::of(ids::WOODEN_AXE);
    slots[2] = ItemStack::of(ids::BREAD, 8);
    slots[3] = ItemStack::of(ids::TORCH, 16);
  }
  return slots;
}

// Block id that should be placed for an item (blocks 0-255 map 1:1 for most)
inline std::uint8_t itemToBlockId(std::int16_t item_id) {
  if (item_id > 0 && item_id < 256) return static_cast<std::uint8_t>(item_id);
  return 0;
}

// --- Tool durability (PM Tool.php / Shears / FlintSteel subset) ---
// damage field = uses so far; break when damage >= maxDurability

enum class ToolKind { None, Sword, Pickaxe, Axe, Shovel, Hoe, Shears, FlintSteel, Bow };

inline ToolKind toolKind(std::int16_t id) {
  using namespace ids;
  switch (id) {
    case WOODEN_SWORD:
    case STONE_SWORD:
    case IRON_SWORD:
    case DIAMOND_SWORD:
    case GOLDEN_SWORD:
      return ToolKind::Sword;
    case WOODEN_PICKAXE:
    case STONE_PICKAXE:
    case IRON_PICKAXE:
    case DIAMOND_PICKAXE:
    case GOLDEN_PICKAXE:
      return ToolKind::Pickaxe;
    case WOODEN_AXE:
    case STONE_AXE:
    case IRON_AXE:
    case DIAMOND_AXE:
    case GOLDEN_AXE:
      return ToolKind::Axe;
    case WOODEN_SHOVEL:
    case STONE_SHOVEL:
    case IRON_SHOVEL:
    case DIAMOND_SHOVEL:
    case GOLDEN_SHOVEL:
      return ToolKind::Shovel;
    case SHEARS:
      return ToolKind::Shears;
    case FLINT_STEEL:
      return ToolKind::FlintSteel;
    case BOW:
      return ToolKind::Bow;
    default:
      return ToolKind::None;
  }
}

inline bool isToolItem(std::int16_t id) { return toolKind(id) != ToolKind::None; }

// PM Tool::getMaxDurability tiers + shears/flint/bow
inline int maxDurability(std::int16_t id) {
  using namespace ids;
  switch (id) {
    // wooden
    case WOODEN_SWORD:
    case WOODEN_PICKAXE:
    case WOODEN_AXE:
    case WOODEN_SHOVEL:
      return 60;
    // gold
    case GOLDEN_SWORD:
    case GOLDEN_PICKAXE:
    case GOLDEN_AXE:
    case GOLDEN_SHOVEL:
      return 33;
    // stone
    case STONE_SWORD:
    case STONE_PICKAXE:
    case STONE_AXE:
    case STONE_SHOVEL:
      return 132;
    // iron
    case IRON_SWORD:
    case IRON_PICKAXE:
    case IRON_AXE:
    case IRON_SHOVEL:
      return 251;
    // diamond
    case DIAMOND_SWORD:
    case DIAMOND_PICKAXE:
    case DIAMOND_AXE:
    case DIAMOND_SHOVEL:
      return 1562;
    case FLINT_STEEL:
      return 65;
    case SHEARS:
      return 239;
    case BOW:
      return 385;
    default:
      return 0;
  }
}

// Apply wear to a tool stack. Returns true if stack changed (including broke → air).
// amount: dig/shear/flint usually 1; tool-as-weapon on entity for pick/axe/shovel is 2 (PM).
inline bool applyDurability(ItemStack& s, int amount = 1) {
  if (s.empty() || amount <= 0) return false;
  const int maxd = maxDurability(s.id);
  if (maxd <= 0) return false;
  s.damage = static_cast<std::int16_t>(s.damage + amount);
  if (s.damage >= maxd) {
    s = ItemStack::air();
    return true;
  }
  return true;
}

// Fuel burn ticks (PM Fuel::$duration subset; 20 ticks = 1s)
inline int furnaceFuelTicks(std::int16_t id, std::int16_t damage = 0) {
  using namespace ids;
  (void)damage;
  switch (id) {
    case COAL: return 1600; // coal + charcoal
    case LAVA_BUCKET: return 20000;
    case LOG:
    case PLANKS:
      return 300;
    case STICK:
    case SAPLING:
      return 100;
    case WOODEN_PICKAXE:
    case WOODEN_AXE:
    case WOODEN_SWORD:
    case WOODEN_SHOVEL:
    case WOODEN_DOOR:
      return 200;
    case 173: // coal block
      return 16000;
    case 85:  // fence
    case 47:  // bookshelf
    case 58:  // workbench
    case 54:  // chest
    case 25:  // noteblock
    case 53:  // wood stairs
      return 300;
    case BED:
      return 300;
    default:
      return 0;
  }
}

// Match furnace smelt result (PM registerFurnace subset). Returns air if none.
inline ItemStack furnaceSmeltResult(const ItemStack& input) {
  using namespace ids;
  if (input.empty()) return ItemStack::air();
  switch (input.id) {
    case COBBLESTONE: return ItemStack::of(STONE);
    case SAND: return ItemStack::of(GLASS);
    case LOG: return ItemStack::of(COAL, 1, 1); // charcoal
    case IRON_ORE: return ItemStack::of(IRON_INGOT);
    case GOLD_ORE: return ItemStack::of(GOLD_INGOT);
    case DIAMOND_ORE: return ItemStack::of(DIAMOND);
    case COAL_ORE: return ItemStack::of(COAL);
    case RAW_PORKCHOP: return ItemStack::of(COOKED_PORKCHOP);
    case RAW_BEEF: return ItemStack::of(STEAK);
    case RAW_CHICKEN: return ItemStack::of(COOKED_CHICKEN);
    case RAW_FISH: return ItemStack::of(COOKED_FISH, 1, input.damage);
    case CLAY_BALL: return ItemStack::of(BRICK_ITEM);
    case CACTUS: return ItemStack::of(DYE, 1, 2); // green dye
    default: return ItemStack::air();
  }
}

// Drop when breaking a block (simplified)
inline ItemStack breakDrop(std::uint8_t block_id, std::uint8_t meta) {
  using namespace ids;
  switch (block_id) {
    case GRASS:
      return ItemStack::of(DIRT);
    case STONE:
      return ItemStack::of(COBBLESTONE);
    case COAL_ORE:
      return ItemStack::of(COAL);
    case DIAMOND_ORE:
      return ItemStack::of(DIAMOND);
    case LEAVES:
      return ItemStack::air();
    case BEDROCK:
      return ItemStack::air();
    case 0:
      return ItemStack::air();
    default:
      return ItemStack::of(static_cast<std::int16_t>(block_id), 1, meta);
  }
}

} // namespace mpmpes::item
