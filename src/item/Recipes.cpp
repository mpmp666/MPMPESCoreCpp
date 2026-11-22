#include "mpmpes/item/Recipes.hpp"

#include "mpmpes/binary/BinaryStream.hpp"
#include "mpmpes/protocol/Info.hpp"
#include "mpmpes/protocol/MultiProtocol.hpp"

#include <algorithm>
#include <cstring>

namespace mpmpes::item {

namespace {
// PocketMine Slot on the 0.14 wire: [short id][count][short damage][LShort nbt]
void putRecipeSlot(binary::BinaryStream& o, std::int16_t id, std::uint8_t count,
                   std::int16_t damage) {
  if (id <= 0) {
    o.putShort(0);
    return;
  }
  o.putShort(static_cast<std::uint16_t>(id));
  o.putByte(count);
  o.putShort(static_cast<std::uint16_t>(damage));
  o.putLShort(0);
}
ItemStack toStack(const recipes::RItem& r) {
  ItemStack s;
  s.id = r.id;
  s.count = r.count;
  s.damage = r.damage;
  return s;
}
bool oldOk(const RecipeEntry& e) {
  if (protocol::mp::isUnknownToOldProtocol(e.result.id)) return false;
  if (e.type >= 2)
    return !protocol::mp::isUnknownToOldProtocol(
        e.type == 3 ? (e.furnace_input >> 16) : e.furnace_input);
  if (e.type == 1) {
    for (auto& g : e.grid)
      if (protocol::mp::isUnknownToOldProtocol(g.id)) return false;
    return true;
  }
  for (auto& s : e.ings)
    if (protocol::mp::isUnknownToOldProtocol(s.id)) return false;
  return true;
}
bool p12Ok(const RecipeEntry& e) {
  if (!protocol::mp::is012ProtocolCreativeItem(e.result.id)) return false;
  if (e.type >= 2)
    return protocol::mp::is012ProtocolCreativeItem(
        e.type == 3 ? (e.furnace_input >> 16) : e.furnace_input);
  if (e.type == 1) {
    for (auto& g : e.grid)
      if (!protocol::mp::is012ProtocolCreativeItem(g.id)) return false;
    return true;
  }
  for (auto& s : e.ings)
    if (!protocol::mp::is012ProtocolCreativeItem(s.id)) return false;
  return true;
}
} // namespace

RecipeRegistry::RecipeRegistry() {
  entries_.reserve(recipes::count);
  for (int i = 0; i < recipes::count; ++i) {
    const auto& r = *recipes::all[i];
    RecipeEntry e;
    e.type = r.type;
    e.w = r.w;
    e.h = r.h;
    e.result = toStack(r.result);
    e.furnace_input = r.furnace_input;
    std::memcpy(e.uuid.data(), r.uuid, 16);
    for (int k = 0; k < r.ing_count; ++k) e.ings.push_back(toStack(r.ings[k]));
    if (r.grid) {
      e.grid.reserve(r.w * r.h);
      for (int k = 0; k < r.w * r.h; ++k) e.grid.push_back(toStack(r.grid[k]));
    }
    // Result damage in PHP recipes for wildcard ingredients stays as-is.
    entries_.push_back(std::move(e));
  }
  for (auto& e : entries_) {
    by_uuid_[std::string(reinterpret_cast<const char*>(e.uuid.data()), 16)] = &e;
    if (e.type == 2) furnace_[e.furnace_input] = &e;                    // id only
    if (e.type == 3) furnace_[(static_cast<std::int64_t>(e.furnace_input >> 16) << 32) |
                              (e.furnace_input & 0xffff)] = &e;          // id:damage
  }
  buildNative();
}

void RecipeRegistry::buildNative() {
  auto build = [](const std::vector<const RecipeEntry*>& list, std::string& out) {
    binary::BinaryStream o;
    o.putByte(protocol::CRAFTING_DATA_PACKET);
    o.putInt(static_cast<std::int32_t>(list.size()));
    for (auto* e : list) {
      binary::BinaryStream body;
      std::int32_t type = e->type;
      if (e->type == 0) {
        body.putInt(static_cast<std::int32_t>(e->ings.size()));
        for (auto& s : e->ings) putRecipeSlot(body, s.id, s.count, s.damage);
        body.putInt(1);
        putRecipeSlot(body, e->result.id, e->result.count, e->result.damage);
        body.put(std::string_view(reinterpret_cast<const char*>(e->uuid.data()), 16));
      } else if (e->type == 1) {
        body.putInt(e->w);
        body.putInt(e->h);
        for (auto& s : e->ings) putRecipeSlot(body, s.id, s.count, s.damage);
        body.putInt(1);
        putRecipeSlot(body, e->result.id, e->result.count, e->result.damage);
        body.put(std::string_view(reinterpret_cast<const char*>(e->uuid.data()), 16));
      } else if (e->type == 2) {
        body.putInt(e->furnace_input);
        putRecipeSlot(body, e->result.id, e->result.count, e->result.damage);
      } else {
        body.putInt(e->furnace_input);
        putRecipeSlot(body, e->result.id, e->result.count, e->result.damage);
      }
      o.putInt(type);
      o.putInt(static_cast<std::int32_t>(body.buffer().size()));
      o.put(body.buffer());
    }
    o.putByte(1); // cleanRecipes
    out = o.buffer();
  };
  std::vector<const RecipeEntry*> full, old, p12;
  // PHP: shaped+shapeless first (json order), furnace after
  for (auto& e : entries_) {
    full.push_back(&e);
    if (oldOk(e)) old.push_back(&e);
    if (oldOk(e) && p12Ok(e)) p12.push_back(&e);
  }
  build(full, native_full_);
  build(old, native_old_);
  build(p12, native_012_);
}

const RecipeEntry* RecipeRegistry::findByUuid(const std::array<std::uint8_t, 16>& uuid) const {
  auto it = by_uuid_.find(std::string(reinterpret_cast<const char*>(uuid.data()), 16));
  return it == by_uuid_.end() ? nullptr : it->second;
}

const RecipeEntry* RecipeRegistry::findFurnace(std::int16_t id, std::int16_t damage) const {
  if (auto it = furnace_.find((static_cast<std::int64_t>(id) << 32) | (damage & 0xffff));
      it != furnace_.end())
    return it->second;
  auto it = furnace_.find(id);
  return it == furnace_.end() ? nullptr : it->second;
}

std::optional<ItemStack> RecipeRegistry::tryCraft(const RecipeEntry& recipe,
                                                  const std::vector<ItemStack>& input,
                                                  std::vector<ItemStack>& inventory) {
  // input: 9 crafting grid slots (client CraftingEvent input); counts clamped to 1 (PHP)
  std::vector<ItemStack> grid(9);
  for (std::size_t i = 0; i < 9 && i < input.size(); ++i) {
    grid[i] = input[i];
    if (grid[i].damage < 0) grid[i].damage = 0xffff; // client wildcard marker
    if (grid[i].id > 0 && grid[i].count > 1) grid[i].count = 1;
  }
  if (recipe.type == 1) {
    for (int x = 0; x < 3; ++x)
      for (int y = 0; y < 3; ++y) {
        const auto& it = grid[static_cast<std::size_t>(y * 3 + x)];
        static const ItemStack kAir;
        const ItemStack* ing = &kAir;
        if (x < recipe.w && y < recipe.h)
          ing = &recipe.grid[static_cast<std::size_t>(y * recipe.w + x)];
        if (it.id > 0) {
          if (ing->id == 0) return std::nullopt;
          if (ing->id != it.id || (ing->damage != 0xffff && ing->damage != it.damage))
            return std::nullopt;
        } else if (ing->id != 0) {
          return std::nullopt;
        }
      }
  } else if (recipe.type == 0) {
    std::vector<ItemStack> needed;
    for (auto& s : recipe.ings)
      if (s.id > 0) needed.push_back(s);
    for (auto& it : grid) {
      if (it.id <= 0 || it.count <= 0) continue;
      int left = it.count;
      for (auto n = needed.begin(); n != needed.end() && left > 0;) {
        if (n->id == it.id && (n->damage == 0xffff || n->damage == -1 || n->damage == it.damage)) {
          int take = std::min<int>(n->count, left);
          n->count -= static_cast<std::uint8_t>(take);
          left -= take;
          if (n->count == 0) n = needed.erase(n);
          else ++n;
        } else ++n;
      }
      if (left > 0) return std::nullopt;
    }
    if (!needed.empty()) return std::nullopt;
  } else {
    return std::nullopt;
  }
  // verify inventory has the ingredients and consume (PHP second pass)
  std::vector<ItemStack> ins;
  for (auto& it : grid)
    if (it.id > 0) ins.push_back(it);
  std::vector<int> used(inventory.size(), 0);
  for (auto& ing : ins) {
    bool found = false;
    for (std::size_t i = 0; i < inventory.size(); ++i) {
      auto& slot = inventory[i];
      if (slot.id == ing.id && (ing.damage == 0xffff || ing.damage == slot.damage) &&
          slot.count - used[i] >= 1) {
        used[i]++;
        break;
      }
    }
    if (!found) return std::nullopt;
  }
  for (std::size_t i = 0; i < inventory.size(); ++i) {
    if (used[i] <= 0) continue;
    auto& slot = inventory[i];
    slot.count = static_cast<std::uint8_t>(slot.count - used[i]);
    if (slot.count == 0) slot = ItemStack::air();
  }
  return recipe.result;
}

} // namespace mpmpes::item
