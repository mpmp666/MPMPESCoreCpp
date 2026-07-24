#pragma once

#include "mpmpes/binary/BinaryStream.hpp"
#include "mpmpes/raklib/Datagram.hpp"
#include "mpmpes/raklib/EncapsulatedPacket.hpp"
#include "mpmpes/raklib/UDPServerSocket.hpp"

#include <cstdint>
#include <deque>
#include <functional>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace mpmpes::raklib {

class SessionManager;

enum class SessionState {
  Unconnected = 0,
  Connecting1 = 1,
  Connecting2 = 2,
  Connected = 3,
};

// Callback when a full MCPE-layer payload arrives (buffer[0] >= 0x80).
using EncapsulatedHandler =
    std::function<void(const Endpoint& endpoint, const EncapsulatedPacket& packet)>;
using SessionOpenHandler =
    std::function<void(const Endpoint& endpoint, std::int64_t client_id)>;
using SessionCloseHandler =
    std::function<void(const Endpoint& endpoint, std::string_view reason)>;

class Session {
public:
  static constexpr int kWindowSize = 2048;
  static constexpr int kMaxSplitSize = 128;
  static constexpr int kMaxSplitCount = 4;

  Session(SessionManager& manager, Endpoint endpoint, std::int64_t server_id);

  const Endpoint& endpoint() const { return endpoint_; }
  SessionState state() const { return state_; }
  std::int64_t clientId() const { return client_id_; }
  bool isTemporal() const { return temporal_; }

  void update(double now);
  void handleRaw(const std::string& buffer);
  void disconnect(std::string_view reason = "unknown");
  bool closed() const { return closed_; }

  // Queue an encapsulated packet toward the client.
  void addEncapsulated(EncapsulatedPacket packet, bool immediate = false);
  // Flush pending ACK/NACK immediately (low-latency path after inbound datagrams).
  void flushAcks();

  void setHandlers(EncapsulatedHandler on_encap, SessionOpenHandler on_open,
                   SessionCloseHandler on_close) {
    on_encap_ = std::move(on_encap);
    on_open_ = std::move(on_open);
    on_close_ = std::move(on_close);
  }

private:
  void sendBytes(std::string_view data);
  void sendDatagram(Datagram dgram, double now);
  void flushSendQueue(double now);
  void addToQueue(EncapsulatedPacket pk, bool immediate, double now);

  void handleDatagram(const std::string& buffer, double now);
  void handleAck(const std::string& buffer);
  void handleNack(const std::string& buffer, double now);
  void handleOffline(const std::string& buffer);

  void handleEncapsulated(EncapsulatedPacket packet);
  void handleEncapsulatedRoute(EncapsulatedPacket packet);
  void handleSplit(EncapsulatedPacket packet);
  void handleInternalConnected(const std::string& payload);

  std::string encodeServerHandshake(std::int64_t send_ping);
  void sendOpenConnectionReply2();
  static void putAddress(mpmpes::binary::BinaryStream& out, std::string_view addr,
                         std::uint16_t port);

  SessionManager& manager_;
  Endpoint endpoint_;
  sockaddr_in cached_dst_{}; // resolved once for sendto fast-path
  bool cached_dst_ok_ = false;
  std::int64_t server_id_;
  std::int64_t client_id_ = 0;
  SessionState state_ = SessionState::Unconnected;
  bool temporal_ = true;
  bool active_ = false;
  bool closed_ = false;
  double last_update_ = 0.0;
  // RTO-like recovery resend delay (seconds). PHP-like ~0.5s feels snappier than 8s.
  static constexpr double kRecoveryResendDelay = 0.5;

  std::uint16_t mtu_size_ = 548;
  std::uint32_t message_index_ = 0;
  std::uint16_t split_id_counter_ = 0;
  std::uint32_t channel_index_[32]{};

  std::uint32_t send_seq_ = 0;
  std::int32_t last_seq_ = -1;
  std::int32_t window_start_ = -1;
  std::int32_t window_end_ = kWindowSize;

  std::int32_t reliable_window_start_ = 0;
  std::int32_t reliable_window_end_ = kWindowSize;
  std::int32_t last_reliable_index_ = -1;
  std::map<std::int32_t, EncapsulatedPacket> reliable_window_;

  std::set<std::uint32_t> ack_queue_;
  std::set<std::uint32_t> nack_queue_;
  std::map<std::uint32_t, bool> received_window_;

  Datagram send_queue_;
  std::deque<Datagram> packet_to_send_;
  std::map<std::uint32_t, Datagram> recovery_queue_;

  // splitID -> (splitIndex -> packet)
  std::unordered_map<std::uint16_t, std::map<std::int32_t, EncapsulatedPacket>> splits_;

  EncapsulatedHandler on_encap_;
  SessionOpenHandler on_open_;
  SessionCloseHandler on_close_;
};

} // namespace mpmpes::raklib
