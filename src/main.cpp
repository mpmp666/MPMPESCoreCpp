#include "mpmpes/config/ServerProperties.hpp"
#include "mpmpes/server/Server.hpp"
#include "mpmpes/util/Logger.hpp"

#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <string>

namespace fs = std::filesystem;

namespace {

void printUsage(const char* argv0) {
  std::cerr
      << "Usage: " << argv0
      << " [--config PATH] [--port PORT] [--bind ADDR] [--motd TEXT]\n"
      << "       [--max-players N] [--plugins-dir DIR] [--no-plugins]\n"
      << "  MPMPESCoreCpp — C++ rewrite (RakLib session + C/C++/Python plugins)\n";
}

} // namespace

int main(int argc, char** argv) {
  mpmpes::server::ServerConfig cfg;
  std::string config_path = "server.properties";
  bool no_plugins_flag = false;

  for (int i = 1; i < argc; ++i) {
    const std::string arg = argv[i];
    auto need = [&](const char* name) -> const char* {
      if (i + 1 >= argc) {
        std::cerr << "missing value for " << name << "\n";
        std::exit(2);
      }
      return argv[++i];
    };
    if (arg == "-h" || arg == "--help") {
      printUsage(argv[0]);
      return 0;
    }
    if (arg == "--config") {
      config_path = need("--config");
    } else if (arg == "--port") {
      cfg.port = static_cast<std::uint16_t>(std::stoi(need("--port")));
    } else if (arg == "--bind") {
      cfg.bind = need("--bind");
    } else if (arg == "--motd") {
      cfg.motd = need("--motd");
    } else if (arg == "--max-players") {
      cfg.max_players = std::stoi(need("--max-players"));
    } else if (arg == "--plugins-dir") {
      cfg.plugins_dir = need("--plugins-dir");
    } else if (arg == "--no-plugins") {
      no_plugins_flag = true;
    } else {
      std::cerr << "unknown arg: " << arg << "\n";
      printUsage(argv[0]);
      return 2;
    }
  }

  // Load server.properties if present; create default if missing
  if (!fs::exists(config_path)) {
    mpmpes::config::ServerProperties::writeDefault(config_path, cfg);
    mpmpes::util::Logger::instance().info("Wrote default ", config_path);
  } else {
    auto props = mpmpes::config::ServerProperties::loadFile(config_path);
    mpmpes::config::ServerProperties::applyTo(props, cfg);
    mpmpes::util::Logger::instance().info("Loaded config ", config_path);
  }

  // CLI overrides (re-parse only flags that should win over file)
  // Re-scan argv for overrides after properties load
  for (int i = 1; i < argc; ++i) {
    const std::string arg = argv[i];
    auto need = [&]() -> const char* { return argv[++i]; };
    if (arg == "--port") cfg.port = static_cast<std::uint16_t>(std::stoi(need()));
    else if (arg == "--bind") cfg.bind = need();
    else if (arg == "--motd") cfg.motd = need();
    else if (arg == "--max-players") cfg.max_players = std::stoi(need());
    else if (arg == "--plugins-dir") cfg.plugins_dir = need();
    else if (arg == "--config") ++i;
    else if (arg == "--no-plugins") {
    } else if (arg == "-h" || arg == "--help") {
    }
  }
  if (no_plugins_flag) cfg.enable_plugins = false;

  try {
    mpmpes::server::Server server(std::move(cfg));
    server.start();
    server.runLoop();
  } catch (const std::exception& e) {
    mpmpes::util::Logger::instance().critical("fatal: ", e.what());
    return 1;
  }
  return 0;
}
