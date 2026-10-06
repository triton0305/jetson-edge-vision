#include "vision/tracker.hpp"
#include "protocol/serializer.hpp"

#include <cstdlib>
#include <iostream>
#include <nlohmann/json.hpp>
#include <stdexcept>

namespace
{
void require(bool condition, const char* description)
{
  if (!condition)
    throw std::runtime_error(description);
}

Detection car(int x = 100)
{
  return {2, "car", 0.87f, {x, 100, 80, 60}};
}
}

int main()
{
  try
  {
    using namespace std::chrono;
    const auto start = Tracker::Clock::time_point{};
    Tracker tracker;
    auto result = tracker.update({car()}, start);
    require(result.size() == 1 && result[0].track_id == 1, "first ID");
    result = tracker.update({car(110)}, start + milliseconds(80));
    require(result[0].track_id == 1, "moving vehicle continuity");
    require(tracker.update({}, start + milliseconds(160)).empty(), "no ghost detection");
    require(tracker.retainedTrackCount() == 1, "retain missing track");
    // Many missed frames within the timeout must not expire a track.
    for (int i = 0; i < 100; ++i)
      tracker.update({}, start + milliseconds(200 + i));
    result = tracker.update({car(120)}, start + milliseconds(879));
    require(result[0].track_id == 1, "reconnect just before timeout");
    result = tracker.update({car(120)}, start + milliseconds(1679));
    require(result[0].track_id == 2, "expire at exact timeout before matching");
    tracker.update({}, start + milliseconds(2479));
    require(tracker.retainedTrackCount() == 0, "empty frame cleanup");
    result = tracker.update({car(120)}, start + milliseconds(2480));
    require(result[0].track_id == 3, "deleted IDs never reused");

    Detection truck = car(120);
    truck.class_id = 7;
    truck.class_name = "truck";
    result = tracker.update({truck, car(120), car(125)}, start + milliseconds(2500));
    require(result.size() == 3, "output count");
    require(result[0].track_id == 4 && result[1].track_id == 3 && result[2].track_id == 5,
            "same-class one-to-one and input order");
    require(result[0].detection.class_name == "truck" && result[2].detection.bbox.x == 125,
            "original detection preserved");
    result = tracker.update({car(600)}, start + milliseconds(2510));
    require(result[0].track_id == 6, "distant detection gets new ID");

    Tracker ranking;
    result = ranking.update({car(100), car(180)}, start);
    result = ranking.update({car(175), car(105)}, start + milliseconds(10));
    require(result[0].track_id == 2 && result[1].track_id == 1, "highest IoU first");
    Tracker ties;
    ties.update({car(), car()}, start);
    result = ties.update({car(), car()}, start + milliseconds(10));
    require(result[0].track_id == 1 && result[1].track_id == 2, "deterministic ties");

    Tracker distance_gate;
    distance_gate.update({car(100)}, start);
    result = distance_gate.update({car(260)}, start + milliseconds(10));
    require(result[0].track_id == 1, "center distance accepted at 160 pixels without IoU");
    result = distance_gate.update({car(421)}, start + milliseconds(20));
    require(result[0].track_id == 2, "center distance rejected above 160 without IoU");
    Tracker iou_gate;
    Detection wide = car(0);
    wide.bbox.width = 300;
    iou_gate.update({wide}, start);
    wide.bbox.x = 170;
    result = iou_gate.update({wide}, start + milliseconds(10));
    require(result[0].track_id == 1, "sufficient IoU accepted beyond center-distance gate");

    Serializer serializer;
    const Detection detection = car();
    const auto before = serializer.serialize({100, 123456}, detection, "test-1");
    const auto tracked = ranking.update({detection}, start + milliseconds(20));
    const auto after = serializer.serialize({100, 123456}, tracked[0].detection, "test-1");
    require(before == after, "JSON byte-for-byte unchanged");
    const auto json = nlohmann::json::parse(after);
    require(json["data"].size() == 6 && json["data"]["bbox"].size() == 4 &&
            after.find("track_id") == std::string::npos, "JSON schema unchanged");
    std::cout << "PASS: tracker lifecycle, association, ID policy and unchanged JSON\n";
    return EXIT_SUCCESS;
  }
  catch (const std::exception& error)
  {
    std::cerr << "FAIL: " << error.what() << '\n';
    return EXIT_FAILURE;
  }
}
