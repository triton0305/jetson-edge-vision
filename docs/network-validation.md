# 네트워크·상태 제어 검증

## 검증 요약

Jetson Nano에서 차량을 인지하고 객체별 Vision JSON을 Raspberry Pi Gateway로 전달한 뒤, WSL Server의 SQLite에 저장하는 전체 경로를 검증했습니다. 장시간 실행에서도 서버 통신과 DB 저장의 지속 동작을 확인했습니다.

| 검증 항목 | 환경·방법 | 확인 결과 |
|---|---|---|
| 영상 인지 | USB Webcam + TensorRT FP16 | 차량 Detection 및 실시간 영상 처리 |
| 객체별 전송·저장 | Jetson → Pi → WSL → SQLite | Vision 생성·중계·수신·DB 저장 |
| 장시간 통신 | 실제 Jetson 및 서버 지속 실행 | 서버 통신과 DB 저장의 지속 동작 |
| DB 장애 | SQLite INSERT 오류 주입 | Server → Pi → Jetson PAUSE 전달 및 Vision 송신 중단 |
| DB 복구 | SQLite 오류 원인 제거 | RESUME 전달 및 새 Detection부터 송신 재개 |
| 링크 장애 | Jetson↔Pi TCP 연결 차단 | 연결 오류 감지·자체 PAUSE |
| 링크 복구 | 연결 차단 해제 | 자동 재접속·현재 세션 Control 동기화·새 결과 송신 |
| 재전송 정책 | 복구 전후 패킷·ID·시간 비교 | 확인한 구간에서 과거 데이터 replay 및 중복 ID 없음 |
| Queue·처리량 | Metrics 및 Gateway·Server 카운터 | 관찰 구간의 Queue 안정·overflow 0·생성/송신 처리량 대응 |
| 종료 처리 | SIGINT 및 blocking I/O 테스트 | socket shutdown·스레드 join·정상 종료 |
| 경량 Tracker | 자동 테스트 및 카메라 실행 | ID 연결·800 ms 만료·기존 Vision JSON 유지 |

검증 근거는 Jetson 실행 로그·패킷 캡처·자동 테스트와 사용자가 확인한 Pi/WSL 카운터·SQLite 저장 결과·장시간 실장비 실행입니다. 아래 수치와 이벤트 시간은 각각의 관찰 구간 기준이며, 장시간 실행의 총 시간은 별도로 수치화하지 않습니다.

## 통신·상태 제어 구조

- 하나의 full-duplex TCP 연결에서 Control RX와 Vision TX를 분리합니다.
- Control RX가 연결·재접속과 Control 수신을 담당하고, Data TX는 크기 제한 Queue의 Vision을 송신합니다.
- 메시지 경계는 4바이트 Big-endian 길이 헤더로 구분하며 Partial read/write를 처리합니다.
- Vision은 객체별 ACK 없이 연속 송신합니다. application retry·저장·replay는 사용하지 않습니다.
- PAUSE 시 JSON/ID 생성과 enqueue를 차단하고 대기 Queue를 비웁니다. Camera·TensorRT·Display는 계속 동작합니다.
- 프레임 획득 전 상태 변경 번호(epoch)를 기록하여 PAUSE/RESUME 경계를 가로지른 프레임과 오래된 메시지를 제외합니다.
- 연결·재연결 직후에는 PAUSED로 시작하며, 현재 세션의 RESUME을 받은 뒤 새 결과부터 전송합니다.
- 이미 송신을 시작한 TCP 프레임이나 송신 버퍼의 바이트는 회수할 수 없습니다. 새 송신과 과거 데이터 replay를 차단합니다.
- 종료 시 Queue를 닫고 socket shutdown으로 blocking I/O를 깨운 후 스레드를 join합니다.

### Timeout·연결 설정

