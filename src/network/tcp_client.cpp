#include "network/tcp_client.hpp"

#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/tcp.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>
#include <cerrno>
#include <chrono>
#include <cstdint>

TcpClient::TcpClient(const std::string& ip, int port)
  : server_ip_(ip), server_port_(port)
{
}

TcpClient::~TcpClient()
{
  disconnect();
}

bool TcpClient::connectToServer()
{
  std::lock_guard<std::mutex> lifecycle(lifecycle_mutex_);
  if (isConnected())
    return true;
  int fd = socket(AF_INET, SOCK_STREAM | SOCK_CLOEXEC, 0);
  if (fd < 0)
    return false;
  auto fail = [&]() { close(fd); return false; };
  int flags = fcntl(fd, F_GETFL, 0);
  if (flags < 0 || fcntl(fd, F_SETFL, flags | O_NONBLOCK) < 0)
    return fail();
  int enabled = 1, idle = 10, interval = 3, probes = 3;
  int user_timeout = 20000;
  if (setsockopt(fd, SOL_SOCKET, SO_KEEPALIVE, &enabled, sizeof(enabled)) < 0 ||
      setsockopt(fd, IPPROTO_TCP, TCP_KEEPIDLE, &idle, sizeof(idle)) < 0 ||
      setsockopt(fd, IPPROTO_TCP, TCP_KEEPINTVL, &interval, sizeof(interval)) < 0 ||
      setsockopt(fd, IPPROTO_TCP, TCP_KEEPCNT, &probes, sizeof(probes)) < 0 ||
      setsockopt(fd, IPPROTO_TCP, TCP_USER_TIMEOUT, &user_timeout, sizeof(user_timeout)) < 0)
    return fail();
  sockaddr_in address{};
  address.sin_family = AF_INET;
  address.sin_port = htons(server_port_);
  if (inet_pton(AF_INET, server_ip_.c_str(), &address.sin_addr) != 1)
    return fail();
  int result = connect(fd, reinterpret_cast<sockaddr*>(&address), sizeof(address));
  if (result < 0)
  {
    if (errno != EINPROGRESS)
      return fail();
    pollfd descriptor{fd, POLLOUT, 0};
    // Bounded connection attempt also bounds stop() while connecting.
    if (poll(&descriptor, 1, 1000) <= 0)
      return fail();
    int error = 0;
    socklen_t length = sizeof(error);
    if (getsockopt(fd, SOL_SOCKET, SO_ERROR, &error, &length) < 0 || error != 0)
      return fail();
  }
  if (fcntl(fd, F_SETFL, flags) < 0)
    return fail();
  std::lock_guard<std::mutex> state(state_mutex_);
  socket_fd_ = fd;
  return true;
}

bool TcpClient::sendAll(int fd, const void* data, std::size_t size)
{
  const char* bytes = static_cast<const char*>(data);
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(1);
  while (size > 0)
  {
    if (std::chrono::steady_clock::now() >= deadline)
      return false;
    ssize_t count = send(fd, bytes, size, MSG_NOSIGNAL | MSG_DONTWAIT);
    if (count < 0 && (errno == EAGAIN || errno == EWOULDBLOCK))
    {
      const auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(
        deadline - std::chrono::steady_clock::now()).count();
      if (remaining <= 0)
        return false;
      pollfd descriptor{fd, POLLOUT, 0};
      int result = poll(&descriptor, 1, static_cast<int>(remaining));
      if (result < 0 && errno == EINTR)
        continue;
      if (result <= 0)
        return false;
      continue;
    }
    if (count < 0 && errno == EINTR)
      continue;
    if (count <= 0)
      return false;
    bytes += count;
    size -= static_cast<std::size_t>(count);
  }
  return true;
}

bool TcpClient::readAll(int fd, void* data, std::size_t size)
{
  char* bytes = static_cast<char*>(data);
  while (size > 0)
  {
    ssize_t count = recv(fd, bytes, size, 0);
    if (count < 0 && errno == EINTR)
      continue;
    if (count <= 0)
      return false;
    bytes += count;
    size -= static_cast<std::size_t>(count);
  }
  return true;
}

bool TcpClient::sendData(const std::string& data)
{
  std::lock_guard<std::mutex> io(send_mutex_);
  int fd;
  {
    std::lock_guard<std::mutex> state(state_mutex_);
    fd = socket_fd_;
  }
  if (fd < 0 || data.empty() || data.size() > 1024 * 1024)
    return false;
  std::uint32_t length = htonl(static_cast<std::uint32_t>(data.size()));
  return sendAll(fd, &length, sizeof(length)) && sendAll(fd, data.data(), data.size());
}

bool TcpClient::receiveData(std::string& data)
{
  std::lock_guard<std::mutex> io(receive_mutex_);
  int fd;
  {
    std::lock_guard<std::mutex> state(state_mutex_);
    fd = socket_fd_;
  }
  data.clear();
  std::uint32_t length = 0;
  if (fd < 0 || !readAll(fd, &length, sizeof(length)))
    return false;
  length = ntohl(length);
  if (length == 0 || length > 1024 * 1024)
    return false;
  data.resize(length);
  return readAll(fd, &data[0], data.size());
}

void TcpClient::disconnect()
{
  std::lock_guard<std::mutex> lifecycle(lifecycle_mutex_);
  int fd;
  {
    std::lock_guard<std::mutex> state(state_mutex_);
    fd = socket_fd_;
    socket_fd_ = -1;
    if (fd >= 0)
      shutdown(fd, SHUT_RDWR);
  }
  // Wake blocking I/O first; close only once both users have released the fd.
  std::scoped_lock<std::mutex, std::mutex> io(send_mutex_, receive_mutex_);
  if (fd >= 0)
    close(fd);
}

bool TcpClient::isConnected() const
{
  std::lock_guard<std::mutex> state(state_mutex_);
  return socket_fd_ >= 0;
}
