# Troubleshooting

Jetson Nano 기반 Vision Client를 개발·통합하면서 발생한 문제와 주요 기술적 판단을 **증상 → 원인 추적 → 조치 → 검증** 순서로 정리했습니다. 기존 64개 기록 중 연관된 항목은 하나의 사건으로 통합하고, 설계 원칙·개념 설명·정상 검증은 별도로 구분했습니다.

> **기록 기준** 2026-10-13 정리 · 원본: [`TROUBLESHOOTING_ARCHIVE.md`](TROUBLESHOOTING_ARCHIVE.md) (1~64번). CCTV 전용 Repository의 65~69번은 포함하지 않았습니다. 아래 검증은 원본에 기재된 범위만 옮겼으며, 수치와 결과를 새로 측정하지 않았습니다.

## 통합 사례

| # | 문제 및 해결 | 관련 원본 |
|---|---|---|
| 1 | Jetson 구형 SW 환경에서 YOLO26n TensorRT FP16 실행 | 1, 2, 5~9 |
| 2 | TensorRT 가속 이후 Queue 포화와 ACK 병목 | 12~17, 52 |
| 3 | ACK 제거 후 downstream 장애 상태 전달 | 18~20, 26~27 |
| 4 | 재연결·PAUSE 중 stale data 및 세션 상태 처리 | 21~25 |
| 5 | DB 장애 주입과 PAUSE/RESUME E2E 검증 | 28 |
| 6 | TCP framing, Partial I/O, 소켓 수신 책임 | 29~33 |
| 7 | `Pi Link=UP`인데 PAUSED: 잘못된 실행 포트 | 35~39 |
| 8 | 기존 Pi ACK 서버와 신규 Gateway 역할 구분 | 34, 40~41 |
| 9 | 서버 미실행 시 연결 실패와 Vision 실행 분리 | 42~43 |
| 10 | Jetson SD 이미지·부팅 문제 | 44~45 |
| 11 | Wi-Fi/IP/Remote SSH 환경 설정 문제 | 46~49 |
| 12 | Camera/GUI 환경과 Core Vision 실행 분리 | 10~11 |

---

### 1. Jetson Nano 구형 SW 환경에서 TensorRT FP16 실행

**상황·문제**  Raspberry Pi 4의 YOLO26n ONNX/OpenCV DNN CPU 추론은 약 2.2~2.3 FPS로 동작했습니다. Jetson Nano는 CUDA 10.2, TensorRT 8.0.1 등의 구형 런타임이어서 최신 PC용 AI 환경을 그대로 적용하기 어려웠고, 포팅 중 JSON 개발 패키지 누락과 `trtexec` 실행 경로 확인 문제도 있었습니다.

**원인 추적**  모델 준비 환경과 실제 배포 환경의 호환성을 분리했습니다. ONNX 모델 전달 시 SHA-256을 확인하고, CUDA `deviceQuery` 및 TensorRT 실행 파일의 설치 위치를 확인했습니다. 모델 출력 형태와 기존 후처리의 연결도 확인했습니다.

**조치**  기존 Camera·Letterbox·후처리·Serializer·Network 구조를 유지하고, **OpenCV DNN/CPU 추론 부분만 TensorRT/CUDA/FP16으로 교체**했습니다. 누락된 `nlohmann-json3-dev` 패키지를 설치하고 Jetson 환경에서 엔진을 실행했습니다.

**검증**  원본 기록상 TensorRT 추론 약 **54.5 ms**, Effective FPS 약 **11.8~12 FPS**를 확인했습니다. 이는 전체 OpenCV를 TensorRT로 대체했다는 뜻이 아니라 **추론 백엔드 전환** 결과입니다.

**원본:** #1, #2, #5~#9. (#3~#4는 기술 설명 및 구조적 판단으로 아래 별도 정리.)

### 2. TensorRT 가속 이후 Queue 포화와 Stop-and-Wait ACK 병목

**증상**  GPU 추론 전환으로 Vision 생산 속도는 증가했지만, 기존 Pi 방식의 객체별 `send → ACK 대기 → 다음 send`를 유지하면서 Queue가 `16/16`까지 차고 `Queue full: dropping best-effort message`가 반복됐습니다.

