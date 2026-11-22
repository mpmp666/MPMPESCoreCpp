// Incoming-direction tests: family wire -> native buffers -> native decoders.
#include "mpmpes/nbt/Tag.hpp"
#include "mpmpes/protocol/MultiProtocol.hpp"
#include "mpmpes/protocol/Packets.hpp"

#include <cstring>
#include <iostream>
#include <string>
#include <vector>
#include <zlib.h>

using namespace mpmpes;

static int failures = 0;
#define CHECK(cond) \
  do { \
    if (!(cond)) { \
      std::cout << "FAIL line " << __LINE__ << ": " #cond "\n"; \
      ++failures; \
    } \
  } while (0)

static std::string zlib(std::string_view raw, int level = 7) { return nbt::zlibCompress(raw, level); }

static std::string b64url(std::string_view raw) {
  static const char* d = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";
  std::string out;
  size_t i = 0;
  for (; i + 2 < raw.size(); i += 3) {
    unsigned v = (static_cast<unsigned char>(raw[i]) << 16) | (static_cast<unsigned char>(raw[i + 1]) << 8) |
                 static_cast<unsigned char>(raw[i + 2]);
    out += d[(v >> 18) & 63];
    out += d[(v >> 12) & 63];
    out += d[(v >> 6) & 63];
    out += d[v & 63];
  }
  if (raw.size() - i == 1) {
    unsigned v = static_cast<unsigned char>(raw[i]) << 16;
    out += d[(v >> 18) & 63];
    out += d[(v >> 12) & 63];
  } else if (raw.size() - i == 2) {
    unsigned v = (static_cast<unsigned char>(raw[i]) << 16) | (static_cast<unsigned char>(raw[i + 1]) << 8);
    out += d[(v >> 18) & 63];
    out += d[(v >> 12) & 63];
    out += d[(v >> 6) & 63];
  }
  return out;
}

static std::string jwt(std::string_view payload_json) {
  return b64url("{\"alg\":\"none\"}") + "." + b64url(payload_json) + ".sig";
}

