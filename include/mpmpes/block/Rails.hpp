#pragma once

// PE 0.14 rail auto-connect (PocketMine Rail.php / PoweredRail.php subset).
// Meta 0-5: NS / EW / ascend E / ascend W / ascend N / ascend S
// Meta 6-9 (normal rail only): SE / SW / NW / NE curves

#include "mpmpes/level/Level.hpp"
#include "mpmpes/protocol/Info.hpp"

#include <array>
#include <cmath>
#include <cstdint>
#include <utility>
#include <vector>

namespace mpmpes::block {

inline bool isRailId(std::uint8_t id) {
  return id == protocol::BLOCK_RAIL || id == protocol::BLOCK_POWERED_RAIL ||
         id == protocol::BLOCK_DETECTOR_RAIL || id == protocol::BLOCK_ACTIVATOR_RAIL;
}

inline bool isNormalRail(std::uint8_t id) { return id == protocol::BLOCK_RAIL; }

// Connection offsets for each meta (xz pairs) — Rail::check::$array
inline const std::array<std::array<std::array<int, 2>, 2>, 10>& railConnTable() {
  static const std::array<std::array<std::array<int, 2>, 2>, 10> t = {{
      {{{{0, 1}}, {{0, -1}}}},   // 0 NS
      {{{{1, 0}}, {{-1, 0}}}},   // 1 EW
      {{{{1, 0}}, {{-1, 0}}}},   // 2 ascend E
      {{{{1, 0}}, {{-1, 0}}}},   // 3 ascend W
      {{{{0, 1}}, {{0, -1}}}},   // 4 ascend N
      {{{{0, 1}}, {{0, -1}}}},   // 5 ascend S
      {{{{1, 0}}, {{0, 1}}}},    // 6 SE
      {{{{0, 1}}, {{-1, 0}}}},   // 7 SW
      {{{{-1, 0}}, {{0, -1}}}},  // 8 NW
      {{{{0, -1}}, {{1, 0}}}},   // 9 NE
  }};
  return t;
}

inline int railBaseMeta(std::uint8_t id, std::uint8_t meta) {
  // powered/detector/activator: bit 0x8 = powered flag
  if (id != protocol::BLOCK_RAIL) return meta & 0x7;
  return meta & 0x0f;
}

inline std::uint8_t railPackMeta(std::uint8_t id, int base, bool powered) {
  if (id == protocol::BLOCK_RAIL) return static_cast<std::uint8_t>(base & 0x0f);
  return static_cast<std::uint8_t>((base & 0x7) | (powered ? 0x8 : 0));
}

struct RailNeighbor {
  int x = 0, y = 0, z = 0;
  std::uint8_t id = 0;
  std::uint8_t meta = 0;
};

// How many ends of this rail already "claim" a neighbor (0..2)
inline int railConnectedCount(level::Level& lvl, int x, int y, int z) {
  const auto id = lvl.getBlockId(x, y, z);
  if (!isRailId(id)) return 0;
  const int base = railBaseMeta(id, lvl.getBlockMeta(x, y, z));
  if (base < 0 || base > 9) return 0;
  // special rails only use 0-5
  const int use = (id != protocol::BLOCK_RAIL && base > 5) ? 0 : base;
  const auto& ends = railConnTable()[static_cast<std::size_t>(use)];
  int n = 0;
  for (int i = 0; i < 2; ++i) {
    const int dx = ends[static_cast<std::size_t>(i)][0];
    const int dz = ends[static_cast<std::size_t>(i)][1];
    for (int dy : {0, 1, -1}) {
      const int nx = x + dx, ny = y + dy, nz = z + dz;
      if (!isRailId(lvl.getBlockId(nx, ny, nz))) continue;
      // neighbor should point back toward us in xz
      const auto nid = lvl.getBlockId(nx, ny, nz);
      int nbase = railBaseMeta(nid, lvl.getBlockMeta(nx, ny, nz));
      if (nid != protocol::BLOCK_RAIL && nbase > 5) nbase = 0;
      if (nbase < 0 || nbase > 9) continue;
      const auto& nends = railConnTable()[static_cast<std::size_t>(nbase)];
      bool back = false;
      for (int j = 0; j < 2; ++j) {
        if (nends[static_cast<std::size_t>(j)][0] == -dx &&
            nends[static_cast<std::size_t>(j)][1] == -dz) {
          back = true;
          break;
        }
      }
      if (back) {
        ++n;
        break;
      }
    }
  }
  return n;
}

// Can this existing rail accept one more connection from (fromx,fromy,fromz)?
inline bool railCanAccept(level::Level& lvl, int x, int y, int z, int fromx, int fromy, int fromz) {
  (void)fromy;
  if (!isRailId(lvl.getBlockId(x, y, z))) return false;
  if (railConnectedCount(lvl, x, y, z) >= 2) return false;
  // special rails: refuse pure 90° turn off existing straight axis when already connected once
  const auto id = lvl.getBlockId(x, y, z);
  if (id != protocol::BLOCK_RAIL) {
    const int c = railConnectedCount(lvl, x, y, z);
    if (c == 1) {
      // if already has a neighbor, new one must be colinear (not curve)
      const int base = railBaseMeta(id, lvl.getBlockMeta(x, y, z));
      const int use = base > 5 ? 0 : base;
      const auto& ends = railConnTable()[static_cast<std::size_t>(use)];
      // find existing connection direction
      int ex = 0, ez = 0;
      bool found = false;
      for (int i = 0; i < 2 && !found; ++i) {
        for (int dy : {0, 1, -1}) {
          const int nx = x + ends[static_cast<std::size_t>(i)][0];
          const int nz = z + ends[static_cast<std::size_t>(i)][1];
          if (isRailId(lvl.getBlockId(nx, y + dy, nz))) {
            ex = ends[static_cast<std::size_t>(i)][0];
            ez = ends[static_cast<std::size_t>(i)][1];
            found = true;
            break;
          }
        }
      }
      if (found) {
        const int dx = fromx - x;
        const int dz = fromz - z;
        // curve would be |dx|==|ez| style perpendicular
        if (std::abs(dx) == std::abs(ez) && std::abs(dz) == std::abs(ex) && (dx != 0 || dz != 0)) {
          // if existing is along ex,ez and new is perpendicular
          if ((ex != 0 && dz != 0 && dx == 0) || (ez != 0 && dx != 0 && dz == 0)) return false;
        }
      }
    }
  }
  return true;
}

// Compute meta from up to 2 connected relative offsets (dx,dy,dz)
inline int metaFromConnections(const std::vector<std::array<int, 3>>& rels, bool allow_curve) {
  if (rels.empty()) return 0;
  if (rels.size() == 1) {
    const auto& v = rels[0];
    if (v[1] != 1) {
      return (v[0] == 0) ? 0 : 1; // NS if x==0 else EW
    }
    // ascending toward neighbor above
    if (v[2] == 0) {
      // (x/-2)+2.5 → x==1 → 2, x==-1 → 3
      return (v[0] == 1) ? 2 : 3;
    }
    // (z/2)+4.5 → z==1 → 5, z==-1 → 4
    return (v[2] == 1) ? 5 : 4;
  }
  // two connections
  const auto& a = rels[0];
  const auto& b = rels[1];
  // curve: perpendicular flat
  if (allow_curve && std::abs(a[0]) == std::abs(b[2]) && std::abs(b[0]) == std::abs(a[2]) &&
      a[1] == 0 && b[1] == 0) {
    const int sx = a[0] + b[0];
    const int sz = a[2] + b[2];
    if (sx == 1) return (sz == 1) ? 6 : 9;
    return (sz == 1) ? 7 : 8;
  }
  // slope
  if (a[1] == 1 || b[1] == 1) {
    const auto& v = (a[1] == 1) ? a : b;
    if (v[0] == 0) return (v[2] == -1) ? 4 : 5;
    return (v[0] == 1) ? 2 : 3;
  }
  return (a[0] == 0) ? 0 : 1;
}

// Recompute and set meta for rail at x,y,z based on neighbors that connect.
// Updates block in level; returns new meta. Does not broadcast.
inline std::uint8_t recomputeRailMeta(level::Level& lvl, int x, int y, int z) {
  const auto id = lvl.getBlockId(x, y, z);
  if (!isRailId(id)) return 0;
  const bool powered = (id != protocol::BLOCK_RAIL) && (lvl.getBlockMeta(x, y, z) & 0x8);
  const bool allow_curve = isNormalRail(id);

  static constexpr int xz[4][2] = {{1, 0}, {0, 1}, {-1, 0}, {0, -1}};
  std::vector<std::array<int, 3>> connected;
  for (auto& d : xz) {
    bool got = false;
    for (int dy : {0, 1, -1}) {
      const int nx = x + d[0], ny = y + dy, nz = z + d[1];
      if (!isRailId(lvl.getBlockId(nx, ny, nz))) continue;
      // Prefer neighbors that can accept us, or already point at us
      if (!railCanAccept(lvl, nx, ny, nz, x, y, z) &&
          railConnectedCount(lvl, nx, ny, nz) >= 2) {
        // still allow if their meta already faces us
        bool faces = false;
        const auto nid = lvl.getBlockId(nx, ny, nz);
        int nbase = railBaseMeta(nid, lvl.getBlockMeta(nx, ny, nz));
        if (nid != protocol::BLOCK_RAIL && nbase > 5) nbase = 0;
        if (nbase >= 0 && nbase <= 9) {
          for (int j = 0; j < 2; ++j) {
            const auto& e = railConnTable()[static_cast<std::size_t>(nbase)][static_cast<std::size_t>(j)];
            if (nx + e[0] == x && nz + e[1] == z) faces = true;
          }
        }
        if (!faces) continue;
      }
      connected.push_back({d[0], dy, d[1]});
      got = true;
      break;
    }
    if (connected.size() == 2) break;
    (void)got;
  }

  int base = metaFromConnections(connected, allow_curve);
  if (!allow_curve && base > 5) base = (base == 6 || base == 8) ? 1 : 0;
  const auto meta = railPackMeta(id, base, powered);
  lvl.setBlock(x, y, z, id, meta);
  return meta;
}

// Place rail and auto-merge with neighbors (updates self + adjacent rails).
// Returns false if cannot place (no solid below / on rail).
// out_updates: list of (x,y,z,id,meta) that changed for broadcast.
inline bool placeRailAuto(level::Level& lvl, int x, int y, int z, std::uint8_t rail_id,
                          std::vector<std::array<int, 5>>& out_updates) {
  if (!isRailId(rail_id)) return false;
  if (y <= 0 || y >= 128) return false;
  if (lvl.getBlockId(x, y, z) != 0) return false;
  const auto down = lvl.getBlockId(x, y - 1, z);
  if (down == 0 || isRailId(down)) return false; // need solid-ish support (not air/rail)

  // Place provisional
  lvl.setBlock(x, y, z, rail_id, 0);

  // Connect to neighbors: first let neighbors reorient toward us if they can accept
  static constexpr int xz[4][2] = {{1, 0}, {0, 1}, {-1, 0}, {0, -1}};
  std::vector<std::array<int, 3>> touched; // include self
  touched.push_back({x, y, z});

  for (auto& d : xz) {
    for (int dy : {0, 1, -1}) {
      const int nx = x + d[0], ny = y + dy, nz = z + d[1];
      if (!isRailId(lvl.getBlockId(nx, ny, nz))) continue;
      if (railCanAccept(lvl, nx, ny, nz, x, y, z) || railConnectedCount(lvl, nx, ny, nz) < 2) {
        touched.push_back({nx, ny, nz});
      }
      break;
    }
  }

  // Recompute all touched (self first for better curves)
  for (auto& p : touched) {
    const auto id = lvl.getBlockId(p[0], p[1], p[2]);
    const auto meta = recomputeRailMeta(lvl, p[0], p[1], p[2]);
    out_updates.push_back({p[0], p[1], p[2], static_cast<int>(id), static_cast<int>(meta)});
  }

  // Second pass: neighbors again so mutual connect settles
  for (std::size_t i = 1; i < touched.size(); ++i) {
    auto& p = touched[i];
    const auto id = lvl.getBlockId(p[0], p[1], p[2]);
    const auto meta = recomputeRailMeta(lvl, p[0], p[1], p[2]);
    // update existing entry or push
    bool found = false;
    for (auto& u : out_updates) {
      if (u[0] == p[0] && u[1] == p[1] && u[2] == p[2]) {
        u[3] = id;
        u[4] = meta;
        found = true;
        break;
      }
    }
    if (!found) out_updates.push_back({p[0], p[1], p[2], static_cast<int>(id), static_cast<int>(meta)});
  }
  {
    const auto id = lvl.getBlockId(x, y, z);
    const auto meta = recomputeRailMeta(lvl, x, y, z);
    for (auto& u : out_updates) {
      if (u[0] == x && u[1] == y && u[2] == z) {
        u[3] = id;
        u[4] = meta;
        break;
      }
    }
  }
  return true;
}

// After break, recompute adjacent rails
inline void updateRailsAround(level::Level& lvl, int x, int y, int z,
                              std::vector<std::array<int, 5>>& out_updates) {
  static constexpr int xz[4][2] = {{1, 0}, {0, 1}, {-1, 0}, {0, -1}};
  for (auto& d : xz) {
    for (int dy : {0, 1, -1}) {
      const int nx = x + d[0], ny = y + dy, nz = z + d[1];
      if (!isRailId(lvl.getBlockId(nx, ny, nz))) continue;
      const auto id = lvl.getBlockId(nx, ny, nz);
      const auto meta = recomputeRailMeta(lvl, nx, ny, nz);
      out_updates.push_back({nx, ny, nz, static_cast<int>(id), static_cast<int>(meta)});
      break;
    }
  }
}

// Motion helper for minecart: given rail meta, return unit direction along track
// prefer_dir: approximate desired motion (dx,dz); picks matching end.
inline void railMotion(int base_meta, float prefer_dx, float prefer_dz, float& out_dx, float& out_dy,
                       float& out_dz) {
  out_dx = out_dy = out_dz = 0.f;
  int m = base_meta;
  if (m < 0) m = 0;
  if (m > 9) m = 0;
  const auto& ends = railConnTable()[static_cast<std::size_t>(m)];
  // two ends as directions from center
  float best = -1e9f;
  int best_i = 0;
  for (int i = 0; i < 2; ++i) {
    const float dx = static_cast<float>(ends[static_cast<std::size_t>(i)][0]);
    const float dz = static_cast<float>(ends[static_cast<std::size_t>(i)][1]);
    const float dot = dx * prefer_dx + dz * prefer_dz;
    if (dot > best) {
      best = dot;
      best_i = i;
    }
  }
  out_dx = static_cast<float>(ends[static_cast<std::size_t>(best_i)][0]);
  out_dz = static_cast<float>(ends[static_cast<std::size_t>(best_i)][1]);
  // ascending metas: climb when moving toward high end
  if (m == 2) out_dy = (out_dx > 0) ? 1.f : 0.f;      // ascend east
  else if (m == 3) out_dy = (out_dx < 0) ? 1.f : 0.f; // ascend west
  else if (m == 4) out_dy = (out_dz < 0) ? 1.f : 0.f; // ascend north
  else if (m == 5) out_dy = (out_dz > 0) ? 1.f : 0.f; // ascend south
  else out_dy = 0.f;
}

} // namespace mpmpes::block
