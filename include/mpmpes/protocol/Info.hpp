#pragma once

#include <cmath>
#include <cstdint>

namespace mpmpes::protocol {

// pocketmine\network\protocol\Info — MCPE 0.14.x / protocol 70
inline constexpr int CURRENT_PROTOCOL = 70;

// Every MCPE game packet on the wire is prefixed with 0x8e (RakLibInterface.php).
// Wire: [0x8e][pid][payload...]; decode uses pid at offset 1.
inline constexpr std::uint8_t MCPE_RAKNET_CUSTOM_PACKET_ID = 0x8e;

inline constexpr std::uint8_t LOGIN_PACKET = 0x8f;
inline constexpr std::uint8_t PLAY_STATUS_PACKET = 0x90;
inline constexpr std::uint8_t DISCONNECT_PACKET = 0x91;
inline constexpr std::uint8_t BATCH_PACKET = 0x92;
inline constexpr std::uint8_t TEXT_PACKET = 0x93;
inline constexpr std::uint8_t SET_TIME_PACKET = 0x94;
inline constexpr std::uint8_t START_GAME_PACKET = 0x95;
inline constexpr std::uint8_t ADD_PLAYER_PACKET = 0x96;
inline constexpr std::uint8_t REMOVE_PLAYER_PACKET = 0x97;
inline constexpr std::uint8_t ADD_ENTITY_PACKET = 0x98;
inline constexpr std::uint8_t REMOVE_ENTITY_PACKET = 0x99;
inline constexpr std::uint8_t ADD_ITEM_ENTITY_PACKET = 0x9a;
inline constexpr std::uint8_t TAKE_ITEM_ENTITY_PACKET = 0x9b;
inline constexpr std::uint8_t MOVE_ENTITY_PACKET = 0x9c;
inline constexpr std::uint8_t MOVE_PLAYER_PACKET = 0x9d;
inline constexpr std::uint8_t REMOVE_BLOCK_PACKET = 0x9e;
inline constexpr std::uint8_t UPDATE_BLOCK_PACKET = 0x9f;
inline constexpr std::uint8_t ADD_PAINTING_PACKET = 0xa0;
inline constexpr std::uint8_t EXPLODE_PACKET = 0xa1;
inline constexpr std::uint8_t LEVEL_EVENT_PACKET = 0xa2;
inline constexpr std::uint8_t BLOCK_EVENT_PACKET = 0xa3;
inline constexpr std::uint8_t ENTITY_EVENT_PACKET = 0xa4;
inline constexpr std::uint8_t MOB_EFFECT_PACKET = 0xa5;
inline constexpr std::uint8_t UPDATE_ATTRIBUTES_PACKET = 0xa6;
inline constexpr std::uint8_t MOB_EQUIPMENT_PACKET = 0xa7;
inline constexpr std::uint8_t MOB_ARMOR_EQUIPMENT_PACKET = 0xa8;
inline constexpr std::uint8_t INTERACT_PACKET = 0xa9;
inline constexpr std::uint8_t USE_ITEM_PACKET = 0xaa;
inline constexpr std::uint8_t PLAYER_ACTION_PACKET = 0xab;
inline constexpr std::uint8_t HURT_ARMOR_PACKET = 0xac;
inline constexpr std::uint8_t SET_ENTITY_DATA_PACKET = 0xad;
inline constexpr std::uint8_t SET_ENTITY_MOTION_PACKET = 0xae;
inline constexpr std::uint8_t SET_ENTITY_LINK_PACKET = 0xaf;
inline constexpr std::uint8_t SET_HEALTH_PACKET = 0xb0;
inline constexpr std::uint8_t SET_SPAWN_POSITION_PACKET = 0xb1;
inline constexpr std::uint8_t ANIMATE_PACKET = 0xb2;
inline constexpr std::uint8_t RESPAWN_PACKET = 0xb3;
inline constexpr std::uint8_t DROP_ITEM_PACKET = 0xb4;
inline constexpr std::uint8_t CONTAINER_OPEN_PACKET = 0xb5;
inline constexpr std::uint8_t CONTAINER_CLOSE_PACKET = 0xb6;
inline constexpr std::uint8_t CONTAINER_SET_SLOT_PACKET = 0xb7;
inline constexpr std::uint8_t CONTAINER_SET_DATA_PACKET = 0xb8;
inline constexpr std::uint8_t CONTAINER_SET_CONTENT_PACKET = 0xb9;
inline constexpr std::uint8_t CRAFTING_DATA_PACKET = 0xba;
inline constexpr std::uint8_t CRAFTING_EVENT_PACKET = 0xbb;
inline constexpr std::uint8_t ADVENTURE_SETTINGS_PACKET = 0xbc;
inline constexpr std::uint8_t BLOCK_ENTITY_DATA_PACKET = 0xbd;
inline constexpr std::uint8_t PLAYER_INPUT_PACKET = 0xbe;
inline constexpr std::uint8_t FULL_CHUNK_DATA_PACKET = 0xbf;
inline constexpr std::uint8_t SET_DIFFICULTY_PACKET = 0xc0;
inline constexpr std::uint8_t CHANGE_DIMENSION_PACKET = 0xc1;
inline constexpr std::uint8_t SET_PLAYER_GAMETYPE_PACKET = 0xc2;
inline constexpr std::uint8_t PLAYER_LIST_PACKET = 0xc3;
inline constexpr std::uint8_t REQUEST_CHUNK_RADIUS_PACKET = 0xc8;
inline constexpr std::uint8_t CHUNK_RADIUS_UPDATE_PACKET = 0xc9;
inline constexpr std::uint8_t ITEM_FRAME_DROP_ITEM_PACKET = 0xca;

// PlayStatus
inline constexpr std::int32_t PLAY_STATUS_LOGIN_SUCCESS = 0;
inline constexpr std::int32_t PLAY_STATUS_LOGIN_FAILED_CLIENT = 1;
inline constexpr std::int32_t PLAY_STATUS_LOGIN_FAILED_SERVER = 2;
inline constexpr std::int32_t PLAY_STATUS_PLAYER_SPAWN = 3;

// Text types
inline constexpr std::uint8_t TEXT_RAW = 0;
inline constexpr std::uint8_t TEXT_CHAT = 1;
inline constexpr std::uint8_t TEXT_TRANSLATION = 2;
inline constexpr std::uint8_t TEXT_POPUP = 3;
inline constexpr std::uint8_t TEXT_TIP = 4;
inline constexpr std::uint8_t TEXT_SYSTEM = 5;

// FullChunk order
inline constexpr std::uint8_t CHUNK_ORDER_COLUMNS = 0;
inline constexpr std::uint8_t CHUNK_ORDER_LAYERED = 1;

// Generators (StartGame)
inline constexpr std::int32_t GENERATOR_OLD = 0;
inline constexpr std::int32_t GENERATOR_INFINITE = 1;
inline constexpr std::int32_t GENERATOR_FLAT = 2;

// UpdateBlock flags
inline constexpr std::uint8_t UPDATE_FLAG_NONE = 0;
inline constexpr std::uint8_t UPDATE_FLAG_NEIGHBORS = 0b0001;
inline constexpr std::uint8_t UPDATE_FLAG_NETWORK = 0b0010;
inline constexpr std::uint8_t UPDATE_FLAG_NOGRAPHIC = 0b0100;
inline constexpr std::uint8_t UPDATE_FLAG_PRIORITY = 0b1000;
inline constexpr std::uint8_t UPDATE_FLAG_ALL =
    static_cast<std::uint8_t>(UPDATE_FLAG_NEIGHBORS | UPDATE_FLAG_NETWORK);

// LevelEvent
inline constexpr std::int16_t EVENT_PARTICLE_DESTROY = 2001;
inline constexpr std::int16_t EVENT_PARTICLE_SPAWN = 2004;
inline constexpr std::int16_t EVENT_SOUND_CLICK = 1000;
inline constexpr std::int16_t EVENT_SOUND_DOOR = 1003; // chest open/close click-ish
inline constexpr std::int16_t EVENT_ADD_PARTICLE_MASK = 0x4000;

// Container windows
inline constexpr std::uint8_t WINDOW_INVENTORY = 0;
inline constexpr std::uint8_t WINDOW_ARMOR = 0x78;
inline constexpr std::uint8_t WINDOW_CREATIVE = 0x79;
// Dynamic container windows (chest etc.) use 2..99 like PM Player::$windowCnt
inline constexpr std::uint8_t WINDOW_FIRST_DYNAMIC = 2;

// ContainerOpen typeId (InventoryType network type)
inline constexpr std::uint8_t CONTAINER_TYPE_CHEST = 0; // 27 slots
inline constexpr std::uint8_t CONTAINER_TYPE_WORKBENCH = 1;
inline constexpr std::uint8_t CONTAINER_TYPE_FURNACE = 2;
// PE 0.14 InventoryType network typeId for hopper (5 slots)
inline constexpr std::uint8_t CONTAINER_TYPE_HOPPER = 8;

// CraftingData entry types
inline constexpr std::int32_t CRAFT_ENTRY_SHAPELESS = 0;
inline constexpr std::int32_t CRAFT_ENTRY_SHAPED = 1;
inline constexpr std::int32_t CRAFT_ENTRY_FURNACE = 2;
inline constexpr std::int32_t CRAFT_ENTRY_FURNACE_DATA = 3;

// PlayerAction
inline constexpr std::int32_t ACTION_START_BREAK = 0;
inline constexpr std::int32_t ACTION_ABORT_BREAK = 1;
inline constexpr std::int32_t ACTION_STOP_BREAK = 2;
inline constexpr std::int32_t ACTION_GET_UPDATED_BLOCK = 3;
inline constexpr std::int32_t ACTION_DROP_ITEM = 4;
inline constexpr std::int32_t ACTION_RELEASE_ITEM = 5;
inline constexpr std::int32_t ACTION_STOP_SLEEPING = 6;
inline constexpr std::int32_t ACTION_RESPAWN = 7;
inline constexpr std::int32_t ACTION_JUMP = 8;
inline constexpr std::int32_t ACTION_START_SPRINT = 9;
inline constexpr std::int32_t ACTION_STOP_SPRINT = 10;
inline constexpr std::int32_t ACTION_START_SNEAK = 11;
inline constexpr std::int32_t ACTION_STOP_SNEAK = 12;
inline constexpr std::int32_t ACTION_DIMENSION_CHANGE = 13;
inline constexpr std::int32_t ACTION_ABORT_BREAK_2 = 14; // some clients

// Entity network types
inline constexpr std::int32_t ENTITY_CHICKEN = 10;
inline constexpr std::int32_t ENTITY_COW = 11;
inline constexpr std::int32_t ENTITY_PIG = 12;
inline constexpr std::int32_t ENTITY_SHEEP = 13;
inline constexpr std::int32_t ENTITY_ZOMBIE = 32;
inline constexpr std::int32_t ENTITY_CREEPER = 33;
inline constexpr std::int32_t ENTITY_SKELETON = 34;
inline constexpr std::int32_t ENTITY_SPIDER = 35;
inline constexpr std::int32_t ENTITY_ITEM = 64;
inline constexpr std::int32_t ENTITY_MINECART = 84;
// PE 0.14 / Genisys special minecarts
inline constexpr std::int32_t ENTITY_MINECART_HOPPER = 96;
inline constexpr std::int32_t ENTITY_MINECART_TNT = 97;
inline constexpr std::int32_t ENTITY_MINECART_CHEST = 98;

// Entity metadata
inline constexpr std::uint8_t DATA_TYPE_BYTE = 0;
inline constexpr std::uint8_t DATA_TYPE_SHORT = 1;
inline constexpr std::uint8_t DATA_TYPE_INT = 2;
inline constexpr std::uint8_t DATA_TYPE_FLOAT = 3;
inline constexpr std::uint8_t DATA_TYPE_STRING = 4;
inline constexpr std::uint8_t DATA_TYPE_LONG = 8;
inline constexpr std::uint8_t DATA_FLAGS = 0;
inline constexpr std::uint8_t DATA_AIR = 1;
inline constexpr std::uint8_t DATA_NAMETAG = 2;
inline constexpr std::uint8_t DATA_SHOW_NAMETAG = 3;
inline constexpr std::uint8_t DATA_NO_AI = 15;
// Sheep wool color / sheared flag (PM Sheep::DATA_COLOR_INFO = 16; bit4 = sheared)
inline constexpr std::uint8_t DATA_COLOR_INFO = 16;

// Common block IDs
inline constexpr std::uint8_t BLOCK_AIR = 0;
inline constexpr std::uint8_t BLOCK_STONE = 1;
inline constexpr std::uint8_t BLOCK_GRASS = 2;
inline constexpr std::uint8_t BLOCK_DIRT = 3;
inline constexpr std::uint8_t BLOCK_COBBLE = 4;
inline constexpr std::uint8_t BLOCK_BEDROCK = 7;
inline constexpr std::uint8_t BLOCK_WATER = 8;
inline constexpr std::uint8_t BLOCK_LAVA = 10;
inline constexpr std::uint8_t BLOCK_SAND = 12;
inline constexpr std::uint8_t BLOCK_GRAVEL = 13;
inline constexpr std::uint8_t BLOCK_GOLD_ORE = 14;
inline constexpr std::uint8_t BLOCK_IRON_ORE = 15;
inline constexpr std::uint8_t BLOCK_COAL_ORE = 16;
inline constexpr std::uint8_t BLOCK_LOG = 17;
inline constexpr std::uint8_t BLOCK_LEAVES = 18;
inline constexpr std::uint8_t BLOCK_OBSIDIAN = 49;
inline constexpr std::uint8_t BLOCK_FIRE = 51;
inline constexpr std::uint8_t BLOCK_CHEST = 54;
inline constexpr std::uint8_t BLOCK_DIAMOND_ORE = 56;
inline constexpr std::uint8_t BLOCK_WORKBENCH = 58;
inline constexpr std::uint8_t BLOCK_FURNACE = 61;
inline constexpr std::uint8_t BLOCK_BURNING_FURNACE = 62;
inline constexpr std::uint8_t BLOCK_SIGN_POST = 63;
inline constexpr std::uint8_t BLOCK_WALL_SIGN = 68;
inline constexpr std::uint8_t BLOCK_NETHERRACK = 87;
inline constexpr std::uint8_t BLOCK_GLOWSTONE = 89;
inline constexpr std::uint8_t BLOCK_PORTAL = 90; // nether portal
inline constexpr std::uint8_t BLOCK_END_STONE = 121;
// Rails / redstone (PE 0.14 + Genisys subset)
inline constexpr std::uint8_t BLOCK_POWERED_RAIL = 27;
inline constexpr std::uint8_t BLOCK_DETECTOR_RAIL = 28;
inline constexpr std::uint8_t BLOCK_REDSTONE_WIRE = 55;
inline constexpr std::uint8_t BLOCK_RAIL = 66;
inline constexpr std::uint8_t BLOCK_LEVER = 69;
inline constexpr std::uint8_t BLOCK_STONE_PRESSURE_PLATE = 70;
inline constexpr std::uint8_t BLOCK_WOODEN_PRESSURE_PLATE = 72;
inline constexpr std::uint8_t BLOCK_UNLIT_REDSTONE_TORCH = 75;
inline constexpr std::uint8_t BLOCK_REDSTONE_TORCH = 76;
inline constexpr std::uint8_t BLOCK_STONE_BUTTON = 77;
inline constexpr std::uint8_t BLOCK_UNPOWERED_REPEATER = 93;
inline constexpr std::uint8_t BLOCK_POWERED_REPEATER = 94;
inline constexpr std::uint8_t BLOCK_INACTIVE_REDSTONE_LAMP = 123;
inline constexpr std::uint8_t BLOCK_ACTIVE_REDSTONE_LAMP = 124;
inline constexpr std::uint8_t BLOCK_ACTIVATOR_RAIL = 126;
inline constexpr std::uint8_t BLOCK_WOODEN_BUTTON = 143;
inline constexpr std::uint8_t BLOCK_LIGHT_WEIGHTED_PRESSURE_PLATE = 147;
inline constexpr std::uint8_t BLOCK_HEAVY_WEIGHTED_PRESSURE_PLATE = 148;
// Comparator: PC/PE later ids; server supports for plugins / newer clients
inline constexpr std::uint8_t BLOCK_UNPOWERED_COMPARATOR = 149;
inline constexpr std::uint8_t BLOCK_POWERED_COMPARATOR = 150;
inline constexpr std::uint8_t BLOCK_DAYLIGHT_SENSOR = 151;
inline constexpr std::uint8_t BLOCK_REDSTONE_BLOCK = 152;
// PE 0.14 hopper block (registerBlock HopperBlock in libminecraftpe)
inline constexpr std::uint8_t BLOCK_HOPPER = 154;

// Dimensions (StartGame / ChangeDimension)
// PE 0.14 ChangeDimension only accepts NORMAL=0 and NETHER=1 (PM parity).
// End is a separate Level (folder "ender"); wire dimension stays 0 — dim=2 crashes clients.
inline constexpr std::uint8_t DIMENSION_OVERWORLD = 0;
inline constexpr std::uint8_t DIMENSION_NETHER = 1;
// Internal only (never put on ChangeDimension / prefer StartGame as OVERWORLD for End worlds)
inline constexpr std::uint8_t DIMENSION_END_INTERNAL = 2;

// EntityEvent event ids (PM EntityEventPacket)
inline constexpr std::uint8_t ENTITY_EVENT_HURT = 2;
inline constexpr std::uint8_t ENTITY_EVENT_DEATH = 3;

// Standing sign rotation 0..15 (PM SignPost place: floor(((yaw+180)*16/360)+0.5)&0x0F)
inline std::uint8_t signPostRotationMeta(float yaw) {
  float v = ((yaw + 180.f) * 16.f / 360.f) + 0.5f;
  int m = static_cast<int>(std::floor(v)) & 0x0F;
  return static_cast<std::uint8_t>(m);
}

// Horizontal facing meta for chest/furnace (PM place faces[])
// player getDirection: 0=S 1=W 2=N 3=E → meta 3,4,2,5
inline std::uint8_t horizontalFaceMeta(float yaw) {
  // PHP Entity::getDirection: rotation = (yaw - 90) % 360
  float rotation = std::fmod(yaw - 90.f, 360.f);
  if (rotation < 0.f) rotation += 360.f;
  int dir = 0; // South
  if ((0.f <= rotation && rotation < 45.f) || (315.f <= rotation && rotation < 360.f))
    dir = 2; // North
  else if (45.f <= rotation && rotation < 135.f)
    dir = 3; // East
  else if (135.f <= rotation && rotation < 225.f)
    dir = 0; // South
  else if (225.f <= rotation && rotation < 315.f)
    dir = 1; // West
  static constexpr std::uint8_t faces[4] = {4, 2, 5, 3}; // dir→meta
  return faces[dir & 3];
}

} // namespace mpmpes::protocol