**원인 추적**  추론 시간과 FPS만으로 판단하지 않고 Produced/Sent 메시지 수, Queue depth, overflow drop을 함께 관찰했습니다. **Producer의 생성 속도가 Network Consumer의 직렬 ACK 처리 속도를 초과**한 것이 핵심 병목으로 정리됐습니다. Queue 용량 증설은 지속적인 처리량 차이를 해결하지 못합니다.

**조치**  정상 Vision 전송 경로에서 application-level **per-message ACK/Retry를 제거**하고 Continuous TCP TX로 전환했습니다. Produced/Sent, Queue, drop 등 운영 지표도 확장했습니다.

**검증**  원본의 정상 실행 로그에서 **Produced ≈ Sent, Queue=0, overflow dropped=0**을 확인했습니다. 이 결과는 측정된 실행 구간의 값이며 모든 환경에서 무손실을 보장한다는 의미는 아닙니다.

**원본:** #12~#17, #52.

### 3. ACK 제거 이후 downstream 장애 상태 전달

**문제**  per-message ACK를 없애면 전송 병목은 줄지만, Jetson이 각 메시지의 DB 저장 성공 여부를 직접 알 수 없습니다. TCP 연결이 살아 있어도 검증 서버 내부의 DB write가 실패할 수 있습니다.

**조치**  정상 Vision 데이터와 장애 제어를 분리했습니다. Vision은 Continuous TX로 보내고, 각 노드는 **직접 관찰 가능한 장애**를 판단합니다. 서버의 DB 오류는 서버가 감지해 `PAUSE`를 Pi Gateway를 통해 Jetson에 전파하고, 복구 시 `RESUME`을 전달하도록 설계했습니다.

**검증 범위**  DB 장애를 실제 주입한 결과는 아래 **5번 사례**에 별도 기록했습니다. TCP 연결 성공만으로 Application Ready를 판단하지 않는 정책입니다.

**원본:** #18~#20, #26~#27.

### 4. 재연결과 PAUSE 경계에서 오래된 데이터 차단

**문제**  TCP가 다시 연결됐다고 DB까지 준비된 것은 아닙니다. PAUSE 중 Queue에 Detection이 쌓이면 복구 후 과거 데이터를 전송하게 됩니다.

**조치**  새 TCP 세션은 **PAUSED 상태**에서 시작하고, downstream 상태를 동기화해 `RESUME`을 수신한 후에만 전송합니다. PAUSE 시 대기 Queue를 비우고 신규 Vision JSON/message_id 생성과 enqueue를 차단했습니다. 상태 변경 epoch를 기준으로 PAUSE/RESUME 경계를 가로지른 오래된 프레임도 제외했습니다. **Camera·TensorRT·Display는 계속 실행**하도록 처리 경로와 전송 경로를 분리했습니다. 이미 소켓으로 전송된 데이터를 모두 되돌리는 트랜잭션 보장은 요구하지 않았습니다.

**검증**  원본의 PAUSED 구간에서 **Produced=0, Sent=0, Queue=0**이면서 추론 약 **54.5 ms**, Effective FPS 약 **12** 수준으로 영상 처리가 지속된 기록이 있습니다.

**원본:** #21~#25.

### 5. 실제 DB 장애 주입을 통한 PAUSE/RESUME 검증

**검증 목적**  코드상 장애 처리 경로가 존재하는 것과 실제 분산 장애·복구가 동작하는 것은 다르므로, WSL 검증 서버에서 SQLite INSERT 오류를 주입했습니다.

**관찰 흐름**  DB 실패 → 서버의 `PAUSE` → Pi Gateway → Jetson PAUSED → Vision 전송 중단 → DB 복구 → 서버의 `RESUME` → Pi/Jetson RUNNING → 새 Detection 전송.

**결과**  장애 중 **Produced=0, Sent=0, Queue=0**을 유지하면서 영상 처리는 약 **12 FPS**, TensorRT 추론은 약 **55 ms** 수준으로 지속됐습니다. DB 복구 후 새 Detection부터 송신했으며, 검증 구간에서 과거 timestamp replay와 중복 message_id가 없음을 확인했습니다. 이 검증은 **개발용 WSL/SQLite 검증 서버 경로**에 대한 기록이며, 별도의 상대 Ubuntu/MariaDB 저장 확인은 **8번 사례**에 구분했습니다.

**원본:** #28 (연관 정책 #19~#24).

### 6. TCP 메시지 경계·Partial I/O·수신 소유권

