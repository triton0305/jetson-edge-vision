# Jetson Edge Vision — Troubleshooting Master (1–64)

# I. Jetson Nano 포팅 / TensorRT

## 1. Raspberry Pi CPU 추론 성능 한계 → Jetson Nano 포팅

### 상황

기존 Raspberry Pi 4에서는 YOLO26n ONNX를 OpenCV DNN CPU backend로 실행했다.

```text
Camera
→ OpenCV/V4L2
→ Letterbox
→ YOLO26n ONNX
→ OpenCV DNN / CPU
→ Post-processing
→ JSON
→ Network
```

Pi에서 실제 측정한 추론 시간은 대략 **410~490ms 수준**, Effective FPS는 약 **2.2~2.3 FPS**였다.

### 문제

Vision Client의 구조와 기능은 정상적으로 동작했지만 실시간 영상 처리 성능을 높일 필요가 있었다.

### 판단

전체 프로그램을 새로 만드는 대신 기존 구조를 그대로 승계하고 **Detector backend만 GPU 기반으로 교체**하는 방향을 선택했다.

```text
Pi
OpenCV DNN / CPU

        ↓

Jetson Nano
TensorRT / CUDA / GPU
```

### 결과

Jetson 최종 측정에서:

```text
Avg TensorRT inference ≈ 54.5 ms
Effective FPS ≈ 11.8~12 FPS
```

수준까지 향상됐다.

### 의미

단순히 하드웨어를 바꾼 게 아니라 기존 소프트웨어 Architecture가 **Inference Backend 교체를 수용할 수 있는 구조인지 실제 포팅으로 검증**했다.

---

# 2. Jetson Nano 구형 SW Stack과 최신 YOLO 호환성

### 환경

Jetson Nano 환경은 대략:

```text
CUDA 10.2
TensorRT 8.0.1
OpenCV 4.8.0
CMake 3.16.3
g++ 8.4.0
```

이었다.

### 문제

최신 PC 환경에서 사용하는 Python/Ultralytics/TensorRT 생태계를 Jetson Nano에 그대로 설치하고 실행하는 방식은 구형 JetPack stack과 충돌할 가능성이 컸다.

### 해결 방향

**모델 준비 환경과 Runtime 환경을 분리**했다.

```text
모델 준비
YOLO26n
↓
ONNX Export

Jetson Runtime
ONNX
↓
TensorRT Engine Build
↓
FP16 Engine
↓
C++ TensorRT Runtime
```

즉 Jetson에서 최신 Python AI 환경 전체를 억지로 구성하지 않고 **최종 추론 산출물만 Jetson Runtime에 맞췄다.**

### 결과

기존 JetPack/CUDA 환경을 유지하면서 YOLO26n 기반 TensorRT GPU inference에 성공했다.

### 의미

```text
Training/Export Environment
≠
Deployment Runtime Environment
```

라는 배포 환경 분리를 실제로 적용한 사례다.

---

# 3. OpenCV 전체를 TensorRT로 바꾸는 것이라는 오해

초기 설명 과정에서 “OpenCV 기반 → TensorRT 기반”이라는 표현이 나왔지만 정확하게는 그렇지 않았다.

### 실제 구조

Pi:

```text
Camera       → OpenCV
Letterbox    → OpenCV
Inference    → OpenCV DNN / CPU
Postprocess  → C++
Network      → C++
```

Jetson:

```text
Camera       → OpenCV
Letterbox    → OpenCV
Inference    → TensorRT / CUDA / GPU
Postprocess  → C++
Network      → C++
```

### 정리

바뀐 부분은 정확히:

> **OpenCV DNN 기반 CPU inference → TensorRT/CUDA 기반 GPU inference**

였다.

Camera capture나 Letterbox 같은 OpenCV 기반 영상처리는 유지했다.

### 의미

포트폴리오에서도 “OpenCV를 TensorRT로 교체했다”고 쓰는 것보다 **Inference Backend를 교체했다**고 표현하는 게 기술적으로 정확하다.

---

# 4. Pi 코드를 Jetson에서 전체 재작성하지 않음

### 문제

하드웨어가 Pi → Jetson으로 바뀌었다고 Client 전체를 새로 만들면 기존에 검증한 다음 구조를 다시 구현해야 했다.

```text
Detection
Serializer
MessageQueue
TcpClient
NetworkWorker
Protocol
Metrics
```

### 해결

기존 Pi Vision Client의 모듈 경계를 유지했다.

핵심적으로 교체한 부분은 Detector였다.

```text
Pi Detector
readNetFromONNX()
OpenCV DNN
CPU

        ↓

Jetson Detector
TensorRT Engine
CUDA
FP16
GPU
```

### 결과

기존:

```text
Detection 구조
Serializer
Queue
TCP
Protocol
Network 구조
```

를 상당 부분 승계할 수 있었다.

### 의미

이 프로젝트의 포팅 가치는 단순 GPU 사용보다 **기존 Architecture의 재사용성을 실제 다른 Hardware에서 검증했다는 것**에도 있다.

---

# 5. ONNX 모델 전달 무결성 확인

Pi/PC 쪽에서 사용하던 YOLO26n ONNX를 Jetson으로 옮겼다.

단순히 파일 이름이 같다고 동일 모델이라고 가정하지 않고 **SHA-256을 확인**했다.

기록된 hash는:

```text
f7c4bc6c...
```

계열이었다.

### 의미

모델 추론 결과가 달라졌을 때:

```text
Runtime 문제인가?
모델 파일이 달라졌나?
전송 중 파일이 깨졌나?
```

