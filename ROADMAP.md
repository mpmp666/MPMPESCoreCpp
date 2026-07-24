# MPMPESCoreCpp Roadmap

Port of `~/MPMPESCore` (Genisys / PocketMine, MCPE **0.14.x**, protocol **70**) to C++20.

## Phase 0 — Scaffold + offline RakLib ✅

- [x] CMake project, C++20, s390x build
- [x] Binary / BinaryStream
- [x] UDP non-blocking socket
- [x] UNCONNECTED_PING / PONG (server list)
- [x] OPEN_CONNECTION_REQUEST/REPLY 1 & 2 (offline handshake)

## Phase 1 — Reliable RakLib session ✅

- [x] EncapsulatedPacket encode/decode
- [x] Datagram DATA_PACKET, ACK, NACK
- [x] Session state machine
- [x] CLIENT_CONNECT / SERVER_HANDSHAKE / CLIENT_HANDSHAKE
- [x] Split / reassembly, reliability + seq windows
- [ ] Stress-tested retransmit / reorder edge cases

## Phase 2 — MCPE protocol 70 + join path ✅ (v0.3 stub)

- [x] LoginPacket decode
- [x] PlayStatus (LOGIN_SUCCESS / PLAYER_SPAWN)
- [x] StartGame / SetTime / SetSpawn / SetHealth / SetDifficulty
- [x] FullChunkData (flat/void/normal-stub columns)
- [x] ChunkRadius request/update
- [x] Text (chat/system) + MovePlayer decode
- [x] PlayerAction → block events
- [ ] BatchPacket zlib inflate (clientbound batch optional later)

## Phase 2.7 — Dig/place + creative + craft + mobs ✅ (v0.4)

- [x] UpdateBlock broadcast + LevelEvent destroy particles
- [x] RemoveBlock / UseItem / PlayerAction dig-place world mutation
- [x] Creative inventory (`ContainerSetContent` SPECIAL_CREATIVE)
- [x] Player inventory + MobEquipment hotbar
- [x] CraftingData basic recipes + CraftingEvent accept stub
- [x] AddEntity / MoveEntity / RemoveEntity
- [x] Simple mob AI (wander + gravity): pig/chicken/cow/sheep/zombie
- [x] Commands: `/gm` `/give` `/spawnmob` `/clear`
- [ ] Full recipe table / furnace tick UI
- [ ] Pathfinding / combat AI / drops on death

## Phase 2.5 — Config + multi-lang plugins ✅ (v0.2→0.3)

- [x] `server.properties` + CLI
- [x] PluginManager: native `.so` + process JSON-line
- [x] Events: server_start, session_*, player_login/join/quit, chat, command, move, block, world_load
- [x] Examples: HelloC/Cpp/Python/Node/Go/PHP/Rust scaffolds

## Phase 2.6 — Map gen + multi-world ✅ (v0.3)

- [x] Chunk column generator (flat / void / normal-stub)
- [x] Level + LevelManager
- [x] `worlds.txt` multi-world load
- [x] `/worlds` `/goto <world>` `/spawn` commands
- [ ] Persist chunks to disk (Anvil/McRegion)
- [ ] Real noise terrain / biomes / population

## Phase 3 — Game core

- [x] Player inventory + creative content (v0.4)
- [x] Block place/break + UpdateBlock + particles (v0.4)
- [x] Item stack encode + basic registries (v0.4)
- [x] Entity spawn + simple wander AI (v0.4)
- [ ] Entity metadata full / item frame etc.
- [ ] TPS metrics / save loop
- [ ] Chunk disk persist

## Phase 4 — Parity

- [ ] Permissions
- [ ] pocketmine.yml subset
- [ ] More plugin events (entity, damage)
- [ ] Load PHP-generated worlds if feasible

## Non-goals (for now)

- Full PHP plugin **binary** compatibility
- Synapse proxy
- Modern Bedrock protocol (>0.14)