int main() {
  using namespace protocol;

  // ---- 0.11 login: [0x82][str name][int p1][int p2][int cid][byte slim][str skin]
  {
    std::string wire = "\x82";
    wire += protocol::encodeDisconnect(""); // dummy to reuse nothing; build manually below
  }

  auto mkLogin011 = [] {
    std::string w = "\x82";
    binary::BinaryStream o;
    o.putString("steve011");
    o.putInt(23);
    o.putInt(23);
    o.putInt(123456789);
    o.putByte(1);
    o.putString(std::string(64 * 32 * 4, '\x7f'));
    return w + o.buffer();
  };
  {
    auto v = mp::translateIncoming(mp::F011, mkLogin011());
    CHECK(v.size() == 1);
    auto d = v.empty() ? protocol::LoginDecoded{} : protocol::decodeLogin(v[0]);
    CHECK(d.ok && d.username == "steve011" && d.protocol1 == 23 && d.client_id == 123456789);
    CHECK(d.skin_name == "CustomSlim");
  }

  // ---- 0.13 login bare: [0x8f][str name][int 37][int 37]
  {
    binary::BinaryStream o;
    o.putString("steve013");
    o.putInt(37);
    o.putInt(37);
    std::string wire = "\x8f" + o.buffer();
    auto v = mp::translateIncoming(mp::F013, wire);
    CHECK(v.size() == 1);
    auto d = v.empty() ? protocol::LoginDecoded{} : protocol::decodeLogin(v[0]);
    CHECK(d.ok && d.username == "steve013" && d.protocol1 == 37);
  }

  // ---- 0.12 login: name, 34, 34, long cid, uuid16, addr, secret, slim byte, skin
  {
    binary::BinaryStream o;
    o.putString("steve012");
    o.putInt(34);
    o.putInt(34);
    o.putLong(987654321);
    o.put(std::string_view("\x01\x02\x03\x04\x05\x06\x07\x08\x09\x0a\x0b\x0c\x0d\x0e\x0f\x10", 16));
    o.putString("192.168.1.5:19132");
    o.putString("secret");
    o.putByte(1);
    o.putString(std::string(64 * 64 * 4, '\x55'));
    std::string wire = "\x8f" + o.buffer();
    auto v = mp::translateIncoming(mp::F012, wire);
    CHECK(v.size() == 1);
    auto d = v.empty() ? protocol::LoginDecoded{} : protocol::decodeLogin(v[0]);
    CHECK(d.ok && d.username == "steve012" && d.protocol1 == 34 && d.client_id == 987654321);
    CHECK(d.skin_name == "CustomSlim" && d.skin_data.size() == 64 * 64 * 4);
  }
  // ---- 0.15 JWT login: [0x01][int 81][int zlen][zlib [Lint chain][Lint skinjwt]]

  auto jwtLogin = [&](int protocol, bool edition) {
    std::string chain_json = std::string("{\"chain\":[\"") +
        jwt("{\"extraData\":{\"displayName\":\"jwtuser\",\"identity\":\"00112233-4455-6677-8899-aabbccddeeff\"}}") + "\"]}";
    std::string skin_jwt = jwt(
        "{\"ClientRandomId\":777000111,\"ServerAddress\":\"1.2.3.4:19132\","
        "\"SkinData\":\"" +
        b64url(std::string(64 * 64 * 4, '\x33')) + "\",\"SkinId\":\"CustomSlim\"}");
    binary::BinaryStream body;
    body.putLInt(static_cast<std::int32_t>(chain_json.size()));
    body.put(chain_json);
    body.putLInt(static_cast<std::int32_t>(skin_jwt.size()));
    body.put(skin_jwt);
    auto z = zlib(body.buffer());
    binary::BinaryStream o;
    o.putInt(protocol);
    if (edition) {
      o.putByte(0);
      // uvarint length
      std::size_t n = z.size();
      do {
        std::uint8_t c = n & 0x7f;
        n >>= 7;
        if (n) c |= 0x80;
        o.putByte(c);
      } while (n);
    } else {
      o.putInt(static_cast<std::int32_t>(z.size()));
    }
    o.put(z);
    return std::string("\x01") + o.buffer();
  };

  for (int proto : {81, 90}) {
    auto v = mp::translateIncoming(mp::wireFamily(proto), jwtLogin(proto, proto >= 90));
    CHECK(v.size() == 1);
    auto d = v.empty() ? protocol::LoginDecoded{} : protocol::decodeLogin(v[0]);
    CHECK(d.ok && d.username == "jwtuser" && d.protocol1 == proto);
    CHECK(d.client_id == 777000111);
    CHECK(d.skin_name == "CustomSlim" && d.skin_data.size() == 64 * 64 * 4);
    CHECK(d.server_address == "1.2.3.4:19132");
    // uuid from identity
    CHECK(d.uuid[0] == 0x00 && d.uuid[1] == 0x11 && d.uuid[15] == 0xff);
  }

  // ---- 0.15 inbound batch: inner int-len framed, ids remapped
  {
    // text chat 0x07 in 0.15: [07][type][str src][str msg]
    binary::BinaryStream t;
    t.putByte(0x07);
    t.putByte(1);
    t.putString("steve");
    t.putString("hello");
    binary::BinaryStream inner;
    inner.putInt(static_cast<std::int32_t>(t.buffer().size()));
    inner.put(t.buffer());
    auto z = zlib(inner.buffer());
    binary::BinaryStream batch;
    batch.putByte(0x06); // 0.15 batch id
    batch.putInt(static_cast<std::int32_t>(z.size())); // length = compressed size
    batch.put(z);
    auto v = mp::translateIncoming(mp::F015, batch.buffer());
    CHECK(v.size() == 1);
    auto d = v.empty() ? protocol::TextDecoded{} : protocol::decodeText(v[0]);
    CHECK(d.ok && d.type == 1 && d.source == "steve" && d.message == "hello");
  }
  // ---- 0.14 inbound batch with 0x8e inner
  {
    binary::BinaryStream t;
    t.putByte(0x8e);
    t.put(protocol::encodeTextChat("a", "b"));
    binary::BinaryStream inner;
    inner.putInt(static_cast<std::int32_t>(t.buffer().size()));
    inner.put(t.buffer());
    auto z = zlib(inner.buffer());
    binary::BinaryStream batch;
    batch.putByte(protocol::BATCH_PACKET);
    batch.putInt(static_cast<std::int32_t>(z.size()));
    batch.put(z);
    auto v = mp::translateIncoming(mp::F014, batch.buffer());
    CHECK(v.size() == 1);
    auto d = v.empty() ? protocol::TextDecoded{} : protocol::decodeText(v[0]);
    CHECK(d.ok && d.source == "a" && d.message == "b");
  }
  // ---- 0.16 inbound batch: uvarint lens
  {
    // move player 0x14: eid varint, pos 3 LFloat, pitch/yaw/bodyYaw, mode, onGround
    binary::BinaryStream t;
    t.putByte(0x14);
    // varint eid=5 (zigzag 10)
    t.putByte(10);
    for (float f : {1.f, 2.f, 3.f, 45.f, 90.f, 90.f}) t.putLFloat(f);
    t.putByte(0);
    t.putByte(1);
    std::string inner;
    {
      std::size_t n = t.buffer().size();
      do {
        std::uint8_t c = n & 0x7f;
        n >>= 7;
        if (n) c |= 0x80;
        inner.push_back(static_cast<char>(c));
      } while (n);
      inner += t.buffer();
    }
    std::string batch = "\x06";
    {
      auto z = zlib(inner);
      std::size_t n = z.size();
      do {
        std::uint8_t c = n & 0x7f;
        n >>= 7;
        if (n) c |= 0x80;
        batch.push_back(static_cast<char>(c));
      } while (n);
      batch += z;
    }
    auto v = mp::translateIncoming(mp::F016, batch);
    CHECK(v.size() == 1);
    auto d = v.empty() ? protocol::MoveDecoded{} : protocol::decodeMovePlayer(v[0]);
    CHECK(d.ok && d.eid == 5 && d.x == 1.f && d.pitch == 45.f && d.yaw == 90.f && d.on_ground);
  }
  // ---- 0.16 use item round trip
  {
    binary::BinaryStream t;
    t.putByte(0x23);
    // blockcoords: varint x=10, byte y=64, varint z=-2
    t.putByte(20);
    t.putByte(64);
    t.putByte(3);
    t.putByte(2); // face varint 1
    for (float f : {0.5f, 0.5f, 0.5f, 10.5f, 64.5f, -1.5f}) t.putLFloat(f);
    t.putByte(0); // slot varint 0
    // slot16: id=1 -> varint zigzag 2, aux=(0<<8)|1=1 -> 2, nbt len LShort 0
    t.putByte(2);
    t.putByte(2);
    t.putLShort(0);
    auto v = mp::translateIncoming(mp::F016, t.buffer());
    CHECK(v.size() == 1);
    auto d = v.empty() ? protocol::UseItemDecoded{} : protocol::decodeUseItem(v[0]);
    CHECK(d.ok && d.x == 10 && d.y == 64 && d.z == -2 && d.face == 1);
    CHECK(d.item.id == 1 && d.item.count == 1);
  }
  // ---- 0.11 inbound move + container set slot round trip
  {
    // move: [0x8f][eid long][xyz 3f][yaw][bodyYaw][pitch][mode]
    binary::BinaryStream t;
    t.putByte(0x8f);
    t.putLong(7);
    for (float f : {1.f, 2.f, 3.f, 90.f, 90.f, 45.f}) t.putFloat(f);
    t.putByte(0);
    auto v = mp::translateIncoming(mp::F011, t.buffer());
    CHECK(v.size() == 1);
    auto d = v.empty() ? protocol::MoveDecoded{} : protocol::decodeMovePlayer(v[0]);
    CHECK(d.ok && d.eid == 7 && d.y == 2.f && d.pitch == 45.f);

    // container set slot: [0xa8][wid][short slot][item id short][count][damage short]
    binary::BinaryStream c;
    c.putByte(0xa8);
    c.putByte(0);
    c.putShort(3);
    c.putShort(1);
    c.putByte(5);
    c.putShort(0);
    auto v2 = mp::translateIncoming(mp::F011, c.buffer());
    CHECK(v2.size() == 1);
    auto d2 = v2.empty() ? protocol::ContainerSetSlotDecoded{} : protocol::decodeContainerSetSlot(v2[0]);
    CHECK(d2.ok && d2.window_id == 0 && d2.slot == 3 && d2.item.id == 1 && d2.item.count == 5);
  }
  // ---- 0.12 inbound container set slot (no hotbar short)
  {
    binary::BinaryStream c;
    c.putByte(protocol::CONTAINER_SET_SLOT_PACKET);
    c.putByte(0);
    c.putShort(4);
    c.putShort(5); // item id
    c.putByte(2);
    c.putShort(0); // damage
    c.putLShort(0); // nbt
    auto v = mp::translateIncoming(mp::F012, c.buffer());
    CHECK(v.size() == 1);
    auto d = v.empty() ? protocol::ContainerSetSlotDecoded{} : protocol::decodeContainerSetSlot(v[0]);
    CHECK(d.ok && d.slot == 4 && d.hotbar_slot == 0 && d.item.id == 5);
  }
  // ---- 0.11 batch [0xb1][int size][zlib(raw concat of bare packets)]
  {
    std::string concat = mkLogin011();
    binary::BinaryStream t;
    t.putByte(0x85);
    t.putByte(1);
    t.putString("steve011");
    t.putString("hi");
    concat += t.buffer();
    auto z = zlib(concat);
    binary::BinaryStream batch;
    batch.putByte(0xb1);
    batch.putInt(static_cast<std::int32_t>(z.size()));
    batch.put(z);
    auto v = mp::translateIncoming(mp::F011, batch.buffer());
    CHECK(v.size() == 2);
    CHECK(!v.empty() && protocol::decodeLogin(v[0]).ok);
    auto d = v.size() < 2 ? protocol::TextDecoded{} : protocol::decodeText(v[1]);
    CHECK(d.ok && d.message == "hi");
  }
  // ---- 0.16 command step -> native chat
  {
    binary::BinaryStream t;
    t.putByte(0x4c);
    auto putStr16 = [](binary::BinaryStream& o, std::string_view s) {
      std::size_t n = s.size();
      do {
        std::uint8_t c = n & 0x7f;
        n >>= 7;
        if (n) c |= 0x80;
        o.putByte(c);
      } while (n);
      o.put(s);
    };
    putStr16(t, "gamemode");
    putStr16(t, "default");
    t.putByte(0);
    t.putByte(0);
    t.putByte(0);
    t.putByte(0);
    putStr16(t, "{\"args\":\"1\"}");
    auto v = mp::translateIncoming(mp::F016, t.buffer());
    CHECK(v.size() == 1);
    auto d = v.empty() ? protocol::TextDecoded{} : protocol::decodeText(v[0]);
    CHECK(d.ok && d.message == "/gamemode 1");
  }

  std::cout << (failures == 0 ? "OK" : "MISMATCH") << " failures=" << failures << "\n";
  return failures ? 1 : 0;
}
