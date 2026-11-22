#pragma once
// GameSpy/UT3 query (network/query/QueryHandler.php) + SRCDS RCON (network/rcon/*).

#include <atomic>
#include <functional>
#include <cstdint>
#include <mutex>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

namespace mpmpes::network {

struct QueryInfo {
  std::string hostname;  // motd
  std::string gametype = "SMP";
  std::string map = "world";
  std::string version = "0.14.3";
  int num_players = 0;
  int max_players = 20;
  bool whitelist = false;
  std::string ip = "0.0.0.0";
  std::uint16_t port = 19132;
};

// Handles one Query UDP packet; returns reply (empty = drop).
class QueryHandler {
public:
  QueryHandler();
  std::string handle(std::string_view address, std::string_view packet, const QueryInfo& info);
  void regenerateToken();

private:
  std::int32_t tokenFor(std::string_view address, bool last) const;
  std::string token_;
  std::string last_token_;
  double info_timeout_ = 0;
};

// Minimal SRCDS-compatible RCON server (TCP). Commands run on the game thread.
class RconServer {
public:
  using CommandFn = std::function<std::string(std::string)>;
  RconServer() = default;
  ~RconServer() { stop(); }

  bool start(std::string bind, std::uint16_t port, std::string password, CommandFn fn);
  void stop();
  bool running() const { return running_; }

private:
  void threadMain();
  std::atomic<bool> running_{false};
  std::thread thread_;
  int listen_fd_ = -1;
  std::string password_;
  CommandFn fn_;
};

} // namespace mpmpes::network