| 항목 | 설정 |
|---|---|
| 연결 시도 | 최대 1초 |
| 재접속 간격 | 1초 |
| 송신 deadline | prefix와 payload 각각 최대 1초 |
| TCP keepalive | idle 10초·interval 3초·probes 3회 |
| TCP_USER_TIMEOUT | 20초 |
| Control idle timeout | 없음 |

실제 장애 감지 시간은 커널과 네트워크 상태에 따라 달라집니다. Reconnect count는 성공한 재접속 횟수이며 시도 횟수와 구분합니다. Sent는 전체 프레임의 로컬 socket 송신 완료를 의미하며, DB 저장은 서버·SQLite 결과로 확인합니다.

## 자동 테스트·실행 확인

### Network integration

실제 localhost TCP socket을 사용하여 다음을 확인했습니다.

- 최초 접속 실패 후 Queue가 비어 있어도 재접속.
- 분할 Control prefix/payload 수신과 잘못된 JSON·필드·action 처리.
- ACK 없는 Vision 연속 송신과 기존 JSON 의미 유지.
- PAUSE/RESUME·오래된 프레임 제외·새 세션 상태 동기화.
- 이전 세션 Control 제외와 장애 중 대기 데이터 replay 차단.
- 송신 정체 시 deadline·Queue 정리·재접속.
- blocking/partial receive 상태에서 종료.
- Overflow drop과 PAUSE discard 카운터 구분.
- Producer와 PAUSE의 동시 실행 100회.

### Network transport

- 테스트용 short send·EINTR·EAGAIN과 작은 단위 수신으로 1 MiB framed transfer 복원.
- 수신하지 않는 상대에 대한 송신 deadline.
- 동시 TX/RX/disconnect/reconnect 40회.
- 잘못된 payload 길이 처리.

### Tracker

전체 빌드와 Tracker / Network / Transport 테스트 3개 통과 기록을 확인했습니다. Tracker 테스트는 같은 클래스·일대일 ID 연결, IoU·중심 거리 경계, 800 ms 만료, 빈 Detection, 삭제 ID 재사용 금지 및 Tracking 전후 JSON 일치를 확인했습니다.

실제 카메라·TensorRT·localhost Gateway 실행에서는 Vision 244개 수신, 기존 JSON 필드 유지와 중복 message_id 없음을 확인했습니다. Tracker ID는 내부 화면 표시용이며 Vision JSON에는 포함하지 않습니다.

### 실제 카메라·통신

Camera/TensorRT와 localhost Gateway 실행에서 두 세션에 걸쳐 Vision 133개를 확인했습니다. PAUSE·RESUME·연결 종료·재접속·새 세션 대기·복구와 SIGINT 종료 코드 0을 확인했습니다. 안정 구간은 약 12 FPS·54.6–55 ms 추론, Queue 0–1·overflow 0이었습니다.

실제 Pi Gateway 연결에서도 PAUSE 및 RESUME Control 수신, 생성/송신 처리량 대응과 정상 종료를 확인했습니다. 전체 DB 저장과 장시간 통신은 실제 3노드 연동 결과로 확인했습니다.

## 실장비 장애·복구 기록

아래 이벤트 시간은 한국 표준시(KST)이며, 상세 장애 관찰 기록은 2026.10.01 실행 기준입니다.

### 정상 전송·DB 저장

Pi의 Received/Forwarded 및 WSL의 Received/Saved 카운터와 SQLite 저장 결과로 전체 경로를 확인했습니다. Jetson 로그·패킷에서도 연속 Vision 송신, Produced/Sent 대응과 관찰 구간의 overflow 0을 확인했습니다.

### DB 장애 → PAUSE

SQLite INSERT 오류로 Server가 `pause/database_write_failed`를 생성하고, Pi가 Jetson으로 전달했습니다.

