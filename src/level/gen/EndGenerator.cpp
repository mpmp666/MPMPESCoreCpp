// End terrain noise stack — 1:1 port of ender/noise/*.
#include "mpmpes/level/gen/EndGenerator.hpp"
#include "mpmpes/level/Chunk.hpp"

#include <algorithm>

namespace mpmpes::level::gen {

// ---------------------------------------------------------------- JavaRandom
std::int32_t JavaRandom::next(int bits) {
  // PHP works on the 48-bit state with split 24-bit halves
  std::int64_t lo = state_ & 0xffffff;
  std::int64_t hi = state_ >> 24;
  std::int64_t product = lo * 0xece66dLL + 11;
  std::int64_t upper = ((product >> 24) + hi * 0xece66dLL + lo * 0x5deLL) & 0xffffff;
  state_ = (upper << 24) | (product & 0xffffff);
  std::int64_t value = state_ >> (48 - bits);
  if (bits == 32 && value >= 0x80000000) value -= 0x100000000;
  return static_cast<std::int32_t>(value);
}

std::int32_t JavaRandom::nextInt(std::int32_t bound) {
  if ((bound & (bound - 1)) == 0) return static_cast<std::int32_t>((bound * static_cast<std::int64_t>(next(31))) >> 31);
  std::int32_t sample, mod;
  do {
    sample = next(31);
    mod = sample % bound;
  } while (sample - mod + (bound - 1) >= 0x80000000);
  return mod;
}

std::int64_t JavaRandom::nextLong() {
  std::int64_t upper = next(32);
  std::int64_t lower = next(32);
  if (lower < 0) upper = (upper - 1) & 0xffffffff;
  return (upper << 32) | (lower & 0xffffffff);
}

double JavaRandom::nextDouble() {
  std::int64_t upper = next(26);
  std::int64_t lower = next(27);
  return static_cast<double>((upper << 27) + lower) * 1.1102230246251565E-16;
}

// ---------------------------------------------------------------- ImprovedNoise
namespace {
const double GRADIENT[16][3] = {
    {1, 1, 0}, {-1, 1, 0}, {1, -1, 0}, {-1, -1, 0}, {1, 0, 1}, {-1, 0, 1}, {1, 0, -1}, {-1, 0, -1},
    {0, 1, 1}, {0, -1, 1}, {0, 1, -1}, {0, -1, -1}, {1, 1, 0}, {0, -1, 1}, {-1, 1, 0}, {0, -1, -1}};
inline double gradDot(int hash, double x, double y, double z) {
  const double* g = GRADIENT[hash & 15];
  return g[0] * x + g[1] * y + g[2] * z;
}
} // namespace

ImprovedNoise::ImprovedNoise(JavaRandom& random) {
  xo_ = random.nextDouble() * 256.0;
  yo_ = random.nextDouble() * 256.0;
  zo_ = random.nextDouble() * 256.0;
  for (int i = 0; i < 256; ++i) p_[i] = i;
  for (int i = 0; i < 256; ++i) {
    int j = i + random.nextInt(256 - i);
    std::swap(p_[i], p_[j]);
  }
}

double ImprovedNoise::noise(double x, double y, double z, double yScale, double yFudge) const {
  x += xo_;
  y += yo_;
  z += zo_;
  int xf = static_cast<int>(std::floor(x));
  int yf = static_cast<int>(std::floor(y));
  int zf = static_cast<int>(std::floor(z));
  double xr = x - xf;
  double yrOriginal = y - yf;
  double zr = z - zf;
  double yrFudge = 0.0;
  if (yScale != 0.0) {
    double fudgeLimit = (yFudge >= 0.0 && yFudge < yrOriginal) ? yFudge : yrOriginal;
    yrFudge = std::floor(fudgeLimit / yScale + 1.0000000116860974E-7) * yScale;
  }
  double yr = yrOriginal - yrFudge;
  int x0 = p_[xf & 255];
  int x1 = p_[(xf + 1) & 255];
  int xy00 = p_[(x0 + yf) & 255];
  int xy01 = p_[(x0 + yf + 1) & 255];
  int xy10 = p_[(x1 + yf) & 255];
  int xy11 = p_[(x1 + yf + 1) & 255];
  double d000 = gradDot(p_[(xy00 + zf) & 255], xr, yr, zr);
  double d100 = gradDot(p_[(xy10 + zf) & 255], xr - 1.0, yr, zr);
  double d010 = gradDot(p_[(xy01 + zf) & 255], xr, yr - 1.0, zr);
  double d110 = gradDot(p_[(xy11 + zf) & 255], xr - 1.0, yr - 1.0, zr);
  double d001 = gradDot(p_[(xy00 + zf + 1) & 255], xr, yr, zr - 1.0);
  double d101 = gradDot(p_[(xy10 + zf + 1) & 255], xr - 1.0, yr, zr - 1.0);
  double d011 = gradDot(p_[(xy01 + zf + 1) & 255], xr, yr - 1.0, zr - 1.0);
  double d111 = gradDot(p_[(xy11 + zf + 1) & 255], xr - 1.0, yr - 1.0, zr - 1.0);
  double xa = xr * xr * xr * (xr * (xr * 6.0 - 15.0) + 10.0);
  double ya = yrOriginal * yrOriginal * yrOriginal * (yrOriginal * (yrOriginal * 6.0 - 15.0) + 10.0);
  double za = zr * zr * zr * (zr * (zr * 6.0 - 15.0) + 10.0);
  double x00 = d000 + xa * (d100 - d000);
  double x10 = d010 + xa * (d110 - d010);
  double x01 = d001 + xa * (d101 - d001);
  double x11 = d011 + xa * (d111 - d011);
  double z0 = x00 + ya * (x10 - x00);
  double z1 = x01 + ya * (x11 - x01);
  return z0 + za * (z1 - z0);
}

// ---------------------------------------------------------------- SimplexNoise (2D)
EndSimplexNoise::EndSimplexNoise(JavaRandom& random) {
  xo_ = random.nextDouble() * 256.0;
  yo_ = random.nextDouble() * 256.0;
  zo_ = random.nextDouble() * 256.0;
  for (int i = 0; i < 256; ++i) p_[i] = i;
  for (int i = 0; i < 256; ++i) {
    int j = i + random.nextInt(256 - i);
    std::swap(p_[i], p_[j]);
  }
  f2_ = 0.5 * (std::sqrt(3.0) - 1.0);
  g2_ = (3.0 - std::sqrt(3.0)) / 6.0;
}

static double corner2D(int index, double x, double y) {
  double t = 0.5 - x * x - y * y;
  if (t < 0.0) return 0.0;
  t *= t;
  const double* g = GRADIENT[index];
  return t * t * (g[0] * x + g[1] * y + g[2] * 0.0);
}

double EndSimplexNoise::getValue(double x, double y) const {
  double s = (x + y) * f2_;
  int i = static_cast<int>(std::floor(x + s));
  int j = static_cast<int>(std::floor(y + s));
  double t = (i + j) * g2_;
  double x0 = x - (i - t);
  double y0 = y - (j - t);
  int i1 = x0 > y0 ? 1 : 0;
  int j1 = x0 > y0 ? 0 : 1;
  double x1 = x0 - i1 + g2_;
  double y1 = y0 - j1 + g2_;
  double x2 = x0 - 1.0 + 2.0 * g2_;
  double y2 = y0 - 1.0 + 2.0 * g2_;
  int ii = i & 255, jj = j & 255;
  int gi0 = p_[(ii + p_[jj]) & 255] % 12;
  int gi1 = p_[(ii + i1 + p_[(jj + j1) & 255]) & 255] % 12;
  int gi2 = p_[(ii + 1 + p_[(jj + 1) & 255]) & 255] % 12;
  double n0 = corner2D(gi0, x0, y0);
  double n1 = corner2D(gi1, x1, y1);
  double n2 = corner2D(gi2, x2, y2);
  return 70.0 * (n0 + n1 + n2);
}

// ---------------------------------------------------------------- BlendedNoise
BlendedNoise::BlendedNoise(JavaRandom& random) {
  min_limit_.reserve(16);
  max_limit_.reserve(16);
  main_.reserve(8);
  for (int i = 0; i < 16; ++i) min_limit_.emplace_back(random);
  for (int i = 0; i < 16; ++i) max_limit_.emplace_back(random);
  for (int i = 0; i < 8; ++i) main_.emplace_back(random);
}

static double wrap32m(double x) {
  return x - std::floor(x / 33554432.0 + 0.5) * 33554432.0;
}

double BlendedNoise::compute(int x, int y, int z) const {
  constexpr double MULTIPLIER = 684.412 * 0.25;
  double limitX = x * MULTIPLIER, limitY = y * MULTIPLIER, limitZ = z * MULTIPLIER;
  double mainX = limitX / 80.0, mainY = limitY / 160.0, mainZ = limitZ / 80.0;
  double limitSmear = MULTIPLIER * 4.0;
  double mainSmear = limitSmear / 160.0;
  double main = 0.0, pow = 1.0;
  for (int i = 0; i < 8; ++i) {
    main += main_[i].noise(wrap32m(mainX * pow), wrap32m(mainY * pow), wrap32m(mainZ * pow),
                           mainSmear * pow, mainY * pow) / pow;
    pow /= 2.0;
  }
  double factor = (main / 10.0 + 1.0) / 2.0;
  bool isMax = factor >= 1.0, isMin = factor <= 0.0;
  double min = 0.0, max = 0.0;
  pow = 1.0;
  for (int i = 0; i < 16; ++i) {
    double wx = wrap32m(limitX * pow);
    double wy = wrap32m(limitY * pow);
    double wz = wrap32m(limitZ * pow);
    double smear = limitSmear * pow;
    if (!isMax) min += min_limit_[i].noise(wx, wy, wz, smear, limitY * pow) / pow;
    if (!isMin) max += max_limit_[i].noise(wx, wy, wz, smear, limitY * pow) / pow;
    pow /= 2.0;
  }
  min /= 512.0;
  max /= 512.0;
  return (factor < 0.0 ? min : (factor > 1.0 ? max : min + factor * (max - min))) / 128.0;
}

// ---------------------------------------------------------------- EndIslands
// PHP f32(): round through float32
static inline float f32(float v) { return v; } // float is already f32 in C++
static double clampHeight(double v) { return std::isnan(v) ? NAN : (v < -100.0 ? -100.0 : std::min(v, 80.0)); }

EndIslands::EndIslands(std::int64_t seed) {
  JavaRandom r(seed);
  r.consumeCount(17292);
  noise_ = std::make_unique<EndSimplexNoise>(r);
}

// neighborhood: returns accepted island candidates [(xo, zo, size)]
static std::vector<std::tuple<int, int, float>> islandNeighborhood(const EndSimplexNoise& noise, int chunkX, int chunkZ) {
  std::vector<std::tuple<int, int, float>> accepted;
  for (int xo = -12; xo <= 12; ++xo)
    for (int zo = -12; zo <= 12; ++zo) {
      int x = chunkX + xo, z = chunkZ + zo;
      if (x * x + z * z <= 4096) continue;
      // candidates cache per (x,z): compute each call (PHP caches, deterministic)
      float size = NAN;
      if (noise.getValue(static_cast<float>(x), static_cast<float>(z)) < -0.8999999761581421f) {
        float ax = f32(std::fabs(f32(static_cast<float>(x))) * 3439.0f);
        float az = f32(std::fabs(f32(static_cast<float>(z))) * 147.0f);
        size = f32(f32(std::fmod(f32(ax + az), 13.0f)) + 9.0f);
      }
      if (!std::isnan(size)) accepted.emplace_back(xo, zo, size);
    }
  return accepted;
}

double EndIslands::heightValue(int sectionX, int sectionZ) {
  int chunkX = floorDiv(sectionX, 2), chunkZ = floorDiv(sectionZ, 2);
  int subX = sectionX % 2, subZ = sectionZ % 2;
  if (subX < 0) subX += 2;
  if (subZ < 0) subZ += 2;
  std::int64_t distanceSquared = (static_cast<std::int64_t>(sectionX) * sectionX + static_cast<std::int64_t>(sectionZ) * sectionZ) & 0xffffffffLL;
  if (distanceSquared >= 0x80000000LL) distanceSquared -= 0x100000000LL;
  float radius = f32(std::sqrt(f32(static_cast<float>(distanceSquared))));
  double height = clampHeight(f32(100.0f - f32(radius * 8.0f)));
  for (auto& [xo, zo, size] : islandNeighborhood(*noise_, chunkX, chunkZ)) {
    float xd = static_cast<float>(subX - xo * 2);
    float zd = static_cast<float>(subZ - zo * 2);
    float distance = f32(std::sqrt(f32(f32(xd * xd) + f32(zd * zd))));
    double newHeight = clampHeight(f32(100.0f - f32(distance * size)));
    height = std::isnan(height) ? NAN : std::max(height, newHeight);
  }
  return height;
}

double EndIslands::compute(int bx, int bz) {
  return (height(bx, bz) - 8.0) / 128.0;
}

// ---------------------------------------------------------------- EndTerrainNoise
EndTerrainNoise::EndTerrainNoise(std::int64_t seed) : base_(nullptr), islands_(seed) {
  JavaRandom r(seed);
  base_ = std::make_unique<BlendedNoise>(r);
}

double EndTerrainNoise::density(int x, int y, int z) {
  double bottomAlpha = (y - 4.0) / 28.0;
  double bottom = bottomAlpha < 0.0 ? 0.0 : (bottomAlpha > 1.0 ? 1.0 : bottomAlpha);
  if (bottom == 0.0) return 0.64 * -0.234375;
  double topAlpha = (y - 56.0) / 256.0;
  double top = topAlpha < 0.0 ? 1.0 : (topAlpha > 1.0 ? 0.0 : 1.0 + topAlpha * (0.0 - 1.0));
  double topSlide;
  if (top == 0.0) {
    topSlide = -23.4375;
  } else {
    double slopedCheese = islands_.compute(x, z) + base_->compute(x, y, z);
    topSlide = -23.4375 + top * (23.4375 + slopedCheese);
  }
  return 0.64 * (-0.234375 + bottom * (0.234375 + topSlide));
}

void EndTerrainNoise::chunkNodes(int cx, int cz, double out[297]) {
  int i = 0;
  for (int gx = 0; gx <= 2; ++gx)
    for (int gz = 0; gz <= 2; ++gz)
      for (int gy = 0; gy <= 32; ++gy) out[i++] = density(cx * 16 + gx * 8, gy * 4, cz * 16 + gz * 8);
}

void generateEndTerrainChunk(level::Chunk& chunk, std::int64_t seed) {
  const int cx = chunk.x(), cz = chunk.z();
  EndTerrainNoise terrain(seed);
  double nodes[297];
  terrain.chunkNodes(cx, cz, nodes);
  auto nodeAt = [&](int gx, int gz, int gy) -> double { return nodes[(gx * 3 + gz) * 33 + gy]; };
  for (int x = 0; x < 16; ++x)
    for (int z = 0; z < 16; ++z) {
      chunk.setBiome(x, z, 9, 0x1a0b2e); // END, color (26,11,46)
      // EndChunkGrid::forEachSolid interpolation for this column
      for (int y = 0; y < 128; ++y) {
        int gx = x >> 3, gz = z >> 3, gy = y >> 2;
        double fy = (y & 3) / 4.0, fx = (x & 7) / 8.0, fz = (z & 7) / 8.0;
        double n00 = nodeAt(gx, gz, gy), n10 = nodeAt(gx + 1, gz, gy);
        double n01 = nodeAt(gx, gz + 1, gy), n11 = nodeAt(gx + 1, gz + 1, gy);
        double xz00 = n00 + fy * (nodeAt(gx, gz, gy + 1) - n00);
        double xz10 = n10 + fy * (nodeAt(gx + 1, gz, gy + 1) - n10);
        double xz01 = n01 + fy * (nodeAt(gx, gz + 1, gy + 1) - n01);
        double xz11 = n11 + fy * (nodeAt(gx + 1, gz + 1, gy + 1) - n11);
        double z0 = xz00 + fx * (xz10 - xz00);
        double z1 = xz01 + fx * (xz11 - xz01);
        double d = z0 + fz * (z1 - z0);
        if (d > 0.0) chunk.setBlock(x, y, z, 121, 0); // END_STONE
      }
    }
  chunk.setPopulated(true);
}

} // namespace mpmpes::level::gen
