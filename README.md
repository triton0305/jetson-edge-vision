# Raspberry Pi Edge Vision

팀프로젝트에서 담당한 Raspberry Pi Vision Client입니다. USB Webcam 영상에서 YOLO26n ONNX로 차량을 탐지·추적하고, 기준선 통과량을 5초 단위로 집계합니다. 생성한 결과를 TCP로 팀원의 Server에 전달하는 것이 프로젝트의 핵심 연동 구조입니다.

이 Client를 별도로 실행하고 통신·DB 저장까지 확인할 수 있도록 [테스트용 Relay Server](https://github.com/triton0305/edge-vision-relay-server)를 독립된 Repository로 제공합니다.

## Key Features

- `car`, `motorcycle`, `bus`, `truck` 탐지 및 Class-aware NMS
- IoU와 중심점 거리를 이용한 차량 Tracking
- 수평 기준선 통과 감지 및 차종별 5초 Traffic Counting
- Detection별 `vision` 메시지와 구간별 `traffic_count` 메시지 생성
- 영상 처리와 네트워크 처리를 분리한 Thread-safe Message Queue
- ACK 확인, Timeout, Retry 및 자동 재연결

```text
USB Webcam → OpenCV / V4L2 → Letterbox → YOLO26n ONNX
→ Vehicle Detection → Tracking → Line Crossing / Traffic Counting
→ JSON → Message Queue → TCP → Server
```

Tracker의 `track_id`는 영상에서 동일 차량을 추정하기 위한 임시 ID입니다. 번호판 ID나 전송 메시지의 `message_id`와는 다릅니다. 기준선은 화면의 `y = 240`에 있으며, 이동 방향은 구분하지 않고 차량별 통과를 한 번만 집계합니다.

## Development Environment

| Item | Environment |
|---|---|
| Board | Raspberry Pi 4 |
| OS | Debian GNU/Linux 13, 64-bit |
| Camera | USB Webcam / OpenCV V4L2 |
| Camera Resolution | 640 × 480 |
| Camera FPS | 30 FPS로 설정 시도 |
| Language / Build | C++17 / CMake |
| Vision / Inference | OpenCV 4.10.0 / OpenCV DNN CPU |
| Model | YOLO26n ONNX, 640 × 640 input |
| Serialization | nlohmann/json |
| Network | TCP/IP, `std::thread` |

카메라에서 실제로 프레임을 가져온 속도는 약 `21.6 FPS`였습니다. 입력 프레임은 Letterbox로 변환해 추론하고, 탐지 bbox는 원본 `640 × 480` 좌표로 복원합니다. 처리 대상 COCO Class는 `car(2)`, `motorcycle(3)`, `bus(5)`, `truck(7)`입니다.

## Traffic Counting and Delivery

Detection이 없는 프레임에서는 `vision` 메시지를 보내지 않습니다. Traffic Count는 차량이 통과하지 않은 구간도 0으로 전송합니다.

| Message | Unit | Delivery Policy |
|---|---|---|
| `vision` | Detection 1개 | BestEffort: ACK 실패 시 최초 전송 포함 최대 3회 시도 |
| `traffic_count` | 5초 집계 구간 1개 | Reliable: ACK를 받을 때까지 재연결·재시도 |

메시지는 `4-byte big-endian length-prefix + JSON` 형식으로 전송합니다. 재시도할 때는 동일한 `message_id`와 payload를 유지합니다. 서버가 실행되지 않은 상태에서 Client를 시작해도 영상 처리는 계속되고, Network Worker가 재연결을 시도합니다.

## Performance

Raspberry Pi 4 CPU 환경에서 측정한 결과입니다.

| Metric | Result |
|---|---:|
| Camera Frame Grab | 약 21.6 FPS |
| Effective FPS | 약 2.2–2.34 FPS |
| Inference Latency | 약 405–425 ms |
| Delivery Latency | 약 100–130 ms |

측정값은 테스트 당시의 장면과 실행 환경을 기준으로 하며, 조건에 따라 달라질 수 있습니다.

## Build and Run

OpenCV 개발 패키지와 nlohmann/json 헤더가 필요합니다. 기본 ONNX 모델 경로는 `models/yolo26n.onnx`입니다.

```bash
cmake -S . -B build
cmake --build build -j
./build/bin/edge_vision <server_ip> <server_port>
```

통신 상대가 필요한 경우 [테스트용 Relay Server](https://github.com/triton0305/edge-vision-relay-server)를 사용할 수 있습니다. 서버의 빌드·실행 방법은 해당 Repository의 README를 참고하세요.

## Project Structure

```text
include/                 src/
├── core/                ├── core/
├── vision/              ├── vision/
├── protocol/            ├── protocol/
└── network/             ├── network/
                        └── main.cpp
```

- `vision`: Camera, 전처리, 추론, 후처리, Tracking, Traffic Counting
- `core`: 설정, Detection·Traffic Count 데이터, Message ID, Metrics
- `protocol`: JSON 직렬화
- `network`: Message Queue, TCP, ACK, Retry, Reconnect
- `main.cpp`: 컴포넌트 초기화와 실행·종료 흐름

## Integration Verification

팀프로젝트에서는 Vision Client와 팀원의 Server 간 TCP/ACK 연동을 확인했습니다. 별도로 제공한 테스트용 Relay Server로는 Raspberry Pi → Windows/WSL → Relay Server → SQLite → ACK 흐름을 검증했습니다. 이 테스트에서 SQLite `detections`에 약 970행, `traffic_counts`에 약 81행이 누적되는 것을 확인했습니다.

## Operational Follow-up

- systemd 자동 실행 및 장애 시 재시작
- Camera 장애 복구
- 장시간 Soak Test와 운영 로그 보강
- 장시간 서버 장애 시 Reliable Queue 운영 정책 보완
- 최종 배포·데모 정리
