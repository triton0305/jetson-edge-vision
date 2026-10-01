#include "network/message_queue.hpp"

#include <iostream>
#include <utility>
#include <stdexcept>

MessageQueue::MessageQueue(std::size_t max_size)
  : max_size_(max_size)
{
  if (max_size_ == 0)
    throw std::invalid_argument("Queue capacity must be positive");
}

bool MessageQueue::push(OutboundMessage message)
{
  {
    std::lock_guard<std::mutex> lock(mutex_);

    if (closed_)
      return false;

    if (queue_.size() >= max_size_)
    {
      std::cerr << "Queue full: dropping best-effort message "
                << queue_.front().message_id << '\n';
      queue_.pop();
      ++dropped_count_;
    }

    queue_.push(std::move(message));
  }

  condition_.notify_one();

  return true;
}

bool MessageQueue::pop(OutboundMessage& message)
{
  std::unique_lock<std::mutex> lock(mutex_);

  condition_.wait(lock, [this]
  {
    return closed_ || !queue_.empty();
  });

  if (queue_.empty())
    return false;

  message = std::move(queue_.front());
  queue_.pop();

  return true;
}

std::uint64_t MessageQueue::clear()
{
  std::lock_guard<std::mutex> lock(mutex_);

  const std::uint64_t cleared_count =
    static_cast<std::uint64_t>(queue_.size());

  while (!queue_.empty())
  {
    queue_.pop();
  }

  return cleared_count;
}

void MessageQueue::close()
{
  {
    std::lock_guard<std::mutex> lock(mutex_);
    closed_ = true;
  }

  condition_.notify_all();
}

std::size_t MessageQueue::size() const
{
  std::lock_guard<std::mutex> lock(mutex_);
  return queue_.size();
}

std::uint64_t MessageQueue::droppedCount() const
{
  std::lock_guard<std::mutex> lock(mutex_);
  return dropped_count_;
}

bool MessageQueue::isClosed() const
{
  std::lock_guard<std::mutex> lock(mutex_);
  return closed_;
}
