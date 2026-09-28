#ifndef TRACKER_HPP
#define TRACKER_HPP

#include "core/detection_result.hpp"

#include <cstdint>
#include <vector>

struct TrackedDetection
{
  std::uint64_t track_id;
  Detection detection;
};

class Tracker
{
public:
  Tracker();

  std::vector<TrackedDetection> update(const std::vector<Detection>& detections);

private:
  struct Track
  {
    std::uint64_t track_id;
    int class_id;
    BoundingBox bbox;
    int missed_frames;
  };

  std::vector<Track> tracks_;
  std::uint64_t next_track_id_;
};

#endif // TRACKER_HPP