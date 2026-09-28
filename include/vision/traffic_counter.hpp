#ifndef TRAFFIC_COUNTER_HPP
#define TRAFFIC_COUNTER_HPP

#include "vision/tracker.hpp"

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

struct CrossingEvent
{
  std::uint64_t track_id;
  int class_id;
  std::string class_name;
};

class TrafficCounter
{
public:
  explicit TrafficCounter(int line_y);

  std::vector<CrossingEvent> update(const std::vector<TrackedDetection>& tracked_detections);
  int lineY() const;

private:
  struct TrackState
  {
    int previous_side;
    bool crossed;
  };

  int getSide(const BoundingBox& bbox) const;

  int line_y_;
  std::unordered_map<std::uint64_t, TrackState> track_states_;
};

#endif // TRAFFIC_COUNTER_HPP