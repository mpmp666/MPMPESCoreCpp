#include "mpmpes/level/gen/NormalGenerator.hpp"

#include <zlib.h>

#include <cmath>
#include <algorithm>
#include <cstring>

namespace mpmpes::level::gen {

void PmRandom::setSeed(std::int64_t seed) {
  // crc32(pack("N", seed)) — big-endian int32; PHP stores it as positive int64
  unsigned char b[4] = {static_cast<unsigned char>(seed >> 24), static_cast<unsigned char>(seed >> 16),
                        static_cast<unsigned char>(seed >> 8), static_cast<unsigned char>(seed)};
  seed_ = static_cast<std::int64_t>(static_cast<std::uint32_t>(crc32(0, b, 4)));
}

std::int32_t PmRandom::nextSignedInt() {
  // PHP 64-bit ints: no 32-bit truncation on the multiply, sign-extended xor
  std::int64_t t = (((seed_ * 65535) + 31337) >> 8) + 1337;
  t = static_cast<std::int32_t>(static_cast<std::uint32_t>(t)); // $t << 32 >> 32
  seed_ ^= t;
  return static_cast<std::int32_t>(t);
}

// ------------------------------------------------------------------- Perlin base
namespace {
const int GRAD3[12][3] = {{1, 1, 0},  {-1, 1, 0}, {1, -1, 0}, {-1, -1, 0},
                          {1, 0, 1},  {-1, 0, 1}, {1, 0, -1}, {-1, 0, -1},
                          {0, 1, 1},  {0, -1, 1}, {0, 1, -1}, {0, -1, -1}};
inline int ifloor(double x) { return x >= 0 ? static_cast<int>(x) : static_cast<int>(x) - 1; }
} // namespace

Simplex::Simplex(PmRandom& random, int octaves, double persistence, double expansion)
    : octaves_(octaves), persistence_(persistence), expansion_(expansion) {
  // Perlin::__construct first, THEN Simplex offsetW (PHP parent::__construct order)
  offset_x_ = random.nextFloat() * 256;
  offset_y_ = random.nextFloat() * 256;
  offset_z_ = random.nextFloat() * 256;
  for (int i = 0; i < 512; ++i) perm_[i] = 0;
  for (int i = 0; i < 256; ++i) perm_[i] = random.nextBoundedInt(256);
  for (int i = 0; i < 256; ++i) {
    int pos = random.nextBoundedInt(256 - i) + i;
    int old = perm_[i];
    perm_[i] = perm_[pos];
    perm_[pos] = old;
    perm_[i + 256] = perm_[i];
  }
  (void)random.nextFloat(); // offsetW (Simplex 4D; unused but consumes the stream)
}

double Simplex::noise2D(double x, double z, bool normalized) const {
  double result = 0, amp = 1, freq = 1, max = 0;
  x *= expansion_;
  z *= expansion_;
  for (int i = 0; i < octaves_; ++i) {
    result += getNoise2D(x * freq, z * freq) * amp;
    max += amp;
    freq *= 2;
    amp *= persistence_;
  }
  if (normalized) result /= max;
  return result;
}

double Simplex::noise3D(double x, double y, double z, bool normalized) const {
  double result = 0, amp = 1, freq = 1, max = 0;
  x *= expansion_;
  y *= expansion_;
  z *= expansion_;
  for (int i = 0; i < octaves_; ++i) {
    result += getNoise3D(x * freq, y * freq, z * freq) * amp;
    max += amp;
    freq *= 2;
    amp *= persistence_;
  }
  if (normalized) result /= max;
  return result;
}

double Simplex::getNoise2D(double x, double y) const {
  static const double F2 = 0.5 * (std::sqrt(3.0) - 1.0);
  static const double G2 = (3.0 - std::sqrt(3.0)) / 6.0;
  static const double G22 = G2 * 2.0 - 1.0;
  x += offset_x_;
  y += offset_y_;
  double s = (x + y) * F2;
  int i = static_cast<int>(x + s), j = static_cast<int>(y + s); // PHP (int) truncation
  double t = (i + j) * G2;
  double x0 = x - (i - t), y0 = y - (j - t);
  int i1, j1;
  if (x0 > y0) { i1 = 1; j1 = 0; } else { i1 = 0; j1 = 1; }
  double x1 = x0 - i1 + G2, y1 = y0 - j1 + G2;
  double x2 = x0 + G22, y2 = y0 + G22;
  int ii = i & 255, jj = j & 255;
  double n = 0;
  double t0 = 0.5 - x0 * x0 - y0 * y0;
  if (t0 > 0) {
    const int* g = GRAD3[perm_[ii + perm_[jj]] % 12];
    n += t0 * t0 * t0 * t0 * (g[0] * x0 + g[1] * y0);
  }
  double t1 = 0.5 - x1 * x1 - y1 * y1;
  if (t1 > 0) {
    const int* g = GRAD3[perm_[ii + i1 + perm_[jj + j1]] % 12];
    n += t1 * t1 * t1 * t1 * (g[0] * x1 + g[1] * y1);
  }
  double t2 = 0.5 - x2 * x2 - y2 * y2;
  if (t2 > 0) {
    const int* g = GRAD3[perm_[ii + 1 + perm_[jj + 1]] % 12];
    n += t2 * t2 * t2 * t2 * (g[0] * x2 + g[1] * y2);
  }
  return 70.0 * n;
}

double Simplex::getNoise3D(double x, double y, double z) const {
  static const double F3 = 1.0 / 3.0;
  static const double G3 = 1.0 / 6.0;
  x += offset_x_;
  y += offset_y_;
  z += offset_z_;
  double s = (x + y + z) * F3;
  int i = static_cast<int>(x + s), j = static_cast<int>(y + s), k = static_cast<int>(z + s);
  double t = (i + j + k) * G3;
  double x0 = x - (i - t), y0 = y - (j - t), z0 = z - (k - t);
  int i1, j1, k1, i2, j2, k2;
  if (x0 >= y0) {
    if (y0 >= z0) { i1 = 1; j1 = 0; k1 = 0; i2 = 1; j2 = 1; k2 = 0; }
    else if (x0 >= z0) { i1 = 1; j1 = 0; k1 = 0; i2 = 1; j2 = 0; k2 = 1; }
    else { i1 = 0; j1 = 0; k1 = 1; i2 = 1; j2 = 0; k2 = 1; }
  } else {
    if (y0 < z0) { i1 = 0; j1 = 0; k1 = 1; i2 = 0; j2 = 1; k2 = 1; }
    else if (x0 < z0) { i1 = 0; j1 = 1; k1 = 0; i2 = 0; j2 = 1; k2 = 1; }
    else { i1 = 0; j1 = 1; k1 = 0; i2 = 1; j2 = 1; k2 = 0; }
  }
  double x1 = x0 - i1 + G3, y1 = y0 - j1 + G3, z1 = z0 - k1 + G3;
  double x2 = x0 - i2 + 2 * G3, y2 = y0 - j2 + 2 * G3, z2 = z0 - k2 + 2 * G3;
  double x3 = x0 - 1 + 3 * G3, y3 = y0 - 1 + 3 * G3, z3 = z0 - 1 + 3 * G3;
  int ii = i & 255, jj = j & 255, kk = k & 255;
  double n = 0;
  double t0 = 0.6 - x0 * x0 - y0 * y0 - z0 * z0;
  if (t0 > 0) {
    const int* g = GRAD3[perm_[ii + perm_[jj + perm_[kk]]] % 12];
    n += t0 * t0 * t0 * t0 * (g[0] * x0 + g[1] * y0 + g[2] * z0);
  }
  double t1 = 0.6 - x1 * x1 - y1 * y1 - z1 * z1;
  if (t1 > 0) {
    const int* g = GRAD3[perm_[ii + i1 + perm_[jj + j1 + perm_[kk + k1]]] % 12];
    n += t1 * t1 * t1 * t1 * (g[0] * x1 + g[1] * y1 + g[2] * z1);
  }
  double t2 = 0.6 - x2 * x2 - y2 * y2 - z2 * z2;
  if (t2 > 0) {
    const int* g = GRAD3[perm_[ii + i2 + perm_[jj + j2 + perm_[kk + k2]]] % 12];
    n += t2 * t2 * t2 * t2 * (g[0] * x2 + g[1] * y2 + g[2] * z2);
  }
  double t3 = 0.6 - x3 * x3 - y3 * y3 - z3 * z3;
  if (t3 > 0) {
    const int* g = GRAD3[perm_[ii + 1 + perm_[jj + 1 + perm_[kk + 1]]] % 12];
    n += t3 * t3 * t3 * t3 * (g[0] * x3 + g[1] * y3 + g[2] * z3);
  }
  return 32.0 * n;
}

std::vector<double> fastNoise3D(const Simplex& noise, int xs, int ys, int zs, int xr, int yr,
                                int zr, int bx, int by, int bz) {
  // grid [x][z][y], sizes xs+1 / zs+1 / ys+1, sampled every xr/yr/zr then trilinear-filled
  std::vector<double> a(static_cast<std::size_t>(xs + 1) * (zs + 1) * (ys + 1), 0.0);
  auto at = [&](int x, int z, int y) -> double& {
    return a[(static_cast<std::size_t>(x) * (zs + 1) + z) * (ys + 1) + y];
  };
  for (int xx = 0; xx <= xs; xx += xr)
    for (int zz = 0; zz <= zs; zz += zr)
      for (int yy = 0; yy <= ys; yy += yr)
        at(xx, zz, yy) = noise.noise3D(bx + xx, by + yy, bz + zz, true);
  for (int xx = 0; xx < xs; ++xx)
    for (int zz = 0; zz < zs; ++zz)
      for (int yy = 0; yy < ys; ++yy) {
        if (xx % xr == 0 && zz % zr == 0 && yy % yr == 0) continue;
        int nx = (xx / xr) * xr, ny = (yy / yr) * yr, nz = (zz / zr) * zr;
        int nnx = nx + xr, nny = ny + yr, nnz = nz + zr;
        double dx1 = static_cast<double>(nnx - xx) / (nnx - nx);
        double dx2 = static_cast<double>(xx - nx) / (nnx - nx);
        double dy1 = static_cast<double>(nny - yy) / (nny - ny);
        double dy2 = static_cast<double>(yy - ny) / (nny - ny);
        at(xx, zz, yy) =
            (static_cast<double>(nnz - zz) / (nnz - nz)) *
                (dy1 * (dx1 * at(nx, nz, ny) + dx2 * at(nnx, nz, ny)) +
                 dy2 * (dx1 * at(nx, nz, nny) + dx2 * at(nnx, nz, nny))) +
            (static_cast<double>(zz - nz) / (nnz - nz)) *
                (dy1 * (dx1 * at(nx, nnz, ny) + dx2 * at(nnx, nnz, ny)) +
                 dy2 * (dx1 * at(nx, nnz, nny) + dx2 * at(nnx, nnz, nny)));
      }
  return a;
}

// ------------------------------------------------------------------- biomes
namespace {
// generateBiomeColor(temperature, rainfall)
std::uint32_t biomeColor(double temperature, double rainfall) {
  auto lerpColor = [](const double a[3], const double b[3], double s, double out[3]) {
    double inv = 1 - s;
    for (int i = 0; i < 3; ++i) out[i] = a[i] * inv + b[i] * s;
  };
  double x = (1 - temperature) * 255;
  double z = (1 - rainfall * temperature) * 255;
  const double c1[3] = {0x47, 0xd0, 0x33}, c2[3] = {0x6c, 0xb4, 0x93};
  const double c3[3] = {0xbf, 0xb6, 0x55}, c4[3] = {0x80, 0xb4, 0x97};
  double l1[3], l2[3], c[3];
  lerpColor(c1, c2, x / 256, l1);
  lerpColor(c3, c4, x / 256, l2);
  lerpColor(l1, l2, z / 256, c);
  return (static_cast<std::uint32_t>(static_cast<int>(c[0])) << 16) |
         (static_cast<std::uint32_t>(static_cast<int>(c[1])) << 8) |
         static_cast<std::uint32_t>(static_cast<int>(c[2]));
}

// Block ids used by covers/decorations
constexpr std::uint8_t GRASS = 2, DIRT = 3, STONE = 1, BEDROCK = 7, STILL_WATER = 9;
constexpr std::uint8_t SAND = 12, SANDSTONE = 24, SNOW_LAYER = 78, PODZOL = 243, MOSS_STONE = 48;

BiomeDef mk(int id, int mn, int mx, double temp, double rain,
            std::vector<std::pair<std::uint8_t, std::uint8_t>> cover, std::uint32_t color_override = 0) {
  BiomeDef b;
  b.id = id;
  b.min_y = mn;
  b.max_y = mx;
  b.color = color_override ? color_override : biomeColor(temp, rain);
  b.cover = std::move(cover);
  return b;
}

constexpr std::size_t BIOME_TABLE_SIZE = 11;
const BiomeDef* buildTable() {
  static const BiomeDef table[] = {
      // OCEAN 0
      mk(0, 46, 68, 0.5, 0.5, {{GRASS, 0}, {DIRT, 0}, {DIRT, 0}, {DIRT, 0}, {DIRT, 0}}),
      // PLAINS 1
      mk(1, 61, 68, 0.8, 0.4, {{GRASS, 0}, {DIRT, 0}, {DIRT, 0}, {DIRT, 0}, {DIRT, 0}}),
      // DESERT 2 (SandyBiome: 4x sand + 31x sandstone)
      mk(2, 63, 74, 2.0, 0.0, [] {
        std::vector<std::pair<std::uint8_t, std::uint8_t>> c(4, {SAND, 0});
        for (int i = 0; i < 31; ++i) c.push_back({SANDSTONE, 0});
        return c;
      }()),
      // MOUNTAINS 3
      mk(3, 63, 127, 0.4, 0.5, {{GRASS, 0}, {DIRT, 0}, {DIRT, 0}, {DIRT, 0}, {DIRT, 0}}),
      // FOREST 4
      mk(4, 63, 81, 0.8, 0.5, {{GRASS, 0}, {DIRT, 0}, {DIRT, 0}, {DIRT, 0}, {DIRT, 0}}),
      // TAIGA 5 (podzol, podzol, moss stone x3)
      mk(5, 63, 81, 0.05, 0.8, {{PODZOL, 0}, {PODZOL, 0}, {MOSS_STONE, 0}, {MOSS_STONE, 0}, {MOSS_STONE, 0}}),
      // SWAMP 6
      mk(6, 62, 63, 0.8, 0.9, {{GRASS, 0}, {DIRT, 0}, {DIRT, 0}, {DIRT, 0}, {DIRT, 0}}, 0x6a7039),
      // RIVER 7
      mk(7, 58, 62, 0.5, 0.7, {{GRASS, 0}, {DIRT, 0}, {DIRT, 0}, {DIRT, 0}, {DIRT, 0}}),
      // ICE_PLAINS 12
      mk(12, 63, 74, 0.05, 0.8, {{SNOW_LAYER, 0}, {GRASS, 0}, {DIRT, 0}, {DIRT, 0}, {DIRT, 0}}),
      // SMALL_MOUNTAINS 20
      mk(20, 63, 97, 0.4, 0.5, {{GRASS, 0}, {DIRT, 0}, {DIRT, 0}, {DIRT, 0}, {DIRT, 0}}),
      // BIRCH_FOREST 27
      mk(27, 63, 81, 0.5, 0.5, {{GRASS, 0}, {DIRT, 0}, {DIRT, 0}, {DIRT, 0}, {DIRT, 0}}),
  };
  return table;
}
} // namespace

const BiomeDef& biomeDef(int id) {
  static const BiomeDef* table = buildTable();
  static const BiomeDef fallback = mk(1, 61, 68, 0.8, 0.4,
                                      {{GRASS, 0}, {DIRT, 0}, {DIRT, 0}, {DIRT, 0}, {DIRT, 0}});
  for (std::size_t i = 0; i < BIOME_TABLE_SIZE; ++i)
    if (table[i].id == id) return table[i];
  return fallback;
}

BiomeSelector::BiomeSelector(PmRandom& random)
    : temperature_(random, 2, 1.0 / 16.0, 1.0 / 512.0), rainfall_(random, 2, 1.0 / 16.0, 1.0 / 512.0) {
  for (int i = 0; i < 64; ++i)
    for (int j = 0; j < 64; ++j) {
      double temp = i / 63.0, rain = j / 63.0;
      int id;
      if (rain < 0.25) {
        if (temp < 0.7) id = 0;
        else if (temp < 0.85) id = 7;
        else id = 6;
      } else if (rain < 0.60) {
        if (temp < 0.25) id = 12;
        else if (temp < 0.75) id = 1;
        else id = 2;
      } else if (rain < 0.80) {
        if (temp < 0.25) id = 5;
        else if (temp < 0.75) id = 4;
        else id = 27;
      } else {
        if (temp < 0.25) id = 3;
        else if (temp < 0.70) id = 20;
        else id = 7;
      }
      map_[i + (j << 6)] = static_cast<std::uint8_t>(id);
    }
}

int BiomeSelector::pick(int x, int z) const {
  double t = (temperature_.noise2D(x, z, true) + 1) / 2;
  double r = (rainfall_.noise2D(x, z, true) + 1) / 2;
  int ti = static_cast<int>(t * 63), ri = static_cast<int>(r * 63);
  ti = std::clamp(ti, 0, 63);
  ri = std::clamp(ri, 0, 63);
  return map_[ti + (ri << 6)];
}

} // namespace mpmpes::level::gen
