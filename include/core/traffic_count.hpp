#ifndef TRAFFIC_COUNT_HPP
#define TRAFFIC_COUNT_HPP

#include <cstdint>

struct TrafficCount
{
  std::int64_t period_start_ms;
  std::int64_t period_end_ms;
  int car_count;
  int motorcycle_count;
  int bus_count;
  int truck_count;
};

#endif // TRAFFIC_COUNT_HPP
