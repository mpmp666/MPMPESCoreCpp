#include "mpmpes/network/QueryRcon.hpp"

#include "mpmpes/util/Logger.hpp"

#include <arpa/inet.h>
#include <chrono>
#include <cstring>
#include <random>
#include <sys/socket.h>
#include <unistd.h>

namespace mpmpes::network {

namespace {
std::int32_t readBE32(std::string_view s, std::size_t off) {
  if (off + 4 > s.size()) return 0;
  return (std::int32_t)(((std::uint32_t)(std::uint8_t)s[off] << 24) |
                        ((std::uint32_t)(std::uint8_t)s[off + 1] << 16) |
                        ((std::uint32_t)(std::uint8_t)s[off + 2] << 8) |
                        (std::uint32_t)(std::uint8_t)s[off + 3]);
}
void putBE32(std::string& out, std::int32_t v) {
  out.push_back((char)(v >> 24));
  out.push_back((char)(v >> 16));
  out.push_back((char)(v >> 8));
  out.push_back((char)v);
}
void putLE16(std::string& out, std::uint16_t v) {
  out.push_back((char)(v & 0xff));
  out.push_back((char)(v >> 8));
}
} // namespace

QueryHandler::QueryHandler() { regenerateToken(); }

void QueryHandler::regenerateToken() {
  std::random_device rd;
  last_token_ = token_;
  token_.clear();
  for (int i = 0; i < 16; ++i) token_.push_back((char)(rd() & 0xff));
}

// PHP: Binary::readInt(substr(hash("sha512", salt.":".token, true), 7, 4)) — we don't ship
// sha512; the token only needs to be an unguessable per-address secret, so use a hash of
// addr:token via a strong mixer. (The reference value is server-internal; clients just
// echo it back.)
std::int32_t QueryHandler::tokenFor(std::string_view address, bool last) const {
  const std::string& t = last ? last_token_ : token_;
  std::uint64_t h = 1469598103934665603ull;
  auto mix = [&](char c) { h = (h ^ (std::uint8_t)c) * 1099511628211ull; };
  for (char c : address) mix(c);
  mix(':');
  for (char c : t) mix(c);
  h ^= h >> 13;
  h *= 1274126177ull;
  h ^= h >> 16;
  return (std::int32_t)(h >> 32);
}

std::string QueryHandler::handle(std::string_view address, std::string_view packet,
                                 const QueryInfo& info) {
  if (packet.size() < 7 || packet[0] != '\xfe' || packet[1] != '\xfd') return {};
  const auto type = (std::uint8_t)packet[2];
  const std::int32_t session = readBE32(packet, 3);
  std::string_view payload = packet.substr(7);
  std::string reply;
  if (type == 9) { // handshake
    reply.push_back((char)9);
    putBE32(reply, session);
    reply += std::to_string(tokenFor(address, false));
    reply.push_back('\0');
    return reply;
  }
  if (type == 0) { // stat
    if (payload.size() < 4) return {};
    std::int32_t token = readBE32(payload, 0);
    if (token != tokenFor(address, false) && token != tokenFor(address, true)) return {};
    reply.push_back((char)0);
    putBE32(reply, session);
    if (payload.size() >= 8) {
      // long query
      std::string q;
      q += "splitnum";
      q.push_back('\0');
      q.push_back((char)128);
      q.push_back('\0');
      auto kv = [&](const char* k, const std::string& v) {
        q += k;
        q.push_back('\0');
        q += v;
        q.push_back('\0');
      };
      kv("hostname", info.hostname);
      kv("gametype", info.gametype);
      kv("game_id", "MINECRAFTPE");
      kv("version", info.version);
      kv("server_engine", "MPMPESCoreCpp");
      kv("plugins", "");
      kv("map", info.map);
      kv("numplayers", std::to_string(info.num_players));
      kv("maxplayers", std::to_string(info.max_players));
      kv("whitelist", info.whitelist ? "on" : "off");
      kv("hostip", info.ip);
      kv("hostport", std::to_string(info.port));
      q.push_back('\0');
      // player_ list (empty)
      q += "player_";
      q.push_back('\0');
      q.push_back('\0');
      reply += q;
    } else {
      reply += info.hostname;
      reply.push_back('\0');
      reply += info.gametype;
      reply.push_back('\0');
      reply += info.map;
      reply.push_back('\0');
      reply += std::to_string(info.num_players);
      reply.push_back('\0');
      reply += std::to_string(info.max_players);
      reply.push_back('\0');
      putLE16(reply, info.port);
      reply += info.ip;
      reply.push_back('\0');
    }
    return reply;
  }
  return {};
}

// ------------------------------------------------------------------ RCON
bool RconServer::start(std::string bind, std::uint16_t port, std::string password,
                       CommandFn fn) {
  if (running_) return false;
  password_ = std::move(password);
  fn_ = std::move(fn);
  listen_fd_ = ::socket(AF_INET, SOCK_STREAM, 0);
  if (listen_fd_ < 0) return false;
  int one = 1;
  setsockopt(listen_fd_, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
  sockaddr_in addr{};
  addr.sin_family = AF_INET;
  addr.sin_port = htons(port);
  addr.sin_addr.s_addr = inet_addr(bind.empty() ? "0.0.0.0" : bind.c_str());
  if (::bind(listen_fd_, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0 ||
      ::listen(listen_fd_, 16) != 0) {
    ::close(listen_fd_);
    listen_fd_ = -1;
    return false;
  }
  running_ = true;
  thread_ = std::thread([this] { threadMain(); });
  util::Logger::instance().info("RCON listening on port ", port);
  return true;
}

void RconServer::stop() {
  if (!running_.exchange(false)) return;
  if (listen_fd_ >= 0) {
    ::shutdown(listen_fd_, SHUT_RDWR);
    ::close(listen_fd_);
    listen_fd_ = -1;
  }
  if (thread_.joinable()) thread_.join();
}

namespace {
void putLE32(std::string& out, std::int32_t v) {
  out.push_back((char)(v & 0xff));
  out.push_back((char)((v >> 8) & 0xff));
  out.push_back((char)((v >> 16) & 0xff));
  out.push_back((char)((v >> 24) & 0xff));
}
bool readExact(int fd, char* buf, std::size_t len) {
  std::size_t got = 0;
  while (got < len) {
    auto n = ::recv(fd, buf + got, len - got, 0);
    if (n <= 0) return false;
    got += static_cast<std::size_t>(n);
  }
  return true;
}
void writePacket(int fd, std::int32_t id, std::int32_t type, std::string_view payload) {
  std::string body;
  putLE32(body, id);
  putLE32(body, type);
  body += payload;
  body.push_back('\0');
  body.push_back('\0');
  std::string out;
  putLE32(out, static_cast<std::int32_t>(body.size()));
  out += body;
  ::send(fd, out.data(), out.size(), MSG_NOSIGNAL);
}
} // namespace

void RconServer::threadMain() {
  while (running_) {
    fd_set rfds;
    FD_ZERO(&rfds);
    FD_SET(listen_fd_, &rfds);
    timeval tv{1, 0};
    if (::select(listen_fd_ + 1, &rfds, nullptr, nullptr, &tv) <= 0) continue;
    int client = ::accept(listen_fd_, nullptr, nullptr);
    if (client < 0) continue;
    // Simple sequential handling (one client at a time; PHP supports more but this
    // matches the common rcon usage pattern)
    bool authed = false;
    auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (running_ && std::chrono::steady_clock::now() < deadline) {
      char hdr[4];
      fd_set cfds;
      FD_ZERO(&cfds);
      FD_SET(client, &cfds);
      timeval ctv{1, 0};
      int sel = ::select(client + 1, &cfds, nullptr, nullptr, &ctv);
      if (sel < 0) break;
      if (sel == 0) continue;
      if (!readExact(client, hdr, 4)) break;
      std::int32_t size = (std::int32_t)((std::uint8_t)hdr[0] | ((std::uint8_t)hdr[1] << 8) |
                                         ((std::uint8_t)hdr[2] << 16) | ((std::uint8_t)hdr[3] << 24));
      if (size < 10 || size > 65535) break;
      std::string body(static_cast<std::size_t>(size), '\0');
      if (!readExact(client, body.data(), size)) break;
      std::int32_t id = (std::int32_t)((std::uint8_t)body[0] | ((std::uint8_t)body[1] << 8) |
                                       ((std::uint8_t)body[2] << 16) | ((std::uint8_t)body[3] << 24));
      std::int32_t type = (std::int32_t)((std::uint8_t)body[4] | ((std::uint8_t)body[5] << 8) |
                                         ((std::uint8_t)body[6] << 16) | ((std::uint8_t)body[7] << 24));
      std::string payload = body.substr(8);
      while (!payload.empty() && payload.back() == '\0') payload.pop_back();
      if (type == 3) { // login
        if (payload == password_) {
          authed = true;
          deadline = std::chrono::steady_clock::now() + std::chrono::seconds(300);
          writePacket(client, id, 2, "");
        } else {
          writePacket(client, -1, 2, "");
          break;
        }
      } else if (type == 2 && authed) { // command
        std::string out = fn_ ? fn_(payload) : "";
        writePacket(client, id, 0, out);
      } else if (type == 9) { // protocol check
        writePacket(client, id, 0, "");
      }
    }
    ::shutdown(client, SHUT_RDWR);
    ::close(client);
  }
}

} // namespace mpmpes::network
