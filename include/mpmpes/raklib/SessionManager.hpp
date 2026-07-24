#pragma once

#include "mpmpes/raklib/Session.hpp"
#include "mpmpes/raklib/UDPServerSocket.hpp"

#include <chrono>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>

namespace mpmpes::raklib {

class SessionManager {
public:
  SessionManager(UDPServerSocket& socket, std::int64_t server_id);

  void setName(std::string name) { name_ = std::move(name); }
  const std::string& name() const { return name_; }
  std::int64_t serverId() const { return server_id_; }
  std::uint16_t port() const { return port_; }
  void setPort(std::uint16_t p) { port_ = p; }
  bool portChecking() const { return port_checking_; }

  double now() const;

  void setHandlers(EncapsulatedHandler on_encap, SessionOpenHandler on_open,
                   SessionCloseHandler on_close) {
    on_encap_ = std::move(on_encap);
    on_open_ = std::move(on_open);
    on_close_ = std::move(on_close);
  }

  // Drain UDP + update sessions (non-blocking).
  // Returns number of UDP packets handled this call (0 => idle).
  int tick();

  void sendRaw(std::string_view data, const Endpoint& to);
  // Cached-address send (skip inet_pton)
  void sendRawTo(std::string_view data, const sockaddr_in& dst);
  void markSessionClosed(const Endpoint& ep, std::string_view reason);
  void removeSession(const Endpoint& ep, std::string_view reason);

  Session* getSession(const Endpoint& ep);
  Session& getOrCreateSession(const Endpoint& ep);

private:
  static std::string keyOf(const Endpoint& ep);
  void handlePacket(const std::string& buffer, const Endpoint& from);
  void handleUnconnectedPing(const std::string& buffer, const Endpoint& from);
  static bool hasMagic(const std::string& buffer, std::size_t offset);

  UDPServerSocket& socket_;
  std::int64_t server_id_;
  std::uint16_t port_ = 19132;
  bool port_checking_ = true;
  std::string name_;

  std::unordered_map<std::string, std::unique_ptr<Session>> sessions_;

  EncapsulatedHandler on_encap_;
  SessionOpenHandler on_open_;
  SessionCloseHandler on_close_;

  std::chrono::steady_clock::time_point start_{std::chrono::steady_clock::now()};
};

} // namespace mpmpes::raklib
