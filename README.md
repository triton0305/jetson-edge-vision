<p align="center">
  <img src="docs/assets/header.svg" alt="Jetson Edge Vision — TensorRT FP16 vehicle perception and TCP delivery" width="100%">
</p>

<p align="center">
  <img src="https://img.shields.io/badge/C%2B%2B-17-00599C?style=flat-square" alt="C++17">
  <img src="https://img.shields.io/badge/NVIDIA-Jetson_Nano-76B900?style=flat-square" alt="NVIDIA Jetson Nano">
  <img src="https://img.shields.io/badge/TensorRT-FP16-087F8C?style=flat-square" alt="TensorRT FP16">
  <img src="https://img.shields.io/badge/CUDA-GPU-76B900?style=flat-square" alt="CUDA GPU">
  <img src="https://img.shields.io/badge/OpenCV-V4L2-5C3EE8?style=flat-square" alt="OpenCV V4L2">
</p>

<p align="center">
  <a href="#demo">Demo</a> · <a href="#performance">Performance</a> · <a href="#architecture">Architecture</a> · <a href="#validation">Validation</a> · <a href="#build">Build</a> · <a href="docs/jetson-portfolio.pdf">Portfolio</a>
</p>

# Jetson Edge Vision

**USB Webcam의 차량을 인지하고, 객체별 JSON을 Raspberry Pi Gateway로 전달하는 C++17 Vision Client입니다.**

