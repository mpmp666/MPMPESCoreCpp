// Liquid flow — port of pocketmine\block\Liquid (Water/Lava) scheduled updates.
#include "mpmpes/level/Level.hpp"
#include <algorithm>
#include <array>

namespace mpmpes::level {

namespace {
constexpr std::uint8_t WATER = 8, STILL_WATER = 9, LAVA = 10, STILL_LAVA = 11;
constexpr std::uint8_t OBSIDIAN = 49, COBBLESTONE = 4;

bool isLiquid(std::uint8_t id) { return id == WATER || id == STILL_WATER || id == LAVA || id == STILL_LAVA; }
bool isWater(std::uint8_t id) { return id == WATER || id == STILL_WATER; }
bool isLava(std::uint8_t id) { return id == LAVA || id == STILL_LAVA; }
// blocks a liquid can flow into (air + plants + non-solid)
bool canFlowInto(std::uint8_t id) {
  switch (id) {
    case 0: case 6: case 18: case 161: case 26: case 27: case 28: case 30: case 31: case 32:
    case 37: case 38: case 39: case 40: case 50: case 51: case 55: case 59: case 63: case 64:
    case 65: case 66: case 68: case 71: case 75: case 76: case 78: case 83: case 85: case 92:
    case 93: case 94: case 96: case 101: case 102: case 104: case 105: case 106: case 107:
    case 111: case 115: case 116: case 117: case 118: case 119: case 120: case 126: case 127:
    case 131: case 132: case 140: case 141: case 142: case 143: case 144: case 145: case 146:
    case 147: case 148: case 151: case 154: case 167: case 175: case 176: case 177: case 183:
    case 184: case 185: case 186: case 187: case 244: case 245: case 246: case 247:
      return true;
    default:
      return false;
  }
}
bool isSolidBlock(std::uint8_t id) { return !canFlowInto(id) && !isLiquid(id); }
} // namespace

void Level::scheduleUpdate(int x, int y, int z, int delay_ticks) {
  // overwrite same position (PHP scheduleUpdate replaces)
  for (auto& s : scheduled_blocks_)
    if (s.x == x && s.y == y && s.z == z) {
      s.due = static_cast<int>(tick_counter_) + delay_ticks;
      return;
    }
  scheduled_blocks_.push_back({x, y, z, static_cast<int>(tick_counter_) + delay_ticks});
}

void Level::onBlockChanged(int x, int y, int z) {
  // PHP: BLOCK_UPDATE_NORMAL on liquid schedules a flow; also flows neighbors' liquids
  auto id = getBlockId(x, y, z);
  if (isLiquid(id)) scheduleUpdate(x, y, z, isLava(id) ? 30 : 5);
  // adjacent liquids react to the changed block
  const int nb[4][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}};
  for (auto& [dx, dz] : nb) {
    auto nid = getBlockId(x + dx, y, z + dz);
    if (isLiquid(nid)) scheduleUpdate(x + dx, y, z + dz, isLava(nid) ? 30 : 5);
  }
  auto up = getBlockId(x, y + 1, z);
  if (isLiquid(up)) scheduleUpdate(x, y + 1, z, isLava(up) ? 30 : 5);
  auto dn = getBlockId(x, y - 1, z);
  if (isLiquid(dn)) scheduleUpdate(x, y - 1, z, isLava(dn) ? 30 : 5);
}

void Level::tickScheduled() {
  ++tick_counter_;
  if (scheduled_blocks_.empty()) return;
  std::vector<ScheduledBlock> due, keep;
  for (auto& s : scheduled_blocks_) (s.due <= tick_counter_ ? due : keep).push_back(s);
  scheduled_blocks_ = std::move(keep);
  for (auto& s : due) {
    auto id = getBlockId(s.x, s.y, s.z);
    if (!isLiquid(id)) continue;
    flowTick(s.x, s.y, s.z, id);
  }
}

} // namespace mpmpes::level

