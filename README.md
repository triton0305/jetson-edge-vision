# Raspberry Pi Edge Vision

**개발 기간:** 2026.09.21 ~ 2026.09.28

Raspberry Pi 4에서 USB Webcam 영상을 YOLO26n ONNX로 처리해 차량 Detection을 생성하는 C++17 Vision Client입니다. 탐지 객체마다 `vision` JSON을 만들고, 별도 네트워크 스레드에서 TCP/ACK로 전달합니다. 독립 검증용 [Relay Server](https://github.com/triton0305/edge-vision-relay-server)를 통해 SQLite 저장까지 확인했습니다.

## Key Features

- `car`, `motorcycle`, `bus`, `truck` 탐지 및 Class-aware NMS
- Detection 1개당 `vision` JSON과 `message_id` 각각 1개 생성
- 영상 처리와 송신을 분리하는 유한 Message Queue와 Network Worker
- 4-byte big-endian length-prefix, partial read/write, ACK 검증
- ACK Timeout, Retry, 재연결 및 재전송 시 동일 payload 유지
- FPS, 추론 시간, Queue 크기 및 Drop 수 측정

## Architecture

| Stage | Flow |
|---|---|
| **① Vision loop** | USB Webcam (V4L2) → Letterbox → YOLO26n ONNX → Class-aware NMS |
| **② Delivery** | 객체별 `vision` JSON → Message Queue → Network Worker → TCP / ACK |
| **③ Relay Server** | JSON 검증 → SQLite `detections` 저장 → ACK 반환 |

## Message Protocol

TCP 메시지는 `4-byte big-endian payload length + JSON payload` 형식입니다. `timestamp_ms`는 프레임 획득 시각의 Unix ms이고, bbox는 원본 640×480 프레임의 픽셀 좌표입니다.

```json
{
  "version": 1,
  "type": "vision",
  "device_id": "vision-pi-01",
  "message_id": "vision-pi-01-000059-00000001",
  "data": {
    "frame_id": 0,
    "timestamp_ms": 1790580489875,
    "class_id": 2,
    "class_name": "car",
    "confidence": 0.2803,
    "bbox": {
      "x": 131,
      "y": 328,
      "width": 85,
      "height": 70
    }
  }
}
```

서버는 같은 length-prefix 형식으로 ACK를 반환합니다.

```json
{
  "version": 1,
  "type": "ack",
  "message_id": "vision-pi-01-000059-00000001",
  "status": "ok"
}
```

## Reliability and Network Behavior

Client는 ACK의 `message_id`와 상태를 검증합니다. 송신 또는 ACK 수신에 실패하면 최초 전송을 포함해 최대 3회 시도하며, 재전송할 때 `message_id`와 payload를 유지합니다. 연결에 실패해도 Vision Loop는 계속 실행되고 Network Worker가 재연결을 시도합니다. 재시도 한도를 넘은 메시지는 Drop하며, Server Error ACK 또는 유효하지 않은 ACK는 실행 실패로 처리합니다.

## Client / Server Responsibility

| Component | Responsibility |
|---|---|
| Vision Client | 프레임 획득, 차량 탐지, 객체별 JSON 생성, Queue 및 TCP/ACK 전달 |
| Relay Server | `vision` 검증, SQLite `detections` 저장, 동일 `message_id` 중복 방지, ACK 반환 |
| Server-side analysis | 저장된 Detection을 `timestamp_ms` 기준으로 조회·집계하는 후속 기능 |

현재 Relay Server는 원본 저장과 ACK까지 구현했습니다. 시간 구간·차종·confidence별 통계 조회와 시각화는 서버 측 후처리 대상이며, 현재 Relay Server의 구현 기능으로 포함하지 않습니다.

## Development Environment

| Item | Environment |
|---|---|
| Board / OS | Raspberry Pi 4 / Debian GNU/Linux 13, 64-bit |
| Camera | USB Webcam, OpenCV V4L2, 640×480 (30 FPS 요청) |
| Language / Build | C++17 / CMake |
| Inference | OpenCV 4.10.0 DNN CPU / YOLO26n ONNX, 640×640 input |
| JSON / Network | nlohmann/json / TCP, `std::thread` |

대상 COCO Class는 `car(2)`, `motorcycle(3)`, `bus(5)`, `truck(7)`입니다.

## Performance

Raspberry Pi 4 CPU 환경에서 측정한 결과입니다. 장면과 실행 조건에 따라 값은 달라질 수 있습니다.

| Metric | Result |
|---|---:|
| Camera Frame Grab | 약 21.6 FPS |
| Effective FPS | 약 2.2–2.34 FPS |
| Inference Latency | 약 405–425 ms |

## Build and Run

### Requirements

- Raspberry Pi 4 / 64-bit Linux
- C++17 compiler
- CMake
- OpenCV with DNN, V4L2 and HighGUI support
- nlohmann/json
- YOLO26n ONNX model

The ONNX model is not included in the repository.  
For development, place it at:

`models/yolo26n.onnx`

### Development Build

From the repository root:

```bash
cmake -S . -B build
cmake --build build -j

**운영 배포:** 모델을 `/opt/edge_vision/models/yolo26n.onnx`에 배치하고, `edgevision` 계정이 `/var/lib/edge_vision/boot_id.dat`에 쓸 수 있도록 준비합니다. 운영 경로를 지정해 빌드한 뒤 실행 파일을 배치합니다.

```bash
cmake -S . -B build-deploy \
  -DMODEL_PATH=/opt/edge_vision/models/yolo26n.onnx \
  -DBOOT_ID_PATH=/var/lib/edge_vision/boot_id.dat
cmake --build build-deploy -j
sudo install -m 755 build-deploy/bin/edge_vision /opt/edge_vision/bin/edge_vision
./pirun <server_ip> <server_port>
```

Repository 루트의 `pirun`은 `sudo -u edgevision /opt/edge_vision/bin/edge_vision`을 호출해 서버 IP와 Port를 전달합니다. 서버 실행 방법은 [Relay Server README](https://github.com/triton0305/edge-vision-relay-server)를 참고하세요.

## Project Structure

```text
.
├── include/
│   ├── core/
│   ├── vision/
│   ├── protocol/
│   └── network/
├── src/
│   ├── core/
│   ├── vision/
│   ├── protocol/
│   ├── network/
│   └── main.cpp
├── test/
├── CMakeLists.txt
└── pirun
```

- `vision`: Camera, 전처리, 추론, 후처리
- `protocol`: JSON 직렬화와 송신 메시지 형식
- `network`: Message Queue, TCP, ACK, Retry, 재연결
- `core`: 설정, Boot ID, Message ID, Metrics
- `main.cpp`: 컴포넌트 초기화와 실행·종료 흐름

## Scope and Data Semantics

- Detection이 없는 프레임은 `vision` 메시지를 생성하지 않습니다.
- 동일 차량이 여러 추론 프레임에서 반복 탐지되면 각각 별도의 Detection 이력으로 남습니다.
- Detection 건수는 고유 차량 대수나 기준선 통과량을 의미하지 않습니다.
- 시간 구간별 통계는 서버가 저장된 `timestamp_ms`를 기준으로 산출해야 합니다. 재전송이나 네트워크 지연이 발생해도 서버 수신 시각을 집계 기준으로 사용하지 않습니다.
- Tracking, Line Crossing, `traffic_count`, `traffic_state`는 현재 Runtime에 포함되지 않습니다.

개발 중 Tracking과 Line Crossing 기반 Traffic Counting을 구현·검증했으나, 최종 Runtime은 객체별 Detection 이력 전달에 집중합니다. 관련 소스 일부는 저장소에 남아 있지만 실행 경로에서 호출하지 않습니다.

## Integration Verification

USB Webcam → Raspberry Pi Client → TCP → Windows/WSL Relay Server → SQLite → ACK → Raspberry Pi 흐름을 실제로 검증했습니다. 최종 Vision-only 테스트(Boot ID 59)에서 `detections`의 vision row 407건과 신규 `traffic_count` row 0건을 확인했습니다. Relay Server에서 동일 `message_id` 재전송 시 중복 행을 만들지 않는 동작과 Error ACK도 별도로 검증했습니다.

## Future Extensions

- systemd 자동 실행 및 장애 시 재시작
- Camera 장애 복구와 장시간 실행 테스트
- 장기 네트워크 장애 시 Queue/Drop 운영 정책 보완
- 서버 측 Detection 통계 조회, 시각화 및 보관 정책
