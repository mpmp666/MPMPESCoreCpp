#pragma once

#include <cctype>
#include <fstream>
#include <map>
#include <mutex>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace mpmpes::server {

// Permission ladder: Player < Op < Console
enum class PermLevel : int {
  Player = 0,
  Op = 1,
  Console = 2,
};

inline std::string toLowerName(std::string_view s) {
  std::string out;
  out.reserve(s.size());
  for (unsigned char c : s) out.push_back(static_cast<char>(std::tolower(c)));
  return out;
}

// ops.txt — one username per line (case-insensitive). Console always has Console level.
class OpsList {
public:
  void setPath(std::string path) { path_ = std::move(path); }
  const std::string& path() const { return path_; }

  void load() {
    std::lock_guard lock(mu_);
    names_.clear();
    if (path_.empty()) return;
    std::ifstream in(path_);
    if (!in) return;
    std::string line;
    while (std::getline(in, line)) {
      while (!line.empty() && (line.back() == '\r' || line.back() == ' ' || line.back() == '\t'))
        line.pop_back();
      std::size_t i = 0;
      while (i < line.size() && (line[i] == ' ' || line[i] == '\t')) ++i;
      if (i > 0) line = line.substr(i);
      if (line.empty() || line[0] == '#') continue;
      names_.insert(toLowerName(line));
    }
  }

  void save() const {
    std::lock_guard lock(mu_);
    if (path_.empty()) return;
    std::ofstream out(path_, std::ios::trunc);
    if (!out) return;
    out << "# MPMPESCoreCpp operators (one name per line)\n";
    for (const auto& n : names_) out << n << "\n";
  }

  bool isOp(std::string_view name) const {
    std::lock_guard lock(mu_);
    return names_.count(toLowerName(name)) > 0;
  }

  bool addOp(std::string_view name) {
    if (name.empty()) return false;
    std::lock_guard lock(mu_);
    auto [_, inserted] = names_.insert(toLowerName(name));
    return inserted;
  }

  bool removeOp(std::string_view name) {
    std::lock_guard lock(mu_);
    return names_.erase(toLowerName(name)) > 0;
  }

  std::vector<std::string> list() const {
    std::lock_guard lock(mu_);
    return {names_.begin(), names_.end()};
  }

  std::size_t size() const {
    std::lock_guard lock(mu_);
    return names_.size();
  }

private:
  mutable std::mutex mu_;
  std::string path_ = "ops.txt";
  std::set<std::string> names_;
};

inline const char* permName(PermLevel p) {
  switch (p) {
    case PermLevel::Console:
      return "console";
    case PermLevel::Op:
      return "op";
    default:
      return "player";
  }
}

// Minimum level required for each builtin command name (lowercase).
inline PermLevel commandRequiredLevel(std::string_view cmd) {
  // public
  if (cmd == "help" || cmd == "?" || cmd == "list" || cmd == "worlds" || cmd == "spawn" ||
      cmd == "me" || cmd == "ver" || cmd == "version")
    return PermLevel::Player;
  // operator
  if (cmd == "gm" || cmd == "gamemode" || cmd == "give" || cmd == "spawnmob" || cmd == "summon" ||
      cmd == "clear" || cmd == "goto" || cmd == "op" || cmd == "deop" || cmd == "stop" ||
      cmd == "kick" || cmd == "ban" || cmd == "unban" || cmd == "pardon" || cmd == "ban-ip" ||
      cmd == "banip" || cmd == "unban-ip" || cmd == "pardon-ip" || cmd == "ban-cid" ||
      cmd == "bancid" || cmd == "unban-cid" || cmd == "pardon-cid" || cmd == "banlist")
    return PermLevel::Op;
  // unknown: still Player so we can reply "unknown" rather than silent deny
  return PermLevel::Player;
}

// bans.txt — name / ip / cid bans (one entry per line, case-insensitive keys)
// Format:
//   name:<player> [reason...]
//   ip:<address> [reason...]
//   cid:<clientId> [reason...]
// Bare lines (no kind:) treated as name bans.
class BanList {
public:
  void setPath(std::string path) { path_ = std::move(path); }
  const std::string& path() const { return path_; }

