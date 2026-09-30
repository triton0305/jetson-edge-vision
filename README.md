# Jetson Edge Vision

[Raspberry Pi Edge Vision](https://github.com/triton0305/raspberry-pi-edge-vision)을 Jetson Nano 환경으로 확장한 C++17 차량 인지 Client입니다. OpenCV DNN의 CPU 추론을 TensorRT FP16 기반 GPU 추론으로 전환했습니다.

USB Webcam에서 차량을 탐지하고, 객체별 `vision` JSON을 별도 네트워크 스레드에서 상대 서버로 전달합니다. [Relay Server](https://github.com/triton0305/edge-vision-relay-server)는 배포 편의를 위해 직접 만든 보조 서버입니다.

## Key Features

- `car`, `motorcycle`, `bus`, `truck` 탐지 및 Class-aware NMS
- YOLO26n TensorRT engine 기반 GPU 추론
- Detection 1개당 `vision` JSON 및 `message_id` 생성
- Message Queue / Network Worker 기반 영상 처리·송신 분리
- 4-byte big-endian length-prefix, Partial read/write, ACK / Retry / Reconnect
- 실시간 탐지 화면 및 FPS·추론 시간·Queue·Drop 측정
- `/opt`, `/var/lib` 기반 운영 배포

## Changes from Raspberry Pi

| 항목 | Raspberry Pi Edge Vision | Jetson Edge Vision |
|---|---|---|
| 장비 | Raspberry Pi 4 | NVIDIA Jetson Nano |
| 추론 | OpenCV DNN / CPU | TensorRT / CUDA GPU |
| 모델 | YOLO26n ONNX | YOLO26n FP16 engine |
| Detector | OpenCV DNN 모델 로딩·추론 | TensorRT engine·CUDA 버퍼 관리 |
| 카메라 | V4L2 | V4L2 / YUYV 명시 |
| 이미지 저장 | 최초 탐지 스냅샷 | 실시간 표시만 수행 |
| 잔존 코드 | 미사용 Tracker·traffic_count 소스 잔존 | 관련 소스 및 전용 전달 정책 제거 |

Letterbox, 차량 필터링, Class-aware NMS와 기존 `vision` 프로토콜·ACK/Retry 구조는 유지했습니다.

## Performance

동일 장소·촬영 환경에서 Raspberry Pi와 Jetson Nano의 실행 성능을 비교했습니다.

| 지표 | Raspberry Pi 4 | Jetson Nano |
|---|---|---|
| 추론 방식 | OpenCV DNN / CPU | TensorRT FP16 / GPU |
| Effective FPS | 약 2.2–2.3 FPS | 약 7.4 FPS |
| 평균 추론 시간 | 약 410–490 ms | 약 54.5 ms |

기존 Raspberry Pi 측정값 대비 처리 FPS는 약 3.2–3.4배 증가했고, 추론 시간은 약 87–89% 감소했습니다.

Effective FPS는 영상 처리 속도이며 서버 전달 처리량과 구분합니다. 두 측정은 동일 장소의 실시간 카메라 영상을 사용했습니다.

## Architecture

| Stage | Flow |
|---|---|
| **① Vision Loop** | USB Webcam / V4L2 → Letterbox 640×640 → TensorRT → Class-aware NMS |
| **② Message Generation** | Detection → 객체별 vision JSON → Message Queue |
| **③ Delivery** | Network Worker → TCP → 상대 서버 → ACK / Retry |

| Component | Responsibility |
|---|---|
| **Vision Client** | 프레임 획득, 전처리·추론·후처리, Detection 생성 및 전송 |
| **수신 서버** | 수신 데이터 저장, 중복 처리 및 후처리 |
| **보조 Relay Server** | JSON 검증, SQLite 저장 및 ACK 반환 |

## Message Protocol

TCP 메시지와 ACK는 `4-byte big-endian payload length + JSON` 형식입니다.

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
    "bbox": {"x": 131, "y": 328, "width": 85, "height": 70}
  }
}
```

```json
{"version":1,"type":"ack","message_id":"vision-pi-01-000059-00000001","status":"ok"}
```

| 필드 | 기준 |
|---|---|
| message_id | device_id + 실행마다 증가하는 영속 boot_id + 객체별 sequence |
| frame_id | 실행 내 프레임 번호, 0부터 시작 |
| timestamp_ms | 프레임 읽기 완료 직후 Unix ms |
| bbox | 원본 프레임 좌측 상단 기준 픽셀 좌표·크기 |

현재 device_id는 `vision-pi-01`입니다. 여러 장비 운영 시 장비별 ID를 구분하고 기존 boot_id를 초기화하지 않습니다.

## Reliability and Network Behavior

| 항목 | 동작 |
|---|---|
| 수신 timeout | 1500ms / 블로킹 수신 호출 기준 |
| 전달 시도 | 메시지당 최대 3회, 연결 실패 포함 |
| 재연결 실패 | 1000ms 대기 |
| 재전송 | 동일 message_id / payload 유지 |
| Queue | 대기 최대 16개, 초과 시 가장 오래된 메시지 Drop |
| 재시도 초과 | 해당 메시지 Drop |
| 연결·수신 실패 | Vision Loop 계속 실행 |
| 잘못된 ACK / Error ACK | Network Worker 실패 및 Vision Loop 종료 |

Error ACK는 `status=error`와 `error_code`가 필요합니다. 중복 저장 방지는 수신 서버에서 구현합니다.

현재 전달은 best-effort이며 종료 시 Queue drain은 수행하지 않습니다. 연결·송신의 별도 timeout은 없고, 진행 중인 호출은 종료를 지연시킬 수 있습니다. Metrics의 Dropped는 Queue 초과 건수입니다.

## Build

### Requirements

- C++17 compiler / CMake 3.16 이상
- OpenCV: core, imgproc, highgui, videoio, dnn
- CUDA / TensorRT: 현재 binding 기반 `enqueueV2`·`destroy` API 지원 환경
- nlohmann/json
- 대상 Jetson 환경에 호환되는 `models/yolo26n_fp16.engine`

Engine은 별도로 준비합니다. 현재 Detector가 사용하는 I/O 규격은 다음과 같습니다.

| Binding | Shape | Type |
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
./build/bin/edge_vision <server_ip> <server_port>
```

