// Byte-compare the C++ CraftingData builders against PHP reference dumps.
#include "mpmpes/item/Recipes.hpp"

#include <fstream>
#include <iostream>
#include <iterator>
#include <string>

int main() {
  auto read = [](const char* p) {
    std::ifstream in(p, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(in), {});
  };
  auto& reg = mpmpes::item::RecipeRegistry::instance();
  int fails = 0;
  auto cmp = [&](const char* name, const std::string& got, const std::string& exp) {
    if (got == exp) {
      std::cout << "OK " << name << " bytes=" << got.size() << "\n";
      return;
    }
    ++fails;
    std::cout << "FAIL " << name << " got=" << got.size() << " exp=" << exp.size() << "\n";
    std::size_t n = std::min(got.size(), exp.size());
    for (std::size_t i = 0; i < n; ++i) {
      if (got[i] != exp[i]) {
        std::cout << "  first diff at " << i << ": got=0x"
                  << std::hex << (int)(unsigned char)got[i] << " exp=0x" << (int)(unsigned char)exp[i]
                  << std::dec << "\n";
        std::cout << "  got ..." << got.substr(i > 8 ? i - 8 : 0, 24).size() << "B around\n";
        break;
      }
    }
  };
  cmp("full", reg.nativeFull(), read("tests/ref_crafting_native.bin"));
  cmp("old-0.13", reg.nativeOld(), read("tests/ref_crafting_old.bin"));
  cmp("0.12", reg.native012(), read("tests/ref_crafting_12.bin"));
  std::cout << (fails ? "MISMATCH" : "OK") << "\n";
  return fails ? 1 : 0;
}
