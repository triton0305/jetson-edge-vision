# Raspberry Pi Edge Vision

Raspberry Pi 4와 USB Webcam으로 차량을 탐지·추적하고, 기준선 통과량을 5초 단위로 집계하는 C++17 Edge Vision Client입니다. YOLO26n ONNX 모델을 OpenCV DNN의 CPU 환경에서 실행하며, 결과는 TCP로 별도 [Relay Server](https://github.com/triton0305/edge-vision-relay-server)에 전달합니다.

Raspberry Pi → Windows/WSL Relay Server → SQLite 저장 → ACK 반환까지 실제 장비로 검증했습니다. Client와 Server는 독립된 Repository로 유지합니다.

## System Overview

```text
USB Webcam → OpenCV / V4L2 → YOLO26n ONNX
→ Vehicle Detection → Class-aware NMS → Tracking
                                 ├─ vision message
                                 └─ Line Crossing → 5-second traffic_count message
→ Message Queue → Network Worker → TCP / ACK
→ Relay Server → SQLite
```

COCO 클래스 중 `car(2)`, `motorcycle(3)`, `bus(5)`, `truck(7)`을 처리합니다. 탐지 결과의 bbox는 원본 `640 × 480` 프레임 좌표입니다.

Tracker는 영상에서 같은 차량으로 추정되는 객체에 `track_id`를 부여합니다. bbox 중심점이 수평 기준선(`y = 240`)을 통과하면 해당 차량을 한 번 집계하며, 방향은 구분하지 않습니다. `track_id`는 번호판 ID가 아니고 메시지의 `message_id`와도 별개입니다.

## Environment

| Item | Tested Environment |
|---|---|
| Board | Raspberry Pi 4 |
| OS | Debian GNU/Linux 13, 64-bit |
| Camera | USB Webcam / OpenCV V4L2 |
| Camera Input | 640 × 480, 30 FPS 요청 |
| Language / Build | C++17 / CMake |
| Vision | OpenCV 4.10.0 / OpenCV DNN |
| Model / Inference | YOLO26n ONNX / CPU |
| Serialization | nlohmann/json |
| Network | TCP/IP, `std::thread` |

카메라의 실제 Frame Grab은 테스트 환경에서 약 `21.6 FPS`였습니다. 모델 추론을 포함한 전체 처리 속도는 아래 Performance 수치를 참고하세요.

## Messages and Delivery

| Type | 전송 단위 | 정책 |
|---|---|---|
| `vision` | Detection 1개당 메시지 1개 | BestEffort: ACK 실패 시 최초 전송 포함 최대 3회 시도 |
| `traffic_count` | 5초 구간당 메시지 1개 | Reliable: ACK를 받을 때까지 재연결·재시도 |

Detection이 없는 프레임에서는 `vision` 메시지를 보내지 않습니다. 반면 기준선을 통과한 차량이 없는 5초 구간도 `traffic_count`를 0으로 전송합니다. Queue가 가득 차면 BestEffort Detection을 버릴 수 있지만 Reliable Traffic Count를 우선 보호합니다.

각 메시지는 `4-byte big-endian length-prefix + JSON payload`로 전송하며, Client는 메시지별 ACK를 확인한 뒤 다음 메시지를 보냅니다. 재전송 시 `message_id`와 payload를 유지합니다. 서버가 꺼진 상태에서 시작해도 영상 처리는 계속되고 Network Worker가 재연결을 시도합니다.

현재 `traffic_count`의 `data` 형식은 다음과 같습니다.

```json
{
  "period_start_ms": 1790571230000,
  "period_end_ms": 1790571235000,
  "car_count": 3,
  "motorcycle_count": 1,
  "bus_count": 0,
  "truck_count": 0
}
```

`vision`에는 프레임 획득 시각인 `timestamp_ms`, 차량 클래스, confidence, bbox 등이 담깁니다. Relay Server는 `vision`을 SQLite `detections` 테이블에, `traffic_count`를 `traffic_counts` 테이블에 저장하며, 동일 `message_id`의 중복 저장을 방지합니다.

## Build and Run

OpenCV 개발 패키지와 nlohmann/json 헤더가 필요합니다. 기본 모델 경로는 `models/yolo26n.onnx`입니다.

```bash
cmake -S . -B build
cmake --build build -j
./build/bin/edge_vision <server_ip> <server_port>
```

Relay Server의 기본 포트는 `5000`입니다. 서버의 빌드·실행 방법은 [Relay Server README](https://github.com/triton0305/edge-vision-relay-server)를 참고하세요.

## Performance and Verification

| Metric | Raspberry Pi 4 Test Result |
|---|---:|
| Effective FPS | 약 2.2–2.34 FPS |
| Inference Latency | 약 405–425 ms |
| Delivery Latency | 약 100–130 ms |
| Camera Frame Grab | 약 21.6 FPS |

위 성능은 기존 테스트 환경의 측정값으로, 장면과 실행 조건에 따라 달라질 수 있습니다. 실제 Pi–WSL E2E 통합 테스트에서는 SQLite `detections`에 약 970행, `traffic_counts`에 약 81행이 누적되는 것을 확인했습니다.

## Project Structure

```text
include/                 src/
├── core/                ├── core/
├── vision/              ├── vision/
├── protocol/            ├── protocol/
└── network/             ├── network/
                        └── main.cpp
```

`vision`은 Camera·추론·후처리·Tracking·Traffic Counting, `protocol`은 JSON 직렬화, `network`는 Queue·TCP·ACK·재연결을 담당합니다. `main.cpp`는 초기화와 실행·종료 흐름을 연결합니다.

## Operational Follow-up

- systemd 자동 실행 및 장애 시 재시작
- Camera 장애 복구
- 장시간 Soak Test와 운영 로그 보강
- 장시간 서버 장애 시 Reliable Queue 운영 정책 보완
- 최종 배포·데모 정리
