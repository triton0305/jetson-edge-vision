#include "network/tcp_client.hpp"
#include <arpa/inet.h>
#include <sys/socket.h>
#include <unistd.h>
#include <atomic>
#include <algorithm>
#include <cerrno>
#include <chrono>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <thread>

// Test-only linker wrapper: force interrupted/would-block/short sends.
static std::atomic<bool> inject_short_writes{true};
static std::atomic<int> wrapped_calls{0};
extern "C" ssize_t __real_send(int, const void*, size_t, int);
extern "C" ssize_t __wrap_send(int fd, const void* data, size_t size, int flags)
{
  if (inject_short_writes)
  {
    int call = wrapped_calls++;
    if (call < 2)
    {
      errno = call == 0 ? EINTR : EAGAIN;
      return -1;
    }
    size = std::min(size, static_cast<size_t>(997));
  }
  return __real_send(fd, data, size, flags);
}

namespace
{
void check(bool result, const char* message)
{
  if (!result)
    throw std::runtime_error(message);
}
struct Socket
{
  int fd;
  ~Socket() { if (fd >= 0) close(fd); }
};
}

int main()
{
  try
  {
    Socket listener{socket(AF_INET, SOCK_STREAM, 0)};
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    check(bind(listener.fd, reinterpret_cast<sockaddr*>(&address), sizeof(address)) == 0, "bind");
    socklen_t length = sizeof(address);
    check(getsockname(listener.fd, reinterpret_cast<sockaddr*>(&address), &length) == 0, "address");
    check(listen(listener.fd, 8) == 0, "listen");
    TcpClient client("127.0.0.1", ntohs(address.sin_port));
    const std::string payload(1024 * 1024, 'x');
    check(client.connectToServer(), "connect");
    Socket peer{accept(listener.fd, nullptr, nullptr)};
    check(peer.fd >= 0, "accept");
    std::string received;
    std::thread reader([&]
    {
      char bytes[997];
      while (received.size() < payload.size() + 4)
      {
        ssize_t count = recv(peer.fd, bytes, sizeof(bytes), 0);
        if (count <= 0)
          break;
        received.append(bytes, count);
      }
    });
    bool sent = client.sendData(payload);
    if (!sent)
      shutdown(peer.fd, SHUT_RDWR);
    reader.join();
    inject_short_writes = false;
    check(wrapped_calls > 100, "short-write injection missing");
    check(sent && received.size() == payload.size() + 4, "large fragmented transfer");
    std::uint32_t header;
    std::memcpy(&header, received.data(), 4);
    check(ntohl(header) == payload.size() && received.substr(4) == payload, "payload corruption");
    // Peer stops reading. The bounded sender must report failure, not hang.
    auto start = std::chrono::steady_clock::now();
    bool failed = false;
    for (int i = 0; i < 32; ++i)
    {
      if (!client.sendData(payload))
      {
        failed = true;
        break;
      }
    }
    check(failed, "backpressure was not detected");
    check(std::chrono::steady_clock::now() - start < std::chrono::seconds(5), "send deadline");
    client.disconnect();
    close(peer.fd);
    peer.fd = -1;
    // Repeated full-duplex I/O interrupted by lifecycle shutdown.
    for (int i = 0; i < 40; ++i)
    {
      check(client.connectToServer(), "stress reconnect");
      peer.fd = accept(listener.fd, nullptr, nullptr);
      check(peer.fd >= 0, "stress accept");
      std::atomic<bool> entered{false};
      std::thread rx([&]
      {
        std::string data;
        entered = true;
        client.receiveData(data);
      });
      std::thread tx([&] { client.sendData(payload); });
      while (!entered)
        std::this_thread::yield();
      client.disconnect();
      rx.join();
      tx.join();
      check(!client.isConnected(), "disconnect state");
      close(peer.fd);
      peer.fd = -1;
    }
    // Invalid framing must be rejected before payload allocation/read.
    check(client.connectToServer(), "invalid frame connect");
    peer.fd = accept(listener.fd, nullptr, nullptr);
    std::uint32_t bad = htonl(1024 * 1024 + 1);
    check(send(peer.fd, &bad, sizeof(bad), MSG_NOSIGNAL) == 4, "invalid prefix send");
    std::string data;
    check(!client.receiveData(data), "oversized frame accepted");
    client.disconnect();
    std::cout << "PASS: large transfer, backpressure deadline, 40 concurrent lifecycle cycles, invalid framing\n";
    return 0;
  }
  catch (const std::exception& error)
  {
    std::cerr << "FAIL: " << error.what() << '\n';
    return 1;
  }
}
