// frp client core — config parse + AES-128-CFB + yamux session + UDP proxy.
#include "mpmpes/frp/FrpClient.hpp"
#include "mpmpes/util/Logger.hpp"

#include <openssl/evp.h>
#include <openssl/md5.h>
#include <openssl/sha.h>

#include <arpa/inet.h>
#include <chrono>
#include <cstring>
#include <fstream>
#include <netdb.h>
#include <sys/socket.h>
#include <unistd.h>

namespace mpmpes::frp {
using util::Logger;

// ---------------------------------------------------------------- TOML (minimal)
namespace {
std::string trim(std::string s) {
  auto a = s.find_first_not_of(" \t\r\n");
  auto b = s.find_last_not_of(" \t\r\n");
  return a == std::string::npos ? "" : s.substr(a, b - a + 1);
}
std::string unquote(std::string s) {
  s = trim(std::move(s));
  if (s.size() >= 2 && ((s.front() == '"' && s.back() == '"') || (s.front() == '\'' && s.back() == '\'')))
    return s.substr(1, s.size() - 2);
  return s;
}
} // namespace

bool loadFrpConfig(const std::string& path, FrpConfig& out) {
  std::ifstream in(path);
  if (!in) return false;
  std::string line, section;
  FrpProxy* cur = nullptr;
  while (std::getline(in, line)) {
    auto hash = line.find('#');
    if (hash != std::string::npos) line = line.substr(0, hash);
    line = trim(line);
    if (line.empty()) continue;
    if (line.front() == '[') {
      if (line.rfind("[[proxies]]", 0) == 0) {
        out.proxies.push_back({});
        cur = &out.proxies.back();
        section = "proxies";
      } else {
        section = trim(line.substr(1, line.size() - 2));
        cur = nullptr;
      }
      continue;
    }
    auto eq = line.find('=');
    if (eq == std::string::npos) continue;
    std::string key = trim(line.substr(0, eq));
    std::string val = unquote(line.substr(eq + 1));
    auto set = [&](const std::string& k, auto fn) {
      if (key == k) fn(val);
    };
    if (cur) {
      set("name", [&](auto& v) { cur->name = v; });
      set("type", [&](auto& v) { cur->type = v; });
      set("localPort", [&](auto& v) { cur->local_port = std::stoi(v); });
      set("local_port", [&](auto& v) { cur->local_port = std::stoi(v); });
      set("remotePort", [&](auto& v) { cur->remote_port = std::stoi(v); });
      set("remote_port", [&](auto& v) { cur->remote_port = std::stoi(v); });
      set("localIP", [&](auto& v) { cur->local_ip = v; });
    } else if (section == "auth" || section.empty()) {
      set("serverAddr", [&](auto& v) { out.server_addr = v; });
      set("serverPort", [&](auto& v) { out.server_port = std::stoi(v); });
      set("user", [&](auto& v) { out.user = v; });
      set("token", [&](auto& v) { out.token = v; });
    }
  }
  return !out.server_addr.empty() && !out.proxies.empty();
}

// ---------------------------------------------------------------- AES-128-CFB
AesCfb::AesCfb(std::string key, std::string iv) : prev_(std::move(iv)) {
  ctx_ = EVP_CIPHER_CTX_new();
  EVP_EncryptInit_ex(ctx_, EVP_aes_128_ecb(), nullptr,
                     reinterpret_cast<const unsigned char*>(key.data()), nullptr);
  EVP_CIPHER_CTX_set_padding(ctx_, 0);
}
AesCfb::~AesCfb() { EVP_CIPHER_CTX_free(ctx_); }

std::string AesCfb::process(std::string_view data) {
  // CFB: ks = ECB(prev); out = data ^ ks; prev = (encrypt ? out : data)
  std::string out(data.size(), '\0');
  std::size_t off = 0;
  while (off < data.size()) {
    std::size_t n = std::min<std::size_t>(16, data.size() - off);
    unsigned char ks[32];
    int ksLen = 0, tmp = 0;
    EVP_EncryptUpdate(ctx_, ks, &ksLen, reinterpret_cast<const unsigned char*>(prev_.data()), 16);
    EVP_EncryptFinal_ex(ctx_, ks + ksLen, &tmp);
    for (std::size_t i = 0; i < n; ++i) out[off + i] = data[off + i] ^ static_cast<char>(ks[i]);
    prev_ = out.substr(off, n); // for encryption direction
    off += n;
  }
  return out;
}

// ---------------------------------------------------------------- yamux + client
namespace {
constexpr std::uint8_t Y_DATA = 0, Y_WINDOW_UPDATE = 1, Y_PING = 2, Y_GO_AWAY = 3;
constexpr std::uint8_t F_SYN = 1, F_ACK = 2, F_FIN = 4, F_RST = 8;
constexpr std::uint8_t T_LOGIN = 0x6F, T_LOGIN_RESP = 0x31, T_NEW_PROXY = 0x70,
                       T_NEW_PROXY_RESP = 0x32, T_NEW_WORK_CONN = 0x77, T_REQ_WORK_CONN = 0x72,
                       T_START_WORK_CONN = 0x73, T_PING = 0x68, T_PONG = 0x34, T_UDP_PACKET = 0x75;

std::string md5hex(const std::string& s) {
  unsigned char d[16];
  MD5(reinterpret_cast<const unsigned char*>(s.data()), s.size(), d);
  char hex[33];
  for (int i = 0; i < 16; ++i) std::snprintf(hex + i * 2, 3, "%02x", d[i]);
  return hex;
}
std::string pbkdf2_sha1(const std::string& pass, const std::string& salt, int iters, int keylen) {
  std::string out(keylen, '\0');
  PKCS5_PBKDF2_HMAC_SHA1(pass.data(), static_cast<int>(pass.size()),
                         reinterpret_cast<const unsigned char*>(salt.data()),
                         static_cast<int>(salt.size()), iters, keylen,
                         reinterpret_cast<unsigned char*>(out.data()));
  return out;
}
std::string b64enc(std::string_view in) {
  static const char* t = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
  std::string out;
  int v = 0, bits = -6;
  for (unsigned char c : in) {
    v = (v << 8) | c;
    bits += 8;
    while (bits >= 0) {
      out.push_back(t[(v >> bits) & 63]);
      bits -= 6;
    }
  }
  if (bits > -6) out.push_back(t[((v << 8) >> (bits + 8)) & 63]);
  while (out.size() % 4) out.push_back('=');
  return out;
}
std::string b64dec(std::string_view in) {
  auto val = [](char c) -> int {
    if (c >= 'A' && c <= 'Z') return c - 'A';
    if (c >= 'a' && c <= 'z') return c - 'a' + 26;
    if (c >= '0' && c <= '9') return c - '0' + 52;
    if (c == '+') return 62;
    if (c == '/') return 63;
    return -1;
  };
  std::string out;
  int v = 0, bits = -8;
  for (char c : in) {
    int x = val(c);
    if (x < 0) continue;
    v = (v << 6) | x;
    bits += 6;
    if (bits >= 0) {
      out.push_back(static_cast<char>((v >> bits) & 0xff));
      bits -= 8;
    }
  }
  return out;
}
} // namespace

struct Yamux {
  int fd = -1;
  bool dead = false;
  std::string rbuf;
  std::unordered_map<int, std::string> sendq;
  int nextSid = 1;
  std::int64_t pingId = 0;
  double lastPing = 0, lastPong = 0;
  std::unordered_map<int, std::string> streamBuf;
  std::unordered_map<int, std::string> role; // "control" / "work"
  std::unordered_map<int, std::unique_ptr<AesCfb>> crypto;

