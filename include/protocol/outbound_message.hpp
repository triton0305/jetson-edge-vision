#ifndef OUTBOUND_MESSAGE_HPP
#define OUTBOUND_MESSAGE_HPP

#include <string>
#include <cstdint>

struct OutboundMessage
{
  std::string message_id;
  std::string payload;
  std::uint64_t epoch = 0;
};

#endif // OUTBOUND_MESSAGE_HPP