를 분리할 수 있도록 했다.

---

# 6. TensorRT Output과 기존 Post-processing 호환

기존 YOLO26n ONNX 출력은:

```text
1 × 84 × 8400
```

구조였다.

### 문제

TensorRT로 Backend를 바꾸더라도 기존 PostProcessor가 기대하는 output layout과 실제 TensorRT output이 맞아야 했다.

### 해결

Engine output을 확인하고 기존:

```text
reshape
→ transpose
→ confidence/class filtering
→ bbox conversion
→ NMS
```

처리와 연결했다.

### 결과

기존 Detection 구조와 Serializer를 유지하면서 TensorRT 결과를 사용할 수 있었다.

---

# II. Jetson C++ 빌드 / Runtime 환경

# 7. `nlohmann-json3-dev` 누락

Pi에서 정상적으로 빌드되던 코드를 Jetson으로 가져왔지만 JSON header dependency가 Jetson에 없었다.

### 문제

CMake/source 자체 문제가 아니라 **배포 환경 dependency 차이**였다.

### 해결

필요한 package만 설치했다.

```bash
sudo apt install nlohmann-json3-dev
```

### 결과

기존 JSON Serializer 구조를 변경하지 않고 build 정상화.

### 의미

포팅 과정에서 소스를 무작정 고치기 전에 **Build dependency와 Source error를 분리**한 사례다.

---

# 8. TensorRT는 설치돼 있는데 `trtexec`가 바로 안 보임

Jetson에 TensorRT package/library는 존재했지만:

```bash
which trtexec
```

만으로는 원하는 결과가 바로 나오지 않았다.

### 잘못된 판단 가능성

```text
which trtexec 실패
→ TensorRT가 설치 안 됐나?
```

라고 판단할 수 있었다.

### 확인

Package와 binary 위치를 따로 조사했다.

예:

```bash
dpkg -L libnvinfer-bin | grep trtexec
```

### 결과

TensorRT 설치 여부와 PATH 문제를 분리할 수 있었고, 이후 `trtexec` sanity test를 수행했다.

### 최종 검증

TensorRT test 및 GPU 동작을 확인했다.

---

# 9. CUDA GPU 자체가 정상인지 별도 검증

TensorRT application부터 실행해서 실패 원인을 한꺼번에 추적하지 않았다.

먼저 CUDA 자체를:

```text
deviceQuery
```

등으로 확인했다.

### 결과

```text
PASS
```

확인.

그 다음:

```text
CUDA
→ TensorRT
→ OpenCV
→ Webcam
→ 실제 Edge Vision
```

순으로 검증했다.

### 의미

복합 Pipeline 문제를 **Layer별 sanity test**로 분리한 방식이다.

---

# III. Camera / OpenCV

# 10. Webcam이 열리는 것만으로 정상이라고 판단하지 않음

Jetson 포팅 과정에서 Camera도 별도 검증했다.

확인 대상:

```text
/dev/video0
V4L2
640×480
YUYV
30 FPS request
```

### 이유

실제 Vision FPS가 낮을 때:

```text
Camera가 느린가?
OpenCV capture 문제인가?
TensorRT가 느린가?
Post-processing인가?
Network인가?
```

를 구분해야 하기 때문이다.

### 결과

C++ OpenCV webcam capture를 별도로 실행하고 실제 이미지를 저장하여 입력 정상 확인.

---

# 11. SSH 환경에서 `cv::imshow()` 문제

Jetson Vision Client에는 실제 Detection 화면을 보는 `imshow()` 경로가 있었다.

### 문제

Remote SSH나 display가 없는 환경에서 GUI 함수를 실행하면 Vision/Inference 자체와 관계없는 HighGUI/X display 문제가 발생할 수 있었다.

### 해결 방향

```text
Inference Runtime
≠
Display Runtime
```

으로 분리해서 봤다.

필요한 경우 Jetson GUI session의 DISPLAY 환경을 사용하거나 GUI 없는 실행에서는 HighGUI 의존성을 주의했다.

### 의미

“프로그램이 죽었다 = TensorRT 문제”라고 단정하지 않고 **GUI Layer와 Core Vision Layer를 분리**했다.

---

# IV. 가장 중요한 성능 트러블슈팅

# 12. TensorRT 적용 후 오히려 Queue Full 폭증

이게 **Jetson 프로젝트에서 가장 강한 트러블슈팅**이다.

### TensorRT 적용 전

Pi 구조는 대략:

```text
Vision
→ Detection
→ Queue
→ Network Worker
→ send
→ ACK wait
→ ACK validation
→ next
```

이었다.

Pi에서는 Vision 생산 속도가 느렸기 때문에 이 구조가 큰 문제 없이 돌아갔다.

### TensorRT 적용 후

Inference가 약 54.5ms까지 빨라지면서 Detection 생산량이 증가했다.

그런데 Network Worker는 기존 방식 그대로였다.

실제 로그에서는:

```text
Queue=16/16
```

이 지속되고:

```text
Queue full: dropping best-effort message ...
```

가 계속 발생했다.

Dropped count도 지속 증가했다.

### 처음 관찰된 현상

흥미로운 점은:

```text
Inference 성능 ↑
그런데
전체 시스템 상태 악화
```

였다.

### 핵심

이 시점에서 바로 ACK가 원인이라고 단정한 게 아니다.

**Queue 포화라는 증상을 먼저 발견했고 이후 계측을 통해 원인을 좁혔다.**

---

