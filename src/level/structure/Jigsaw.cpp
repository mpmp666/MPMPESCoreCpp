#include "mpmpes/level/structure/Jigsaw.hpp"

#include <algorithm>
#include <array>
#include <cmath>

namespace mpmpes::level::structure {

namespace {
constexpr int MAX_PIECES = 80, MAX_SPAN = 112, MAX_HEIGHT = 110;
std::string strip(const std::string& k) {
  return k.rfind("minecraft:", 0) == 0 ? k.substr(10) : k;
}
} // namespace

JigsawLayout::JigsawLayout(std::int64_t seed) : initial_seed_(seed) {
  random_state_ = seed % 2147483646;
  if (random_state_ < 0) random_state_ += 2147483646;
  ++random_state_;
}

int JigsawLayout::random(int bound) {
  random_state_ = (random_state_ * 48271) % 2147483647;
  return bound > 1 ? static_cast<int>(random_state_ % bound) : 0;
}

Piece JigsawLayout::piece(const std::string& key, int x, int y, int z, int rotation) {
  Piece p;
  p.templ = key;
  p.x = x;
  p.y = y;
  p.z = z;
  p.rotation = rotation & 3;
  p.ignore_air = false;
  p.generation_depth = 0;
  return p;
}

std::array<int, 6> JigsawLayout::bounds(const Piece& p) {
  int size[3] = {0, 0, 0};
  StructureRepository::sizeOf(p.templ, size);
  auto [x1, z1] = StructureRepository::rotate(0, 0, p.rotation);
  auto [x2, z2] = StructureRepository::rotate(size[0] - 1, size[2] - 1, p.rotation);
  return {p.x + std::min(x1, x2), p.y, p.z + std::min(z1, z2),
          p.x + std::max(x1, x2), p.y + size[1] - 1, p.z + std::max(z1, z2)};
}

bool JigsawLayout::fits(const std::vector<Piece>& candidate, const std::vector<Piece>& existing) {
  if (static_cast<int>(candidate.size() + existing.size()) > MAX_PIECES) return false;
  bool first = true;
  std::array<int, 6> b{};
  auto merge = [&](const Piece& p) {
    auto c = bounds(p);
    if (first) {
      b = c;
      first = false;
    } else {
      for (int i = 0; i < 3; ++i) b[i] = std::min(b[i], c[i]);
      for (int i = 3; i < 6; ++i) b[i] = std::max(b[i], c[i]);
    }
  };
  for (const auto& p : existing) merge(p);
  for (const auto& p : candidate) merge(p);
  return first || (b[3] - b[0] + 1 <= MAX_SPAN && b[5] - b[2] + 1 <= MAX_SPAN &&
                   b[4] - b[1] + 1 <= MAX_HEIGHT);
}

Piece JigsawLayout::relative(const Piece& parent, const std::string& key, int x, int y, int z,
                             int rotation) {
  auto [rx, rz] = StructureRepository::rotate(x, z, parent.rotation);
  return piece(key, parent.x + rx, parent.y + y, parent.z + rz, rotation);
}

std::array<int, 3> JigsawLayout::direction(const std::string& d) {
  if (d == "north") return {0, 0, -1};
  if (d == "south") return {0, 0, 1};
  if (d == "west") return {-1, 0, 0};
  if (d == "east") return {1, 0, 0};
  if (d == "up") return {0, 1, 0};
  if (d == "down") return {0, -1, 0};
  return {0, 0, 0};
}

std::string JigsawLayout::rotatedDirection(const std::string& d, int rotation) {
  static const char* dirs[] = {"north", "east", "south", "west"};
  for (int i = 0; i < 4; ++i)
    if (d == dirs[i]) return dirs[(i + rotation) & 3];
  return d;
}

bool JigsawLayout::containsPoint(const int b[6], const int p[3]) {
  return p[0] >= b[0] && p[0] <= b[3] && p[1] >= b[1] && p[1] <= b[4] && p[2] >= b[2] && p[2] <= b[5];
}
bool JigsawLayout::containsBox(const int a[6], const int b[6]) {
  return b[0] >= a[0] && b[1] >= a[1] && b[2] >= a[2] && b[3] <= a[3] && b[4] <= a[4] && b[5] <= a[5];
}

JigsawLayout::Conn JigsawLayout::connectorWorld(const Conn& c, const Piece& p) {
  Conn out = c;
  auto [rx, rz] = StructureRepository::rotate(c.pos[0], c.pos[2], p.rotation);
  out.pos[0] = p.x + rx;
  out.pos[1] = p.y + c.pos[1];
  out.pos[2] = p.z + rz;
  out.front = rotatedDirection(c.front, p.rotation);
  out.top = rotatedDirection(c.top, p.rotation);
  return out;
}

bool JigsawLayout::matches(const Conn& source, const Conn& target) {
  auto a = direction(source.front);
  auto b = direction(target.front);
  std::string joint = source.joint.empty()
                          ? ((source.front == "up" || source.front == "down") ? "rollable" : "aligned")
                          : source.joint;
  return a[0] == -b[0] && a[1] == -b[1] && a[2] == -b[2] &&
         (joint == "rollable" || source.top == target.top) &&
         strip(source.target.empty() ? "empty" : source.target) ==
             strip(target.name.empty() ? "empty" : target.name);
}

std::string JigsawLayout::connectorId(const Conn& c) {
  return std::to_string(c.pos[0]) + ":" + std::to_string(c.pos[1]) + ":" + std::to_string(c.pos[2]) +
         ":" + c.front + ":" + c.name + ":" + c.pool;
}

std::vector<JigsawLayout::Conn> JigsawLayout::templateConnectors(const std::string& key) {
  const Template* t = StructureRepository::get(key);
  std::vector<Conn> out;
  if (!t) return out;
  for (const auto& tc : t->connectors) {
    Conn c;
    c.pos[0] = tc.pos[0];
    c.pos[1] = tc.pos[1];
    c.pos[2] = tc.pos[2];
    c.front = tc.front;
    c.top = tc.top;
    c.name = tc.name;
    c.target = tc.target;
    c.joint = tc.joint;
    c.pool = tc.pool;
    c.selection_priority = tc.selection_priority;
    c.placement_priority = tc.placement_priority;
    out.push_back(c);
  }
  // orderedConnectors: shuffle then stable-sort by selection_priority desc
  out = shuffle(out);
  std::stable_sort(out.begin(), out.end(), [](const Conn& a, const Conn& b) {
    return a.selection_priority > b.selection_priority;
  });
  return out;
}

bool JigsawLayout::collisionFree(const std::vector<Piece>& group, int context,
                                 const std::vector<int>& ignore) {
  for (const auto& piece : group) {
    auto b = bounds(piece);
    int bb[6];
    for (int i = 0; i < 6; ++i) bb[i] = b[i];
    if (context >= 0) {
      int region[6];
      for (int i = 0; i < 6; ++i) region[i] = nodes_[context].box[i];
      if (family_ == "village" && region[4] - region[1] <= 2) {
        region[1] = -MAX_HEIGHT;
        region[4] = MAX_HEIGHT;
      }
      if (!containsBox(region, bb)) return false;
    }
    for (std::size_t index = 0; index < nodes_.size(); ++index) {
      if (std::find(ignore.begin(), ignore.end(), static_cast<int>(index)) != ignore.end())
        continue;
      const auto& a = nodes_[index].box;
      int dx = std::min(a[3], bb[3]) - std::max(a[0], bb[0]) + 1;
      int dy = std::min(a[4], bb[4]) - std::max(a[1], bb[1]) + 1;
      int dz = std::min(a[5], bb[5]) - std::max(a[2], bb[2]) + 1;
      if (dx > 1 && dy > 1 && dz > 1) return false;
    }
  }
  return true;
}

int JigsawLayout::accept(const std::vector<Piece>& group, int depth, std::vector<int> ancestors,
                         int context) {
  const auto& piece = group[0];
  pieces_.push_back(piece);
  int index = static_cast<int>(nodes_.size());
  auto box = bounds(piece);
  int bb[6];
  for (int i = 0; i < 6; ++i) bb[i] = box[i];
  for (std::size_t gi = 1; gi < group.size(); ++gi) {
    auto b = bounds(group[gi]);
    for (int i = 0; i < 3; ++i) bb[i] = std::min(bb[i], b[i]);
    for (int i = 3; i < 6; ++i) bb[i] = std::max(bb[i], b[i]);
  }
  Node node;
  node.piece = piece;
  for (int i = 0; i < 6; ++i) node.box[i] = bb[i];
  node.context = context;
  node.ancestors = ancestors;
  nodes_.push_back(node);
  for (const auto& c : templateConnectors(piece.templ)) {
    Task task;
    task.node = index;
    task.connector = c;
    task.depth = depth;
    task.priority = c.placement_priority;
    task.serial = serial_++;
    task.id = std::to_string(index) + ":" + connectorId(c);
    queue_.push_back(task);
  }
  return index;
}

std::string JigsawLayout::poolName(const std::string& name) {
  std::string n = strip(name);
  if (aliases_.count(n)) n = aliases_[n];
  if (family_ == "village" && variant_.find("zombie") != std::string::npos &&
      n.find("/zombie/") == std::string::npos) {
    // replace village/<biome>/<x> with village/<biome>/zombie/<x>
    std::vector<std::string> parts;
    std::string cur;
    for (char c : n) {
      if (c == '/') { parts.push_back(cur); cur.clear(); }
      else cur.push_back(c);
    }
    parts.push_back(cur);
    if (parts.size() >= 3 && parts[0] == "village" && parts[1] != "common") {
      std::string z = parts[0] + "/" + parts[1] + "/zombie/";
      for (std::size_t i = 2; i < parts.size(); ++i) z += (i > 2 ? "/" : "") + parts[i];
      const auto* pools = StructureRepository::pools();
      if (pools && (pools->get(z) || pools->get("minecraft:" + z))) n = z;
    }
  }
  return n;
}

bool JigsawLayout::entityPool(const std::string& pool) {
  return pool.find("/villagers") != std::string::npos ||
         pool.find("/zombie/villagers") != std::string::npos ||
         pool.find("/common/animals") != std::string::npos ||
         pool == "village/common/cats" || pool == "village/common/iron_golem";
}

std::vector<const util::JsonValue*> JigsawLayout::poolElements(const std::string& name, bool fallbackOnly,
                                                      std::unordered_map<std::string, bool> visited) {
  std::string n = poolName(name);
  if (visited.count(n) || n.empty() || n == "empty") return {};
  visited[n] = true;
  const auto* pools = StructureRepository::pools();
  if (!pools) return {};
  const util::JsonValue* pool = pools->get(n);
  if (!pool) pool = pools->get("minecraft:" + n);
  if (!pool || !pool->isObj()) return {};
  std::vector<const util::JsonValue*> result;
  if (!fallbackOnly) {
    std::vector<std::pair<const util::JsonValue*, int>> remaining;
    if (auto* els = pool->get("elements"))
      for (const auto& entry : els->arr()) {
        int weight = std::max(0, std::min(150, entry->get("weight") ? entry->get("weight")->asInt(1) : 1));
        if (weight <= 0) continue;
        const util::JsonValue* el = entry->get("element");
        remaining.push_back({el ? el : entry.get(), weight});
      }
    while (!remaining.empty()) {
      int total = 0;
      for (auto& [_, w] : remaining) total += w;
      int choice = random(total);
      for (std::size_t i = 0; i < remaining.size(); ++i) {
        choice -= remaining[i].second;
        if (choice < 0) {
          result.push_back(remaining[i].first);
          remaining.erase(remaining.begin() + i);
          break;
        }
      }
    }
  }
  std::string fallback = strip(pool->get("fallback") ? pool->get("fallback")->asStr() : "empty");
  auto more = poolElements(fallback, false, visited);
  result.insert(result.end(), more.begin(), more.end());
  return result;
}

std::vector<std::string> JigsawLayout::elementKeys(const util::JsonValue* element) {
  if (!element) return {};
  std::string type = strip(element->get("element_type") ? element->get("element_type")->asStr() : "single_pool_element");
  if (type == "empty_pool_element" || type == "feature_pool_element") return {};
  if (type == "list_pool_element") {
    std::vector<std::string> out;
    if (auto* els = element->get("elements"))
      for (const auto& child : els->arr()) {
        auto sub = elementKeys(child.get());
        out.insert(out.end(), sub.begin(), sub.end());
      }
    return out;
  }
  std::string key = strip(element->get("location") ? element->get("location")->asStr() : "");
  return StructureRepository::has(key) ? std::vector<std::string>{key} : std::vector<std::string>{};
}

bool JigsawLayout::startMatches(const std::string& key) {
  if (family_ == "village")
    return (key.find("/zombie/") != std::string::npos) == (variant_.find("zombie") != std::string::npos);
  if (family_ == "bastion" && !variant_.empty()) {
    std::string v = variant_ == "housing" ? "units" : variant_ == "stable" ? "hoglin_stable"
                    : variant_ == "treasure_room" ? "treasure" : variant_;
    return key.find("bastion/" + v + "/") != std::string::npos;
  }
  return true;
}

std::vector<Piece> JigsawLayout::igloo(const std::string& variant) {
  if (!StructureRepository::has("igloo/top")) return standalone("igloo", variant);
  int r = random(4);
  auto top = piece("igloo/top", 0, 0, 0, r);
  std::vector<Piece> result = {top};
  if (variant == "top" || (variant != "basement" && random(2) == 0)) return result;
  int depth = 4 + random(7);
  for (int i = 0; i < depth; ++i) result.push_back(relative(top, "igloo/middle", 2, -3 * (i + 1), 4, r));
  result.push_back(relative(top, "igloo/bottom", 0, -3 * depth - 6, -2, r));
  return result;
}

std::vector<Piece> JigsawLayout::standalone(const std::string& family, const std::string& variant) {
  if (StructureRepository::has(family))
    return {piece(family, 0, 0, 0, random(4))};
  std::vector<std::string> keys;
  std::string var = strip(variant);
  std::string selVar = (var == "single" || var == "group") ? "" : var;
  for (const auto& key : StructureRepository::templateKeys()) {
    std::string fam = key.substr(0, key.find('/'));
    if (fam != family && key.rfind(family + "/", 0) != 0) continue;
    if (!selVar.empty() && key.find(selVar) == std::string::npos) continue;
    keys.push_back(key);
  }
  std::sort(keys.begin(), keys.end());
  if (keys.empty()) return {};
  std::string key = keys[random(static_cast<int>(keys.size()))];
  std::vector<Piece> result = {piece(key, 0, 0, 0, random(4))};
  return result;
}

std::vector<Piece> JigsawLayout::assemble(const std::string& family, const std::string& pool,
                                          const std::string& variant) {
  family_ = family;
  variant_ = variant;
  // trial_chambers aliases omitted (not a 0.14-relevant family for now)
  auto starts = poolElements(pool, false, {});
  for (const auto& element : starts) {
    auto keys = elementKeys(element);
    if (keys.empty() || !startMatches(keys[0])) continue;
    int r = random(4);
    std::vector<Piece> group;
    for (const auto& key : keys) group.push_back(piece(key, 0, 0, 0, r));
    if (!fits(group, {})) continue;
    accept(group, 0, {}, -1);
    break;
  }
  if (pieces_.empty()) return standalone(family, variant);
  int maxDepth = family == "village" ? 7 : family == "bastion" ? 7 : family == "ancient_city" ? 8
                 : family == "trial_chambers" ? 16 : 8;
  while (!queue_.empty() && static_cast<int>(pieces_.size()) < MAX_PIECES) {
    std::sort(queue_.begin(), queue_.end(), [](const Task& a, const Task& b) {
      if (a.priority != b.priority) return a.priority > b.priority;
      if (a.depth != b.depth) return a.depth < b.depth;
      return a.serial < b.serial;
    });
    Task task = queue_.front();
    queue_.erase(queue_.begin());
    if (used_.count(task.id)) continue;
    used_[task.id] = true;
    const auto& node = nodes_[task.node];
    Conn source = task.connector;
    std::string pool2 = poolName(source.pool.empty() ? "empty" : source.pool);
    if (pool2.empty() || pool2 == "empty" || entityPool(pool2)) continue;
    Conn sourceWorld = connectorWorld(source, node.piece);
    auto dir = direction(sourceWorld.front);
    int destination[3] = {sourceWorld.pos[0] + dir[0], sourceWorld.pos[1] + dir[1],
                          sourceWorld.pos[2] + dir[2]};
    bool inside = containsPoint(node.box, destination);
    int context = inside ? task.node : node.context;
    std::vector<int> ignore;
    if (inside) {
      ignore = node.ancestors;
      ignore.push_back(task.node);
    } else {
      ignore = node.ancestors;
    }
    auto candidates = poolElements(pool2, task.depth >= maxDepth, {});
    bool placed = false;
    for (const auto& element : candidates) {
      std::string type = strip(element->get("element_type") ? element->get("element_type")->asStr() : "");
      if (type == "empty_pool_element") break;
      auto keys = elementKeys(element);
      if (keys.empty()) continue;
      for (int rotation : shuffle(std::vector<int>{0, 1, 2, 3})) {
        auto connectors = templateConnectors(keys[0]);
        for (const auto& target : connectors) {
          Conn targetWorld = connectorWorld(target, piece(keys[0], 0, 0, 0, rotation));
          if (!matches(sourceWorld, targetWorld)) continue;
          int pos[3] = {destination[0] - targetWorld.pos[0], destination[1] - targetWorld.pos[1],
                        destination[2] - targetWorld.pos[2]};
          std::vector<Piece> group;
          for (const auto& key : keys) group.push_back(piece(key, pos[0], pos[1], pos[2], rotation));
          if (!fits(group, pieces_) || !collisionFree(group, context, ignore)) continue;
          int newIdx = accept(group, task.depth + 1, ignore, context);
          used_[std::to_string(newIdx) + ":" + connectorId(target)] = true;
          placed = true;
          goto next_element;
        }
      }
    }
  next_element:;
  }
  return pieces_;
}

} // namespace mpmpes::level::structure
