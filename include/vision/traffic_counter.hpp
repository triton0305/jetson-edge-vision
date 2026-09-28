#ifndef TRAFFIC_COUNTER_HPP
#define TRAFFIC_COUNTER_HPP

#include "core/traffic_count.hpp"
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

  std::vector<TrafficCount> update(
    const std::vector<TrackedDetection>& tracked_detections,
    std::int64_t timestamp_ms);

  int lineY() const;

private:
  struct TrackState
  {
    int previous_side;
    bool crossed;
  };

  int getSide(const BoundingBox& bbox) const;
  std::vector<CrossingEvent> detectCrossings(
    const std::vector<TrackedDetection>& tracked_detections);

  void initializePeriod(std::int64_t timestamp_ms);
  void countEvent(const CrossingEvent& event);
  TrafficCount makeTrafficCount() const;
  void resetCounts();

  int line_y_;
  std::unordered_map<std::uint64_t, TrackState> track_states_;

  bool period_initialized_ = false;
  std::int64_t period_start_ms_ = 0;
  std::int64_t period_end_ms_ = 0;

  int car_count_ = 0;
  int motorcycle_count_ = 0;
  int bus_count_ = 0;
  int truck_count_ = 0;
};

#endif // TRAFFIC_COUNTER_HPP
