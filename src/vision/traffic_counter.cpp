#include "vision/traffic_counter.hpp"

TrafficCounter::TrafficCounter(int line_y)
  : line_y_(line_y)
{
}

std::vector<CrossingEvent> TrafficCounter::update(
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

int TrafficCounter::lineY() const
{
  return line_y_;
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