# 13. FPS만 보고 시스템 성능을 판단한 문제

TensorRT 전환 직후 다음 숫자만 보면 성공처럼 보였다.

```text
Inference ≈ 54.5 ms
FPS ↑
```

하지만 Queue에서는 Drop이 발생했다.

따라서:

```text
Inference 성능
≠
System Throughput
```

이라는 문제가 드러났다.

### 해결

Metrics를 확장했다.

최종적으로 관찰한 지표는:

```text
Effective FPS
Avg Inference
Produced Vision msg/s
Sent Vision msg/s
Queue
Queue overflow dropped
Discarded on PAUSE
Network State
Pi Link
Pause Reason
Reconnect count
```

등이었다.

### 의미

단순 FPS benchmark에서 **Pipeline Observability**로 측정 범위를 확장했다.

---

# 14. Producer와 Consumer 속도 차이를 실제로 관찰

구조는:

```text
Producer
Vision Thread
↓
Detection/JSON 생성

Queue

Consumer
Network Worker
↓
TCP 전송
```

이다.

### 문제

TensorRT 이후 Producer가 빨라졌다.

반면 Consumer는:

```text
send
→ ACK 기다림
→ ACK validation
→ 다음 message
```

으로 직렬 처리됐다.

### 결과

```text
Production Rate > Consumption Rate
```

가 되면서 Queue가 차기 시작했다.

### 중요

Queue Full 자체가 원인이 아니라 **상류와 하류의 처리량 불균형이 원인**이었다.

---

# 15. Queue 크기를 늘리면 해결될 것인가

처음 생각할 수 있는 방법은:

```text
Queue 16
→ 24
→ 32
→ ...
```

처럼 늘리는 것이다.

### 분석

하지만:

```text
Producer = 4 msg/s
Consumer = 2 msg/s
```

라고 가정하면 Queue를 아무리 늘려도 결국 찬다.

즉:

```text
Consumer Rate < Producer Rate
```

인 상황에서 Queue 증설은 **overflow 발생 시점만 늦춘다.**

### 결정

Queue 크기를 크게 만드는 방식 대신 **Network Consumer의 병목 자체를 제거**하기로 했다.

### 의미

메모리를 늘려 증상을 숨기지 않고 **처리량 구조를 수정**했다.

---

# 16. Stop-and-Wait ACK가 실제 병목

기존 Pi 구조에서는 Detection마다:

```text
JSON 1개
→ send
→ ACK 기다림
→ ACK 확인
→ 다음 JSON
```

이었다.

### 문제

이 방식에서는 한 message가 처리될 때까지 다음 message를 전송하지 못한다.

Network RTT와 Server 처리 시간이 매 message의 critical path에 들어간다.

### TensorRT 이후

Vision producer가 빨라지면서 이 Stop-and-Wait 구조의 처리량 한계가 명확해졌다.

### 결론

AI inference가 더 이상 주 병목이 아니고 **Network Worker의 application-level ACK serialization이 새로운 병목**이었다.

---

# 17. Per-message ACK/Retry 제거

### 기존 Reliability

```text
send
→ ACK
→ timeout
→ retry
```

그리고 Retry 시:

```text
same message_id
same payload
```

를 사용했다.

Pi에서는 강한 신뢰성 구조였다.

### Jetson에서의 문제

실시간 Vision stream에서는 모든 Detection을 개별 transaction처럼 처리하는 비용이 너무 커졌다.

### 변경

정상 Vision Data Path에서:

```text
application-level per-message ACK 제거
per-message Retry 제거
continuous TCP send
```

방식으로 변경했다.

### 결과

최종 실제 실행에서:

```text
Produced Vision msg/s ≈ Sent Vision msg/s
Queue = 0
Queue overflow dropped = 0
```

를 확인했다.

---

# 18. ACK를 제거하니 새로운 문제가 생김

ACK는 병목이었지만 역할이 하나 더 있었다.

Server가 데이터를 정상적으로 처리했는지 Client가 알 수 있었다.

ACK를 없애면:

```text
Jetson
→ 계속 send

그런데 downstream DB가 죽음
```

같은 상황을 어떻게 처리할지가 필요했다.

### 해결

**Data Plane과 Control Plane을 분리**했다.

```text
Data
Vision JSON
→ continuous TX

Control
pause
resume
← downstream 상태
```

### 의미

메시지 하나하나를 확인하는 방식 대신 **시스템 상태를 별도의 Control Message로 전달**하도록 Architecture가 발전했다.

---

# V. PAUSE / RESUME

# 19. DB 장애 중 Vision 데이터를 계속 보내면 안 됨

Final Server의 TCP process는 살아 있지만 DB write가 실패할 수 있다.

```text
TCP UP
Server Process UP
DB Write DOWN
```

### 문제

Connection만 보면 Jetson은 계속 데이터를 보낼 수 있다.

### 해결

Final Server가 **자기가 직접 관찰한 DB 장애를 판단**한다.

```text
DB write failed
↓
PAUSE
↓
Pi Gateway
↓
Jetson
```

### 결과

Jetson은 downstream unavailable 상태를 전달받아 Vision transmission을 중단한다.

---

# 20. Connection State와 Application State를 분리

핵심 원칙:

```text
TCP Connected
≠
System Ready
```

이다.

### 예

TCP 연결은 살아 있는데:

```text
MariaDB DOWN
```

일 수 있다.

따라서:

```text
Pi Link=UP
```

이라고 해서:

```text
Network State=RUNNING
```

일 필요가 없다.

실제로:

