#ifndef TCP_CLIENT_HPP
#define TCP_CLIENT_HPP

#include <cstddef>
#include <mutex>
#include <string>

class TcpClient
{
public:
  TcpClient(const std::string& server_ip, int server_port);
  ~TcpClient();
  bool connectToServer();
  bool sendData(const std::string& data);
  bool receiveData(std::string& data);
  void disconnect();
  bool isConnected() const;

private:
  bool sendAll(int fd, const void* data, std::size_t size);
  bool readAll(int fd, void* data, std::size_t size);
  std::string server_ip_;
  int server_port_;
  mutable std::mutex state_mutex_;
  std::mutex lifecycle_mutex_;
  std::mutex send_mutex_;
  std::mutex receive_mutex_;
  int socket_fd_ = -1;
};

#endif // TCP_CLIENT_HPP
