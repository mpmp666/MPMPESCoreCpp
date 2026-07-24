#include "mpmpes/raklib/UDPServerSocket.hpp"

#include <cerrno>
#include <cstring>
#include <stdexcept>
#include <utility>

#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

namespace mpmpes::raklib {

UDPServerSocket::~UDPServerSocket() { close(); }

UDPServerSocket::UDPServerSocket(UDPServerSocket&& other) noexcept
    : fd_(std::exchange(other.fd_, -1)) {}

UDPServerSocket& UDPServerSocket::operator=(UDPServerSocket&& other) noexcept {
  if (this != &other) {
    close();
    fd_ = std::exchange(other.fd_, -1);
  }
  return *this;
}

void UDPServerSocket::close() noexcept {
  if (fd_ >= 0) {
    ::close(fd_);
    fd_ = -1;
  }
}

void UDPServerSocket::bind(std::string_view interface_addr, std::uint16_t port) {
  close();
  fd_ = ::socket(AF_INET, SOCK_DGRAM, 0);
  if (fd_ < 0) {
    throw std::runtime_error(std::string("socket(): ") + std::strerror(errno));
  }

  int yes = 1;
  ::setsockopt(fd_, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));

  sockaddr_in addr{};
  addr.sin_family = AF_INET;
  addr.sin_port = htons(port);
  if (interface_addr.empty() || interface_addr == "0.0.0.0") {
    addr.sin_addr.s_addr = INADDR_ANY;
  } else {
    if (::inet_pton(AF_INET, std::string(interface_addr).c_str(), &addr.sin_addr) != 1) {
      close();
      throw std::runtime_error("invalid bind address: " + std::string(interface_addr));
    }
  }

  if (::bind(fd_, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
    const auto err = std::string("bind(") + std::string(interface_addr) + ":" +
                     std::to_string(port) + "): " + std::strerror(errno);
    close();
    throw std::runtime_error(err);
  }

  const int flags = ::fcntl(fd_, F_GETFL, 0);
  if (flags < 0 || ::fcntl(fd_, F_SETFL, flags | O_NONBLOCK) < 0) {
    close();
    throw std::runtime_error(std::string("fcntl O_NONBLOCK: ") + std::strerror(errno));
  }

  setRecvBuffer(1024 * 1024 * 8);
  setSendBuffer(1024 * 1024 * 8);
}

void UDPServerSocket::setRecvBuffer(int size) {
  if (fd_ >= 0) {
    ::setsockopt(fd_, SOL_SOCKET, SO_RCVBUF, &size, sizeof(size));
  }
}

void UDPServerSocket::setSendBuffer(int size) {
  if (fd_ >= 0) {
    ::setsockopt(fd_, SOL_SOCKET, SO_SNDBUF, &size, sizeof(size));
  }
}

std::size_t UDPServerSocket::readPacket(std::string& buffer, Endpoint& from) {
  if (fd_ < 0) return 0;
  char tmp[65535];
  sockaddr_in src{};
  socklen_t slen = sizeof(src);
  const ssize_t n =
      ::recvfrom(fd_, tmp, sizeof(tmp), 0, reinterpret_cast<sockaddr*>(&src), &slen);
  if (n < 0) {
    if (errno == EAGAIN || errno == EWOULDBLOCK) return 0;
    return 0;
  }
  buffer.assign(tmp, static_cast<std::size_t>(n));
  char ip[INET_ADDRSTRLEN]{};
  ::inet_ntop(AF_INET, &src.sin_addr, ip, sizeof(ip));
  from.address = ip;
  from.port = ntohs(src.sin_port);
  return static_cast<std::size_t>(n);
}

std::size_t UDPServerSocket::writePacket(std::string_view data, const Endpoint& to) {
  if (fd_ < 0 || data.empty()) return 0;
  sockaddr_in dst{};
  dst.sin_family = AF_INET;
  dst.sin_port = htons(to.port);
  if (::inet_pton(AF_INET, to.address.c_str(), &dst.sin_addr) != 1) {
    return 0;
  }
  const ssize_t n = ::sendto(fd_, data.data(), data.size(), 0,
                             reinterpret_cast<sockaddr*>(&dst), sizeof(dst));
  if (n < 0) return 0;
  return static_cast<std::size_t>(n);
}

} // namespace mpmpes::raklib
