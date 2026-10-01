# Jetson Edge Vision

[Raspberry Pi Edge Vision](https://github.com/triton0305/raspberry-pi-edge-vision)을 Jetson Nano 환경으로 확장한 C++17 차량 인지 Client입니다. OpenCV DNN의 CPU 추론을 TensorRT FP16 기반 GPU 추론으로 전환했습니다.

USB Webcam에서 차량을 탐지하고, 객체별 `vision` JSON을 Raspberry Pi Gateway로 전달합니다. Gateway는 Vision을 WSL Final Server로 전달하고, downstream 상태를 Control로 Jetson에 전달합니다.

## System Repositories

전체 시스템은 Jetson Vision Client, Raspberry Pi Gateway, WSL Final Server로 구성됩니다.

- **Jetson Edge Vision** — TensorRT 기반 차량 Detection 및 Vision 전송
- [Raspberry Pi Edge Vision Gateway](https://github.com/triton0305/raspberry-pi-edge-vision-gateway) — Vision forwarding 및 downstream Control 전달
- [Jetson Edge Vision Relay Server](https://github.com/triton0305/jetson-edge-vision-relay-server) — Vision 수신·SQLite 저장 및 DB 상태 Control 생성

## Key Features

- `car`, `motorcycle`, `bus`, `truck` 탐지 및 Class-aware NMS
- YOLO26n TensorRT engine 기반 GPU 추론
- Detection 1개당 `vision` JSON 및 `message_id` 생성
- Message Queue / Network Worker 기반 영상 처리·송신 분리
- 4-byte big-endian length-prefix, Partial read/write, full-duplex Control / Reconnect
- 실시간 탐지 화면 및 FPS·추론 시간·Queue·Drop 측정
- `/opt`, `/var/lib` 기반 운영 배포 구성

## Changes from Raspberry Pi

| **항목** | **Raspberry Pi Edge Vision** | **Jetson Edge Vision** |
|---|---|---|
| 장비 | Raspberry Pi 4 | NVIDIA Jetson Nano |
| 추론 | OpenCV DNN / CPU | TensorRT / CUDA GPU |
| 모델 | YOLO26n ONNX | YOLO26n FP16 engine |
| Detector | OpenCV DNN 모델 로딩·추론 | TensorRT engine·CUDA 버퍼 관리 |
| 카메라 | V4L2 | V4L2 / YUYV 명시 |
| 이미지 저장 | 최초 탐지 스냅샷 | 실시간 표시만 수행 |
| 잔존 코드 | 미사용 Tracker·traffic_count 소스 잔존 | 관련 소스 및 전용 전달 정책 제거 |

Letterbox, 차량 필터링, Class-aware NMS와 기존 `vision` JSON 의미는 유지합니다. Vision의 application ACK/Retry는 제거했습니다.

## Performance

동일 장소·촬영 환경에서 Raspberry Pi와 Jetson Nano의 실행 성능을 비교했습니다.

| **지표** | **Raspberry Pi 4** | **Jetson Nano** |
|---|---|---|
| 추론 방식 | OpenCV DNN / CPU | TensorRT FP16 / GPU |
| Effective FPS | 약 2.2–2.3 FPS | 약 7.4 FPS |
| 평균 추론 시간 | 약 410–490 ms | 약 54.5 ms |

기존 Raspberry Pi 측정값 대비 처리 FPS는 약 3.2–3.4배 증가했고, 추론 시간은 약 87–89% 감소했습니다.

Effective FPS는 영상 처리 속도이며 서버 전달 처리량과 구분합니다. 두 측정은 동일 장소의 실시간 카메라 영상을 사용했습니다.

## Architecture

| **Stage** | **Flow** |
|---|---|
| **① Vision Loop** | USB Webcam / V4L2 → Letterbox 640×640 → TensorRT → Class-aware NMS |
| **② Message Generation** | Detection → 객체별 vision JSON → Message Queue |
| **③ Delivery** | Data TX → Pi Gateway → WSL Final Server |

| **Component** | **Responsibility** |
|---|---|
| **Vision Client** | 프레임 획득, 전처리·추론·후처리, Detection 생성 및 전송 |
| **Pi Gateway** | Vision 전달, downstream Control 전달 |
| **WSL Final Server** | 최종 데이터 처리 및 SQLite 저장 |

현재 검증 환경에서는 Jetson → Pi Gateway 구간에 TCP 8000, Pi Gateway → WSL Final Server 구간에 TCP 9000을 사용했습니다. 포트 번호는 고정 프로토콜 요구사항이 아니며 실행·배포 환경에 맞게 지정할 수 있습니다.

## Message Protocol

Vision과 Control은 `4-byte big-endian payload length + JSON` 형식입니다.

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
    "confidence": 0.85,
    "bbox": {
      "x": 131,
      "y": 328,
      "width": 85,
      "height": 70
    }
  }
}
```

```json
{
  "version": 1,
  "type": "control",
  "device_id": "gateway",
  "message_id": "control-1",
  "data": {
    "timestamp_ms": 1790580489875,
    "action": "resume",
    "reason": "wsl_connection_restored"
  }
}
```

| **필드** | **기준** |
|---|---|
| message_id | device_id + 실행마다 증가하는 영속 boot_id + 객체별 sequence |
| frame_id | 실행 내 프레임 번호, 0부터 시작 |
| timestamp_ms | 프레임 읽기 완료 직후 Unix ms |
| bbox | 원본 프레임 좌측 상단 기준 픽셀 좌표·크기 |

현재 `device_id`는 기존 Raspberry Pi 구현과의 메시지 호환성을 유지하기 위해 `vision-pi-01`을 사용합니다. 여러 장비 운영 시 장비별 ID를 구분하고 기존 boot_id를 초기화하지 않습니다.

## Network / Control

Jetson은 Pi Gateway와 TCP로 연결하고, Pi Gateway는 WSL Final Server와 별도 TCP 연결을 유지합니다. 현재 실환경 검증에서는 각각 8000과 9000을 사용했습니다.

하나의 Jetson↔Pi 연결에서 Data TX가 Vision을 보내고 Control RX가 Control을 받습니다. Control RX가 연결과 재연결을 관리하므로 Queue가 비어 있거나 PAUSED여도 재연결합니다.

- 시작과 재연결 직후 PAUSED. 현재 세션의 `resume` Control을 받아야 RUNNING입니다.
- `pause`는 Camera, TensorRT, Detection, Display를 유지하며 JSON/ID 생성과 enqueue를 차단하고 Queue를 비웁니다.
- 상태 변경 번호로 PAUSE/RESUME을 가로지른 프레임과 이전 대기 메시지를 폐기합니다.
- 송수신 실패는 `pi_connection_lost`, 연결 성공은 `pi_connection_restored`로 표시합니다.
- 전달받은 downstream reason은 그대로 표시합니다. 실패한 Vision을 재전송하지 않습니다.
- 이미 송신을 시작했거나 TCP 버퍼에 들어간 바이트는 PAUSE로 회수할 수 없습니다. 새 송신을 차단하고 복구 시 replay하지 않습니다.
- Queue 최대 16개, overflow 시 가장 오래된 메시지를 폐기합니다.
- 연결 시도 제한 1초, 재시도 간격 1초. 송신 prefix/payload 각각 최대 1초입니다.
- Control idle timeout은 없습니다. Linux TCP keepalive(10초 idle, 3초 interval, 3 probes)와 TCP_USER_TIMEOUT(20초)을 설정합니다. 실제 장애 검출 시간은 커널·네트워크 상태에 따릅니다.
- 종료 시 Queue를 닫고 socket shutdown으로 blocking I/O를 깨운 뒤 join합니다.

Control은 version 1 envelope와 문자열 device_id/message_id, 정수 timestamp_ms, 문자열 action/reason을 요구합니다. action은 소문자 `pause` 또는 `resume`입니다.

잘못된 JSON/Control은 무시하며, 길이 0 또는 1 MiB 초과 framing은 연결을 종료합니다.

Metrics는 FPS, Inference, Produced/Sent msg/s, Queue depth, Overflow drop, PAUSE discard, Network State, Pi Link, Pause Reason, Reconnect count를 출력합니다.

최초 연결은 reconnect count에 포함하지 않습니다. Discard에는 장애 전환 때 버린 pending 메시지와 이미 pop한 전송 불가 메시지를 포함합니다.

## Build

### Requirements

- C++17 compiler / CMake 3.16 이상
- OpenCV: core, imgproc, highgui, videoio, dnn
- CUDA / TensorRT: 현재 binding 기반 `enqueueV2`·`destroy` API 지원 환경
- nlohmann/json
- 대상 Jetson 환경에 호환되는 `models/yolo26n_fp16.engine`

Engine은 별도로 준비합니다. 현재 Detector가 사용하는 I/O 규격은 다음과 같습니다.

| **Binding** | **Shape** | **Type** |
|---|---|---|
| images | 1×3×640×640 | FP32 |
| output0 | 1×84×8400 | FP32 |

FP16은 engine 내부 연산 정밀도입니다. 코드가 engine의 실제 shape·type을 검증하지 않으므로 위 규격에 맞는 engine을 사용해야 합니다.

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j2
```

### Development Run

```bash
./build/bin/edge_vision <pi_gateway_ip> <pi_gateway_port>
```

| **항목** | **개발 경로** |
|---|---|
| Model | models/yolo26n_fp16.engine |
| Boot ID | boot_id.dat |

모델과 boot_id는 Git에 포함하지 않습니다. boot_id가 없으면 생성하므로 부모 디렉터리에 쓰기 권한이 필요합니다. 실시간 화면 표시를 위한 GUI / X 인증 환경이 필요하며 Esc 또는 Ctrl+C로 종료합니다.

## Deployment

운영 계정 `edgevision`의 video 그룹, 모델 읽기 권한, boot_id 파일·디렉터리 쓰기 권한과 TigerVNC / X 인증 환경을 준비합니다.

```bash
cmake -S . -B build-deploy -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_INSTALL_PREFIX=/opt/edge_vision \
  -DMODEL_PATH=/opt/edge_vision/models/yolo26n_fp16.engine \
  -DBOOT_ID_PATH=/var/lib/edge_vision/boot_id.dat

cmake --build build-deploy -j2
sudo cmake --install build-deploy
```

| **항목** | **운영 경로** |
|---|---|
| Binary | /opt/edge_vision/bin/edge_vision |
| Model | /opt/edge_vision/models/yolo26n_fp16.engine |
| Boot ID | /var/lib/edge_vision/boot_id.dat |
| XAUTHORITY | /home/edgevision/.Xauthority |

설치 명령은 바이너리만 배치합니다. 모델·계정·상태 디렉터리는 별도로 준비합니다.

### Production Run

저장소 루트에서 실행합니다.

```bash
./pirun <pi_gateway_ip> <pi_gateway_port>
```

`pirun`은 운영 바이너리를 edgevision 계정으로 실행하며 DISPLAY(기본 `:1`)와 XAUTHORITY를 전달합니다. TigerVNC `:1`을 지정하려면 다음과 같이 실행합니다.

```bash
DISPLAY=:1 ./pirun <pi_gateway_ip> <pi_gateway_port>
```

## Project Structure

| **경로** | **역할** |
|---|---|
| include/ · src/core/ | 설정, Boot ID, Message ID, Runtime State, Metrics |
| include/ · src/vision/ | Camera, 전처리, TensorRT 추론, 후처리 |
| include/ · src/protocol/ | JSON 직렬화 및 메시지 형식 |
| include/ · src/network/ | Queue, TCP, Control RX, Data TX, Reconnect |
| src/main.cpp | 초기화 및 Runtime 관리 |
| tests/ | Network / Transport 자동 검증 |
| test/ | 원본 프로젝트의 수동 검증 이미지 |

## Scope and Data Semantics

- 카메라는 YUYV 640×480@30을 요청하며 실제 처리 FPS와 구분합니다.
- Confidence threshold는 0.25, NMS IoU threshold는 0.45입니다.
- 탐지가 없는 프레임은 메시지를 생성하지 않습니다.
- 동일 차량의 반복 탐지는 별도 이력이며 고유 차량 수·통과 교통량을 의미하지 않습니다.
- Tracking, Line Crossing, 통계 메시지, 녹화·스냅샷 저장은 Runtime에 포함하지 않습니다.
- 시간 구간별 집계와 데이터 보관 정책은 수신 서버의 후처리 영역입니다.

## Tests and Integration

```bash
cmake --build build -j2
(cd build && ctest --output-on-failure)
./build/bin/edge_vision <pi_gateway_ip> 8000
```

`network_integration_test`는 실제 localhost TCP socket으로 ACK 없는 송신, 분할 Control, PAUSE/RESUME, 초기 접속 실패 후 재시도, 새 세션 동기화, stale 데이터 차단, Queue 경쟁, blocking receive 종료를 검증합니다.

`network_transport_test`는 Partial write, EINTR/EAGAIN, 송신 deadline, 동시 TX/RX 및 reconnect 등 TCP transport 동작을 검증합니다.

Jetson → Raspberry Pi Gateway → WSL Final Server → SQLite 전체 경로와 PAUSE/RESUME, DB 장애·복구, Jetson↔Pi 연결 장애·복구를 실환경에서 검증했습니다.

상세 검증 범위와 미검증 항목은 [docs/network-validation.md](docs/network-validation.md)를 참고합니다.

## Related Repositories

연동 구성요소의 구현과 실행 방법은 각 저장소를 참고합니다.

- [Raspberry Pi Edge Vision Gateway](https://github.com/triton0305/raspberry-pi-edge-vision-gateway): Raspberry Pi에서 Jetson의 Vision을 WSL로 전달하고, downstream 상태를 Control로 Jetson에 전달하는 Gateway입니다.
- [Jetson Edge Vision Relay Server](https://github.com/triton0305/jetson-edge-vision-relay-server): WSL에서 Vision을 수신해 SQLite에 저장하고, DB 장애·복구 상태를 Control로 Gateway에 전달하는 최종 서버입니다.
- [Raspberry Pi Edge Vision](https://github.com/triton0305/raspberry-pi-edge-vision): 이 Jetson Client의 기반이 된 CPU 추론 원본 프로젝트입니다.