[Raspberry Pi Edge Vision](https://github.com/triton0305/raspberry-pi-edge-vision)의 OpenCV DNN / CPU 추론을 **TensorRT FP16 / CUDA GPU 추론**으로 전환했습니다. Vision 연속 송신과 Control 수신을 분리하고, 서버 장애 시 PAUSE·복구 시 RESUME으로 전송 상태를 제어합니다.

**기본 시스템 개발:** 2026.09.30–10.01 · **Tracking Stage 1 추가 검증:** 2026.10.02

## Demo

<p align="center">
  <img src="docs/assets/vehicle-detection.jpg" alt="Jetson Nano에서 TensorRT FP16으로 실제 도로의 차량을 탐지하는 실행 화면" width="780">
</p>
<p align="center"><sub>2026.10.01 실제 USB Webcam / TensorRT 실행 화면 · Tracking 추가 전 검증 자료</sub></p>

| 영상 처리 | GPU 추론 | Pi 대비 처리 속도 |
|:---:|:---:|:---:|
| **약 12 FPS** | **약 55 ms** | **약 5.2–5.5배** |
| Effective FPS | 평균 TensorRT 추론 시간 | 기존 CPU 실행 로그 대비 |

> 동일 장소의 실시간 영상으로 측정했으며 차량 수와 장면은 서로 다릅니다. 아래 Performance에서 측정 범위를 확인할 수 있습니다.

## Performance

동일 장소의 실시간 카메라 영상으로 Raspberry Pi와 Jetson Nano의 실행 성능을 측정했습니다.

| 지표 | Raspberry Pi 4 | Jetson Nano |
|---|---|---|
| 추론 방식 | OpenCV DNN / CPU | TensorRT FP16 / GPU |
| Effective FPS | 약 2.2–2.3 FPS | 약 11.9–12.0 FPS |
| 평균 추론 시간 | 약 410–490 ms | 약 54.5–54.6 ms |

측정값 기준 영상 처리 속도는 약 5.2–5.5배 증가했고, 평균 추론 시간은 약 87–89% 감소했습니다. 동일 장소에서 측정했으나 실시간 영상의 차량 수와 장면은 서로 다릅니다.

Effective FPS는 영상 처리 속도이며, Produced/Sent Vision msg/s는 객체별 메시지 생성·송신 처리량입니다. 한 프레임에서 여러 차량을 탐지할 수 있으므로 메시지 처리량은 FPS보다 높을 수 있습니다.

<details>
<summary><strong>Tracking 추가 후 측정 — 2026.10.02 / localhost</strong></summary>

Tracking 추가 후 2026-10-02 실제 USB 카메라 / TensorRT / localhost Gateway로 약 91초 실행했습니다. 초반 5개 Metrics 보고 구간을 제외한 72개 구간의 평균은 **12.32 FPS / 54.43 ms inference**였습니다. Produced/Sent 평균은 각각 약 3.033/3.033 msg/s, Queue 0–3, overflow 0이었고, Gateway에서 Vision 244개를 수신했습니다. 차량 수와 장면이 이전 측정과 달라 직접적인 속도 향상이나 장시간 안정성을 입증하는 비교는 아닙니다. Avg Inference는 기존 TensorRT 구간만 측정하며 Tracker 처리 시간을 포함하지 않습니다.

</details>

## Hardware Setup

<table>
<tr>
<td width="48%"><img src="docs/assets/hardware-setup.jpg" alt="창가의 USB Webcam과 Jetson Nano 실제 촬영 환경" width="440"></td>
<td width="52%">
<strong>실제 장비에서 시작한 CPU → GPU 확장</strong><br><br>
• NVIDIA Jetson Nano + USB Webcam<br>
• V4L2 / YUYV 640 × 480 @ 30 요청<br>
• 640 × 640 Letterbox + YOLO26n FP16<br>
• Raspberry Pi Gateway → WSL Relay → SQLite<br><br>
Pi에서 확인한 처리 속도 한계를 GPU 추론으로 개선하고, 독립 검증용 Gateway·Relay Server를 구성했습니다.
</td>
</tr>
</table>

사진은 포트폴리오의 실제 설치 환경입니다. 카메라 요청 FPS와 영상 처리 FPS는 구분합니다.

## Key Features

| 영역 | 구현 내용 |
|---|---|
| **Perception** | `car`, `motorcycle`, `bus`, `truck` · TensorRT FP16 · Class-aware NMS |
| **Tracking / Display** | 내부 차량 ID · class / confidence · FPS / Inference / Active Tracks 표시 |
| **Delivery** | Detection 1개당 JSON 1개 · Message Queue · Data TX / Control RX 분리 |
| **Recovery** | PAUSE 시 대기 큐 폐기 · 재연결 후 세션 동기화 · RESUME 후 새 결과부터 송신 |
| **Operations** | Queue / Drop / Link / Reconnect 지표 · `/opt`, `/var/lib` 운영 배포 |

`track_id`는 Jetson 내부 표시용입니다. 전송 JSON은 객체별 Detection 형식을 유지하며 고유 차량 수·통과 교통량을 의미하지 않습니다.

## Changes from Raspberry Pi

<details>
<summary><strong>CPU → GPU 전환과 전송 정책 변경 보기</strong></summary>

| **항목** | **Raspberry Pi Edge Vision** | **Jetson Edge Vision** |
|---|---|---|
| 장비 | Raspberry Pi 4 | NVIDIA Jetson Nano |
| 추론 | OpenCV DNN / CPU | TensorRT / CUDA GPU |
| 모델 | YOLO26n ONNX | YOLO26n FP16 engine |
| Detector | OpenCV DNN 모델 로딩·추론 | TensorRT engine·CUDA 버퍼 관리 |
| 카메라 | V4L2 | V4L2 / YUYV 명시 |
| 이미지 저장 | 최초 탐지 스냅샷 | 실시간 표시만 수행 |
| Tracking | 미사용 Tracker 소스 잔존 | Detection 기반 내부 Tracker 실행, 전송 정책은 유지 |
| 교통량 집계 | traffic_count 소스 잔존 | Line Crossing·traffic_count 미구현 |

Letterbox, 차량 필터링, Class-aware NMS와 기존 `vision` JSON 의미는 유지합니다. Vision의 application ACK/Retry는 제거했습니다.

</details>

## Architecture

```mermaid
flowchart TD
    subgraph Vision["Vision loop · PAUSE 중에도 유지"]
        A["USB Webcam / V4L2"] --> B["Letterbox → TensorRT FP16"]
        B --> C["Vehicle filter → NMS → Tracker"]
        C --> D["Display"]
    end
    C --> E{"RUNNING?"}
    E -->|Yes| F["Detection → JSON → Queue"]
    F --> G["Data TX"]
    G -->|Vision| P["Raspberry Pi Gateway"]
    P -->|Vision| S["WSL Relay / SQLite"]
    S -.->|PAUSE / RESUME| P
    P -.->|Control| R["Control RX / Runtime state"]
    R -.->|전송 상태 제어| E
```

| **Stage** | **Flow** |
|---|---|
| **① Vision Loop** | USB Webcam / V4L2 → Letterbox 640×640 → TensorRT → Class-aware NMS → Tracker → Display |
| **② Message Generation** | TrackedDetection의 원본 Detection → 객체별 vision JSON → Message Queue |
| **③ Delivery** | Data TX → Pi Gateway → WSL Final Server |

| **Component** | **Responsibility** |
|---|---|
| **Vision Client** | 프레임 획득, 전처리·추론·후처리, 내부 Tracking·표시, Detection 생성 및 전송 |
| **Pi Gateway** | Vision 전달, downstream Control 전달 |
| **WSL Final Server** | 최종 데이터 처리 및 SQLite 저장 |

현재 검증 환경에서는 Jetson → Pi Gateway 구간에 TCP 8000, Pi Gateway → WSL Final Server 구간에 TCP 9000을 사용했습니다. 포트 번호는 고정 프로토콜 요구사항이 아니며 실행·배포 환경에 맞게 지정할 수 있습니다.

## Network / Control

| 상태 | 영상 처리·Tracking·표시 | JSON 생성·전송 | 대기 데이터 |
|---|---|---|---|
| **RUNNING** | 유지 | 새 Detection을 연속 송신 | 최대 16개 Queue |
| **PAUSED** | 유지 | JSON/ID 생성·enqueue 차단 | Queue 폐기 |
| **재연결 직후** | 유지 | 현재 세션의 `resume`까지 대기 | 과거 데이터 replay 없음 |

DB 장애는 WSL → Pi → Jetson으로 PAUSE를 전달하며, 복구 후 새 결과부터 송신합니다. 이미 송신을 시작했거나 TCP 버퍼에 들어간 바이트는 회수할 수 없습니다.

<details>
<summary><strong>연결·재시도·Timeout·Control 검증·Metrics 상세</strong></summary>

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

</details>

## Validation

| 검증 단계 | 환경 | 확인 결과 |
|---|---|---|
| **기본 Network / Control** | Jetson → 실제 Pi → WSL → SQLite | 전체 저장 경로, DB 장애·복구 PAUSE/RESUME, Jetson↔Pi 연결 장애·복구 검증 |
| **Tracking Stage 1 자동 검증** | Tracker / Network / Transport 테스트 | 빌드 및 테스트 3개 통과 기록 |
| **Tracking Stage 1 실행** | 실제 USB 카메라 + TensorRT + localhost Gateway | 약 91초 실행, Vision 244개 수신, 기존 JSON 유지, 중복 message_id 없음 |
| **Tracking 버전 외부 E2E** | 실제 Pi / WSL / SQLite | **미검증** — 해당 실행에서 Pi Gateway 접속 불가 |

Tracking의 밀집·교차·순간 미탐 상황에서 ID 유지 품질은 별도 실영상 관찰이 필요합니다. 기본 시스템의 검증 결과와 Tracking 추가 후 검증 범위를 구분합니다.

상세 기록: **[Network validation](docs/network-validation.md)**

<details>
<summary><strong>테스트 실행 방법과 검증 항목</strong></summary>

```bash
cmake --build build -j2
(cd build && ctest --output-on-failure)
./build/bin/edge_vision <pi_gateway_ip> 8000
```

`tracker_test`는 합성 monotonic time으로 ID 유지, same-class / one-to-one matching, IoU·중심 거리 경계, 빈 Detection, 800 ms 만료, 삭제 ID 재사용 금지, 입력 순서·Detection 보존 및 Tracking 전후 JSON 일치를 검증합니다.

`network_integration_test`는 실제 localhost TCP socket으로 ACK 없는 송신, 분할 Control, PAUSE/RESUME, 초기 접속 실패 후 재시도, 새 세션 동기화, stale 데이터 차단, Queue 경쟁, blocking receive 종료를 검증합니다.

`network_transport_test`는 Partial write, EINTR/EAGAIN, 송신 deadline, 동시 TX/RX 및 reconnect 등 TCP transport 동작을 검증합니다.

Jetson → Raspberry Pi Gateway → WSL Final Server → SQLite 전체 경로와 PAUSE/RESUME, DB 장애·복구, Jetson↔Pi 연결 장애·복구를 실환경에서 검증했습니다.

상세 검증 범위와 미검증 항목은 [docs/network-validation.md](docs/network-validation.md)를 참고합니다.

</details>

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

<details>
<summary><strong>운영 계정·설치·실행 방법</strong></summary>

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
./run <pi_gateway_ip> <pi_gateway_port>
```

`run`은 운영 바이너리를 edgevision 계정으로 실행하며 DISPLAY(기본 `:1`)와 XAUTHORITY를 전달합니다. TigerVNC `:1`을 지정하려면 다음과 같이 실행합니다.

```bash
DISPLAY=:1 ./run <pi_gateway_ip> <pi_gateway_port>
```

</details>

## Message Protocol

<details>
<summary><strong>Vision / Control JSON과 필드 정의</strong></summary>

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

</details>

## Vehicle Tracking — Stage 1

<details>
<summary><strong>Matching·Track 수명·표시 정책·검증 한계</strong></summary>

`include/vision/tracker.hpp`와 `src/vision/tracker.cpp`에 Detection 기반 Tracker를 구현했습니다. 과거 Tracker의 greedy association을 참고하여 같은 클래스에서 IoU가 가장 큰 후보를 선택하고, 같은 IoU이면 중심 거리가 가까운 후보를 선택합니다. 완전히 같은 점수는 먼저 생성된 Track을 선택합니다. 입력 Detection 순서대로 처리하며 이미 연결된 Track을 같은 프레임에서 다시 사용하지 않습니다. Matching은 **IoU ≥ 0.10 또는 중심 거리 ≤ 160 px**일 때 허용합니다. 좌표는 후처리가 복원한 원본 카메라 프레임 기준입니다.

Track은 `steady_clock` 기준 마지막 검출에서 **800 ms 이상** 지나면 matching 전에 제거합니다. `missed_frames`는 내부 누락 정보이며 삭제 조건이 아닙니다. ID는 1부터 증가하고 같은 실행 중 삭제된 ID를 재사용하지 않습니다. 프로세스 재시작 시 1부터 다시 시작합니다.

Tracker는 PostProcessor 직후, Display 이전에 동기적으로 실행합니다. PAUSE 중과 네트워크 연결 상태 변화 중에도 계속 갱신하며 state를 reset하지 않습니다. 빈 Detection에서도 cleanup을 수행하고, 현재 Detection과 같은 수·순서의 결과만 반환합니다. 누락 중 보존한 Track의 과거·예측 bbox는 화면이나 Vision으로 출력하지 않습니다. **Active Tracks는 현재 프레임의 TrackedDetection 수**이며 timeout 대기 중인 내부 Track 수와 다릅니다. 화면 FPS·Inference는 기존 Metrics의 마지막 완료된 보고 구간을 사용하므로 첫 보고 전에는 0을 표시합니다.

Tracking 추가 후 전체 빌드와 Tracker / Network / Transport 테스트 3개가 통과했습니다. 실제 카메라 실행의 localhost Vision 244개에서 기존 JSON 필드가 유지됐고 track_id 필드·중복 message_id는 없었습니다. 해당 실행은 실제 Pi / WSL / SQLite 검증이 아닙니다. Pi Gateway TCP 접속이 불가능하여 이번 Tracking 버전의 외부 E2E는 미검증입니다. 기존 Network 검증 결과는 위 문서의 이전 실행 범위에 한정됩니다.

실제 영상의 일반 주행 ID 유지, 순간 미탐 재연결, 차량 진입·이탈, 교차·밀집·빠른 이동·class flicker의 ID switch 여부는 별도 관찰이 필요합니다. 동일 클래스의 greedy bbox association이므로 이 상황들의 안정성을 보장하지 않습니다. 현재 timeout·matching 기준은 초기값이며 실제 관찰 없이 튜닝하지 않았습니다. Tentative / Confirmed / min_hits, new-track-only, RUNNING epoch별 전송 상태와 교통량 집계는 이후 별도 단계입니다.

</details>

## Scope and Data Semantics

- 카메라는 YUYV 640×480@30을 요청하며 실제 처리 FPS와 구분합니다.
- Confidence threshold는 0.25, NMS IoU threshold는 0.45입니다.
- 탐지가 없는 프레임은 메시지를 생성하지 않습니다.
- 동일 차량의 반복 탐지는 별도 이력이며 고유 차량 수·통과 교통량을 의미하지 않습니다.
- track_id는 Jetson 내부 Tracking / Display용이며 Vision JSON과 message_id에 포함하지 않습니다. 같은 track_id도 현재 프레임에서 검출될 때마다 기존 방식으로 Vision을 생성합니다.
- Line Crossing, traffic_count, 고유 차량 수 집계, new-track-only 전송, 서버 track_id 저장, 녹화·스냅샷 저장 기능은 Runtime에 포함하지 않습니다.
- 시간 구간별 집계와 데이터 보관 정책은 수신 서버의 후처리 영역입니다.

## Project Structure

<details>
<summary><strong>소스 디렉터리와 책임</strong></summary>

| **경로** | **역할** |
|---|---|
| include/ · src/core/ | 설정, Boot ID, Message ID, Runtime State, Metrics |
| include/ · src/vision/ | Camera, 전처리, TensorRT 추론, 후처리, Tracker, VisionWorker 표시 |
| include/ · src/protocol/ | JSON 직렬화 및 메시지 형식 |
| include/ · src/network/ | Queue, TCP, Control RX, Data TX, Reconnect |
| src/main.cpp | 초기화 및 Runtime 관리 |
| tests/ | Tracker / Network / Transport 자동 검증 |
| test/ | 원본 프로젝트의 수동 검증 이미지 |

</details>

## Related Repositories

| 저장소 | 역할 |
|---|---|
| **이 저장소 · Jetson Edge Vision** | TensorRT 차량 인지 · 내부 Tracking · Vision 송신 |
| [Raspberry Pi Edge Vision Gateway](https://github.com/triton0305/raspberry-pi-edge-vision-gateway) | Vision 중계 · downstream Control 전달 |
| [Jetson Edge Vision Relay Server](https://github.com/triton0305/jetson-edge-vision-relay-server) | Vision 수신 · SQLite 저장 · DB 상태 Control 생성 |
| [Raspberry Pi Edge Vision](https://github.com/triton0305/raspberry-pi-edge-vision) | OpenCV DNN / CPU 기반 원본 프로젝트 |

## Portfolio

**[Jetson 프로젝트 포트폴리오 PDF](docs/jetson-portfolio.pdf)** — 장비 구성 · 설계 · 실행 검증, 원본 포트폴리오 5–7페이지 발췌.

README의 장비 사진과 실행 화면은 해당 자료에서 추출했습니다. PDF는 2026.10.01 기본 시스템 기준이며, 현재 Tracking Stage 1의 동작과 검증 범위는 이 README를 참고합니다.

