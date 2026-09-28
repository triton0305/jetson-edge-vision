#include "vision/traffic_counter.hpp"

#include "core/config.hpp"

TrafficCounter::TrafficCounter(int line_y)
  : line_y_(line_y)
{
}

std::vector<TrafficCount> TrafficCounter::update(
  const std::vector<TrackedDetection>& tracked_detections,
  std::int64_t timestamp_ms)
{
  std::vector<TrafficCount> completed_counts;

  if (!period_initialized_)
    initializePeriod(timestamp_ms);

  while (timestamp_ms >= period_end_ms_)
  {
    completed_counts.push_back(makeTrafficCount());

    period_start_ms_ = period_end_ms_;
    period_end_ms_ = period_start_ms_ + Config::TRAFFIC_COUNT_PERIOD_MS;

    resetCounts();
  }

  std::vector<CrossingEvent> events = detectCrossings(tracked_detections);

  if (timestamp_ms >= period_start_ms_)
  {
    for (const CrossingEvent& event : events)
      countEvent(event);
  }

  return completed_counts;
}

int TrafficCounter::lineY() const
{
  return line_y_;
}

std::vector<CrossingEvent> TrafficCounter::detectCrossings(
  const std::vector<TrackedDetection>& tracked_detections)
{
  std::vector<CrossingEvent> events;

  for (const TrackedDetection& tracked : tracked_detections)
  {
    const int current_side = getSide(tracked.detection.bbox);

    auto it = track_states_.find(tracked.track_id);

    if (it == track_states_.end())
    {
      track_states_[tracked.track_id] = {current_side, false};
      continue;
    }

    TrackState& state = it->second;

    if (!state.crossed &&
        state.previous_side != 0 &&
        current_side != 0 &&
        state.previous_side != current_side)
    {
      events.push_back({
        tracked.track_id,
        tracked.detection.class_id,
        tracked.detection.class_name
      });

      state.crossed = true;
    }

    if (current_side != 0)
      state.previous_side = current_side;
  }

  return events;
}

void TrafficCounter::initializePeriod(std::int64_t timestamp_ms)
{
  const std::int64_t period = Config::TRAFFIC_COUNT_PERIOD_MS;

  if (timestamp_ms % period == 0)
    period_start_ms_ = timestamp_ms;
  else
    period_start_ms_ = ((timestamp_ms / period) + 1) * period;

  period_end_ms_ = period_start_ms_ + period;
  period_initialized_ = true;

  resetCounts();
}

void TrafficCounter::countEvent(const CrossingEvent& event)
{
  if (event.class_name == "car")
    ++car_count_;
  else if (event.class_name == "motorcycle")
    ++motorcycle_count_;
  else if (event.class_name == "bus")
    ++bus_count_;
  else if (event.class_name == "truck")
    ++truck_count_;
}

TrafficCount TrafficCounter::makeTrafficCount() const
{
  TrafficCount count;

  count.period_start_ms = period_start_ms_;
  count.period_end_ms = period_end_ms_;
  count.car_count = car_count_;
  count.motorcycle_count = motorcycle_count_;
  count.bus_count = bus_count_;
  count.truck_count = truck_count_;

  return count;
}

void TrafficCounter::resetCounts()
{
  car_count_ = 0;
  motorcycle_count_ = 0;
  bus_count_ = 0;
  truck_count_ = 0;
}

int TrafficCounter::getSide(const BoundingBox& bbox) const
{
  const int center_y = bbox.y + bbox.height / 2;

  if (center_y < line_y_)
    return -1;

  if (center_y > line_y_)
    return 1;

  return 0;
}