```text
Pi Link=UP
Network State=PAUSED
```

상태가 정상적으로 존재한다.

---

# 21. Reconnect ≠ Resume

Network가 끊겼다가 다시 연결되었다.

### 잘못된 방식

```text
connect() 성공
→ RUNNING
→ Vision 즉시 전송
```

### 문제

Final Server나 DB가 아직 PAUSE 상태일 수 있다.

### 해결

```text
Reconnect
↓
PAUSED
↓
현재 downstream 상태 동기화
↓
RESUME 수신
↓
RUNNING
```

으로 설계했다.

### 핵심

> **Transport recovery와 Application recovery는 다른 사건이다.**

---

# 22. 새 Session에서 자동 RUNNING하지 않음

새 connection이 만들어져도 Control state가 아직 확인되지 않았다면 안전하게:

```text
PAUSED
```

로 둔다.

### State-sync timeout

현재 설계에서는 일정 시간이 지났다고 임의로 RUNNING으로 전환하지 않는다.

```text
RESUME 없음
→ PAUSED 유지
```

### 의미

Fail-safe 정책이다.

---

# 23. PAUSE 중 Queue backlog 문제

Network 장애가 발생했는데 Vision producer가 계속 JSON을 Queue에 넣으면:

```text
장애
↓
Queue 증가
↓
과거 Detection 누적
↓
Network 복구
↓
몇 초 전 차량부터 전송
```

하게 된다.

### 문제

실시간 Vision에서는 오래된 Detection의 가치가 낮다.

### 해결

PAUSE 시:

```text
PAUSE
↓
Queue CLEAR
↓
신규 Vision enqueue 차단
```

한다.

RESUME 후:

```text
새 Detection
→ 새 JSON
→ 전송
```

한다.

### 정책

**Completeness보다 Freshness를 우선**했다.

---

# 24. PAUSE하면 TensorRT까지 멈출 것인가

처음에는:

```text
PAUSE
→ 전체 Vision 정지?
```

도 가능한 선택이었다.

### 문제

Camera/Inference까지 중지하면 Runtime state와 복구 과정이 복잡해진다.

### 최종 설계

PAUSE 중에도:

```text
Camera        RUN
TensorRT      RUN
Detection     RUN
```

을 유지한다.

중단하는 것은:

```text
Vision JSON production/enqueue
Network TX
```

쪽이다.

### 실제 결과

PAUSE 중에도 대략:

```text
Effective FPS ≈ 12
Inference ≈ 54.5 ms

Produced Vision msg/s = 0
Sent Vision msg/s = 0
Queue = 0
```

형태를 확인했다.

### 의미

```text
Vision Processing State
≠
Network Delivery State
```

를 분리했다.

---

# 25. PAUSE 직전 이미 전송된 데이터 문제

PAUSE를 받았다고 이미:

```text
send() 호출 완료
TCP kernel buffer 진입
Server processing 중
DB transaction 진행 중
```

인 데이터를 모두 취소하기는 어렵다.

### 선택지

완벽한 transaction boundary를 만들려면:

```text
generation ID
rollback
transaction coordination
```

같은 훨씬 복잡한 구조가 필요하다.

### 최종 요구사항 판단

이 프로젝트에서는 그 정도의 exactly-once 경계가 필요하지 않았다.

### 정책

```text
이미 진행 중인 일부 데이터
→ 완료 허용

Queue에 대기 중인 backlog
→ 폐기

RESUME 이후
→ 새로운 Detection부터 처리
```

### 의미

필요 이상의 분산 Transaction 복잡도를 넣지 않았다.

---

# 26. 각 Node가 어디까지 장애를 판단할 것인가

전체 시스템:

```text
Jetson
↓
Pi Gateway
↓
Final Server
↓
DB
```

### 잘못된 방향

Jetson이:

```text
DB가 죽었나?
Server 내부 오류인가?
Pi가 죽었나?
```

를 모두 추측하면 안 된다.

### 최종 원칙

> **각 Component는 자신이 직접 관찰 가능한 장애만 판단한다.**

예:

```text
Jetson
→ Jetson↔Pi connection 관찰

Pi
→ Jetson link / Server link 관찰

Final Server
→ DB write / 내부 처리 상태 관찰
```

그리고 필요한 상태를 Control Message로 전달한다.

### 의미

Fault detection responsibility를 Component boundary에 맞췄다.

---

# 27. Server 내부 장애는 TCP 상태만으로 알 수 없음

다시 중요한 사례:

```text
TCP = UP
Server Process = UP
DB = DOWN
```

### 해결

Final Server가 DB write 실패를 직접 판단하고:

```text
PAUSE(database_write_failed)
```

를 보낸다.

DB 복구 후:

```text
RESUME(database_recovered)
```

를 전달한다.

### 의미

**Transport Health와 Application Health를 분리**했다.

---

# 28. 실제 장애를 넣어봐야 함

PAUSE/RESUME 코드를 작성했다고 장애복구가 검증된 것은 아니다.

### 검증

실제 Final Server/DB 쪽에 장애를 발생시켰다.

확인 흐름:

```text
DB Failure
↓
Final Server PAUSE
↓
Pi PAUSED
↓
Jetson PAUSED
↓
Produced=0
Sent=0
Queue=0
```

이후 DB를 복구:

```text
DB Recovery
↓
Final Server RESUME
↓
Pi RUNNING
↓
Jetson RUNNING
↓
새 Detection부터 전송
```

### 결과

실제 Failure Injection을 통해 전체 복구 경로를 검증했다.

