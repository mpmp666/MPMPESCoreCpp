#include "mpmpes/entity/EntityNbt.hpp"

#include "mpmpes/level/Level.hpp" // itemToNbt/itemFromNbt

namespace mpmpes::entity {

namespace {
const char* saveId(EntityKind k) {
  switch (k) {
    case EntityKind::Pig: return "Pig";
    case EntityKind::Chicken: return "Chicken";
    case EntityKind::Cow: return "Cow";
    case EntityKind::Sheep: return "Sheep";
    case EntityKind::Zombie: return "Zombie";
    case EntityKind::ItemDrop: return "Item";
    case EntityKind::Minecart: return "Minecart";
    case EntityKind::MinecartHopper: return "MinecartHopper";
    case EntityKind::MinecartTNT: return "MinecartTNT";
    case EntityKind::MinecartChest: return "MinecartChest";
  }
  return nullptr;
}
std::optional<EntityKind> kindForSaveId(std::string_view id) {
  if (id == "Pig" || id == "12") return EntityKind::Pig;
  if (id == "Chicken" || id == "10") return EntityKind::Chicken;
  if (id == "Cow" || id == "11") return EntityKind::Cow;
  if (id == "Sheep" || id == "13") return EntityKind::Sheep;
  if (id == "Zombie" || id == "32") return EntityKind::Zombie;
  if (id == "Item" || id == "64") return EntityKind::ItemDrop;
  if (id == "Minecart" || id == "84") return EntityKind::Minecart;
  if (id == "MinecartHopper" || id == "96") return EntityKind::MinecartHopper;
  if (id == "MinecartTNT" || id == "97") return EntityKind::MinecartTNT;
  if (id == "MinecartChest" || id == "98") return EntityKind::MinecartChest;
  return std::nullopt;
}

nbt::TagPtr doubleList(std::string_view name, float a, float b, float c) {
  auto l = nbt::Tag::List(nbt::TAG_Double);
  l->list.push_back(nbt::Tag::Double(a));
  l->list.push_back(nbt::Tag::Double(b));
  l->list.push_back(nbt::Tag::Double(c));
  return l;
}
nbt::TagPtr floatList(std::string_view name, float a, float b) {
  auto l = nbt::Tag::List(nbt::TAG_Float);
  l->list.push_back(nbt::Tag::Float(a));
  l->list.push_back(nbt::Tag::Float(b));
  return l;
}
} // namespace

nbt::TagPtr entityToNbt(const Entity& e) {
  using nbt::Tag;
  auto t = Tag::Compound();
  const char* id = saveId(e.kind);
  if (!id) return nullptr;
  t->set("id", Tag::String(id));
  t->set("Pos", doubleList("Pos", e.x, e.y, e.z));
  t->set("Motion", doubleList("Motion", e.motion_x, e.motion_y, e.motion_z));
  t->set("Rotation", floatList("Rotation", e.yaw, e.pitch));
  t->set("FallDistance", Tag::Float(0));
  t->set("Fire", Tag::Short(0));
  t->set("Air", Tag::Short(300));
  t->set("OnGround", Tag::Byte(e.on_ground ? 1 : 0));
  t->set("Invulnerable", Tag::Byte(0));
  t->set("Health", Tag::Short(static_cast<std::int16_t>(std::max(0.f, e.health))));
  if (e.kind == EntityKind::ItemDrop) {
    t->set("Item", level::itemToNbt(e.item_stack));
    t->set("Age", Tag::Short(static_cast<std::int16_t>(e.age)));
    t->set("PickupDelay", Tag::Short(static_cast<std::int16_t>(e.pickup_delay)));
  }
  if (e.kind == EntityKind::Sheep) {
    t->set("Color", Tag::Byte(e.sheep_color & 0x0f));
    t->set("Sheared", Tag::Byte(e.sheared ? 1 : 0));
  }
  if ((e.kind == EntityKind::MinecartChest || e.kind == EntityKind::MinecartHopper) &&
      !e.cart_slots.empty()) {
    auto items = nbt::Tag::List(nbt::TAG_Compound);
    for (std::size_t i = 0; i < e.cart_slots.size(); ++i) {
      if (e.cart_slots[i].empty()) continue;
      items->list.push_back(level::itemToNbt(e.cart_slots[i], static_cast<int>(i)));
    }
    t->set("Items", items);
  }
  if (e.kind == EntityKind::MinecartTNT && e.tnt_fuse >= 0) t->set("TNTFuse", Tag::Int(e.tnt_fuse));
  return t;
}

bool entityFromNbt(const nbt::Tag& t, Entity& out) {
  auto id = t.getString("id");
  auto kind = kindForSaveId(id);
  if (!kind) return false;
  Entity e;
  e.kind = *kind;
  if (auto pos = t.get("Pos"); pos && pos->list.size() == 3) {
    e.x = static_cast<float>(pos->list[0]->d);
    e.y = static_cast<float>(pos->list[1]->d);
    e.z = static_cast<float>(pos->list[2]->d);
  }
  if (auto mo = t.get("Motion"); mo && mo->list.size() == 3) {
    e.motion_x = static_cast<float>(mo->list[0]->d);
    e.motion_y = static_cast<float>(mo->list[1]->d);
    e.motion_z = static_cast<float>(mo->list[2]->d);
  }
  if (auto ro = t.get("Rotation"); ro && ro->list.size() == 2) {
    e.yaw = static_cast<float>(ro->list[0]->d);
    e.pitch = static_cast<float>(ro->list[1]->d);
  }
  e.health = static_cast<float>(t.getInt("Health", static_cast<std::int64_t>(e.health)));
  e.on_ground = t.getInt("OnGround", 1) != 0;
  if (e.kind == EntityKind::ItemDrop) {
    if (auto it = t.get("Item"); it && it->type == nbt::TAG_Compound)
      e.item_stack = level::itemFromNbt(*it);
    e.age = static_cast<int>(t.getInt("Age"));
    e.pickup_delay = static_cast<int>(t.getInt("PickupDelay"));
  }
  if (e.kind == EntityKind::Sheep) {
    e.sheep_color = static_cast<std::uint8_t>(t.getInt("Color") & 0x0f);
    e.sheared = t.getInt("Sheared") != 0;
  }
  if (auto items = t.get("Items"); items && items->type == nbt::TAG_List) {
    std::size_t size = e.kind == EntityKind::MinecartHopper ? 5 : 27;
    e.cart_slots.assign(size, item::ItemStack::air());
    for (auto& c : items->list) {
      if (!c || c->type != nbt::TAG_Compound) continue;
      auto slot = c->getInt("Slot", -1);
      if (slot < 0 || slot >= static_cast<std::int64_t>(size)) continue;
      e.cart_slots[static_cast<std::size_t>(slot)] = item::sanitizeSlot(level::itemFromNbt(*c));
    }
  }
  if (e.kind == EntityKind::MinecartTNT) e.tnt_fuse = static_cast<int>(t.getInt("TNTFuse", -1));
  out = std::move(e);
  return true;
}

} // namespace mpmpes::entity
