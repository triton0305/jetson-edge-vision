#include "protocol/serializer.hpp"

#include <nlohmann/json.hpp>

#include "core/config.hpp"

std::string Serializer::serialize(
  const DetectionResult& result,
  const Detection& detection,
  const std::string& message_id) const
{
  nlohmann::json json;

  json["version"] = Config::PROTOCOL_VERSION;
  json["type"] = "vision";
  json["device_id"] = Config::DEVICE_ID;
  json["message_id"] = message_id;

  json["data"]["frame_id"] = result.frame_id;
  json["data"]["timestamp_ms"] = result.timestamp_ms;
  json["data"]["class_id"] = detection.class_id;
  json["data"]["class_name"] = detection.class_name;
  json["data"]["confidence"] = detection.confidence;

  json["data"]["bbox"]["x"] = detection.bbox.x;
  json["data"]["bbox"]["y"] = detection.bbox.y;
  json["data"]["bbox"]["width"] = detection.bbox.width;
  json["data"]["bbox"]["height"] = detection.bbox.height;

  return json.dump();
}

std::string Serializer::serializeTrafficCount(
  const TrafficCount& count,
  const std::string& message_id) const
{
  nlohmann::json json;

  json["version"] = Config::PROTOCOL_VERSION;
  json["type"] = "traffic_count";
  json["device_id"] = Config::DEVICE_ID;
  json["message_id"] = message_id;

  json["data"]["period_start_ms"] = count.period_start_ms;
  json["data"]["period_end_ms"] = count.period_end_ms;
  json["data"]["car_count"] = count.car_count;
  json["data"]["motorcycle_count"] = count.motorcycle_count;
  json["data"]["bus_count"] = count.bus_count;
  json["data"]["truck_count"] = count.truck_count;

  return json.dump();
}