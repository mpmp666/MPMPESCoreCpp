#pragma once
// Entity <-> PocketMine chunk "Entities" NBT (Entity::saveNBT layout).

#include "mpmpes/entity/Entity.hpp"
#include "mpmpes/nbt/Tag.hpp"

namespace mpmpes::entity {

nbt::TagPtr entityToNbt(const Entity& e);
// Returns false when the tag is not a known entity (caller keeps it as raw NBT).
bool entityFromNbt(const nbt::Tag& t, Entity& out);

} // namespace mpmpes::entity
