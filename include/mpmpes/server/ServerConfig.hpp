#pragma once

#include <cstdint>
#include <string>

namespace mpmpes::server {

struct ServerConfig {
  std::string bind = "0.0.0.0";
  std::uint16_t port = 19132;
  std::string motd = "MPMPESCoreCpp";
  int max_players = 20;
  std::string version_name = "0.14.3";
  int protocol = 70;
  std::string level_name = "world";
  std::string level_type = "flat";
  int gamemode = 0;
  std::string plugins_dir = "plugins";
  bool enable_plugins = true;
  std::string data_path = "."; // working directory for properties/plugins

  // Outbound Batch (PM network.compression-level / batch-threshold)
  // threshold in KB: packet payload >= this size is zlib-wrapped as Batch 0x92.
  // Set threshold_kb < 0 to disable batching; 0 = batch everything.
  int network_compression_level = 7; // zlib 0..9 (PM default 7; pocketmine.yml often 2)
  int network_batch_threshold_kb = 1; // 1 KB ≈ 1024 bytes (PM uses 256 bytes; we use KB as requested)

  // Always drop items into the world when breaking blocks (even creative).
  // Vanilla creative does not drop; testing servers often want this on.
  bool always_drop_on_break = true;

  // Autosave interval in seconds (0 = only on quit/stop)
  int autosave_seconds = 60;
};

} // namespace mpmpes::server