namespace mpmpes::level {
namespace {
// Liquid::getFlowDecay / getEffectiveFlowDecay / getSmallestFlowDecay
int getFlowDecayAt(Level& lvl, int x, int y, int z, std::uint8_t id) {
  auto b = lvl.getBlockId(x, y, z);
  if (!isLiquid(b)) return -1;
  return lvl.getBlockMeta(x, y, z);
}
int effectiveFlowDecay(Level& lvl, int x, int y, int z, std::uint8_t id) {
  auto b = lvl.getBlockId(x, y, z);
  if (!isLiquid(b)) return -1;
  int d = lvl.getBlockMeta(x, y, z);
  return d >= 8 ? 0 : d;
}

struct FlowState {
  Level& lvl;
  std::uint8_t id;
  bool lava;
  int multiplier;
  int adjacent_sources = 0;
};

int smallestFlowDecay(FlowState& st, int x, int y, int z, int decay) {
  int bd = getFlowDecayAt(st.lvl, x, y, z, st.id);
  if (bd < 0) return decay;
  if (bd == 0) ++st.adjacent_sources;
  else if (bd >= 8) bd = 0;
  return (decay >= 0 && bd >= decay) ? decay : bd;
}

// Liquid::calculateFlowCost
int flowCost(Level& lvl, int x, int y, int z, std::uint8_t id, int acc, int prevDir) {
  int cost = 1000;
  for (int j = 0; j < 4; ++j) {
    if ((j == 0 && prevDir == 1) || (j == 1 && prevDir == 0) || (j == 2 && prevDir == 3) ||
        (j == 3 && prevDir == 2))
      continue;
    int sx = x, sz = z;
    if (j == 0) --sx;
    else if (j == 1) ++sx;
    else if (j == 2) --sz;
    else ++sz;
    auto side = lvl.getBlockId(sx, y, sz);
    if (!canFlowInto(side) && !isLiquid(side)) continue;
    if (isLiquid(side) && lvl.getBlockMeta(sx, y, sz) == 0) continue;
    if (canFlowInto(lvl.getBlockId(sx, y - 1, sz))) return acc;
    if (acc >= 4) continue;
    int rc = flowCost(lvl, sx, y, sz, id, acc + 1, j);
    if (rc < cost) cost = rc;
  }
  return cost;
}

std::array<bool, 4> optimalFlowDirections(Level& lvl, int x, int y, int z, std::uint8_t id) {
  int cost[4] = {1000, 1000, 1000, 1000};
  const int dx[4] = {-1, 1, 0, 0}, dz[4] = {0, 0, -1, 1};
  for (int j = 0; j < 4; ++j) {
    int bx = x + dx[j], bz = z + dz[j];
    auto b = lvl.getBlockId(bx, y, bz);
    if (!canFlowInto(b) && !isLiquid(b)) continue;
    if (isLiquid(b) && lvl.getBlockMeta(bx, y, bz) == 0) continue;
    if (canFlowInto(lvl.getBlockId(bx, y - 1, bz))) {
      cost[j] = 0;
    } else {
      cost[j] = flowCost(lvl, x, y, z, id, 1, j);
    }
  }
  int minCost = *std::min_element(cost, cost + 4);
  std::array<bool, 4> out;
  for (int i = 0; i < 4; ++i) out[i] = cost[i] == minCost;
  return out;
}

void harden(Level& lvl, int x, int y, int z, std::uint8_t id, bool& changed) {
  if (!isLava(id)) return;
  bool colliding = false;
  const int dx[6] = {0, 0, 0, 0, -1, 1};
  const int dy[6] = {-1, 1, 0, 0, 0, 0};
  const int dz[6] = {0, 0, -1, 1, 0, 0};
  for (int s = 0; s < 6 && !colliding; ++s)
    colliding = isWater(lvl.getBlockId(x + dx[s], y + dy[s], z + dz[s]));
  if (colliding) {
    int meta = lvl.getBlockMeta(x, y, z);
    if (meta == 0) {
      lvl.setBlock(x, y, z, OBSIDIAN, 0);
    } else if (meta <= 4) {
      lvl.setBlock(x, y, z, COBBLESTONE, 0);
    }
    changed = true;
  }
}

void flowInto(Level& lvl, int x, int y, int z, std::uint8_t id, int newDecay, int tickRate, bool& changed) {
  auto target = lvl.getBlockId(x, y, z);
  if (!canFlowInto(target)) return;
  if (isLava(target)) {
    harden(lvl, x, y, z, id, changed); // lava mix: fizz + maybe solidify
    changed = true;
  } else if (target != 0) {
    // break plants etc (drops omitted at gen-time; in-game break would drop)
  }
  lvl.setBlock(x, y, z, isLava(id) ? LAVA : WATER, static_cast<std::uint8_t>(newDecay));
  lvl.scheduleUpdate(x, y, z, tickRate);
  changed = true;
}
} // namespace
} // namespace mpmpes::level

