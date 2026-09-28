#ifndef OUTBOUND_MESSAGE_HPP
#define OUTBOUND_MESSAGE_HPP

#include <string>

enum class DeliveryPolicy
{
  BestEffort,
  Reliable
};

struct OutboundMessage
{
  std::string message_id;
  std::string payload;
  DeliveryPolicy delivery_policy = DeliveryPolicy::BestEffort;
};

#endif // OUTBOUND_MESSAGE_HPP