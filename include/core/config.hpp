#ifndef CONFIG_HPP
#define CONFIG_HPP

#include <cstddef>
#include <cstdint>

namespace Config
{
constexpr const char* DEVICE_ID = "vision-pi-01";
constexpr const char* BOOT_ID_PATH = BOOT_ID_FILE_PATH;

constexpr int PROTOCOL_VERSION = 1;
constexpr int ACK_TIMEOUT_MS = 1500;
constexpr int MAX_RETRY_COUNT = 2;
constexpr int RECONNECT_DELAY_MS = 1000;
constexpr std::size_t MAX_QUEUE_SIZE = 16;

constexpr float TRACKER_MIN_IOU = 0.10f;
constexpr double TRACKER_MAX_CENTER_DISTANCE = 160.0;
constexpr int TRACKER_MAX_MISSED_FRAMES = 3;

constexpr std::int64_t TRAFFIC_COUNT_PERIOD_MS = 5000;
}

#endif // CONFIG_HPP