namespace mpmpes::level {

void Level::flowTick(int x, int y, int z, std::uint8_t id) {
  const int tickRate = isLava(id) ? 30 : 5;
  bool changed = false;
  harden(*this, x, y, z, id, changed);

  int decay = getBlockMeta(x, y, z);
  FlowState st{*this, id, isLava(id), isLava(id) ? 2 : 1, 0};

  if (decay > 0) {
    int smallest = -100;
    smallest = smallestFlowDecay(st, x, y, z - 1, smallest);
    smallest = smallestFlowDecay(st, x, y, z + 1, smallest);
    smallest = smallestFlowDecay(st, x - 1, y, z, smallest);
    smallest = smallestFlowDecay(st, x + 1, y, z, smallest);
    int k = smallest + st.multiplier;
    if (k >= 8 || smallest < 0) k = -1;
    int topDecay = getFlowDecayAt(*this, x, y + 1, z, id);
    if (topDecay >= 0) k = topDecay >= 8 ? topDecay : (topDecay | 0x08);
    if (st.adjacent_sources >= 2 && isWater(id)) {
      auto below = getBlockId(x, y - 1, z);
      if (isSolidBlock(below)) k = 0;
      else if (isWater(below) && getBlockMeta(x, y - 1, z) == 0) k = 0;
    }
    if (isLava(id) && decay < 8 && k < 8 && k > 1) {
      k = decay; // lava mostly keeps level (PHP random skip)
    }
    if (k != decay) {
      decay = k;
      if (decay < 0) {
        setBlock(x, y, z, 0, 0);
        changed = true;
      } else {
        setBlock(x, y, z, id, static_cast<std::uint8_t>(decay));
        scheduleUpdate(x, y, z, tickRate);
        changed = true;
      }
    }
  }

  auto below = getBlockId(x, y - 1, z);
  if (canFlowInto(below) || isLiquid(below)) {
    if (isLava(id) && isWater(below)) {
      // lava over water -> stone
      setBlock(x, y - 1, z, 1, 0);
      changed = true;
    } else if (isWater(below) && getBlockMeta(x, y - 1, z) == 0 && isWater(id)) {
      // merging with a source below: no extra flow
    } else {
      if (decay >= 8) flowInto(*this, x, y - 1, z, id, decay, tickRate, changed);
      else flowInto(*this, x, y - 1, z, id, decay | 0x08, tickRate, changed);
    }
  } else if (decay >= 0 && (decay == 0 || !canFlowInto(below))) {
    auto flags = optimalFlowDirections(*this, x, y, z, id);
    int l = decay + st.multiplier;
    if (decay >= 8) l = 1;
    if (l < 8) {
      const int dx[4] = {-1, 1, 0, 0}, dz[4] = {0, 0, -1, 1};
      for (int j = 0; j < 4; ++j)
        if (flags[j]) flowInto(*this, x + dx[j], y, z + dz[j], id, l, tickRate, changed);
    } else {
      harden(*this, x, y, z, id, changed);
    }
  }
  if (changed && on_block_update_)
    on_block_update_(x, y, z, getBlockId(x, y, z), getBlockMeta(x, y, z));
}

} // namespace mpmpes::level