| 항목 | 개발 경로 |
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

| 항목 | 운영 경로 |
|---|---|
| Binary | /opt/edge_vision/bin/edge_vision |
| Model | /opt/edge_vision/models/yolo26n_fp16.engine |
| Boot ID | /var/lib/edge_vision/boot_id.dat |
| XAUTHORITY | /home/edgevision/.Xauthority |

설치 명령은 바이너리만 배치합니다. 모델·계정·상태 디렉터리는 별도로 준비합니다.

### Production Run

저장소 루트에서 실행합니다.

```bash
./pirun <server_ip> <server_port>
```

`pirun`은 운영 바이너리를 edgevision 계정으로 실행하며 DISPLAY(기본 `:1`)와 XAUTHORITY를 전달합니다. TigerVNC `:1`을 지정하려면 다음과 같이 실행합니다.

```bash
DISPLAY=:1 ./pirun <server_ip> <server_port>
```

## Project Structure

| 경로 | 역할 |
|---|---|
| include/ · src/core/ | 설정, Boot ID, Message ID, Metrics |
| include/ · src/vision/ | Camera, 전처리, TensorRT 추론, 후처리 |
| include/ · src/protocol/ | JSON 직렬화 및 메시지 형식 |
| include/ · src/network/ | Queue, TCP, ACK, Retry / Reconnect |
| src/main.cpp | 초기화 및 Runtime 관리 |
| test/ | 원본 프로젝트의 수동 검증 이미지 |

## Scope and Data Semantics

- 카메라는 YUYV 640×480@30을 요청하며 실제 처리 FPS와 구분합니다.
- Confidence threshold는 0.25, NMS IoU threshold는 0.45입니다.
- 탐지가 없는 프레임은 메시지를 생성하지 않습니다.
- 동일 차량의 반복 탐지는 별도 이력이며 고유 차량 수·통과 교통량을 의미하지 않습니다.
- Tracking, Line Crossing, 통계 메시지, 녹화·스냅샷 저장은 Runtime에 포함하지 않습니다.
- 시간 구간별 집계와 데이터 보관 정책은 수신 서버의 후처리 영역입니다.

## Integration Check

1. 상대 서버 또는 보조 Relay Server 실행
2. Jetson에서 실제 서버 IPv4와 포트로 pirun 실행
3. 실시간 탐지 화면과 ACK OK 확인
4. 서버 수신·처리 결과 확인

보조 Relay Server를 사용하는 경우 SQLite 저장도 확인합니다.

## Related Projects

- [Raspberry Pi Edge Vision](https://github.com/triton0305/raspberry-pi-edge-vision): CPU 추론 기반 원본 프로젝트
- [Edge Vision Relay Server](https://github.com/triton0305/edge-vision-relay-server): 배포 편의를 위한 보조 서버