### 의미

이건 자소서에서 매우 강하다.

단순히:

> “장애복구 기능을 구현했습니다.”

가 아니라:

> “실제 DB 장애를 주입해 Relay→Gateway→Edge까지 PAUSE가 전파되고 복구 후 RESUME되는 전체 경로를 검증했습니다.”

라고 말할 수 있기 때문이다.

---

# VI. TCP / Protocol

# 29. TCP Message Boundary 문제

TCP는 byte stream이다.

따라서:

```text
send() 1회
≠
recv() 1회
```

다.

### 문제

JSON 하나를 `send()`했다고 상대 `recv()`에서 정확히 JSON 하나가 나온다는 보장이 없다.

### 해결

Application framing:

```text
[4-byte big-endian payload length]
[JSON payload]
```

을 사용했다.

이 구조는 Pi에서 만들어 Jetson/Gateway/Server까지 승계했다.

---

# 30. Partial Send / Receive

예:

```text
send(300 bytes)
```

를 호출해도 300B가 한 번에 전송됐다고 가정하면 안 된다.

마찬가지로 `recv()`도 필요한 전체 길이를 한 번에 줄 필요가 없다.

### 해결

```text
writeAll()
readAll()
```

형태로 남은 byte를 끝까지 처리한다.

### 의미

TCP framing과 partial I/O를 함께 처리해야 protocol이 완성된다.

---

# 31. 잘못된 payload size

Header에서 읽은 payload length가:

```text
0
```

이거나 지나치게 크면 정상 JSON으로 처리하면 안 된다.

### Jetson guard

대략:

```text
1 ... 1,048,576 bytes
```

범위의 guard를 사용했다.

### 처리

범위를 벗어나면:

```text
Protocol Error
/ Connection Failure
```

로 취급한다.

### 이유

잘못된 length를 그대로 믿으면:

```text
과도한 memory allocation
framing desync
잘못된 JSON read
```

문제로 이어질 수 있다.

---

# 32. 상대 서버는 4KB인데 Jetson은 1MiB — 호환되지 않는가?

상대 Final Server의 maximum payload:

```text
4096 bytes
```

Jetson guard:

```text
1 MiB
```

### 처음 볼 때

```text
1MiB vs 4KB
→ Protocol 불일치?
```

처럼 보일 수 있다.

### 실제 확인

Vision JSON sample은 약:

```text
257 bytes
```

수준이었다.

Length prefix까지 포함해도 약:

```text
261 bytes
```

다.

### 결론

현재 실제 message는 4KB보다 훨씬 작기 때문에 호환된다.

### 교훈

```text
Protocol Maximum
≠
Actual Payload Size
```

다.

---

# 33. Full-duplex Socket의 `recv()` Owner

같은 TCP connection에서:

```text
Jetson → Pi : Vision
Pi → Jetson : Control
```

을 처리한다.

TCP는 full-duplex이므로 가능하다.

### 문제

여러 Thread가 같은 socket에서 동시에 `recv()`하면 누가 어느 message를 가져갈지 제어가 어려워진다.

### 해결 원칙

> **한 Socket의 receive owner는 하나.**

Data TX와 Control RX의 책임을 명확히 나눴다.

### 의미

단순 mutex 문제를 넘어 **Socket ownership을 명확하게 정의**했다.

---

# VII. Pi Gateway / Final Server

# 34. 기존 Pi Client의 ACK Server와 새 Pi Gateway 역할 혼동

초기 Pi 프로젝트에서 Pi는:

```text
Vision Client
→ Relay Server
```

였다.

Jetson 확장 후에는:

```text
Jetson
→ Pi Gateway
→ Final Server
```

형태가 됐다.

### 문제

기존 Pi의 ACK/Retry contract를 그대로 모든 구간에 적용하면 Jetson에서 발견된 throughput 문제가 다시 발생한다.

### 해결

Pi를 단순 과거 Relay 구조의 복사본으로 두지 않고 **Gateway 역할에 맞춰 Data forwarding + Control propagation 책임을 정의**했다.

---

# 35. 포트 번호를 Architecture 요구사항처럼 고정하는 문제

개발 과정에서:

```text
8000
9000
5000
5002
```

등 여러 포트를 사용했다.

### 중요한 정리

포트 번호 자체가 Protocol의 본질적인 요구사항은 아니다.

즉:

```text
Jetson↔Pi = 반드시 8000
Pi↔Server = 반드시 9000
```

같은 규격으로 고정할 필요가 없다.

### 최종 원칙

배포 환경에서 사용 가능한 endpoint를 명확하게 설정하고 **양쪽 Contract가 일치하는 것이 중요**하다.

이 정리가 이후 실제 `5000/5002` 문제에서도 중요해졌다.

---

# VIII. 실제 상대 서버 연동

# 36. `Pi Link=UP`인데 Jetson이 계속 PAUSED

실제 Jetson 로그:

```text
Pi Link=UP
Network State=PAUSED
Pause Reason=pi_connection_restored
Produced Vision msg/s=0
Sent Vision msg/s=0
Queue=0
```

### 처음 보이는 증상

TCP connection은 살아 있다.

따라서:

> “Pi가 RESUME을 안 보내는 건가?”

> “Control state-sync가 잘못됐나?”

처럼 보였다.

### 중요한 점

여기서 바로 코드를 수정하지 않았다.

---

# 37. 실제 Socket Endpoint를 확인하니 다른 포트에 연결

Pi 쪽 연결을 조사했다.

확인 결과 Jetson은:

```text
10.10.16.153
→
10.10.16.81:5000
```

에 연결돼 있었다.

그런데 새 Vision endpoint는:

```text
5002
```

였다.

즉 새 서버 로직 자체가 아니라 **다른 service endpoint에 붙어 있던 것**이다.

---

# 38. 실행 중 Process Argument 확인

Jetson에서:

```bash
pgrep -af edge_vision
```

으로 실제 실행 command line을 확인했다.

결과:

```text
/opt/edge_vision/bin/edge_vision 10.10.16.81 5000
```

### 원인 확정

코드가 `5000`으로 hard-code된 게 아니라 **실행 인자로 과거 port를 넘기고 있었다.**

즉:

```text
Control Logic Bug X
TensorRT Bug X
Pi Gateway Bug X

Deployment / Runtime Argument Error O
```

였다.

---

# 39. 코드 수정 없이 Endpoint 수정

실제 process를 올바른 endpoint:

```text
10.10.16.81:5002
```

로 다시 실행했다.

### 결과

```text
Network State=RUNNING
Pi Link=UP
Produced Vision msg/s=3.93767
Sent Vision msg/s=3.93767
Queue=0
Queue overflow dropped=0
```

를 확인했다.

### 그리고

상대 Ubuntu의 MariaDB까지 실제 데이터가 들어가는 것을 확인했다.

### 의미

매우 좋은 디버깅 사례다.

증상을 보고 코드부터 고친 것이 아니라:

```text
State
→ Connection
→ Socket endpoint
→ Process argv
```

순으로 실제 Runtime을 추적해 **배포 설정 오류를 특정**했다.

---

# 40. 상대 서버가 WSL/SQLite 구조를 그대로 구현해야 한다는 오해

개발용 Reference 구조는:

```text
Jetson
→ Pi
→ WSL Final Server
→ SQLite
```

였다.

상대 시스템은 이미:

```text
Ubuntu
MariaDB
Metabase
Arduino sensor
...
```

를 사용하고 있었다.

### 문제

내부 Architecture까지 동일하게 만들려고 하면 상대 시스템을 불필요하게 재작성해야 한다.

### 해결

공유해야 할 것은 구현이 아니라 **Wire Contract**라고 정의했다.

필요한 것은:

```text
TCP framing
Vision JSON schema
Control JSON
PAUSE semantics
RESUME semantics
Reconnect semantics
```

이다.

### 결과

상대는 기존 Ubuntu/MariaDB 구조를 유지하고 Jetson/Pi 쪽 protocol과 연결할 수 있었다.

### 의미

```text
Implementation Compatibility
```

가 아니라:

```text
Interface Compatibility
```

로 통합했다.

---

# 41. 최종 E2E

최종 실제 흐름:

```text
Jetson Nano
TensorRT Vision
↓
Pi Gateway
↓
Ubuntu Final Server
↓
MariaDB
```

### Jetson 측

대략:

```text
Effective FPS ≈ 11.8~12
Avg Inference ≈ 54.5 ms
Produced ≈ Sent
Queue = 0
Overflow Drop = 0
Network State = RUNNING
Pi Link = UP
Reconnect count = 0
```

### Server 측

실제 MariaDB 저장 확인.

### 의미

단순 Local benchmark가 아니라 **GPU inference → network → gateway → 실제 상대 backend/database까지 전체 E2E 검증**이 완료됐다.

---

# IX. Network Failure / 초기 연결

# 42. Server가 없으면 Client가 종료되는 구조

Pi 초기 구조에서는 Server 연결 실패가 Runtime 전체 실패와 연결될 여지가 있었다.

### 문제

```text
Network DOWN
≠
Camera DOWN
≠
Inference DOWN
```

이다.

### 해결

Network Worker가 별도로 connection lifecycle을 관리한다.

```text
Vision Thread
→ 계속 실행

Network Worker
→ reconnect
→ reconnect
→ reconnect
```

### 결과

Server가 없어도 Vision 자체는 계속 실행될 수 있는 구조가 됐다.

이 원칙은 Jetson에서도 그대로 승계됐다.

---

# 43. `Connection refused`의 의미

Relay/Gateway가 실행되지 않은 상태에서 Jetson을 실행하면 connection refused가 발생할 수 있었다.

### 중요한 구분

이것은:

```text
TensorRT Failure X
Camera Failure X
```

가 아니라:

```text
Target endpoint에 listener 없음
```

이라는 Network 문제다.

### 해결

Vision loop와 reconnect path를 분리하여 Network 장애가 Vision core를 종료시키지 않도록 했다.

---

# X. Jetson OS / 초기 환경

# 44. 최초 SD Image / Boot 이상

초기 Jetson Nano 설정 과정에서 정상 GUI까지 올라가지 못하는 문제가 있었다.

화면에는 SQUASHFS 관련 read 오류 및 일부 Jetson 관련 system 메시지가 나타났다.

### 판단

애플리케이션을 만지기 전에:

```text
OS Image
Filesystem
SD
Boot Environment
```

문제로 분리했다.

### 해결

정상 Jetson Nano용 image를 다시 SD card에 기록하여 환경을 재구축했다.

### 결과

정상 boot 후:

```text
CUDA
TensorRT
OpenCV
Camera
```

검증으로 넘어갈 수 있었다.

---

# 45. SD Image write가 약 33분 걸림

약 29GB 규모 image write가 상당히 오래 걸렸다.

### 질문

GPU를 사용하면 빨라질 수 있는가?

### 원인

Image flashing은 GPU inference 작업이 아니다.

