#include "mpmpes/raklib/Session.hpp"

#include "mpmpes/binary/BinaryStream.hpp"
#include "mpmpes/raklib/AcknowledgePacket.hpp"
#include "mpmpes/raklib/RakLibConstants.hpp"
#include "mpmpes/raklib/SessionManager.hpp"
#include "mpmpes/util/Logger.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <sstream>

namespace mpmpes::raklib {
namespace {
namespace Binary = mpmpes::binary::Binary;
using mpmpes::binary::BinaryStream;
using mpmpes::util::Logger;

constexpr std::uint8_t kIdClientConnect = 0x09;
constexpr std::uint8_t kIdServerHandshake = 0x10;
constexpr std::uint8_t kIdClientHandshake = 0x13;
constexpr std::uint8_t kIdClientDisconnect = 0x15;
constexpr std::uint8_t kIdConnectedPing = 0x00;
constexpr std::uint8_t kIdConnectedPong = 0x03;
} // namespace

Session::Session(SessionManager& manager, Endpoint endpoint, std::int64_t server_id)
    : manager_(manager), endpoint_(std::move(endpoint)), server_id_(server_id) {
  send_queue_.id = 0x84;
  for (auto& c : channel_index_) c = 0;
}

void Session::sendBytes(std::string_view data) {
  manager_.sendRaw(data, endpoint_);
}

void Session::putAddress(BinaryStream& out, std::string_view addr, std::uint16_t port) {
  out.putByte(4);
  unsigned a = 0, b = 0, c = 0, d = 0;
  if (std::sscanf(std::string(addr).c_str(), "%u.%u.%u.%u", &a, &b, &c, &d) == 4) {
    out.putByte(static_cast<std::uint8_t>((~a) & 0xFF));
    out.putByte(static_cast<std::uint8_t>((~b) & 0xFF));
    out.putByte(static_cast<std::uint8_t>((~c) & 0xFF));
    out.putByte(static_cast<std::uint8_t>((~d) & 0xFF));
  } else {
    out.putByte(0xFF);
    out.putByte(0xFF);
    out.putByte(0xFF);
    out.putByte(0xFF);
  }
  out.putShort(port);
}

std::string Session::encodeServerHandshake(std::int64_t send_ping) {
  BinaryStream out;
  out.putByte(kIdServerHandshake);
  putAddress(out, endpoint_.address, endpoint_.port);
  out.putShort(0);
  // 10 system addresses
  for (int i = 0; i < 10; ++i) {
    if (i == 0) {
      putAddress(out, "127.0.0.1", 0);
    } else {
      putAddress(out, "0.0.0.0", 0);
    }
  }
  out.putLong(send_ping);
  out.putLong(send_ping + 1000);
  return out.buffer();
}

void Session::sendDatagram(Datagram dgram, double now) {
  dgram.send_time = now;
  auto raw = dgram.encode();
  recovery_queue_[dgram.seq_number] = dgram;
  sendBytes(raw);
}

void Session::flushSendQueue(double now) {
  if (send_queue_.packets.empty()) return;
  send_queue_.seq_number = send_seq_++;
  send_queue_.id = 0x84;
  sendDatagram(std::move(send_queue_), now);
  send_queue_ = Datagram{};
  send_queue_.id = 0x84;
}

void Session::addToQueue(EncapsulatedPacket pk, bool immediate, double now) {
  if (immediate) {
    Datagram d;
    d.id = 0x80;
    d.seq_number = send_seq_++;
    d.packets.push_back(std::move(pk));
    sendDatagram(std::move(d), now);
    return;
  }
  if (send_queue_.length() + pk.totalLength() > mtu_size_) {
    flushSendQueue(now);
  }
  send_queue_.packets.push_back(std::move(pk));
}

void Session::addEncapsulated(EncapsulatedPacket packet, bool immediate) {
  const double now = manager_.now();
  const auto rel = packet.reliability;
  if (rel == 2 || rel == 3 || rel == 4 || rel == 6 || rel == 7) {
    packet.message_index = message_index_++;
    if (rel == 3) {
      const auto ch = packet.order_channel.value_or(0);
      packet.order_channel = ch;
      packet.order_index = channel_index_[ch % 32]++;
    }
  }

  if (packet.totalLength() + 4 > mtu_size_) {
    // split
    const std::size_t chunk = mtu_size_ > 34 ? static_cast<std::size_t>(mtu_size_ - 34) : 64;
    const auto& buf = packet.buffer;
    const std::size_t count = (buf.size() + chunk - 1) / chunk;
    const auto sid = static_cast<std::uint16_t>(++split_id_counter_ % 65536);
    for (std::size_t i = 0; i < count; ++i) {
      EncapsulatedPacket part;
      part.has_split = true;
      part.split_id = sid;
      part.split_count = static_cast<std::int32_t>(count);
      part.split_index = static_cast<std::int32_t>(i);
      part.reliability = packet.reliability;
      part.buffer = buf.substr(i * chunk, chunk);
      if (i == 0) {
        part.message_index = packet.message_index;
      } else if (rel == 2 || rel == 3 || rel == 4 || rel == 6 || rel == 7) {
        part.message_index = message_index_++;
      }
      if (rel == 3) {
        part.order_channel = packet.order_channel;
        part.order_index = packet.order_index;
      }
      addToQueue(std::move(part), true, now);
    }
  } else {
    addToQueue(std::move(packet), immediate, now);
  }
}

void Session::update(double now) {
  if (!active_ && last_update_ > 0 && (last_update_ + 10.0) < now) {
    disconnect("timeout");
    return;
  }
  active_ = false;

  if (!ack_queue_.empty()) {
    AcknowledgePacket ack;
    ack.id = ID_ACK;
    ack.packets.assign(ack_queue_.begin(), ack_queue_.end());
    sendBytes(ack.encode());
    ack_queue_.clear();
  }
  if (!nack_queue_.empty()) {
    AcknowledgePacket nack;
    nack.id = ID_NACK;
    nack.packets.assign(nack_queue_.begin(), nack_queue_.end());
    sendBytes(nack.encode());
    nack_queue_.clear();
  }

  int limit = 16;
  while (!packet_to_send_.empty() && limit-- > 0) {
    auto pk = std::move(packet_to_send_.front());
    packet_to_send_.pop_front();
    sendDatagram(std::move(pk), now);
  }

  // resend old recovery
  for (auto it = recovery_queue_.begin(); it != recovery_queue_.end();) {
    if (it->second.send_time < (now - 8.0)) {
      auto pk = it->second;
      pk.seq_number = send_seq_++;
      packet_to_send_.push_back(std::move(pk));
      it = recovery_queue_.erase(it);
    } else {
      ++it;
    }
  }

  for (auto it = received_window_.begin(); it != received_window_.end();) {
    if (static_cast<std::int32_t>(it->first) < window_start_) {
      it = received_window_.erase(it);
    } else {
      break;
    }
  }

  flushSendQueue(now);
}

void Session::disconnect(std::string_view reason) {
  if (closed_) return;
  closed_ = true;
  if (on_close_) {
    on_close_(endpoint_, reason);
  }
  // Deferred erase: SessionManager drops closed sessions after update().
  manager_.markSessionClosed(endpoint_, reason);
}

void Session::handleRaw(const std::string& buffer) {
  if (buffer.empty()) return;
  const double now = manager_.now();
  active_ = true;
  last_update_ = now;

  const auto pid = static_cast<std::uint8_t>(buffer[0]);

  // Match PHP raklib Session::handlePacket:
  // when CONNECTING_2 / CONNECTED, only DATA (0x80-0x8f) + ACK/NACK.
  // Offline OCR1 must NOT reset state after OCR2 — that drops CLIENT_CONNECT.
  if (state_ == SessionState::Connected || state_ == SessionState::Connecting2) {
    if (pid >= 0x80 && pid <= 0x8f) {
      handleDatagram(buffer, now);
      return;
    }
    if (pid == ID_ACK) {
      handleAck(buffer);
      return;
    }
    if (pid == ID_NACK) {
      handleNack(buffer, now);
      return;
    }
    // Ignore offline retransmits (OCR1/OCR2) while past CONNECTING_1.
    // Optionally re-send OCR2 if client never got it (rare).
    if (state_ == SessionState::Connecting2 && pid == ID_OPEN_CONNECTION_REQUEST_2) {
      // Client retransmit of OCR2: re-reply without regressing state
      sendOpenConnectionReply2();
    }
    return;
  }

  if (pid > 0x00 && pid < 0x80) {
    handleOffline(buffer);
  }
}

void Session::sendOpenConnectionReply2() {
  BinaryStream out;
  out.putByte(ID_OPEN_CONNECTION_REPLY_2);
  out.put(std::string(MAGIC));
  out.putLong(server_id_);
  putAddress(out, endpoint_.address, endpoint_.port);
  out.putShort(mtu_size_);
  out.putByte(0);
  sendBytes(out.buffer());
}

void Session::handleOffline(const std::string& buffer) {
  const auto pid = static_cast<std::uint8_t>(buffer[0]);
  if (pid == ID_OPEN_CONNECTION_REQUEST_1) {
    if (buffer.size() < 1 + MAGIC.size() + 1) return;
    if (std::memcmp(buffer.data() + 1, MAGIC.data(), MAGIC.size()) != 0) return;
    // mtuSize = packet length (same as PHP: strlen(pad) + 18)
    const auto mtu = static_cast<std::uint16_t>(buffer.size());
    mtu_size_ = mtu;
    BinaryStream out;
    out.putByte(ID_OPEN_CONNECTION_REPLY_1);
    out.put(std::string(MAGIC));
    out.putLong(server_id_);
    out.putByte(0); // server security
    out.putShort(mtu);
    sendBytes(out.buffer());
    state_ = SessionState::Connecting1;
    Logger::instance().info("Session OCR1 ", endpoint_.address, ":", endpoint_.port,
                            " mtu=", mtu);
    return;
  }

  if (state_ == SessionState::Connecting1 && pid == ID_OPEN_CONNECTION_REQUEST_2) {
    try {
      BinaryStream in(buffer);
      in.getByte();
      // MAGIC may contain \0 — compare by size, not C-string
      const auto magic = in.get(MAGIC.size());
      if (magic.size() != MAGIC.size() ||
          std::memcmp(magic.data(), MAGIC.data(), MAGIC.size()) != 0) {
        Logger::instance().warning("OCR2 bad magic from ", endpoint_.address);
        return;
      }
      // Address: version(1) + ipv4(4 inverted) + port(2 BE)
      const auto version = in.getByte();
      if (version != 4) {
        Logger::instance().warning("OCR2 unsupported IP version ", static_cast<int>(version),
                                   " from ", endpoint_.address);
        return;
      }
      in.get(4); // address octets (inverted)
      const auto server_port = in.getShort();
      (void)server_port; // port check optional like PHP portChecking=false
      auto mtu = in.getShort();
      client_id_ = in.getLong();
      mtu_size_ = static_cast<std::uint16_t>(
          std::min(static_cast<int>(std::abs(static_cast<int>(mtu))), 1464));

      sendOpenConnectionReply2();
      state_ = SessionState::Connecting2;
      Logger::instance().info("Session OCR2 ", endpoint_.address, ":", endpoint_.port,
                              " mtu=", mtu_size_, " clientId=", client_id_);
    } catch (const std::exception& e) {
      Logger::instance().warning("OCR2 parse fail ", endpoint_.address, ": ", e.what());
    } catch (...) {
      Logger::instance().warning("OCR2 parse fail ", endpoint_.address);
    }
  }
}

void Session::handleDatagram(const std::string& buffer, double now) {
  (void)now;
  Datagram dgram;
  if (!dgram.decode(buffer)) return;

  const auto seq = static_cast<std::int32_t>(dgram.seq_number);
  if (seq < window_start_ || seq > window_end_ || received_window_.count(dgram.seq_number)) {
    return;
  }

  const auto diff = seq - last_seq_;
  nack_queue_.erase(dgram.seq_number);
  ack_queue_.insert(dgram.seq_number);
  received_window_[dgram.seq_number] = true;

  if (diff != 1) {
    for (std::int32_t i = last_seq_ + 1; i < seq; ++i) {
      if (!received_window_.count(static_cast<std::uint32_t>(i))) {
        nack_queue_.insert(static_cast<std::uint32_t>(i));
      }
    }
  }
  if (diff >= 1) {
    last_seq_ = seq;
    window_start_ += diff;
    window_end_ += diff;
  }

  for (auto& pk : dgram.packets) {
    handleEncapsulated(std::move(pk));
  }
}

void Session::handleAck(const std::string& buffer) {
  AcknowledgePacket ack;
  if (!ack.decode(buffer)) return;
  for (auto seq : ack.packets) {
    recovery_queue_.erase(seq);
  }
}

void Session::handleNack(const std::string& buffer, double now) {
  AcknowledgePacket nack;
  if (!nack.decode(buffer)) return;
  for (auto seq : nack.packets) {
    auto it = recovery_queue_.find(seq);
    if (it != recovery_queue_.end()) {
      auto pk = it->second;
      pk.seq_number = send_seq_++;
      recovery_queue_.erase(it);
      sendDatagram(std::move(pk), now);
    }
  }
}

void Session::handleEncapsulated(EncapsulatedPacket packet) {
  if (!packet.message_index) {
    handleEncapsulatedRoute(std::move(packet));
    return;
  }

  const auto idx = static_cast<std::int32_t>(*packet.message_index);
  if (idx < reliable_window_start_ || idx > reliable_window_end_) {
    return;
  }

  if ((idx - last_reliable_index_) == 1) {
    last_reliable_index_++;
    reliable_window_start_++;
    reliable_window_end_++;
    handleEncapsulatedRoute(std::move(packet));

    while (!reliable_window_.empty()) {
      auto it = reliable_window_.begin();
      if ((it->first - last_reliable_index_) != 1) break;
      last_reliable_index_++;
      reliable_window_start_++;
      reliable_window_end_++;
      auto pk = std::move(it->second);
      reliable_window_.erase(it);
      handleEncapsulatedRoute(std::move(pk));
    }
  } else {
    reliable_window_[idx] = std::move(packet);
  }
}

void Session::handleSplit(EncapsulatedPacket packet) {
  if (!packet.split_count || !packet.split_index || !packet.split_id) return;
  const auto count = *packet.split_count;
  const auto index = *packet.split_index;
  const auto sid = *packet.split_id;
  if (count >= kMaxSplitSize || index >= kMaxSplitSize || index < 0) return;

  auto& parts = splits_[sid];
  if (parts.empty() && splits_.size() > static_cast<std::size_t>(kMaxSplitCount)) {
    return;
  }
  parts[index] = std::move(packet);
  if (static_cast<std::int32_t>(parts.size()) != count) return;

  EncapsulatedPacket combined;
  combined.buffer.reserve(static_cast<std::size_t>(count) * 64);
  for (std::int32_t i = 0; i < count; ++i) {
    auto it = parts.find(i);
    if (it == parts.end()) {
      splits_.erase(sid);
      return;
    }
    combined.buffer += it->second.buffer;
  }
  splits_.erase(sid);
  handleEncapsulatedRoute(std::move(combined));
}

void Session::handleEncapsulatedRoute(EncapsulatedPacket packet) {
  if (packet.has_split) {
    if (state_ == SessionState::Connected) {
      handleSplit(std::move(packet));
    }
    return;
  }
  if (packet.buffer.empty()) return;

  const auto id = static_cast<std::uint8_t>(packet.buffer[0]);
  if (id < 0x80) {
    handleInternalConnected(packet.buffer);
    return;
  }

  if (state_ == SessionState::Connected && on_encap_) {
    on_encap_(endpoint_, packet);
  }
}

void Session::handleInternalConnected(const std::string& payload) {
  if (payload.empty()) return;
  const auto id = static_cast<std::uint8_t>(payload[0]);
  const double now = manager_.now();

  if (state_ == SessionState::Connecting2) {
    if (id == kIdClientConnect) {
      try {
        BinaryStream in(payload);
        in.getByte();
        const auto client_id = in.getLong();
        const auto send_ping = in.getLong();
        // optional useSecurity byte — ignore
        (void)client_id;
        EncapsulatedPacket reply;
        reply.reliability = 0;
        reply.buffer = encodeServerHandshake(send_ping);
        addToQueue(std::move(reply), true, now);
        Logger::instance().info("CLIENT_CONNECT from ", endpoint_.address, ":",
                                endpoint_.port, " -> SERVER_HANDSHAKE");
      } catch (const std::exception& e) {
        Logger::instance().warning("CLIENT_CONNECT parse fail: ", e.what());
      } catch (...) {
      }
      return;
    }
    if (id == kIdClientHandshake) {
      // Accept handshake; port check optional (PHP portChecking)
      state_ = SessionState::Connected;
      temporal_ = false;
      Logger::instance().notice("Session CONNECTED ", endpoint_.address, ":",
                                endpoint_.port, " clientId=", client_id_);
      if (on_open_) {
        on_open_(endpoint_, client_id_);
      }
      return;
    }
    Logger::instance().info("Connecting2 internal id=0x",
                            [&] {
                              std::ostringstream os;
                              os << std::hex << static_cast<unsigned>(id);
                              return os.str();
                            }(),
                            " from ", endpoint_.address);
  }

  if (id == kIdClientDisconnect) {
    disconnect("client disconnect");
    return;
  }

  if (id == kIdConnectedPing) {
    try {
      BinaryStream in(payload);
      in.getByte();
      const auto ping_id = in.getLong();
      BinaryStream out;
      out.putByte(kIdConnectedPong);
      out.putLong(ping_id);
      EncapsulatedPacket reply;
      reply.reliability = 0;
      reply.buffer = out.buffer();
      addToQueue(std::move(reply), false, now);
    } catch (...) {
    }
  }
}

} // namespace mpmpes::raklib
