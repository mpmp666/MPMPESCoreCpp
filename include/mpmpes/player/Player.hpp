#pragma once

#include "mpmpes/item/Item.hpp"
#include "mpmpes/level/Level.hpp"
#include "mpmpes/raklib/Session.hpp"
#include "mpmpes/raklib/SessionManager.hpp"

#include <array>
#include <cstdint>
#include <set>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace mpmpes::player {

enum class PlayerState {
  Connected,
  LoggingIn,
  Playing,
  Closed,
};

struct Player {
  raklib::Endpoint endpoint;
  std::string username;
  std::int64_t client_id = 0;
  std::int64_t entity_id = 0; // runtime eid (StartGame uses 0 for self; list uses this)
  std::int32_t protocol = 0;
  PlayerState state = PlayerState::Connected;

  std::array<std::uint8_t, 16> uuid{};
  std::string skin_name = "Standard_Custom";
  std::string skin_data; // 64*32*4 or 64*64*4 RGBA

  level::Level* level = nullptr;
  float x = 0, y = 5, z = 0;
  float yaw = 0, pitch = 0;
  int gamemode = 0; // overwritten by server.properties / players.dat on login
  int health = 20;
  int chunk_radius = 4;
  bool spawned = false;
  int selected_hotbar = 0; // 0-8
  // hotbar_link[i] = inventory index linked to hotbar slot i; -1 = empty link
  // Client wire index for hotbar is inventory_index + 9 (PM PlayerInventory)
  std::array<std::int32_t, 9> hotbar_link{0, 1, 2, 3, 4, 5, 6, 7, 8};
  std::vector<item::ItemStack> inventory; // 36 slots (0-8 hotbar items, 9-35 main)
  std::vector<item::ItemStack> armor;     // 4

  // break progress
  int breaking_x = 0, breaking_y = -1, breaking_z = 0;
  int break_ticks = 0;

  // ContainerSetSlot transaction group (PM SimpleTransactionGroup)
  // Each entry: apply target to slot after balance check; source is server item at add-time.
  struct SlotTx {
    std::uint8_t window_id = 0; // 0 inventory, 0x78 armor (armor slots are 0-3 in armor[])
    std::int16_t slot = 0;
    item::ItemStack source; // server item when packet arrived
    item::ItemStack target; // client-requested item
  };
  std::vector<SlotTx> pending_txs;
  double pending_tx_time = 0; // wall seconds; expire ~8s like PM

  // Open container window: window_id 2..99
  // open_container_type: 0=chest(27), 2=furnace(3), 8=hopper(5)
  std::uint8_t open_window_id = 0; // 0 = none
  std::uint8_t open_container_type = 0;
  int open_chest_x = 0, open_chest_y = -1, open_chest_z = 0;
  // Double chest: partner half (INT_MIN = single). UI slots 0..26 left, 27..53 right.
  int open_pair_x = 0x80000000, open_pair_z = 0x80000000;
  // Entity container (chest/hopper minecart). 0 = block-based container.
  std::int64_t open_entity_eid = 0;
  bool openChestPaired() const { return open_pair_x != static_cast<int>(0x80000000); }
  bool openEntityContainer() const { return open_entity_eid != 0; }

  // Loaded from players/*.dat before spawn
  bool has_saved_data = false;
  std::string saved_world_name;

  // Nether portal: stand in portal to travel (cooldown ticks ~4s)
  int portal_ticks_ = 0;
  int portal_cooldown_ = 0; // after travel, ignore portal briefly

  // Survival damage tracking
  float fall_distance_ = 0.f;
  bool on_ground_ = true;
  int fire_ticks_ = 0;      // standing in fire/lava
  int hurt_cooldown_ = 0;   // i-frames after damage
  int death_ticks_ = -1;    // >=0 while dead awaiting respawn

  // Client sky/atmosphere dim override (any wire value). -1 = follow level.
  // Used by visual-dim experiments without world switch; not remapped/whitelist-filtered.
  int visual_dim = -1;

  // Minecart ride: vehicle entity eid (0 = not riding)
  std::int64_t riding_eid = 0;
  // PlayerInput (0xbe) while riding: motX=strafe, motY=forward (-1..1); jump/sneak flags
  float ride_input_x = 0.f;
  float ride_input_y = 0.f;
  bool ride_jumping = false;
  bool ride_sneaking = false;