병목은:

```text
SD Card Write
Storage I/O
Reader
```

등이다.

### 의미

성능 문제를 무조건 CPU/GPU 문제로 연결하지 않고 **작업 특성에 따라 병목 자원을 구분**했다.

---

# XI. Network / SSH 개발환경

# 46. Wi-Fi `nmcli` 권한 문제

Jetson에서 Wi-Fi 설정 중 일반 사용자 권한으로 NetworkManager 설정이 제대로 적용되지 않는 문제가 있었다.

### 해결

필요한 network configuration 명령에 한해:

```bash
sudo nmcli ...
```

를 사용했다.

### 최종

```text
wlan0
10.10.16.153/24
Gateway 10.10.16.253
```

구성.

---

# 47. 원하는 IP가 이미 사용 중

처음 사용할 static IP를 정하는 과정에서 특정 주소가 이미 다른 장비에서 사용 중이었다.

### 문제

IP 충돌 상태에서 사용하면:

```text
SSH 불안정
잘못된 장비 접속
ARP 충돌
Network 통신 오류
```

등으로 이어질 수 있다.

### 해결

실제 사용 여부를 확인하고 다른 주소로 변경했다.

---

# 48. Ethernet과 Wi-Fi IP 혼동

Jetson에는:

```text
eth0
wlan0
```

가 존재했다.

초기에는 여러 IP가 보이면서 실제 SSH 대상이 혼동됐다.

### 해결

사용할 interface를 Wi-Fi `wlan0`로 확정하고 endpoint를:

```text
10.10.16.153
```

기준으로 통일했다.

---

# 49. VS Code Remote SSH가 갑자기 WSL을 엶

Jetson에 접속하려 했는데 VS Code Remote SSH가 예상과 달리 WSL 환경으로 들어가는 문제가 있었다.

### 원인 방향

SSH config/Host alias/target 설정이 꼬여 실제 접속 대상과 의도한 대상이 달라진 상태였다.

### 해결

불필요하게 복잡한 alias/config를 정리하고 실제:

```text
jetson@<Jetson IP>
```

형태로 접속 대상을 명확히 했다.

### 의미

IDE 문제처럼 보여도 실제로는 **Remote target configuration 문제**일 수 있다는 사례다.

---

# 50. GitHub SSH 인증

Jetson에서 직접 repo를 관리하기 위해 GitHub SSH를 구성했다.

최종 확인:

```bash
ssh -T git@github.com
```

결과:

```text
Hi triton0305! You've successfully authenticated,
but GitHub does not provide shell access.
```

### Host key 변경 경고

이전에 저장된 host key와 현재 연결 대상 정보가 맞지 않을 경우 `known_hosts` 정리가 필요했다.

### 결과

Jetson에서 직접:

```text
clone
pull
commit
push
```

할 수 있는 개발환경 구축.

---

# XII. Metrics / Build 관련

# 51. Metrics를 추가하다 Header/Source 불일치 가능성

성능 분석을 위해 Metrics 항목을 계속 확장했다.

예:

```text
Produced
Sent
Queue overflow
Discarded on PAUSE
Network State
Pi Link
Reconnect
```

### 문제

C++에서는 Header declaration과 `.cpp` definition signature가 달라지면 build가 깨진다.

### 해결

Metrics API를 한 단계씩 추가하고:

```text
수정
→ build
→ 실행
→ 실제 log 확인
```

방식으로 검증했다.

### 의미

이후 개발 규칙인 **작게 수정 → 빌드 → 실행 → 실제 결과 확인**이 강화된 배경 중 하나다.

---

# 52. 성능 로그 자체가 너무 부족했던 문제

초기에는:

```text
FPS
Inference
Queue
Dropped
```

정도만 있었다.

하지만 Network Architecture를 분석하기에는 부족했다.

### 개선

```text
Effective FPS
Avg Inference
Produced Vision msg/s
Sent Vision msg/s
Queue
Queue overflow dropped
Discarded on PAUSE
Network State
Pi Link
Pause Reason
Reconnect count
```

까지 확장했다.

### 결과

예를 들어:

```text
Produced=Sent
Queue=0
```

이면 Consumer가 producer를 따라가고 있다는 걸 바로 알 수 있게 됐다.

### 의미

**Observability 자체가 Troubleshooting 기능**이 됐다.

---

# XIII. Graceful Shutdown

# 53. 여러 Thread가 있는 상태에서 Ctrl+C 종료

Vision/Network/Queue가 분리되면서 단순히 main을 종료하면 Worker가 socket이나 condition variable에서 대기하고 있을 수 있다.

### 필요 사항

```text
stop flag
Queue wake-up
Network thread 종료
resource release
join
```

등을 고려해야 했다.

### 결과

정상적인 종료 흐름을 유지하도록 구성했다.

### 의미

Thread 분리의 장점만 얻는 게 아니라 **Lifecycle 관리까지 함께 처리**해야 한다는 사례다.

---

# XIV. 데이터 의미 / Pi에서 승계된 중요한 문제

# 54. Detection Count ≠ Vehicle Count

YOLO가 한 차량을 여러 frame에서 탐지하면:

```text
Frame 1 → car
Frame 2 → car
Frame 3 → car
...
```

가 발생한다.

따라서 Detection row가 10개라고 차량 10대가 아니다.

### 구분

```text
Detection Count
≠
Unique Vehicle Count
≠
Traffic Flow
≠
Congestion
```

### 의미

Jetson에서 FPS가 올라가면 같은 차량에 대한 Detection sample이 오히려 더 많아질 수 있다.

