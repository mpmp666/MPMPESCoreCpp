#pragma once
// Dynamic custom maps (MPApi createDynamicMap/setMapColors/serveMapRequest).
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>
namespace mpmpes::server {
class MapManager {
public:
  // allocate a new dynamic map id (PHP createDynamicMap)
  std::int64_t createMap();
  bool setColors(std::int64_t id, const std::vector<std::uint8_t>& rgba_128x128);
  bool loadImage(std::int64_t id, const std::string& path);
  const std::vector<std::uint8_t>* colors(std::int64_t id) const;
  // decode a PNG file to 128x128 RGBA (nearest-resize). false on failure.
  static bool decodePng128(const std::string& path, std::vector<std::uint8_t>& out);
private:
  std::unordered_map<std::int64_t, std::vector<std::uint8_t>> colors_;
  std::int64_t next_id_ = 0;
};
}