**문제**  TCP는 바이트 스트림이므로 `send()` 한 번이 상대 `recv()` 한 번에 대응하지 않습니다. 또한 한 소켓을 여러 스레드가 동시에 `recv()`하면 Control 메시지 수신 책임이 불명확해집니다.

**조치**  **4-byte big-endian payload length + JSON** framing, 전체 바이트를 처리하는 read/write 루프, payload 길이 guard를 적용했습니다. 한 TCP 소켓의 receive owner를 명확히 하고 Vision TX와 Control RX 역할을 구분했습니다.

**확인 사항**  원본에서 Jetson 측 guard는 약 **1 MiB**, 상대 서버의 제한은 **4 KiB**로 기록되어 있으나, 확인한 Vision JSON 예시는 약 **257 B**로 상대 제한 이내였습니다. 제한값의 차이만으로 현재 메시지의 호환성 실패를 단정하지 않았습니다. 별도의 `network_transport` 자동 테스트에서는 short write, EINTR/EAGAIN, 분할 수신을 통한 1 MiB framed transfer 복원, 잘못된 payload 길이, 송신 deadline 및 reconnect 동작을 검증했습니다.

**원본:** #29~#33. (구현·검증 정책이 포함된 항목이며, 각각을 실제 발생한 독립 오류로 취급하지 않았습니다.)

### 7. `Pi Link=UP`인데 PAUSED: 실제 실행 포트 불일치

**증상**  Jetson 로그에는 `Pi Link=UP`, `Network State=PAUSED`, `Pause Reason=pi_connection_restored`, `Produced=0`, `Sent=0`이 표시됐습니다.

**원인 추적**  Control 로직을 수정하기 전에 실제 TCP endpoint와 `pgrep -af edge_vision`으로 프로세스 실행 인자를 확인했습니다. Jetson이 `10.10.16.81:5000`으로 실행됐지만 당시 연결해야 할 Vision endpoint는 **`:5002`**였습니다.

**조치**  소스 수정 없이 실행 인자를 올바른 포트로 바꿔 재실행했습니다. 포트 번호 자체는 고정된 아키텍처 규격이 아니라 양쪽 실행 환경에서 일치해야 하는 설정값으로 관리했습니다.

**검증**  수정 후 `Network State=RUNNING`, `Pi Link=UP`, `Produced=Sent≈3.93767 msg/s`, `Queue=0`, `overflow dropped=0`을 확인했고, 상대 Ubuntu/MariaDB 저장까지 확인했습니다.

**원본:** #35~#39.

### 8. Pi Gateway 역할 구분과 실제 서버 E2E 통합

**상황**  기존 Raspberry Pi 프로젝트는 Vision Client → ACK 기반 Relay Server 구조였지만, Jetson 확장 후에는 Jetson → Pi Gateway → 검증 서버 구조가 됐습니다. 상대 팀은 별도로 Ubuntu/MariaDB를 사용했습니다.

**문제·판단**  기존 ACK/Retry 방식을 새 경로에 그대로 복제하면 앞서 확인한 전송 병목이 되살아납니다. 또한 상대 서버가 개발용 WSL/SQLite 내부 구조까지 동일하게 구현할 필요는 없습니다.

**조치**  Gateway는 Vision forwarding과 Control 전파 역할을 맡고, 시스템 간 공유 대상은 TCP framing, Vision/Control JSON, PAUSE/RESUME 및 재연결 **wire contract**로 제한했습니다.

**검증**  원본 기록에서 Jetson TensorRT → Pi Gateway → 상대 Ubuntu 서버 → **MariaDB 저장**을 확인했습니다. WSL/SQLite 검증 경로와 상대 팀 서버의 실제 저장 경로는 구분해야 합니다.

**원본:** #34, #40~#41 (실제 포트 문제 해결은 #36~#39).

### 9. 서버가 없는 상태의 연결 실패와 Vision 실행 분리

**증상**  Gateway/Relay가 실행되지 않았을 때 `Connection refused`가 발생할 수 있었습니다.

**원인·조치**  이는 Camera/TensorRT 장애가 아니라 해당 endpoint에 TCP listener가 없는 연결 실패입니다. Network Worker가 재연결을 관리하고, Camera/Inference는 별도로 계속 동작하도록 분리했습니다.

**검증 범위**  원본에는 서버가 없어도 Vision을 계속 실행할 수 있는 구조로 변경했다고 기록되어 있습니다. 재연결 횟수·간격에 관한 새로운 측정은 추가하지 않았습니다.