  void load() {
    std::lock_guard lock(mu_);
    names_.clear();
    ips_.clear();
    cids_.clear();
    if (path_.empty()) return;
    std::ifstream in(path_);
    if (!in) return;
    std::string line;
    while (std::getline(in, line)) {
      while (!line.empty() && (line.back() == '\r' || line.back() == ' ' || line.back() == '\t'))
        line.pop_back();
      std::size_t i = 0;
      while (i < line.size() && (line[i] == ' ' || line[i] == '\t')) ++i;
      if (i > 0) line = line.substr(i);
      if (line.empty() || line[0] == '#') continue;

      std::string kind = "name";
      std::string rest = line;
      const auto colon = line.find(':');
      if (colon != std::string::npos) {
        kind = toLowerName(line.substr(0, colon));
        rest = line.substr(colon + 1);
        while (!rest.empty() && (rest[0] == ' ' || rest[0] == '\t')) rest.erase(rest.begin());
      }
      if (rest.empty()) continue;

      std::string key;
      std::string reason;
      const auto sp = rest.find(' ');
      if (sp == std::string::npos) {
        key = rest;
      } else {
        key = rest.substr(0, sp);
        reason = rest.substr(sp + 1);
        while (!reason.empty() && (reason[0] == ' ' || reason[0] == '\t'))
          reason.erase(reason.begin());
      }
      if (key.empty()) continue;

      if (kind == "ip") {
        ips_[toLowerName(key)] = reason;
      } else if (kind == "cid" || kind == "client" || kind == "clientid") {
        cids_[toLowerName(key)] = reason;
      } else {
        names_[toLowerName(key)] = reason;
      }
    }
  }

  void save() const {
    std::lock_guard lock(mu_);
    if (path_.empty()) return;
    std::ofstream out(path_, std::ios::trunc);
    if (!out) return;
    out << "# MPMPESCoreCpp bans\n";
    out << "# name:<player> [reason]\n";
    out << "# ip:<address> [reason]\n";
    out << "# cid:<clientId> [reason]\n";
    for (const auto& [k, r] : names_) {
      out << "name:" << k;
      if (!r.empty()) out << " " << r;
      out << "\n";
    }
    for (const auto& [k, r] : ips_) {
      out << "ip:" << k;
      if (!r.empty()) out << " " << r;
      out << "\n";
    }
    for (const auto& [k, r] : cids_) {
      out << "cid:" << k;
      if (!r.empty()) out << " " << r;
      out << "\n";
    }
  }

  // Returns reason if banned (empty reason still means banned).
  std::optional<std::string> reasonForName(std::string_view name) const {
    std::lock_guard lock(mu_);
    auto it = names_.find(toLowerName(name));
    if (it == names_.end()) return std::nullopt;
    return it->second;
  }
  std::optional<std::string> reasonForIp(std::string_view ip) const {
    std::lock_guard lock(mu_);
    auto it = ips_.find(toLowerName(ip));
    if (it == ips_.end()) return std::nullopt;
    return it->second;
  }
  std::optional<std::string> reasonForCid(std::int64_t cid) const {
    std::lock_guard lock(mu_);
    auto it = cids_.find(std::to_string(cid));
    if (it == cids_.end()) return std::nullopt;
    return it->second;
  }

  bool banName(std::string_view name, std::string_view reason) {
    if (name.empty()) return false;
    std::lock_guard lock(mu_);
    names_[toLowerName(name)] = std::string(reason);
    return true;
  }
  bool banIp(std::string_view ip, std::string_view reason) {
    if (ip.empty()) return false;
    std::lock_guard lock(mu_);
    ips_[toLowerName(ip)] = std::string(reason);
    return true;
  }
  bool banCid(std::int64_t cid, std::string_view reason) {
    std::lock_guard lock(mu_);
    cids_[std::to_string(cid)] = std::string(reason);
    return true;
  }

  bool unbanName(std::string_view name) {
    std::lock_guard lock(mu_);
    return names_.erase(toLowerName(name)) > 0;
  }
  bool unbanIp(std::string_view ip) {
    std::lock_guard lock(mu_);
    return ips_.erase(toLowerName(ip)) > 0;
  }
  bool unbanCid(std::int64_t cid) {
    std::lock_guard lock(mu_);
    return cids_.erase(std::to_string(cid)) > 0;
  }

  std::size_t nameCount() const {
    std::lock_guard lock(mu_);
    return names_.size();
  }
  std::size_t ipCount() const {
    std::lock_guard lock(mu_);
    return ips_.size();
  }
  std::size_t cidCount() const {
    std::lock_guard lock(mu_);
    return cids_.size();
  }

  std::vector<std::pair<std::string, std::string>> listNames() const {
    std::lock_guard lock(mu_);
    return {names_.begin(), names_.end()};
  }
  std::vector<std::pair<std::string, std::string>> listIps() const {
    std::lock_guard lock(mu_);
    return {ips_.begin(), ips_.end()};
  }
  std::vector<std::pair<std::string, std::string>> listCids() const {
    std::lock_guard lock(mu_);
    return {cids_.begin(), cids_.end()};
  }

private:
  mutable std::mutex mu_;
  std::string path_ = "bans.txt";
  std::map<std::string, std::string> names_; // lower -> reason
  std::map<std::string, std::string> ips_;
  std::map<std::string, std::string> cids_; // decimal string of client_id
};

} // namespace mpmpes::server
