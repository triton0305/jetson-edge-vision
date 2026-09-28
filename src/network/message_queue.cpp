#include "network/message_queue.hpp"

#include <iostream>
#include <utility>

MessageQueue::MessageQueue(std::size_t max_size)
  : max_size_(max_size)
{
}

bool MessageQueue::push(OutboundMessage message)
{
  {
    std::lock_guard<std::mutex> lock(mutex_);

    if (closed_)
      return false;

    if (queue_.size() >= max_size_)
    {
      bool dropped = false;
      std::queue<OutboundMessage> retained;

      while (!queue_.empty())
      {
        OutboundMessage current = std::move(queue_.front());
        queue_.pop();

        if (!dropped && current.delivery_policy == DeliveryPolicy::BestEffort)
        {
          std::cerr << "Queue full: dropping best-effort message "
                    << current.message_id << '\n';

          ++dropped_count_;
          dropped = true;
          continue;
        }

        retained.push(std::move(current));
      }

      queue_ = std::move(retained);

      if (!dropped && message.delivery_policy == DeliveryPolicy::BestEffort)
      {
        std::cerr << "Queue full: dropping incoming best-effort message "
                  << message.message_id << '\n';

        ++dropped_count_;
        return true;
      }
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