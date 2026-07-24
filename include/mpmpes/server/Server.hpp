#pragma once

#include "mpmpes/entity/Entity.hpp"
#include "mpmpes/level/Level.hpp"
#include "mpmpes/player/Player.hpp"
#include "mpmpes/plugin/PluginManager.hpp"
#include "mpmpes/raklib/SessionManager.hpp"
#include "mpmpes/raklib/UDPServerSocket.hpp"
#include "mpmpes/server/Permission.hpp"
#include "mpmpes/server/ServerConfig.hpp"

#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <queue>
#include <string>
#include <thread>

namespace mpmpes::server {

class Server {
public:
  explicit Server(ServerConfig cfg);
  ~Server();

  void start();
  void stop();
  void runLoop();

  const ServerConfig& config() const { return cfg_; }
  plugin::PluginManager& plugins() { return plugins_; }
  level::LevelManager& levels() { return levels_; }
  entity::EntityManager& entities() { return entities_; }

private:
  std::string buildRaklibName() const;
  void handleMcpePayload(const raklib::Endpoint& ep, const raklib::EncapsulatedPacket& pk);
  // buffer is bare MCPE packet starting with pid (0x8f...), no 0x8e prefix
  void dispatchMcpePacket(player::Player& p, std::string_view buffer);
  void handleBatch(player::Player& p, std::string_view buffer);
  void handleLogin(player::Player& p, std::string_view buffer);
  void handleText(player::Player& p, std::string_view buffer);
  void handleMove(player::Player& p, std::string_view buffer);
  void handleChunkRadius(player::Player& p, std::string_view buffer);
  void handlePlayerAction(player::Player& p, std::string_view buffer);
  void handleRemoveBlock(player::Player& p, std::string_view buffer);
  void handleUseItem(player::Player& p, std::string_view buffer);
  void handleMobEquipment(player::Player& p, std::string_view buffer);
  void handleContainerSetSlot(player::Player& p, std::string_view buffer);
  void handleCraftingEvent(player::Player& p, std::string_view buffer);
  void handleInteract(player::Player& p, std::string_view buffer);
  void handleDropItem(player::Player& p, std::string_view buffer);
  void handleBuiltinCommand(player::Player& p, std::string_view cmd, std::string_view args);

  // Unified command dispatch (player or console). reply() delivers feedback.
  using CommandReply = std::function<void(std::string_view)>;
  void dispatchCommand(std::string_view source_name, PermLevel level, std::string_view cmd,
                       std::string_view args, player::Player* player, const CommandReply& reply);
  void handleConsoleLine(std::string line);
  void processConsoleQueue();
  void consoleThreadMain();

  // Moderation: clean disconnect (Disconnect packet + RakNet session close)
  player::Player* findPlayerByName(std::string_view name);
  void kickPlayer(player::Player& p, std::string_view reason);
  // Returns ban reason string if banned by name/ip/cid, else empty optional
  std::optional<std::string> checkBanned(const player::Player& p) const;

  void breakBlock(player::Player& p, int x, int y, int z);
  void placeBlock(player::Player& p, int x, int y, int z, std::uint8_t face,
                  const item::ItemStack& held);
  // Flint & steel on obsidian → try light nether portal frame; else place fire
  bool tryLightNetherPortal(player::Player& p, int ox, int oy, int oz);
  // Teleport player between overworld <-> nether (or /goto style world switch)
  void changePlayerWorld(player::Player& p, level::Level* target, float x, float y, float z,
                         bool from_portal = false);
  void tickPlayerPortals();
  // Survival: fall / fire / lava; respawn after death
  void damagePlayer(player::Player& p, float amount, const char* cause);
  void tickPlayerDamage();
  void respawnPlayer(player::Player& p);
  void broadcastBlockUpdate(level::Level* level, int x, int y, int z, std::uint8_t id,
                            std::uint8_t meta, player::Player* except = nullptr);
  void spawnEntityToPlayer(player::Player& p, const entity::Entity& e);
  void syncEntitiesToPlayer(player::Player& p);
  void tickEntities();
  void tickItemPickups();
  void tickFurnaces();
  void tickAutosave();
  void saveEverything(bool force_chunks = false);

  // Drop / throw: spawn world item entity with motion
  entity::Entity* dropItemInWorld(level::Level* level, float x, float y, float z,
                                  item::ItemStack stack, float mx, float my, float mz,
                                  int pickup_delay = 10);
  // Player Q / ACTION_DROP: remove from inv and throw along look vector
  bool playerThrowHeld(player::Player& p, item::ItemStack stack, bool full_stack);

  // Container windows (chest type 0 / furnace type 2)
  void openChest(player::Player& p, int x, int y, int z);
  void openFurnace(player::Player& p, int x, int y, int z);
  void closeContainer(player::Player& p, bool send_close_pk = true);
  void closeChest(player::Player& p, bool send_close_pk = true) { closeContainer(p, send_close_pk); }
  void handleContainerClose(player::Player& p, std::string_view buffer);
  void broadcastChestLid(level::Level* level, int x, int y, int z, bool open);

  void networkThreadMain();

  int autosave_ticks_left_ = 0;

  ServerConfig cfg_;
  raklib::UDPServerSocket socket_;
  std::unique_ptr<raklib::SessionManager> sessions_holder_;
  raklib::SessionManager* sessions_ = nullptr;
  std::unique_ptr<player::PlayerManager> players_;
  level::LevelManager levels_;
  entity::EntityManager entities_;
  plugin::PluginManager plugins_;
  std::atomic<bool> running_{false};
  std::int64_t server_id_ = 0;
  int tick_counter_ = 0;

  // Network I/O on dedicated thread; game tick stays on main (lock around shared state)
  std::thread network_thread_;
  std::mutex game_mutex_;
  bool network_thread_started_ = false;

  OpsList ops_;
  BanList bans_;
  std::thread console_thread_;
  std::mutex console_mu_;
  std::queue<std::string> console_queue_;
  bool console_thread_started_ = false;
};

} // namespace mpmpes::server
