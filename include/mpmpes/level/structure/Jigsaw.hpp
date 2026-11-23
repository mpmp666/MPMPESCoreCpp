#pragma once
// Generic jigsaw assembly — port of structure/StructureAssembler.php JigsawLayout.

#include "mpmpes/level/structure/Repository.hpp"

#include <string>
#include <unordered_map>
#include <vector>

namespace mpmpes::level::structure {

class JigsawLayout {
public:
  explicit JigsawLayout(std::int64_t seed);
  std::vector<Piece> assemble(const std::string& family, const std::string& pool, const std::string& variant);
  std::vector<Piece> standalone(const std::string& family, const std::string& variant = "");
  std::vector<Piece> igloo(const std::string& variant = "");

  // Park-Miller LCG (PHP $randomState = ($state * 48271) % 2147483647)
  int random(int bound);
  template <typename T> std::vector<T> shuffle(std::vector<T> values) {
    for (int i = static_cast<int>(values.size()) - 1; i > 0; --i) {
      int j = random(i + 1);
      std::swap(values[i], values[j]);
    }
    return values;
  }

  int randomState() const { return random_state_; }
  std::int64_t seed() const { return initial_seed_; }

private:
  struct Conn {
    int pos[3] = {0, 0, 0};
    std::string front = "north", top = "up", name, target, joint, pool;
    int selection_priority = 0, placement_priority = 0;
  };
  struct Node {
    Piece piece;
    int box[6];
    int context;
    std::vector<int> ancestors;
  };
  struct Task {
    int node, depth, priority, serial;
    std::string id;
    Conn connector;
  };

  std::int64_t initial_seed_;
  std::int64_t random_state_;
  std::string family_, variant_;
  std::unordered_map<std::string, std::string> aliases_;
  std::vector<Piece> pieces_;
  std::vector<Node> nodes_;
  std::vector<Task> queue_;
  std::unordered_map<std::string, bool> used_;
  int serial_ = 0;

  bool fits(const std::vector<Piece>& candidate, const std::vector<Piece>& existing);
  std::array<int, 6> bounds(const Piece& piece);
  Piece piece(const std::string& key, int x, int y, int z, int rotation);
  Piece relative(const Piece& parent, const std::string& key, int x, int y, int z, int rotation);
  int accept(const std::vector<Piece>& group, int depth, std::vector<int> ancestors, int context);
  bool collisionFree(const std::vector<Piece>& group, int context, const std::vector<int>& ignore);
  std::vector<const util::JsonValue*> poolElements(const std::string& name, bool fallbackOnly, std::unordered_map<std::string, bool> visited);
  std::string poolName(const std::string& name);
  bool entityPool(const std::string& pool);
  bool startMatches(const std::string& key);
  std::vector<std::string> elementKeys(const util::JsonValue* element);
  std::vector<Conn> templateConnectors(const std::string& key);
  Conn connectorWorld(const Conn& c, const Piece& piece);
  bool matches(const Conn& source, const Conn& target);
  static std::array<int, 3> direction(const std::string& d);
  static std::string rotatedDirection(const std::string& d, int rotation);
  static bool containsPoint(const int b[6], const int p[3]);
  static bool containsBox(const int a[6], const int b[6]);
  std::string connectorId(const Conn& c);
};

} // namespace mpmpes::level::structure
