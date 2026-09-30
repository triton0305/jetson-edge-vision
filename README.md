# Jetson Edge Vision

**Jetson 포팅 및 개발:** 2026.09.30  
**기반 프로젝트 개발:** 2026.09.21 ~ 2026.09.28 (Raspberry Pi Edge Vision)

[Raspberry Pi Edge Vision](https://github.com/triton0305/raspberry-pi-edge-vision)을 Jetson Nano 환경으로 확장한 C++17 차량 인지 Client입니다. 기존 Camera → Detection → Queue → TCP/ACK 구조를 유지하면서, OpenCV DNN의 CPU 추론을 TensorRT FP16 기반 GPU 추론으로 전환했습니다.

주 목적은 Jetson에서 생성한 차량 Detection 이력을 상대 서버로 전달하는 것입니다. 탐지 객체마다 `vision` JSON을 생성하고 별도 네트워크 스레드에서 TCP/ACK로 전송합니다.

별도 [Relay Server](https://github.com/triton0305/edge-vision-relay-server)는 배포 편의를 위해 직접 만든 보조 서버입니다. 이 Client는 정해진 TCP/JSON/ACK 규격을 따르는 상대 서버에 연결해 사용합니다.

## Changes from Raspberry Pi

| 항목 | Raspberry Pi Edge Vision | Jetson Edge Vision |
|---|---|---|
| 실행 장비 | Raspberry Pi 4 | NVIDIA Jetson Nano |
| 추론 방식 | OpenCV DNN / CPU | TensorRT / CUDA GPU |
| 모델 형식 | YOLO26n ONNX | YOLO26n TensorRT FP16 engine |
| Detector | OpenCV DNN 기반 모델 로딩·추론 | TensorRT engine 로딩, CUDA 버퍼 관리·추론 |
| 카메라 입력 | USB Webcam / V4L2 | USB Webcam / V4L2, YUYV 명시 |
| 결과 화면 | VNC에서 실시간 탐지 표시 | TigerVNC에서 실시간 탐지 표시 |
| 이미지 저장 | 최초 탐지 스냅샷 저장 | 실시간 화면 표시만 수행 |
| 잔존 기능 정리 | Tracker·traffic_count 소스가 남아 있으나 Runtime에서 호출하지 않음 | 관련 소스·직렬화·전용 전달 정책 제거 |
| 실행 래퍼 | 운영 계정으로 바이너리 실행 | 인자 개수 검사 및 DISPLAY/XAUTHORITY 전달 |

차량 클래스 필터링, Letterbox 전처리, Class-aware NMS, 객체별 JSON 형식, Message Queue, TCP length-prefix 및 ACK/Retry 구조는 유지했습니다. 기존 서버가 같은 프로토콜로 데이터를 수신할 수 있도록 구성했습니다.

Client는 객체별 Detection 이력 생성과 상대 서버로의 전송을 담당합니다. 이후 데이터 저장과 시간 구간별 집계는 수신 서버의 책임입니다. Detection 건수는 고유 차량 수나 통과 교통량을 의미하지 않습니다.

## 최종 Runtime

```text
USB Webcam → OpenCV/V4L2 Capture → Letterbox 640x640
→ YOLO26n TensorRT FP16 → Vehicle Filtering → Class-aware NMS
→ Detection → vision JSON → Message Queue → Network Worker
→ TCP/ACK/Retry → 상대 서버
```

- `/dev/video0`, YUYV 640×480@30 요청. 실제 처리 FPS는 추론 및 화면 표시 시간에 따라 달라집니다.
- COCO `car(2)`, `motorcycle(3)`, `bus(5)`, `truck(7)`; confidence 0.25, class-aware NMS 0.45.
- TigerVNC에서 `cv::imshow("Edge Vision", ...)`로 bbox, 차종, confidence를 표시합니다. Esc 또는 Ctrl+C로 종료합니다.
- 영상 녹화, 스냅샷 저장, Tracker, Line Crossing, 교통량 집계 및 통계 메시지는 없습니다.
- 운영 진단용 FPS, 추론 시간, Queue/Drop, ACK/Retry 로그는 유지합니다.

## Protocol

TCP는 4-byte big-endian payload length + JSON payload입니다. Detection 하나마다 `vision` 메시지를 생성하고, 탐지가 없으면 메시지를 만들지 않습니다.

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

현재 설정은 기존 식별자 `device_id=vision-pi-01`을 유지합니다. 프로토콜이 이 값을 요구하는 것은 아닙니다. message_id는 device_id, 파일에 보존되는 실행 카운터 boot_id, 실행 중 sequence로 구성됩니다. boot_id는 프로그램 시작 시 증가하고 sequence는 객체마다 증가합니다. 같은 device_id를 사용하는 장비를 함께 운영하거나 boot_id를 초기화하면 message_id가 충돌할 수 있으므로 장비별 식별자와 상태 파일을 구분해야 합니다.

`frame_id`는 실행 내에서 0부터 증가합니다. `timestamp_ms`는 `camera.read()` 완료 직후 생성한 Unix ms이며 카메라 하드웨어의 촬영 timestamp는 아닙니다. bbox의 x/y는 원본 프레임 기준 좌측 상단 픽셀 좌표이고 width/height는 픽셀 단위 크기입니다.

ACK도 같은 length-prefix를 사용합니다.

```json
{"version":1,"type":"ack","message_id":"vision-pi-01-000059-00000001","status":"ok"}
```

소켓 수신 timeout(`SO_RCVTIMEO`)은 1500ms입니다. 이는 각 블로킹 수신 호출에 적용되며 ACK 전체 수신의 총 제한시간은 아닙니다. 연결과 송신에는 별도의 애플리케이션 timeout을 설정하지 않습니다.

메시지당 연결·송신·ACK 처리 시도는 최대 3회입니다. 연결 실패도 시도 횟수에 포함되며, 재연결 실패 후 1000ms 대기합니다. 송신 또는 ACK 수신 실패 시 연결을 끊고 다음 시도에서 재연결합니다. 재전송은 동일 message_id/payload를 사용하고, 한도를 넘은 메시지는 버립니다. 서버의 중복 저장 방지는 수신 서버가 message_id를 기준으로 구현해야 합니다.

Queue는 대기 메시지 최대 16개이며 가득 차면 가장 오래된 대기 메시지를 버립니다. 전송 중인 메시지는 이 Queue 크기에 포함되지 않습니다. Metrics의 Dropped는 Queue 초과로 버린 건수이며 재시도 한도 초과 건수는 별도 로그로 표시합니다.

네트워크 연결·수신 실패 중에도 Vision Loop는 계속 동작합니다. 유효하지 않은 ACK 또는 서버 Error ACK는 Network Worker를 실패 처리하고 Vision Loop도 종료하게 합니다. 서버 Error ACK는 `status=error`와 비어 있지 않은 `error_code`가 필요합니다.

종료 시 Queue를 닫고 네트워크 스레드를 join하지만, 남은 대기 메시지를 모두 전송하는 Queue drain은 구현되어 있지 않습니다. 이미 진행 중인 네트워크 호출은 종료 전까지 기다릴 수 있습니다. 유한 Queue와 유한 재시도를 사용하는 best-effort 전달이므로 모든 Detection의 서버 도착을 보장하지 않습니다.

동일 차량이 여러 프레임에 탐지되면 별도 Detection 이력입니다. Detection 건수는 고유 차량 수나 통과 교통량이 아닙니다.

## Build

Jetson의 C++17 compiler, CMake 3.16 이상, OpenCV(core/imgproc/highgui/videoio/dnn), CUDA, TensorRT(`nvinfer`), nlohmann/json 헤더가 필요합니다. OpenCV DNN은 blob 생성과 NMS에 사용하며 추론은 TensorRT로 수행합니다.

기존 검증된 `models/yolo26n_fp16.engine`을 준비합니다. Detector의 입력은 `images` 1×3×640×640, 출력은 `output0` 1×84×8400이며 I/O 버퍼는 float입니다. FP16 engine은 Git에 포함하지 않습니다. 대상 Jetson/TensorRT 환경과 호환되는 engine을 사용해야 합니다. 현재 Detector는 binding 기반 `getBindingIndex` / `enqueueV2` / `destroy` API를 사용하므로 해당 API를 제공하는 TensorRT 환경이 필요합니다. 최신 TensorRT의 API와 자동 호환되는 구현은 아닙니다.

코드는 두 binding의 이름을 확인하지만 engine의 실제 차원과 데이터형을 검증하지 않고 위 크기와 float 버퍼를 사용합니다. 따라서 입력·출력이 FP32이며 위 shape와 일치하는 engine이 필요합니다. FP16은 engine 내부 연산 정밀도이며 코드만으로 engine의 실제 FP16 적용 여부를 검증할 수는 없습니다.

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j2
./build/bin/edge_vision <server_ip> <server_port>
```

개발 빌드는 구성 시 저장소 경로의 `models/yolo26n_fp16.engine`, `boot_id.dat`을 기본 경로로 지정합니다. 두 파일은 Git에 포함되지 않습니다. 모델은 별도로 배치하고, boot_id 파일이 없으면 프로그램이 1부터 생성합니다. 부모 디렉터리에 쓰기 권한이 필요합니다.

현재 Runtime은 항상 `cv::imshow`와 `cv::waitKey`를 호출하므로 X 화면과 인증이 가능한 GUI 환경에서 실행해야 합니다. headless 실행 옵션은 없습니다.

## 운영 배포

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
| Binary | `/opt/edge_vision/bin/edge_vision` |
| MODEL_PATH | `/opt/edge_vision/models/yolo26n_fp16.engine` |
| BOOT_ID_PATH | `/var/lib/edge_vision/boot_id.dat` |
| XAUTHORITY | `/home/edgevision/.Xauthority` |

`cmake --install`은 바이너리만 설치합니다. 모델 배치, `edgevision` 계정·video 그룹 설정, `/var/lib/edge_vision` 생성과 권한 설정, TigerVNC 및 X 인증 설정은 별도로 준비해야 합니다. boot_id가 없을 때 생성할 수 있도록 부모 디렉터리 쓰기 권한도 필요합니다. 기존 boot_id는 초기화하지 않습니다.

`pirun`은 저장소 루트에서 `./pirun`으로 실행하는 스크립트이며 위 설치 명령으로 PATH에 설치되지는 않습니다.

```bash
strings /opt/edge_vision/bin/edge_vision | grep -F /opt/edge_vision/models/yolo26n_fp16.engine
./pirun <server_ip> <server_port>
```

`pirun`은 `sudo -u edgevision`으로 운영 바이너리에 두 인자를 전달합니다. DISPLAY는 현재 환경 값(없으면 `:1`), XAUTHORITY는 검증된 `/home/edgevision/.Xauthority`만 명시적으로 전달합니다. TigerVNC `:1`을 쓸 때 다른 DISPLAY가 설정되어 있다면 `DISPLAY=:1 ./pirun <server_ip> <server_port>`로 실행합니다. 전체 X 접근을 허용하는 설정은 사용하지 않습니다.

## 검증 절차

개발 환경에서 확인한 카메라 streaming, TigerVNC 인증, TensorRT 추론과 보조 서버 기동은 아래 전체 E2E 검증과 구분합니다. 이 저장소에는 Raspberry Pi와 Jetson을 동일 조건으로 비교한 성능 측정 자료가 포함되어 있지 않으므로 처리 FPS나 속도 향상 배수를 확정하지 않습니다.

1. 상대 서버 또는 보조 Relay Server 실행 후 Jetson에서 접근 가능한 서버 IP와 포트로 `./pirun <server_ip> <server_port>` 실행.
2. TigerVNC의 실시간 Detection 화면 확인.
3. 차량 탐지 시 `ACK OK`와 서버 로그 확인.
4. 상대 서버의 수신·처리 결과와 message_id 확인. 보조 Relay Server를 사용하는 경우 SQLite 저장 결과도 확인.
5. `git diff --check`, `git diff`, `git status --short` 확인.

상대 서버 코드는 이 저장소에 포함되지 않습니다. 배포 편의를 위해 직접 만든 보조 서버는 별도 [Relay Server](https://github.com/triton0305/edge-vision-relay-server) 저장소에서 관리합니다. WSL의 `0.0.0.0`은 바인딩 주소이므로 클라이언트 인자에는 실제 접근 가능한 IP를 사용합니다.

`test/bus.jpg`, `test/bus_result.jpg`, `test/camera_result.jpg`는 기존 수동 검증 자료로 보존하며 Runtime에서 읽거나 생성하지 않습니다. `src`/`include`의 vision, protocol, network, core 디렉터리는 각각 탐지, 직렬화, 전달, 설정·ID·성능 진단을 담당합니다.

## Related Projects

- [Raspberry Pi Edge Vision](https://github.com/triton0305/raspberry-pi-edge-vision): OpenCV DNN / CPU 기반 원본 프로젝트
- [Edge Vision Relay Server](https://github.com/triton0305/edge-vision-relay-server): 배포 편의를 위해 직접 만든 보조 서버. TCP 수신, JSON 검증, SQLite 저장 및 ACK 처리