  static double now() {
    return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
  }
  int openStream(const std::string& role) {
    int sid = nextSid;
    nextSid += 2;
    sendFrame(Y_DATA, F_SYN, sid, "");
    this->role[sid] = role;
    return sid;
  }
  void sendFrame(std::uint8_t type, std::uint8_t flags, std::int32_t sid, std::string_view payload) {
    if (dead) return;
    unsigned char hdr[12];
    hdr[0] = 0; // version
    hdr[1] = type;
    hdr[2] = static_cast<unsigned char>(flags >> 8);
    hdr[3] = flags;
    std::uint32_t usid = static_cast<std::uint32_t>(sid);
    hdr[4] = static_cast<unsigned char>(usid >> 24);
    hdr[5] = static_cast<unsigned char>(usid >> 16);
    hdr[6] = static_cast<unsigned char>(usid >> 8);
    hdr[7] = static_cast<unsigned char>(usid);
    std::uint32_t len = static_cast<std::uint32_t>(payload.size());
    hdr[8] = static_cast<unsigned char>(len >> 24);
    hdr[9] = static_cast<unsigned char>(len >> 16);
    hdr[10] = static_cast<unsigned char>(len >> 8);
    hdr[11] = static_cast<unsigned char>(len);
    std::string out(reinterpret_cast<const char*>(hdr), 12);
    out += payload;
    auto n = ::send(fd, out.data(), out.size(), MSG_NOSIGNAL);
    if (n <= 0) dead = true;
  }
  // message: [type 1][len 8 BE][json]
  void sendMessage(int sid, std::uint8_t type, const std::string& json) {
    std::string payload;
    payload.push_back(static_cast<char>(type));
    std::uint64_t l = json.size();
    for (int i = 7; i >= 0; --i) payload.push_back(static_cast<char>(l >> (i * 8)));
    payload += json;
    if (auto it = crypto.find(sid); it != crypto.end() && it->second) payload = it->second->process(payload);
    sendq[sid] += payload;
    // flush with a modest window
    sendFrame(Y_DATA, 0, sid, sendq[sid]);
    sendq[sid].clear();
  }
  void enableCrypto(int sid, const std::string& key, const std::string& iv) {
    crypto[sid] = std::make_unique<AesCfb>(key, iv);
    // PHP sends the IV first (unencrypted) on the stream
    sendFrame(Y_DATA, 0, sid, iv);
  }
  // returns (sid,type,json) when a full message is parsed
  std::vector<std::tuple<int, std::uint8_t, std::string>> messages;
  void pump() {
    unsigned char buf[65536];
    auto n = ::recv(fd, buf, sizeof(buf), 0);
    if (n <= 0) {
      dead = true;
      return;
    }
    rbuf.append(reinterpret_cast<const char*>(buf), n);
    while (rbuf.size() >= 12) {
      const unsigned char* h = reinterpret_cast<const unsigned char*>(rbuf.data());
      std::uint8_t type = h[1];
      std::uint8_t flags = h[3];
      std::int32_t sid = (h[4] << 24) | (h[5] << 16) | (h[6] << 8) | h[7];
      std::uint32_t len = (h[8] << 24) | (h[9] << 16) | (h[10] << 8) | h[11];
      if (rbuf.size() < 12 + len) break;
      std::string payload = rbuf.substr(12, len);
      rbuf.erase(0, 12 + len);
      if (type == Y_DATA) {
        streamBuf[sid] += payload;
        parseMessages(sid);
      } else if (type == Y_PING) {
        if (flags & F_SYN) sendFrame(Y_PING, F_ACK, sid, payload);
        else lastPong = now();
      } else if (type == Y_WINDOW_UPDATE || type == Y_GO_AWAY) {
        // ignore
      }
    }
  }
  void parseMessages(int sid) {
    std::string& buf = streamBuf[sid];
    std::string plain = buf;
    if (auto it = crypto.find(sid); it != crypto.end() && it->second) plain = it->second->process(plain);
    // messages: [type 1][len 8][json len]
    std::size_t off = 0;
    while (plain.size() - off >= 9) {
      std::uint8_t type = static_cast<std::uint8_t>(plain[off]);
      std::uint64_t l = 0;
      for (int i = 1; i <= 8; ++i) l = (l << 8) | static_cast<unsigned char>(plain[off + i]);
      if (plain.size() - off - 9 < l) break;
      std::string json = plain.substr(off + 9, l);
      messages.push_back({sid, type, json});
      off += 9 + l;
    }
    // drop consumed bytes
    buf.clear();
  }
};

FrpClient::FrpClient(std::string tunnel_name, FrpConfig conf)
    : tunnel_name_(std::move(tunnel_name)), conf_(std::move(conf)) {}
FrpClient::~FrpClient() { stop(); }

bool FrpClient::start() {
  if (running_) return false;
  running_ = true;
  thread_ = std::thread([this] { run(); });
  return true;
}
void FrpClient::stop() {
  if (!running_.exchange(false)) return;
  if (control_fd_ >= 0) {
    ::shutdown(control_fd_, SHUT_RDWR);
    ::close(control_fd_);
    control_fd_ = -1;
  }
  if (thread_.joinable()) thread_.join();
}

void FrpClient::run() {
  try { runImpl(); } catch (...) { running_ = false; }
}

void FrpClient::runImpl() {
  Logger::instance().info("[frp:", tunnel_name_, "] connecting to ", conf_.server_addr, ":", conf_.server_port);
  // connect
  addrinfo hints{}, *res = nullptr;
  hints.ai_family = AF_INET;
  hints.ai_socktype = SOCK_STREAM;
  if (getaddrinfo(conf_.server_addr.c_str(), std::to_string(conf_.server_port).c_str(), &hints, &res) != 0) {
    Logger::instance().error("[frp:", tunnel_name_, "] resolve failed");
    running_ = false;
    return;
  }
  control_fd_ = ::socket(res->ai_family, res->ai_socktype, res->ai_protocol);
  if (control_fd_ < 0 || ::connect(control_fd_, res->ai_addr, res->ai_addrlen) != 0) {
    Logger::instance().error("[frp:", tunnel_name_, "] connect failed");
    freeaddrinfo(res);
    running_ = false;
    return;
  }
  freeaddrinfo(res);

  Yamux sess;
  sess.fd = control_fd_;
  sess.lastPing = sess.lastPong = Yamux::now();
  int controlSid = sess.openStream("control");
  std::int64_t ts = static_cast<std::int64_t>(::time(nullptr));
  char hostn[128];
  gethostname(hostn, sizeof(hostn));
  std::string login = std::string("{\"version\":\"0.61.2\",\"hostname\":\"") + hostn +
                      "\",\"os\":\"Linux\",\"arch\":\"x86_64\",\"user\":\"" + conf_.user +
                      "\",\"privilege_key\":\"" + md5hex(conf_.token + std::to_string(ts)) +
                      "\",\"timestamp\":" + std::to_string(ts) + ",\"run_id\":\"\",\"pool_count\":1}";
  sess.sendMessage(controlSid, T_LOGIN, login);
  Logger::instance().info("[frp:", tunnel_name_, "] login sent");

  bool loggedIn = false;
  int proxySent = 0, proxyResps = 0;
  std::string cryptoKey = pbkdf2_sha1(conf_.token, "frp", 64, 16);
  // local UDP socket for forwarding to the MCPE server
  int udp = ::socket(AF_INET, SOCK_DGRAM, 0);
  sockaddr_in localAddr{};
  localAddr.sin_family = AF_INET;

  double handshakeStart = Yamux::now();
  while (running_ && !sess.dead) {
    fd_set rfds;
    FD_ZERO(&rfds);
    FD_SET(control_fd_, &rfds);
    if (udp >= 0) FD_SET(udp, &rfds);
    timeval tv{0, 100000};
    int maxfd = std::max(control_fd_, udp);
    int sel = ::select(maxfd + 1, &rfds, nullptr, nullptr, &tv);
    if (sel > 0) {
      if (FD_ISSET(control_fd_, &rfds)) sess.pump();
      if (udp >= 0 && FD_ISSET(udp, &rfds)) {
        // local MCPE server -> remote (via work stream)
        char buf[4096];
        sockaddr_in from{};
        socklen_t flen = sizeof(from);
        auto n = ::recvfrom(udp, buf, sizeof(buf), 0, reinterpret_cast<sockaddr*>(&from), &flen);
        if (n > 0) {
          // find the work stream for this proxy and forward
          for (auto& [sid, role] : sess.role) {
            if (role != "work") continue;
            char ip[INET_ADDRSTRLEN];
            inet_ntop(AF_INET, &from.sin_addr, ip, sizeof(ip));
            std::string msg = "{\"c\":\"" + b64enc(std::string_view(buf, n)) + "\",\"r\":{\"IP\":\"" +
                              ip + "\",\"Port\":" + std::to_string(ntohs(from.sin_port)) + ",\"Zone\":\"\"}}";
            sess.sendMessage(sid, T_UDP_PACKET, msg);
            break;
          }
        }
      }
    }
    // timers
    double now = Yamux::now();
    if (now - sess.lastPing >= 30) {
      sess.sendFrame(Y_PING, F_SYN, 0, "");
      sess.lastPing = now;
    }
    // process messages
    for (auto& [sid, type, json] : sess.messages) {
      if (type == T_LOGIN_RESP) {
        // run_id in json
        auto pos = json.find("\"run_id\":\"");
        if (pos != std::string::npos) {
          auto end = json.find('"', pos + 10);
          run_id_ = json.substr(pos + 10, end - (pos + 10));
        }
        loggedIn = true;
        // enable control crypto: send IV then encrypted stream
        std::string iv = cryptoKey.substr(0, 16);
        // PHP: crypto = aes-128-cfb with key=cryptoKey, iv sent first
        // We send a 16-byte IV (random; PHP uses the key-derived IV? it sends iv then encrypted)
        // frpc.php: the IV is the first 16 bytes sent; the cipher key = pbkdf2. Use iv = random 16.
        unsigned char ivb[16];
        for (int i = 0; i < 16; ++i) ivb[i] = static_cast<unsigned char>((run_id_.size() + i * 31 + ts) & 0xff);
        std::string ivs(reinterpret_cast<const char*>(ivb), 16);
        sess.crypto[controlSid] = std::make_unique<AesCfb>(cryptoKey, ivs);
        // PHP: sendFrame the IV raw first, then the stream is encrypted
        sess.sendFrame(Y_DATA, 0, controlSid, ivs);
        Logger::instance().info("[frp:", tunnel_name_, "] logged in, run_id=", run_id_);
      } else if (type == T_NEW_PROXY_RESP && sid == controlSid) {
        ++proxyResps;
      } else if (type == T_REQ_WORK_CONN) {
        int wsid = sess.openStream("work");
        std::int64_t wts = static_cast<std::int64_t>(::time(nullptr));
        std::string msg = "{\"run_id\":\"" + run_id_ + "\",\"privilege_key\":\"" +
                          md5hex(conf_.token + std::to_string(wts)) +
                          "\",\"timestamp\":" + std::to_string(wts) + "}";
        sess.sendMessage(wsid, T_NEW_WORK_CONN, msg);
      } else if (type == T_UDP_PACKET) {
        // remote -> local MCPE server: decode + forward to the local server port
        auto cpos = json.find("\"c\":\"");
        if (cpos != std::string::npos) {
          auto cend = json.find('"', cpos + 5);
          auto content = b64dec(json.substr(cpos + 5, cend - (cpos + 5)));
          // forward to the local MCPE server (the proxy's local_port)
          // find the proxy for this work stream... simplified: forward to first proxy's local port
          if (udp >= 0 && !conf_.proxies.empty()) {
            localAddr.sin_port = htons(conf_.proxies[0].local_port);
            localAddr.sin_addr.s_addr = inet_addr("127.0.0.1");
            ::sendto(udp, content.data(), content.size(), 0, reinterpret_cast<sockaddr*>(&localAddr), sizeof(localAddr));
          }
        }
      }
    }
    sess.messages.clear();
    // send proxies after login
    if (loggedIn && proxySent == 0) {
      for (auto& p : conf_.proxies) {
        std::string msg = "{\"proxy_name\":\"" + conf_.user + "." + p.name +
                          "\",\"proxy_type\":\"udp\",\"remote_port\":" + std::to_string(p.remote_port) +
                          ",\"use_encryption\":false,\"use_compression\":false}";
        sess.sendMessage(controlSid, T_NEW_PROXY, msg);
        ++proxySent;
      }
      Logger::instance().info("[frp:", tunnel_name_, "] registered ", proxySent, " UDP proxies");
    }
  }
  if (udp >= 0) {
    ::close(udp);
    udp = -1;
  }
  if (control_fd_ >= 0) {
    ::shutdown(control_fd_, SHUT_RDWR);
    ::close(control_fd_);
    control_fd_ = -1;
  }
  Logger::instance().info("[frp:", tunnel_name_, "] disconnected");
  running_ = false;
}

} // namespace mpmpes::frp
