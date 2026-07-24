#include "mpmpes/raklib/SessionManager.hpp"

#include "mpmpes/binary/BinaryStream.hpp"
#include "mpmpes/raklib/RakLibConstants.hpp"
#include "mpmpes/util/Logger.hpp"

#include <cstring>
#include <vector>

namespace mpmpes::raklib {
namespace {
using mpmpes::binary::BinaryStream;
using mpmpes::util::Logger;
} // namespace

SessionManager::SessionManager(UDPServerSocket& socket, std::int64_t server_id)
    : socket_(socket), server_id_(server_id) {}

double SessionManager::now() const {
  using namespace std::chrono;
  return duration<double>(steady_clock::now() - start_).count();
}

std::string SessionManager::keyOf(const Endpoint& ep) {
  return ep.address + ":" + std::to_string(ep.port);
}

bool SessionManager::hasMagic(const std::string& buffer, std::size_t offset) {
  if (buffer.size() < offset + MAGIC.size()) return false;
  return std::memcmp(buffer.data() + offset, MAGIC.data(), MAGIC.size()) == 0;
}

void SessionManager::sendRaw(std::string_view data, const Endpoint& to) {
  socket_.writePacket(data, to);
}

void SessionManager::markSessionClosed(const Endpoint& ep, std::string_view reason) {
  Logger::instance().info("Session closing ", keyOf(ep), " reason=", reason);
  // actual erase happens in tick() after update — avoids use-after-free
  (void)ep;
}

void SessionManager::removeSession(const Endpoint& ep, std::string_view reason) {
  const auto k = keyOf(ep);
  if (sessions_.erase(k)) {
    Logger::instance().info("Session removed ", k, " reason=", reason);
  }
}

Session* SessionManager::getSession(const Endpoint& ep) {
  auto it = sessions_.find(keyOf(ep));
  if (it == sessions_.end()) return nullptr;
  return it->second.get();
}

Session& SessionManager::getOrCreateSession(const Endpoint& ep) {
  const auto k = keyOf(ep);
  auto it = sessions_.find(k);
  if (it != sessions_.end()) return *it->second;
  auto session = std::make_unique<Session>(*this, ep, server_id_);
  session->setHandlers(on_encap_, on_open_, on_close_);
  auto* raw = session.get();
  sessions_.emplace(k, std::move(session));
  return *raw;
}

void SessionManager::tick() {
  std::string buffer;
  Endpoint from;
  for (int i = 0; i < 5000; ++i) {
    if (socket_.readPacket(buffer, from) == 0) break;
    if (buffer.empty()) continue;
    handlePacket(buffer, from);
  }

  const double t = now();
  std::vector<Session*> list;
  list.reserve(sessions_.size());
  for (auto& [_, s] : sessions_) {
    list.push_back(s.get());
  }
  for (auto* s : list) {
    if (!s->closed()) {
      s->update(t);
    }
  }
  // Drop sessions marked closed during update (safe after stack frames return)
  for (auto it = sessions_.begin(); it != sessions_.end();) {
    if (it->second->closed()) {
      it = sessions_.erase(it);
    } else {
      ++it;
    }
  }
}

void SessionManager::handlePacket(const std::string& buffer, const Endpoint& from) {
  const auto pid = static_cast<std::uint8_t>(buffer[0]);

  if (pid == ID_UNCONNECTED_PING || pid == ID_UNCONNECTED_PING_OPEN_CONNECTIONS) {
    handleUnconnectedPing(buffer, from);
    return;
  }
  if (pid == ID_UNCONNECTED_PONG) {
    return;
  }

  // Offline open-connection and all connected traffic go through Session
  if (pid == ID_OPEN_CONNECTION_REQUEST_1 || pid == ID_OPEN_CONNECTION_REQUEST_2 ||
      (pid >= 0x80 && pid <= 0x8f) || pid == ID_ACK || pid == ID_NACK ||
      (pid > 0x00 && pid < 0x80)) {
    auto& session = getOrCreateSession(from);
    session.handleRaw(buffer);
  }
}

void SessionManager::handleUnconnectedPing(const std::string& buffer, const Endpoint& from) {
  BinaryStream in(buffer);
  try {
    in.getByte();
    const auto ping_id = in.getLong();
    BinaryStream out;
    out.putByte(ID_UNCONNECTED_PONG);
    out.putLong(ping_id);
    out.putLong(server_id_);
    out.put(MAGIC);
    out.putString(name_);
    sendRaw(out.buffer(), from);
  } catch (...) {
  }
}

} // namespace mpmpes::raklib
