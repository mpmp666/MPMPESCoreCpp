#pragma once
#include <cstdint>
#include <cstddef>
#include <string>
namespace mpmpes::util {
// Minimal SHA-256 (public domain style), enough for the End biome zoom seed.
std::string sha256(std::string_view data); // raw 32 bytes
}
