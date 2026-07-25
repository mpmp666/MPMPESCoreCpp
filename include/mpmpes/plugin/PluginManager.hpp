#pragma once

#include "mpmpes/config/ServerProperties.hpp"
#include "mpmpes/plugin/PluginApi.h"

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace mpmpes::plugin {

struct SessionOpenEvent {
  std::string address;
  std::uint16_t port = 0;
  std::int64_t client_id = 0;
};

struct SessionCloseEvent {
  std::string address;
  std::uint16_t port = 0;
  std::string reason;
};

struct PlayerLoginEvent {
  std::string address;
  std::uint16_t port = 0;
  std::string username;
  std::int32_t protocol = 0;
  std::int64_t client_id = 0;
  std::string world;
};

struct ServerStartEvent {
  std::string motd;
  std::uint16_t port = 0;
};

struct PlayerJoinEvent {
  std::string username;
  std::string world;
  float x = 0, y = 0, z = 0;
};

struct PlayerQuitEvent {
  std::string username;
  std::string reason;
};

struct ChatEvent {
  std::string username;
  std::string message;
  bool cancelled = false;
};

struct CommandEvent {
  std::string username;
  std::string command;
  std::string args;
  bool handled = false;
};

struct MoveEvent {
  std::string username;
  float x = 0, y = 0, z = 0;
  float yaw = 0, pitch = 0;
};

struct BlockEvent {
  std::string username;
  std::int32_t x = 0, y = 0, z = 0;
  std::int32_t action = 0;
  std::int32_t face = 0;
};

struct SignChangeEvent {
  std::string username;
  std::int32_t x = 0, y = 0, z = 0;
  std::string text1, text2, text3, text4;
  bool cancelled = false;
};

struct WorldLoadEvent {
  std::string name;
  std::int32_t generator = 0;
  std::int32_t seed = 0;
};

class IPlugin {
public:
  virtual ~IPlugin() = default;
  virtual const config::PluginManifest& manifest() const = 0;
  virtual bool enable(std::string& error) = 0;
  virtual void disable() = 0;
  virtual void onServerStart(const ServerStartEvent& ev) = 0;
  virtual void onSessionOpen(const SessionOpenEvent& ev) = 0;
  virtual void onSessionClose(const SessionCloseEvent& ev) = 0;
  virtual void onPlayerLogin(const PlayerLoginEvent& ev) = 0;
  virtual void onPlayerJoin(const PlayerJoinEvent& ev) = 0;
  virtual void onPlayerQuit(const PlayerQuitEvent& ev) = 0;
  virtual void onChat(ChatEvent& ev) = 0;
  virtual void onCommand(CommandEvent& ev) = 0;
  virtual void onMove(const MoveEvent& ev) = 0;
  virtual void onBlock(const BlockEvent& ev) = 0;
  virtual void onSignChange(SignChangeEvent& ev) = 0;
  virtual void onWorldLoad(const WorldLoadEvent& ev) = 0;
};

class PluginManager {
public:
  PluginManager() = default;
  ~PluginManager();

  PluginManager(const PluginManager&) = delete;
  PluginManager& operator=(const PluginManager&) = delete;

  void loadAll(std::string_view plugins_dir);
  void enableAll();
  void disableAll();

  void fireServerStart(const ServerStartEvent& ev);
  void fireSessionOpen(const SessionOpenEvent& ev);
  void fireSessionClose(const SessionCloseEvent& ev);
  void firePlayerLogin(const PlayerLoginEvent& ev);
  void firePlayerJoin(const PlayerJoinEvent& ev);
  void firePlayerQuit(const PlayerQuitEvent& ev);
  void fireChat(ChatEvent& ev);
  void fireCommand(CommandEvent& ev);
  void fireMove(const MoveEvent& ev);
  void fireBlock(const BlockEvent& ev);
  void fireSignChange(SignChangeEvent& ev);
  void fireWorldLoad(const WorldLoadEvent& ev);

  std::size_t size() const { return plugins_.size(); }

private:
  std::unique_ptr<IPlugin> tryLoadDir(const std::string& dir);

  std::vector<std::unique_ptr<IPlugin>> plugins_;
};

} // namespace mpmpes::plugin