  std::set<std::pair<int, int>> sent_chunks;
  std::set<std::int64_t> known_entities;
  // Other players' runtime entity_ids currently shown via AddPlayer (not mobs)
  std::set<std::int64_t> known_players;

  std::string key() const {
    return endpoint.address + ":" + std::to_string(endpoint.port);
  }

  item::ItemStack& heldItem() {
    if (inventory.empty()) {
      static item::ItemStack air;
      return air;
    }
    int inv = selected_hotbar;
    if (selected_hotbar >= 0 && selected_hotbar < 9) {
      inv = hotbar_link[static_cast<std::size_t>(selected_hotbar)];
    }
    if (inv < 0 || inv >= static_cast<int>(inventory.size())) {
      static item::ItemStack air;
      return air;
    }
    return inventory[static_cast<std::size_t>(inv)];
  }

  const item::ItemStack& heldItem() const {
    return const_cast<Player*>(this)->heldItem();
  }

  // Wire hotbar array for ContainerSetContent (PM: index + 9, or -1).
  // Clamp links so client never sees out-of-range inv indices (Win PE crash risk on E).
  std::vector<std::int32_t> wireHotbar() const {
    std::vector<std::int32_t> hb(9, -1);
    const int inv_n = static_cast<int>(inventory.size());
    for (int i = 0; i < 9; ++i) {
      int inv = hotbar_link[static_cast<std::size_t>(i)];
      if (inv < 0 || (inv_n > 0 && inv >= inv_n)) {
        hb[static_cast<std::size_t>(i)] = -1;
      } else {
        hb[static_cast<std::size_t>(i)] = inv + 9;
      }
    }
    return hb;
  }
};

class PlayerManager {
public:
  explicit PlayerManager(raklib::SessionManager& sessions);

  Player* get(const raklib::Endpoint& ep);
  Player& getOrCreate(const raklib::Endpoint& ep, std::int64_t client_id);
  void remove(const raklib::Endpoint& ep);

  // Outbound Batch: if payload size >= threshold_kb*1024, wrap as zlib Batch 0x92
  void setNetworkBatch(int compression_level, int threshold_kb) {
    compression_level_ = compression_level;
    batch_threshold_kb_ = threshold_kb;
  }

  void sendPacket(Player& p, std::string payload, bool immediate = false);
  void sendChunksAround(Player& p, int radius_override = -1);
  void doLoginSequence(Player& p);

  void broadcastPacket(const std::string& payload, level::Level* level = nullptr,
                       const Player* except = nullptr);
  void broadcastNear(float x, float y, float z, float radius, const std::string& payload,
                     level::Level* level = nullptr, const Player* except = nullptr);

  void sendInventory(Player& p);
  void sendCreativeContents(Player& p);
  void sendCraftingData(Player& p);
  void sendPlayerListTo(Player& p);
  void broadcastPlayerListAdd(const Player& joined);
  void broadcastPlayerListRemove(const Player& left);

  // Persist player pos/inv/gm under players/<name>.dat
  void setPlayersDataPath(std::string path) { players_data_path_ = std::move(path); }
  bool savePlayer(const Player& p) const;
  bool loadPlayer(Player& p) const;
  void saveAllPlayers() const;

  std::size_t count() const { return players_.size(); }
  std::unordered_map<std::string, Player>& all() { return players_; }

  void setDefaultLevel(level::Level* lvl) { default_level_ = lvl; }
  level::Level* defaultLevel() const { return default_level_; }

  void broadcastText(std::string_view message);
  void broadcastChat(std::string_view source, std::string_view message);

  std::int64_t nextEntityId() { return next_entity_id_++; }

private:
  std::string maybeBatch(std::string payload) const;

  raklib::SessionManager& sessions_;
  level::Level* default_level_ = nullptr;
  std::unordered_map<std::string, Player> players_;
  std::int64_t next_entity_id_ = 1; // 0 reserved for self view in StartGame
  int compression_level_ = 7;
  int batch_threshold_kb_ = 1; // -1 = off
  std::string players_data_path_ = "players";
};

} // namespace mpmpes::player
