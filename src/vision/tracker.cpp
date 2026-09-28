#include "vision/tracker.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <vector>

#include "core/config.hpp"

namespace
{
float calculateIoU(const BoundingBox& a, const BoundingBox& b)
{
  const int left = std::max(a.x, b.x);
  const int top = std::max(a.y, b.y);
  const int right = std::min(a.x + a.width, b.x + b.width);
  const int bottom = std::min(a.y + a.height, b.y + b.height);

  const int intersection_width = std::max(0, right - left);
  const int intersection_height = std::max(0, bottom - top);
  const int intersection_area = intersection_width * intersection_height;

  const int area_a = a.width * a.height;
  const int area_b = b.width * b.height;
  const int union_area = area_a + area_b - intersection_area;

  if (union_area <= 0)
    return 0.0f;

  return static_cast<float>(intersection_area) / union_area;
}

double calculateCenterDistance(const BoundingBox& a, const BoundingBox& b)
{
  const double ax = a.x + a.width / 2.0;
  const double ay = a.y + a.height / 2.0;
  const double bx = b.x + b.width / 2.0;
  const double by = b.y + b.height / 2.0;

  return std::hypot(ax - bx, ay - by);
}
}

Tracker::Tracker()
  : next_track_id_(1)
{
}

std::vector<TrackedDetection> Tracker::update(const std::vector<Detection>& detections)
{
  std::vector<TrackedDetection> results;
  std::vector<bool> matched_tracks(tracks_.size(), false);

  for (const Detection& detection : detections)
  {
    int best_track_index = -1;
    float best_iou = -1.0f;
    double best_distance = std::numeric_limits<double>::max();

    for (std::size_t i = 0; i < tracks_.size(); ++i)
    {
      if (matched_tracks[i])
        continue;

      Track& track = tracks_[i];

      if (track.class_id != detection.class_id)
        continue;

      const float iou = calculateIoU(track.bbox, detection.bbox);
      const double distance = calculateCenterDistance(track.bbox, detection.bbox);

      if (iou < Config::TRACKER_MIN_IOU &&
          distance > Config::TRACKER_MAX_CENTER_DISTANCE)
        continue;

      if (iou > best_iou || (iou == best_iou && distance < best_distance))
      {
        best_track_index = static_cast<int>(i);
        best_iou = iou;
        best_distance = distance;
      }
    }

    if (best_track_index >= 0)
    {
      Track& track = tracks_[best_track_index];

      track.bbox = detection.bbox;
      track.missed_frames = 0;
      matched_tracks[best_track_index] = true;

      results.push_back({track.track_id, detection});
      continue;
    }

    Track track;
    track.track_id = next_track_id_++;
    track.class_id = detection.class_id;
    track.bbox = detection.bbox;
    track.missed_frames = 0;

    tracks_.push_back(track);
    matched_tracks.push_back(true);

    results.push_back({track.track_id, detection});
  }

  for (std::size_t i = 0; i < tracks_.size(); ++i)
  {
    if (!matched_tracks[i])
      ++tracks_[i].missed_frames;
  }

  tracks_.erase(
    std::remove_if(
      tracks_.begin(),
      tracks_.end(),
      [](const Track& track)
      {
        return track.missed_frames > Config::TRACKER_MAX_MISSED_FRAMES;
      }),
    tracks_.end());

  return results;
}