**원본:** #42~#43.

### 10. Jetson SD 이미지 및 부팅 오류

**증상**  초기 Jetson 환경에서 GUI 부팅에 실패하고 SQUASHFS 관련 읽기 오류가 관찰됐습니다.

**원인 추적·조치**  애플리케이션 오류와 분리해 OS 이미지·파일시스템·SD 부팅 환경을 우선 확인하고 Jetson Nano용 이미지를 SD 카드에 다시 기록했습니다. 이미지 기록이 약 **33분** 소요된 것은 GPU 추론 문제가 아닌 저장장치 I/O 관점에서 판단했습니다.

**검증**  정상 부팅 후 CUDA, TensorRT, OpenCV, Camera 검증 단계로 진행했습니다.

**원본:** #44~#45.

### 11. Wi-Fi 권한·IP 충돌·VS Code Remote SSH 대상 오류

**증상**  `nmcli` 설정 권한 문제, 사용 중인 IP와의 충돌, `eth0`/`wlan0` 주소 혼동, Jetson에 접속하려는데 VS Code가 WSL을 여는 문제가 있었습니다.

**조치**  필요한 NetworkManager 설정에 `sudo`를 사용하고, 중복 IP를 피한 뒤 `wlan0` 주소를 확인했습니다. SSH target/alias를 정리해 접속 대상을 `jetson@<Jetson IP>` 형태로 명확히 했습니다.

**검증**  원본에서 `wlan0`의 `10.10.16.153/24` 설정과 Jetson Remote 개발환경 연결을 확인했습니다. 이 IP는 당시 개발망의 예시이며 고정 배포 규격이 아닙니다.

**원본:** #46~#49. (#50 GitHub SSH 인증은 정상 환경 구축 기록으로 별도 분류.)

### 12. Camera와 GUI 표시 문제를 추론 장애와 분리

**상황**  Jetson에서 Webcam `/dev/video0`, V4L2, YUYV 640×480@30 요청 조건을 별도 확인했습니다. SSH 환경에서는 `cv::imshow()`가 X display 설정에 영향을 받을 수 있습니다.

**조치**  Camera capture, GPU 추론, HighGUI 표시 경로를 구분하고, 필요 시 Jetson GUI 세션의 DISPLAY를 사용했습니다.

**검증 범위**  원본에는 C++ OpenCV webcam capture와 실제 이미지 저장을 통한 입력 확인이 있습니다. SSH GUI 부분은 주로 **환경 의존성 및 예방적 분리**에 관한 기록으로, 독립적인 장애 해결이 검증됐다고 확대하지 않았습니다.

**원본:** #10~#11.

---

## 별도 관리한 설계·검증 기록

다음 항목은 중요한 기술 기록이지만, 각각을 독립적인 *발생 장애*로 세면 실제 문제 해결 건수가 과장될 수 있어 위의 핵심 사례 수에 포함하지 않았습니다.

| 주제 | 원본 | 정리 이유 |
|---|---|---|
| 추론 백엔드 전환의 정확한 표현·코드 재사용 | 3~4 | 포팅 구조 설명 |
| FPS와 처리량 관측 기준·Queue 증설 판단 | 13~15, 51~52 | 핵심 사례 #2의 분석 과정에 포함. #51은 Header/Source 불일치 **가능성**에 대한 개발 관리 기록 |
| Socket 종료 및 Graceful Shutdown | 53 | 스레드 수명주기·정상 종료 설계 및 검증 기록 |
| Detection/Tracking/차량 수의 의미 구분 | 54~56 | 데이터 의미·알고리즘 구분 |
| Detection 단위, Event Time, 좌표, 클래스 정책 | 57~60 | JSON/데이터 계약 설계 |
| ACK 유실, idempotency, message_id, retry payload | 61~64 | **기존 Pi ACK/Retry 경로**의 신뢰성 설계. Jetson Continuous TX 경로와 혼동 금지 |
| GitHub SSH 인증 | 50 | 개발환경 정상 구성·접속 확인 |

## 원본 기록

64개 항목의 전체 상세 내용은 [`TROUBLESHOOTING_ARCHIVE.md`](TROUBLESHOOTING_ARCHIVE.md)에 보존했습니다. 원본 번호를 유지해 핵심 사례의 근거와 세부 판단을 역추적할 수 있습니다.