따라서 GPU가 빨라져도 이 의미론적 문제는 해결되지 않는다.

---

# 55. NMS ≠ Tracking

### NMS

```text
같은 Frame 내부
중복 Detection 제거
```

### Tracking

```text
서로 다른 Frame 사이
동일 객체 추정
```

### 문제

NMS threshold를 조정한다고 여러 frame에서 같은 차량이 계속 Detection되는 문제가 해결되지 않는다.

### 의미

Computer Vision Algorithm 각각의 역할을 분리했다.

---

# 56. Tracking을 넣으면 모든 문제가 해결되는가

아니다.

Tracker를 추가하면 데이터 의미 자체가 달라진다.

```text
Raw Detection History
```

와:

```text
Tracked Unique Vehicle
```

은 서로 다른 데이터다.

### 초기 시행착오

Pi 개발 과정에서 Tracking/Traffic Count까지 Client에 넣었던 시점이 있었다.

### 최종 판단

Vision Client 책임을:

```text
Detection 생성
→ 전달
```

중심으로 축소했다.

### 의미

기능을 추가하기 전에 **어떤 데이터를 만들고 싶은지 먼저 정의해야 한다**는 Architecture 교훈으로 남았다.

---

# 57. Detection 단위와 DB 단위

한 frame에는 여러 Detection이 있을 수 있다.

초기 선택지:

```json
{
  "frame_id": 100,
  "detections": [
    {},
    {},
    {}
  ]
}
```

### 최종 선택

```text
Detection 1개
=
JSON 1개
=
message_id 1개
=
전송 단위 1개
=
DB row 1개
```

로 맞췄다.

### 의미

Protocol 단위와 Storage 의미 단위를 일치시켰다.

---

# 58. Event Time과 Arrival Time

Detection 발생:

```text
10:59:59
```

Network 지연 후 DB 도착:

```text
11:00:02
```

가 가능하다.

### 문제

Server receive time만 사용하면 Detection을 잘못된 시간대 데이터로 볼 수 있다.

### 해결

Camera read 직후 Client에서:

```text
timestamp_ms
```

를 생성한다.

Server는 별도로:

```text
received_at
```

을 저장할 수 있다.

### 의미

```text
Event Time
≠
Arrival Time
```

을 분리했다.

---

# 59. Model Coordinate와 Original Coordinate

Camera:

```text
640×480
```

Model:

```text
640×640
```

Letterbox를 사용한다.

### 문제

Model output 좌표를 그대로 Server에 보내면 padding/scale이 포함된 좌표가 된다.

### 해결

Letterbox scale/padding을 역변환해서 원본 frame 기준:

```text
x
y
width
height
```

를 생성했다.

### Protocol 의미

bbox는 **원본 영상 pixel coordinate**로 고정했다.

---

# 60. Model Capability ≠ Application Requirement

COCO YOLO는 많은 class를 출력한다.

프로젝트에 필요한 것은:

```text
2 car
3 motorcycle
5 bus
7 truck
```

이다.

Bicycle은 의도적으로 제외했다.

### 해결

모델을 다시 학습하지 않고 Post-processing에서 필요한 class만 filtering했다.

### 의미

```text
Model이 할 수 있는 것
≠
Application에서 사용할 것
```

을 분리했다.

---

# XV. 기존 Pi Reliability에서 승계된 문제

# 61. ACK 유실과 DB Duplicate

Pi Reliability 구조에서는:

```text
Client send
↓
Server DB write
↓
ACK
```

였다.

### 문제

DB write는 성공했는데 ACK만 유실될 수 있다.

Client는 timeout으로 판단해 다시 보낸다.

### 해결

Retry 시:

```text
same message_id
same payload
```

를 사용했다.

Server에서는 `message_id`를 idempotency key로 사용했다.

### 결과

동일 message를 다시 받아도 DB row가 중복 생성되지 않도록 했다.

### Jetson에서의 의미

이 경험 때문에 Jetson에서 ACK를 제거할 때도 **Reliability 정책을 그냥 삭제한 게 아니라 데이터 특성에 맞춰 Control 기반 정책으로 재설계**할 수 있었다.

---

# 62. Same ID + Different Payload

단순히:

```text
message_id UNIQUE
```

만 사용하면:

```text
정상 Retry
```

와:

```text
ID 재사용 버그
```

를 구분할 수 없다.

### 정책

```text
Same ID + Same Payload
→ 정상 duplicate/retry

Same ID + Different Payload
→ MESSAGE_ID_CONFLICT
```

### 의미

Idempotency와 data corruption/conflict를 분리했다.

---

# 63. 프로그램 재시작 후 message_id 충돌

단순 sequence:

```text
1
2
3
...
```

만 사용하면 process restart 후 다시 1부터 시작할 수 있다.

### 해결

```text
<device_id>-<boot_id>-<sequence>
```

사용.

### 역할

```text
device_id
→ Device 구분

boot_id
→ Process session 구분

sequence
→ Session 내 message 구분
```

### 의미

`frame_id`를 message identity로 오용하지 않았다.

---

# 64. Retry에서 Message를 다시 만들면 안 됨

Retry 때:

```text
새 message_id
새 timestamp
새 frame_id
```

를 만들면 Server는 새로운 Detection으로 볼 수 있다.

### 해결

Retry는:

> **새 메시지 생성이 아니라 동일 메시지 재전송**

으로 정의했다.

```text
same ID
same timestamp
same frame
same payload
```

를 유지했다.

---
