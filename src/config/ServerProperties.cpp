#include "mpmpes/config/ServerProperties.hpp"

#include <cctype>
#include <fstream>
#include <sstream>

namespace mpmpes::config {
namespace {

std::string trim(std::string s) {
  while (!s.empty() && std::isspace(static_cast<unsigned char>(s.front()))) s.erase(s.begin());
  while (!s.empty() && std::isspace(static_cast<unsigned char>(s.back()))) s.pop_back();
  return s;
}

std::string unquote(std::string s) {
  s = trim(std::move(s));
  if (s.size() >= 2 && ((s.front() == '"' && s.back() == '"') ||
                        (s.front() == '\'' && s.back() == '\''))) {
    return s.substr(1, s.size() - 2);
  }
  return s;
}

} // namespace

std::unordered_map<std::string, std::string> ServerProperties::loadFile(std::string_view path) {
  std::unordered_map<std::string, std::string> out;
  std::ifstream in{std::string(path)};
  if (!in) return out;
  std::string line;
  while (std::getline(in, line)) {
    if (!line.empty() && line.back() == '\r') line.pop_back();
    auto t = trim(line);
    if (t.empty() || t[0] == '#' || t[0] == '!') continue;
    const auto eq = t.find('=');
    if (eq == std::string::npos) continue;
    auto key = trim(t.substr(0, eq));
    auto val = trim(t.substr(eq + 1));
    if (!key.empty()) out[std::move(key)] = std::move(val);
  }
  return out;
}

void ServerProperties::applyTo(const std::unordered_map<std::string, std::string>& props,
                               server::ServerConfig& cfg) {
  auto get = [&](const char* k) -> const std::string* {
    auto it = props.find(k);
    return it == props.end() ? nullptr : &it->second;
  };
  if (auto* v = get("motd")) cfg.motd = *v;
  if (auto* v = get("server-port")) {
    try {
      cfg.port = static_cast<std::uint16_t>(std::stoi(*v));
    } catch (...) {
    }
  }
  if (auto* v = get("server-ip")) {
    if (!v->empty() && *v != "0.0.0.0") cfg.bind = *v;
  }
  if (auto* v = get("max-players")) {
    try {
      cfg.max_players = std::stoi(*v);
    } catch (...) {
    }
  }
  if (auto* v = get("level-name")) cfg.level_name = *v;
  if (auto* v = get("level-type")) cfg.level_type = *v;
  // Default join gamemode for NEW players (saved players.dat still wins on rejoin).
  // Accepts: 0 / survival / s | 1 / creative / c  (PE 0.14 only these two)
  if (auto* v = get("gamemode")) {
    auto s = trim(*v);
    for (auto& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    if (s == "1" || s == "creative" || s == "c" || s == "1c") {
      cfg.gamemode = 1;
    } else if (s == "0" || s == "survival" || s == "s" || s.empty()) {
      cfg.gamemode = 0;
    } else {
      try {
        int n = std::stoi(s);
        cfg.gamemode = (n == 1) ? 1 : 0;
      } catch (...) {
        cfg.gamemode = 0;
      }
    }
  }
  if (auto* v = get("plugins-dir")) cfg.plugins_dir = *v;
  if (auto* v = get("enable-plugins")) {
    cfg.enable_plugins = (*v == "on" || *v == "true" || *v == "1");
  }
  if (auto* v = get("network-compression-level")) {
    try {
      cfg.network_compression_level = std::stoi(*v);
      if (cfg.network_compression_level < 0) cfg.network_compression_level = 0;
      if (cfg.network_compression_level > 9) cfg.network_compression_level = 9;
    } catch (...) {
    }
  }
  // Prefer KB key; also accept legacy byte threshold
  if (auto* v = get("network-batch-threshold-kb")) {
    try {
      cfg.network_batch_threshold_kb = std::stoi(*v);
    } catch (...) {
    }
  } else if (auto* v = get("network-batch-threshold")) {
    // bytes → KB (ceil), matching PM pocketmine.yml style when user pastes bytes
    try {
      int bytes = std::stoi(*v);
      cfg.network_batch_threshold_kb = (bytes < 0) ? -1 : (bytes + 1023) / 1024;
      if (bytes == 0) cfg.network_batch_threshold_kb = 0;
    } catch (...) {
    }
  }
  if (auto* v = get("always-drop-on-break")) {
    cfg.always_drop_on_break = (*v == "on" || *v == "true" || *v == "1");
  }
  if (auto* v = get("autosave")) {
    try {
      cfg.autosave_seconds = std::stoi(*v);
      if (cfg.autosave_seconds < 0) cfg.autosave_seconds = 0;
    } catch (...) {
    }
  }
}

void ServerProperties::writeDefault(std::string_view path, const server::ServerConfig& cfg) {
  std::ofstream out{std::string(path)};
  if (!out) return;
  out << "# MPMPESCoreCpp properties\n"
      << "motd=" << cfg.motd << "\n"
      << "server-port=" << cfg.port << "\n"
      << "server-ip=" << cfg.bind << "\n"
      << "max-players=" << cfg.max_players << "\n"
      << "level-name=" << cfg.level_name << "\n"
      << "level-type=" << cfg.level_type << "\n"
      << "# Default gamemode for NEW players: 0=survival 1=creative (also: survival/creative)\n"
      << "# Returning players keep mode from players/<name>.dat; use /gm in-game to switch\n"
      << "gamemode=" << cfg.gamemode << "\n"
      << "plugins-dir=" << cfg.plugins_dir << "\n"
      << "enable-plugins=" << (cfg.enable_plugins ? "on" : "off") << "\n"
      << "# zlib level 0-9 for outbound Batch packets (higher = more CPU, less bandwidth)\n"
      << "network-compression-level=" << cfg.network_compression_level << "\n"
      << "# Batch when bare packet size >= N KB (-1=off, 0=always)\n"
      << "network-batch-threshold-kb=" << cfg.network_batch_threshold_kb << "\n"
      << "# Drop items into world on block break even in creative\n"
      << "always-drop-on-break=" << (cfg.always_drop_on_break ? "on" : "off") << "\n"
      << "# Autosave worlds+players every N seconds (0=only on stop)\n"
      << "autosave=" << cfg.autosave_seconds << "\n";
}

bool loadPluginManifest(std::string_view path, PluginManifest& out, std::string& error) {
  std::ifstream in{std::string(path)};
  if (!in) {
    error = "cannot open plugin.yml";
    return false;
  }
  std::string line;
  while (std::getline(in, line)) {
    if (!line.empty() && line.back() == '\r') line.pop_back();
    auto t = trim(line);
    if (t.empty() || t[0] == '#') continue;
    // skip YAML list lines
    if (!t.empty() && t[0] == '-') continue;
    const auto colon = t.find(':');
    if (colon == std::string::npos) continue;
    auto key = trim(t.substr(0, colon));
    auto val = unquote(t.substr(colon + 1));
    // ignore nested keys (indent)
    if (!line.empty() && (line[0] == ' ' || line[0] == '\t')) {
      // allow simple top-level only for now
      continue;
    }
    if (key == "name") out.name = val;
    else if (key == "version") out.version = val;
    else if (key == "main") out.main = val;
    else if (key == "language" || key == "lang") out.language = val;
    else if (key == "api") out.api = val;
    else if (key == "description") out.description = val;
    else if (key == "author") out.author = val;
  }
  if (out.name.empty()) {
    error = "plugin.yml missing name";
    return false;
  }
  if (out.main.empty()) {
    error = "plugin.yml missing main";
    return false;
  }
  return true;
}

} // namespace mpmpes::config