| 항목 | 기록 |
|---|---|
| Jetson Control 수신 | 15:25:06.736524 |
| Control message_id | `server-wsl-01-000000000042` |
| action / reason | `pause / database_write_failed` |
| 완전한 PAUSED Metrics 구간 | 662개 구간에서 Produced=Sent=0 |
| Queue / overflow | 관찰 구간에서 모두 0 |
| 영상 처리 | 평균 약 12.22 FPS·54.65 ms 추론 유지 |

패킷 복원 구간에서 DB 복구 RESUME까지 Jetson→Pi Vision 송신은 0개였습니다. 전환 시점을 포함하는 Metrics 구간에는 전환 전 처리량이 함께 기록될 수 있으므로, 완전한 PAUSED 구간과 구분했습니다.

### DB 복구 → RESUME

| 항목 | 기록 |
|---|---|
| Jetson Control 수신 | 15:37:55.286723 |
| Control message_id | `server-wsl-01-000000000112` |
| action / reason | `resume / database_recovered` |
| 첫 복구 Vision 송신 | 15:37:55.433122 |

| 경계 | PAUSE 직전 | RESUME 직후 |
|---|---|---|
| Vision sequence | `00010878` | `00010879` |
| frame_id | `13258` | `22643` |
| 프레임 시간 | 15:25:06.590 | 15:37:55.348 |

PAUSE 동안 프레임 처리는 계속됐고 Vision sequence는 증가하지 않았습니다. 첫 복구 프레임의 시간은 RESUME 수신 이후였습니다.

15:43:57까지의 복구 관찰 구간에서 Vision 6,043개를 확인했습니다. RESUME보다 오래된 timestamp·중복 ID·캡처된 과거 ID replay는 없었습니다. Metrics 341개 구간의 평균 Produced/Sent는 약 16.665/16.659 msg/s, Queue 0–4·overflow 0이었습니다.

### Jetson↔Pi 링크 장애

Jetson↔Pi TCP 8000에 iptables DROP을 적용했습니다.

| 항목 | 기록 |
|---|---|
| 연결 오류 관찰 | 15:48:57.711965 · `pi_connection_lost` |
| PAUSED / Pi Link DOWN | 15:48:58.113072 |
| 확인 구간 | 15:50:43까지 Metrics 100개 |
| 송신·Queue | Produced=Sent=0·Queue=0·overflow=0 |
| 영상 처리 | 평균 약 12.18 FPS·54.73 ms 추론 유지 |

Pi에서도 Jetson 연결 끊김·PAUSED 전환·forwarding 중단을 확인했습니다.

### 링크 복구·새 세션 동기화

DROP 해제 후 새 연결에서 다음을 확인했습니다.

| 시각 | 이벤트 |
|---|---|
| 15:52:38.852018 | 새 SYN |
| 15:52:38.876689 | SYN-ACK |
| 15:52:38.919361 | `pause / upstream_drain_pending` 수신 |
| 15:52:38.919403 | `resume / server_ready` 수신 |
| 15:52:40.382166 | 첫 복구 Vision 송신 |

첫 복구 Vision의 frame_id는 `33367`, 프레임 시간은 15:52:40.293이었습니다. 관찰한 새 세션의 Vision 196개에서 RESUME 이전 송신·오래된 timestamp·중복 ID·캡처된 과거 ID replay는 없었습니다.

Metrics 75개 구간은 평균 Produced/Sent 약 2.4594/2.4593 msg/s, Queue 0–1·overflow 0이었습니다. Pi는 별도 관찰 구간에서 Received=Forwarded=177을 확인했습니다. 각 카운터는 서로 다른 관찰 구간 기준입니다.

## 검증 실행 명령

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j2
(cd build && ctest --output-on-failure)
DISPLAY=:1 ./build/bin/edge_vision <pi_gateway_ip> <pi_gateway_port>
```

이 문서는 기존 실행·테스트 기록과 실장비 확인 결과를 정리한 문서이며, 문서 수정 과정에서 테스트를 새로 실행한 것은 아닙니다.
