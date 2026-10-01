#ifndef NETWORK_WORKER_HPP
#define NETWORK_WORKER_HPP

#include <atomic>
#include <condition_variable>
#include <mutex>
#include <thread>
#include "core/metrics.hpp"
#include "core/runtime_state.hpp"
#include "network/message_queue.hpp"
#include "network/tcp_client.hpp"

class NetworkWorker
{
public:
  NetworkWorker(MessageQueue& queue, TcpClient& client,
                RuntimeState& state, Metrics& metrics);
  ~NetworkWorker();
  void start();
  void stop();

private:
  void transmit();
  void receive();
  void lost();
  MessageQueue& queue_;
  TcpClient& tcp_client_;
  RuntimeState& runtime_state_;
  Metrics& metrics_;
  std::atomic<bool> stopping_{false};
  std::thread tx_thread_;
  std::thread rx_thread_;
  std::mutex tx_gate_;
  std::mutex wait_mutex_;
  std::condition_variable wake_;
};

#endif // NETWORK_WORKER_HPP
