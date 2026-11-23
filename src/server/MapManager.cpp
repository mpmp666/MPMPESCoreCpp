// MapManager + minimal PNG decoder (8-bit RGB/RGBA/grayscale, non-interlaced).
#include "mpmpes/server/MapManager.hpp"
#include "mpmpes/nbt/Tag.hpp" // inflateAny

#include <cstring>
#include <fstream>

namespace mpmpes::server {

std::int64_t MapManager::createMap() { return next_id_++; }

bool MapManager::setColors(std::int64_t id, const std::vector<std::uint8_t>& rgba_128x128) {
  if (rgba_128x128.size() < 128 * 128 * 4) return false;
  colors_[id] = rgba_128x128;
  return true;
}

const std::vector<std::uint8_t>* MapManager::colors(std::int64_t id) const {
  auto it = colors_.find(id);
  return it != colors_.end() ? &it->second : nullptr;
}

bool MapManager::loadImage(std::int64_t id, const std::string& path) {
  std::vector<std::uint8_t> rgba;
  if (!decodePng128(path, rgba)) return false;
  return setColors(id, rgba);
}

namespace {
std::uint32_t be32(const unsigned char* p) {
  return (static_cast<std::uint32_t>(p[0]) << 24) | (p[1] << 16) | (p[2] << 8) | p[3];
}
struct Png {
  int w = 0, h = 0, depth = 0, ctype = 0;
  std::string idat;
};
bool parsePng(const std::string& data, Png& png) {
  static const unsigned char sig[8] = {137, 80, 78, 71, 13, 10, 26, 10};
  if (data.size() < 8 || std::memcmp(data.data(), sig, 8) != 0) return false;
  std::size_t off = 8;
  while (off + 12 <= data.size()) {
    std::uint32_t len = be32(reinterpret_cast<const unsigned char*>(data.data() + off));
    std::string type = data.substr(off + 4, 4);
    const unsigned char* body = reinterpret_cast<const unsigned char*>(data.data() + off + 8);
    if (type == "IHDR") {
      png.w = be32(body);
      png.h = be32(body + 4);
      png.depth = body[8];
      png.ctype = body[9];
    } else if (type == "IDAT") {
      png.idat.append(reinterpret_cast<const char*>(body), len);
    } else if (type == "IEND") {
      break;
    }
    off += 12 + len;
  }
  return !png.idat.empty();
}
} // namespace

bool MapManager::decodePng128(const std::string& path, std::vector<std::uint8_t>& out) {
  std::ifstream in(path, std::ios::binary);
  if (!in) return false;
  std::string data((std::istreambuf_iterator<char>(in)), {});
  Png png;
  if (!parsePng(data, png)) return false;
  if (png.depth != 8) return false;
  int channels;
  switch (png.ctype) {
    case 2: channels = 3; break;  // RGB
    case 6: channels = 4; break;  // RGBA
    case 0: channels = 1; break;  // grayscale
    default: return false;
  }
  auto rawOpt = nbt::inflateAny(png.idat);
  if (!rawOpt) return false;
  const auto& raw = *rawOpt;
  const int stride = png.w * channels + 1;
  if (static_cast<int>(raw.size()) < stride * png.h) return false;
  // unfilter scanlines
  std::vector<unsigned char> img(png.h * png.w * channels);
  auto paeth = [](int a, int b, int c) {
    int p = a + b - c;
    int pa = std::abs(p - a), pb = std::abs(p - b), pc = std::abs(p - c);
    return pa <= pb && pa <= pc ? a : (pb <= pc ? b : c);
  };
  for (int y = 0; y < png.h; ++y) {
    const unsigned char* line = reinterpret_cast<const unsigned char*>(raw.data()) + y * stride;
    unsigned char filter = line[0];
    const unsigned char* src = line + 1;
    unsigned char* dst = img.data() + static_cast<std::size_t>(y) * png.w * channels;
    const unsigned char* prev = y > 0 ? img.data() + static_cast<std::size_t>(y - 1) * png.w * channels : nullptr;
    for (int i = 0; i < png.w * channels; ++i) {
      int a = i >= channels ? dst[i - channels] : 0;
      int b = prev ? prev[i] : 0;
      int c = (prev && i >= channels) ? prev[i - channels] : 0;
      int v = src[i];
      switch (filter) {
        case 0: break;
        case 1: v += a; break;
        case 2: v += b; break;
        case 3: v += (a + b) / 2; break;
        case 4: v += paeth(a, b, c); break;
      }
      dst[i] = static_cast<unsigned char>(v);
    }
  }
  // resize to 128x128 RGBA (nearest neighbor)
  out.assign(128 * 128 * 4, 0xff);
  for (int y = 0; y < 128; ++y)
    for (int x = 0; x < 128; ++x) {
      int sx = x * png.w / 128, sy = y * png.h / 128;
      const unsigned char* p = img.data() + (static_cast<std::size_t>(sy) * png.w + sx) * channels;
      unsigned char* q = out.data() + (static_cast<std::size_t>(y) * 128 + x) * 4;
      if (channels == 1) {
        q[0] = q[1] = q[2] = p[0];
        q[3] = 0xff;
      } else {
        q[0] = p[0];
        q[1] = p[1];
        q[2] = p[2];
        q[3] = channels == 4 ? p[3] : 0xff;
      }
    }
  return true;
}

} // namespace mpmpes::server
