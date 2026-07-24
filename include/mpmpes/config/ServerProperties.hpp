#pragma once

#include "mpmpes/server/ServerConfig.hpp"

#include <string>
#include <string_view>
#include <unordered_map>

namespace mpmpes::config {

// Minimal server.properties loader (PocketMine/Genisys style key=value).
class ServerProperties {
public:
  static std::unordered_map<std::string, std::string> loadFile(std::string_view path);
  static void applyTo(const std::unordered_map<std::string, std::string>& props,
                      server::ServerConfig& cfg);
  static void writeDefault(std::string_view path, const server::ServerConfig& cfg);
};

// Simple plugin.yml subset: name/version/main/language/api/description/author
struct PluginManifest {
  std::string name;
  std::string version = "0.1.0";
  std::string main; // path to .so / binary / script relative to plugin dir
  std::string language = "native"; // native|go|c|cpp|python|php|nodejs|rust
  std::string api = "1.0.0";
  std::string description;
  std::string author;
  std::string data_folder; // absolute path filled by loader
  std::string base_dir;    // absolute plugin directory
};

bool loadPluginManifest(std::string_view path, PluginManifest& out, std::string& error);

} // namespace mpmpes::config
