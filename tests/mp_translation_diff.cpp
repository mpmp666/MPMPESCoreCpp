// Differential test: translate the PHP reference native buffers with the C++
// MultiProtocol implementation and compare byte-for-byte against
// translateForFamily() output recorded in ref_php.txt.
#include "mpmpes/protocol/MultiProtocol.hpp"

#include <fstream>
#include <iostream>
#include <map>
#include <sstream>
#include <string>
#include <vector>

using namespace mpmpes::protocol;

static std::string unhex(const std::string& h) {
  std::string out;
  out.reserve(h.size() / 2);
  for (std::size_t i = 0; i + 1 < h.size(); i += 2)
    out.push_back(static_cast<char>(std::stoi(h.substr(i, 2), nullptr, 16)));
  return out;
}

int main(int argc, char** argv) {
  if (argc < 2) {
    std::cerr << "usage: mp_translation_diff <ref_php.txt>\n";
    return 2;
  }
  std::ifstream in(argv[1]);
  if (!in) {
    std::cerr << "cannot open " << argv[1] << "\n";
    return 2;
  }
  std::map<std::string, std::string> native;
  // case -> family -> list of hex
  std::map<std::string, std::map<int, std::vector<std::string>>> ref;
  std::string line;
  while (std::getline(in, line)) {
    std::istringstream ss(line);
    std::string kind, name;
    ss >> kind >> name;
    if (kind == "N") {
      std::string h;
      ss >> h;
      native[name] = unhex(h);
    } else if (kind == "T") {
      int fam, idx;
      std::string h;
      ss >> fam >> idx >> h;
      auto& v = ref[name][fam];
      if (static_cast<int>(v.size()) <= idx) v.resize(idx + 1);
      v[idx] = h;
    }
  }
  mp::setStartGame16Info(1, "金安卓");
  mp::setSkinLookup([](std::int64_t, std::string& skin, bool& slim) {
    skin = "";
    slim = false;
  });

  int pass = 0, fail = 0;
  auto toHex = [](const std::string& s) {
    std::string h;
    static const char* d = "0123456789abcdef";
    for (unsigned char c : s) {
      h.push_back(d[c >> 4]);
      h.push_back(d[c & 15]);
    }
    return h;
  };

  for (auto& [name, nbuf] : native) {
    for (auto& [fam, expected] : ref[name]) {
      auto got = mp::translateOutgoing(fam, nbuf);
      if (fam == mp::F011) {
        // family 5 skin lookup yields empty skin (PHP uses packet->skin="")
      }
      std::vector<std::string> got_hex;
      for (auto& g : got) got_hex.push_back(toHex(g));
      if (got_hex != expected) {
        ++fail;
        std::cout << "FAIL " << name << " fam " << fam << "\n  exp:";
        for (auto& e : expected) std::cout << " " << e;
        std::cout << "\n  got:";
        for (auto& g : got_hex) std::cout << " " << g;
        std::cout << "\n";
      } else {
        ++pass;
      }
    }
  }
  std::cout << (fail == 0 ? "OK" : "MISMATCH") << " pass=" << pass << " fail=" << fail << "\n";
  return fail == 0 ? 0 : 1;
}
