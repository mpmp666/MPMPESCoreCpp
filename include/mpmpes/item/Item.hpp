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
// PE 0.14.x Win/Android is brittle: wild id/count/damage/NBT in inventory or creative
// palette can hard-crash the client on click / open inventory.
inline ItemStack sanitizeSlot(ItemStack s) {
  if (s.id <= 0 || s.count == 0) return ItemStack::air();
  // 0.14 item/block ids stay well under 512; reject wild values from corrupt disk/packets
  if (s.id > 511) return ItemStack::air();
  // Hopper places as item 410, never block form 154 in inventory/creative UI
  if (s.id == 154) s.id = 410;
  // Unpowered comparator block 149 → comparator item 404 (if present)
  if (s.id == 149) s.id = 404;
  if (s.count > 64) s.count = 64;
  // damage is int16_t on wire; clamp negatives only (aux/meta never negative)
  if (s.damage < 0) s.damage = 0;
  // drop oversized / nonsense NBT (creative picks rarely need NBT; corrupt NBT crashes Win)
  if (s.nbt.size() > 1024) s.nbt.clear();
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
  // Cap NBT read so a corrupt packet cannot blow memory / later crash clients on resync
  if (nbt_len > 0) {
    if (nbt_len > 1024) {
      // consume bounded then drop; if stream short, get() throws and caller marks !ok
      (void)in.get(nbt_len);
      s.nbt.clear();
    } else {
      s.nbt = in.get(nbt_len);
    }
  }
  return sanitizeSlot(s);
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
// PE/Java item ids (0.14 client accepts these creative/spawn ids)
inline constexpr std::int16_t MINECART_CHEST = 342;
inline constexpr std::int16_t MINECART_TNT = 407;
inline constexpr std::int16_t MINECART_HOPPER = 408;
inline constexpr std::int16_t HOPPER = 410; // places block 154
inline constexpr std::int16_t SADDLE = 329;
inline constexpr std::int16_t IRON_DOOR = 330;
inline constexpr std::int16_t REDSTONE = 331;
inline constexpr std::int16_t REPEATER = 356;
inline constexpr std::int16_t COMPARATOR = 404; // place → unpowered comparator 149
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

// Auto-ported from /home/linux1/MPMPESCore/src/pocketmine/item/Item.php initCreativeItems
// Do not pad/guess grid math — client categories unknown items as blank cubes.
inline std::vector<ItemStack> creativeItems() {
  std::vector<ItemStack> v;
  v.reserve(507);
  auto add = [&](std::int16_t id, std::int16_t dmg = 0) {
    if (id <= 0) return;
    // Keep PHP palette as-is (incl. ids used by Genisys/0.14 forks).
    // Only drop true air; never invent padding slots.
    ItemStack s = ItemStack::of(id, 1, dmg);
    if (s.id <= 0 || s.count == 0) return;
    if (s.count > 64) s.count = 64;
    if (s.damage < 0) s.damage = 0;
    v.push_back(s);
  };
  // Building
  add(4);
  add(98);
  add(98, 1);
  add(98, 2);
  add(98, 3);
  add(48);
  add(5);
  add(5, 1);
  add(5, 2);
  add(5, 3);
  add(5, 4);
  add(5, 5);
  add(45);
  add(1);
  add(1, 1);
  add(1, 2);
  add(1, 3);
  add(1, 4);
  add(1, 5);
  add(1, 6);
  add(3);
  add(243);
  add(2);
  add(110);
  add(82);
  add(172);
  add(159);
  add(159, 1);
  add(159, 2);
  add(159, 3);
  add(159, 4);
  add(159, 5);
  add(159, 6);
  add(159, 7);
  add(159, 8);
  add(159, 9);
  add(159, 10);
  add(159, 11);
  add(159, 12);
  add(159, 13);
  add(159, 14);
  add(159, 15);
  add(24);
  add(24, 1);
  add(24, 2);
  add(179);
  add(179, 1);
  add(179, 2);
  add(12);
  add(12, 1);
  add(13);
  add(17);
  add(17, 1);
  add(17, 2);
  add(17, 3);
  add(162);
  add(162, 1);
  add(112);
  add(87);
  add(88);
  add(7);
  add(67);
  add(53);
  add(134);
  add(135);
  add(136);
  add(163);
  add(164);
  add(165);
  add(108);
  add(128);
  add(180);
  add(109);
  add(114);
  add(156);
  add(44);
  add(44, 3);
  add(158);
  add(158, 1);
  add(158, 2);
  add(158, 3);
  add(158, 4);
  add(158, 5);
  add(44, 4);
  add(44, 1);
  add(44, 5);
  add(44, 6);
  add(44, 7);
  add(182);
  add(155);
  add(155, 1);
  add(155, 2);
  add(16);
  add(15);
  add(14);
  add(56);
  add(21);
  add(73);
  add(129);
  add(153);
  add(49);
  add(79);
  add(174);
  add(80);
  add(121);
  // Decoration
  add(139);
  add(139, 1);
  add(111);
  add(41);
  add(42);
  add(57);
  add(22);
  add(173);
  add(133);
  add(152);
  add(78);
  add(20);
  add(89);
  add(106);
  add(65);
  add(19);
  add(102);
  add(324);
  add(427);
  add(428);
  add(429);
  add(430);
  add(431);
  add(330);
  add(96);
  add(167);
  add(85);
  add(85, 1);
  add(85, 2);
  add(85, 3);
  add(85, 4);
  add(85, 5);
  add(113);
  add(107);
  add(184);
  add(183);
  add(186);
  add(185);
  add(187);
  add(101);
  add(355);
  add(47);
  add(389);
  add(58);
  add(245);
  add(54);
  add(146);
  add(61);
  add(379);
  add(380);
  add(25);
  add(120);
  add(145);
  add(145, 4);
  add(145, 8);
  add(37);
  add(38);
  add(38, 1);
  add(38, 2);
  add(38, 3);
  add(38, 4);
  add(38, 5);
  add(38, 6);
  add(38, 7);
  add(38, 8);
  add(175);
  add(175, 1);
  add(175, 2);
  add(175, 3);
  add(175, 4);
  add(175, 5);
  add(39);
  add(40);
  add(99, 14);
  add(100, 14);
  add(99);
  add(99, 10);
  add(81);
  add(103);
  add(86);
  add(91);
  add(30);
  add(170);
  add(31, 1);
  add(31, 2);
  add(32);
  add(6);
  add(6, 1);
  add(6, 2);
  add(6, 3);
  add(6, 4);
  add(6, 5);
  add(18);
  add(18, 1);
  add(18, 2);
  add(18, 3);
  add(161);
  add(161, 1);
  add(354);
  add(397);
  add(397, 1);
  add(397, 2);
  add(397, 3);
  add(397, 4);
  add(323);
  add(390);
  add(52);
  add(116);
  add(35);
  add(35, 8);
  add(35, 7);
  add(35, 15);
  add(35, 12);
  add(35, 14);
  add(35, 1);
  add(35, 4);
  add(35, 5);
  add(35, 13);
  add(35, 9);
  add(35, 3);
  add(35, 11);
  add(35, 10);
  add(35, 2);
  add(35, 6);
  add(171);
  add(171, 8);
  add(171, 7);
  add(171, 15);
  add(171, 12);
  add(171, 14);
  add(171, 1);
  add(171, 4);
  add(171, 5);
  add(171, 13);
  add(171, 9);
  add(171, 3);
  add(171, 11);
  add(171, 10);
  add(171, 2);
  add(171, 6);
  // Tools
  add(66);
  add(27);
  add(28);
  add(126);
  add(50);
  add(325);
  add(325, 1);
  add(325, 8);
  add(325, 10);
  add(46);
  add(331);
  add(261);
  add(346);
  add(259);
  add(359);
  add(347);
  add(345);
  add(328);
  // PE 0.14 item forms only — block id 154 / comparator block 149 in creative crash some clients
  add(342);  // minecart with chest
  add(407);  // minecart with TNT
  add(408);  // minecart with hopper
  add(410);  // hopper item (places block 154)
  add(333);
  add(333, 1);
  add(333, 2);
  add(333, 3);
  add(333, 4);
  add(333, 5);
  add(383, 15);
  add(383, 10);
  add(383, 11);
  add(383, 12);
  add(383, 13);
  add(383, 14);
  add(383, 22);
  add(383, 16);
  add(383, 19);
  add(383, 18);
  add(383, 33);
  add(383, 38);
  add(383, 39);
  add(383, 34);
  add(383, 37);
  add(383, 35);
  add(383, 32);
  add(383, 36);
  add(383, 17);
  add(383, 40);
  add(383, 42);
  add(383, 41);
  add(383, 43);
  add(268);
  add(290);
  add(269);
  add(270);
  add(271);
  add(272);
  add(291);
  add(273);
  add(274);
  add(275);
  add(267);
  add(292);
  add(256);
  add(257);
  add(258);
  add(276);
  add(293);
  add(277);
  add(278);
  add(279);
  add(283);
  add(294);
  add(284);
  add(285);
  add(286);
  add(298);
  add(299);
  add(300);
  add(301);
  add(302);
  add(303);
  add(304);
  add(305);
  add(306);
  add(307);
  add(308);
  add(309);
  add(310);
  add(311);
  add(312);
  add(313);
  add(314);
  add(315);
  add(316);
  add(317);
  add(69);
  add(123);
  add(76);
  add(72);
  add(70);
  add(147);
  add(148);
  add(143);
  add(77);
  add(151);
  add(131);
  add(356);
  // Comparator item 404 is optional 0.14 fork content; omit block 149 (client crash risk).
  // Keep only item form if needed later via /give; Genisys vanilla creative has repeater only.
  add(125, 3);
  add(23, 3);
  add(332);
  // Seeds/Materials
  add(263);
  add(263, 1);
  add(264);
  add(265);
  add(266);
  add(388);
  add(280);
  add(281);
  add(287);
  add(288);
  add(318);
  add(334);
  add(415);
  add(337);
  add(353);
  add(406);
  add(339);
  add(360);
  add(262);
  add(352);
  add(338);
  add(296);
  add(295);
  add(361);
  add(362);
  add(458);
  add(260);
  add(322);
  add(466);
  add(349);
  add(350);
  add(463);
  add(367);
  add(282);
  add(297);
  add(319);
  add(320);
  add(365);
  add(366);
  add(363);
  add(364);
  add(360);
  add(391);
  add(392);
  add(393);
  add(394);
  add(357);
  add(400);
  add(411);
  add(412);
  add(413);
  add(378);
  add(369);
  add(371);
  add(396);
  add(382);
  add(414);
  add(370);
  add(341);
  add(377);
  add(372);
  add(289);
  add(348);
  add(375);
  add(376);
  add(384);
  add(351);
  add(351, 8);
  add(351, 7);
  add(351, 15);
  add(351, 12);
  add(351, 14);
  add(351, 1);
  add(351, 4);
  add(351, 5);
  add(351, 13);
  add(351, 9);
  add(351, 3);
  add(351, 11);
  add(351, 10);
  add(351, 2);
  add(351, 6);
  add(374);
  add(373);
  add(373, 1);
  add(373, 2);
  add(373, 3);
  add(373, 4);
  add(373, 5);
  add(373, 6);
  add(373, 7);
  add(373, 8);
  add(373, 9);
  add(373, 10);
  add(373, 11);
  add(373, 12);
  add(373, 13);
  add(373, 14);
  add(373, 15);
  add(373, 16);
  add(373, 17);
  add(373, 18);
  add(373, 19);
  add(373, 20);
  add(373, 21);
  add(373, 22);
  add(373, 23);
  add(373, 24);
  add(373, 25);
  add(373, 26);
  add(373, 27);
  add(373, 28);
  add(373, 29);
  add(373, 30);
  add(373, 31);
  add(373, 32);
  add(373, 33);
  add(373, 34);
  add(373, 35);
  add(438);
  add(438, 1);
  add(438, 2);
  add(438, 3);
  add(438, 4);
  add(438, 5);
  add(438, 6);
  add(438, 7);
  add(438, 8);
  add(438, 9);
  add(438, 10);
  add(438, 11);
  add(438, 12);
  add(438, 13);
  add(438, 14);
  add(438, 15);
  add(438, 16);
  add(438, 17);
  add(438, 18);
  add(438, 19);
  add(438, 20);
  add(438, 21);
  add(438, 22);
  add(438, 23);
  add(438, 24);
  add(438, 25);
  add(438, 26);
  add(438, 27);
  add(438, 28);
  add(438, 29);
  add(438, 30);
  add(438, 31);
  add(438, 32);
  add(438, 33);
  add(438, 34);
  add(438, 35);
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
// Special-case item ids that are not block ids (PM Item::get / place).
inline std::uint8_t itemToBlockId(std::int16_t item_id) {
  using namespace ids;
  if (item_id == SIGN) return 63; // SIGN_POST; wall vs post decided at place time
  if (item_id == REDSTONE) return 55; // redstone wire
  if (item_id == REPEATER) return 93; // unpowered repeater
  if (item_id == COMPARATOR) return 149; // unpowered comparator
  if (item_id == MINECART || item_id == MINECART_CHEST || item_id == MINECART_TNT ||
      item_id == MINECART_HOPPER)
    return 0; // entity, not block
  if (item_id == HOPPER) return 154; // hopper block
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
    case 63: // SIGN_POST
    case 68: // WALL_SIGN
      return ItemStack::of(SIGN, 1, 0);
    case 55: // REDSTONE_WIRE
      return ItemStack::of(REDSTONE, 1, 0);
    case 93: // UNPOWERED_REPEATER
    case 94: // POWERED_REPEATER
      return ItemStack::of(REPEATER, 1, 0);
    case 149: // UNPOWERED_COMPARATOR
    case 150: // POWERED_COMPARATOR
      return ItemStack::of(COMPARATOR, 1, 0);
    case 75: // UNLIT_REDSTONE_TORCH
    case 76: // REDSTONE_TORCH
      return ItemStack::of(76, 1, 0);
    case 123: // inactive lamp
    case 124: // active lamp
      return ItemStack::of(123, 1, 0);
    case 27: // powered rail — drop without power bit
    case 28:
    case 126:
      return ItemStack::of(static_cast<std::int16_t>(block_id), 1, 0);
    case 66:
      return ItemStack::of(66, 1, 0);
    case 154: // hopper block → hopper item
      return ItemStack::of(HOPPER, 1, 0);
    case 0:
      return ItemStack::air();
    default:
      return ItemStack::of(static_cast<std::int16_t>(block_id), 1, meta & 0x0f);
  }
}

} // namespace mpmpes::item
