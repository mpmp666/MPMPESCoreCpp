#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <utility>

#include <netinet/in.h>

namespace mpmpes::raklib {

struct Endpoint {
  std::string address;
  std::uint16_t port = 0;
};

// Non-blocking UDP socket (AF_INET). Mirrors PHP raklib\server\UDPServerSocket.
class UDPServerSocket {
public:
  UDPServerSocket() = default;
  ~UDPServerSocket();

  UDPServerSocket(const UDPServerSocket&) = delete;
  UDPServerSocket& operator=(const UDPServerSocket&) = delete;
  UDPServerSocket(UDPServerSocket&& other) noexcept;
  UDPServerSocket& operator=(UDPServerSocket&& other) noexcept;

  // Bind and set non-blocking. Throws std::runtime_error on failure.
  void bind(std::string_view interface_addr, std::uint16_t port);

  void close() noexcept;
  bool isOpen() const { return fd_ >= 0; }

  // Returns bytes read, or 0 if would-block / empty.
  // On success fills buffer, source address and port.
  std::size_t readPacket(std::string& buffer, Endpoint& from);

  // Returns bytes sent, or 0 on would-block / error.
  std::size_t writePacket(std::string_view data, const Endpoint& to);
  // Fast path: skip inet_pton (use cached sockaddr from Session).
  std::size_t writePacketTo(std::string_view data, const sockaddr_in& dst);

  // Wait until readable or timeout_ms. Returns true if readable.
  bool waitReadable(int timeout_ms);

  void setRecvBuffer(int size);
  void setSendBuffer(int size);

  int fd() const { return fd_; }

private:
  int fd_ = -1;
};

} // namespace mpmpes::raklib
