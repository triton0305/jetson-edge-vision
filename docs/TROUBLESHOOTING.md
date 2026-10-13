# Troubleshooting

Jetson Nano Vision Client의 개발·서버 연동 중 관찰한 문제와, 장애 주입으로 확인한 복구 동작을 정리했습니다. 각 사례는 증상, 원인 추적, 조치, 확인 결과를 중심으로 구성했습니다.

정리 기준일: 2026-10-13. 기존 실행·검증 기록을 바탕으로 작성했습니다. 세부 통신 검증은 [네트워크·상태 제어 검증](network-validation.md), 정책 결정 과정은 [설계 의사 결정 기록](DESIGN_PROCESS.md)에 정리돼 있습니다.

## 주요 사례

| 사례 | 문제 | 기록 성격 | 관련 원본 |
|---|---|---|---|
| 1 | GPU 추론 전환 후 Queue 포화 | 실제 개발 중 관찰 |
| 2 | TCP 연결 유지 중 DB 저장 실패 | DB 장애 주입·복구 검증 |
| 3 | 링크 복구 시 세션 상태와 데이터 신선도 | 링크 장애 주입·복구 검증 |

### 1. TensorRT 가속 후 Queue 포화 — Stop-and-Wait ACK 병목

**증상**

TensorRT FP16 전환으로 영상 처리 속도는 높아졌지만, 메시지 Queue가 `16/16`까지 차고 다음 로그가 반복됐습니다.

```text
Queue full: dropping best-effort message ...
```

**원인 추적**

추론 시간·FPS와 함께 메시지 생성량, 송신량, Queue 깊이와 drop을 확인했습니다. 기존 Network Worker는 객체별로 `send → ACK 대기 → ACK 확인 → 다음 send`를 수행했습니다. Vision Producer가 빨라지면서 생성 속도가 이 직렬 전송 경로의 처리 속도를 초과했습니다.

Queue 용량을 늘려도 지속적인 생성·송신 처리량 차이는 남으므로, ACK 대기를 포함한 Consumer 경로를 개선했습니다.

**조치**

정상 Vision 경로를 per-message ACK/Retry 방식에서 Continuous TCP TX로 전환했습니다. Control RX와 Data TX의 책임을 분리하고, Produced/Sent msg/s, Queue, overflow drop 등 지표를 확장했습니다.

**확인 결과**

기존 정상 실행 기록에서 Produced와 Sent가 대응하고 Queue와 overflow drop이 0인 구간을 확인했습니다. 실제 카메라·통신 검증의 안정 구간은 약 12 FPS, 추론 54.6~55 ms, Queue 0~1, overflow 0이었습니다.

Sent는 로컬 소켓에서 전체 메시지 송신이 완료됐다는 지표입니다. DB 저장은 Gateway·서버 카운터와 DB 결과로 별도 확인했습니다.

### 2. TCP 연결이 살아 있어도 DB 저장 실패 — PAUSE/RESUME 전파

**문제 조건**

ACK 제거 후에도 downstream 서버의 처리 가능 상태를 전달해야 했습니다. TCP 연결과 서버 프로세스가 유지된 상태에서 DB INSERT가 실패하는 조건을 검증했습니다.

**원인 추적**

Jetson이 직접 확인할 수 있는 것은 Pi와의 연결 상태입니다. DB write 오류는 해당 서버가 직접 관찰하므로, 서버가 장애를 판단하고 Pi Gateway를 통해 Jetson에 전달하도록 책임을 나눴습니다.

**조치**

Vision은 연속 송신하고, 상태는 별도 Control 메시지로 전달했습니다. DB write 실패 시 `pause / database_write_failed`, 복구 시 `resume / database_recovered`를 사용했습니다. PAUSE 동안 JSON/message_id 생성과 enqueue를 차단하고 대기 Queue를 비웠습니다. 카메라·추론·화면 표시는 계속 동작합니다.

**확인 결과**

2026-10-01 WSL/SQLite 검증 서버에서 INSERT 오류를 주입했습니다. Jetson은 15:25:06.736524 KST에 PAUSE를 수신했습니다.

| 확인 항목 | 기존 검증 기록 |
|---|---|
| 완전한 PAUSED Metrics | 662개 구간에서 Produced=Sent=0 |
| Queue / overflow | 관찰 구간 모두 0 |
| 영상 처리 | 평균 약 12.22 FPS, 추론 54.65 ms |
| 패킷 확인 | DB 복구 RESUME까지 복원 구간의 Vision 송신 0개 |
| 복구 | 15:37:55.286723 RESUME 수신, 15:37:55.433122 첫 Vision 송신 |

복구 관찰 구간의 Vision 6,043개에서 RESUME보다 오래된 timestamp, 중복 message_id, 캡처된 과거 ID replay가 없었습니다. 이 장애 주입 결과는 WSL/SQLite 검증 경로에 해당합니다.

### 3. 연결 복구 후 새 세션 동기화 — 오래된 데이터 차단

**문제 조건**

TCP 재연결 시 downstream의 준비 상태를 확인해야 합니다. 장애 중 생성된 메시지를 적재하면 복구 후 과거 Detection을 전송하게 되므로, 현재 시점의 데이터를 재개하는 정책을 정했습니다.

**조치**

최초 연결과 재연결 직후 PAUSED로 시작하고, 현재 세션의 RESUME을 받은 뒤 전송합니다. PAUSE 시 Queue를 비우고 신규 enqueue를 차단하며, 상태 변경 번호(epoch)로 PAUSE/RESUME 경계를 가로지른 프레임을 제외합니다. Network 재접속 중에도 영상 처리를 유지합니다.

이미 송신을 시작했거나 TCP 버퍼에 들어간 바이트는 회수할 수 없으므로, 새 송신과 과거 데이터 replay를 차단했습니다.

**확인 결과**

2026-10-01 Jetson↔Pi 연결에 iptables DROP을 적용하고 해제했습니다.

| 확인 항목 | 기존 검증 기록 |
|---|---|
| 장애 감지 | `pi_connection_lost`, PAUSED / Pi Link DOWN |
| 장애 중 Metrics | 100개 구간에서 Produced=Sent=0, Queue=0, overflow=0 |
| 영상 처리 | 평균 약 12.18 FPS, 추론 54.73 ms |
| 복구 세션 | 새 연결에서 PAUSE와 RESUME Control 수신 |
| 복구 Vision | 관찰한 196개에서 RESUME 이전 송신·오래된 timestamp·중복 ID·캡처된 과거 ID replay 없음 |

자동 Network Integration 테스트에서도 최초 접속 실패 후 빈 Queue 상태의 재접속, 새 세션 상태 동기화, 이전 세션 Control 제외와 대기 데이터 replay 차단을 확인했습니다.

## 빌드·초기 환경 사례

| 문제 | 확인 및 조치 | 결과 |
|---|---|---|
| JSON 개발 패키지 누락 | Jetson의 빌드 의존성을 확인하고 `sudo apt install nlohmann-json3-dev` 수행 | 기존 Serializer를 유지하며 빌드 정상화 |
| `trtexec` 실행 경로 확인 | TensorRT 패키지 설치 여부와 바이너리 위치를 각각 확인 | 실행 파일 위치 확인 후 TensorRT sanity test 진행 |
| SQUASHFS 읽기 오류·GUI 부팅 실패 | OS 이미지·파일시스템·SD 부팅 환경을 분리해 확인하고 Jetson Nano용 이미지 재기록 | 정상 부팅 후 CUDA·TensorRT·OpenCV·Camera 검증 진행 |

## 관련 문서

- [네트워크·상태 제어 검증](network-validation.md): 장애·복구 시각, Metrics, 패킷 및 자동 테스트 기록
- [설계 의사 결정 기록](DESIGN_PROCESS.md): ACK 대안, 데이터 신선도와 장치별 장애 책임
- [README](../README.md): TensorRT 포팅, 성능, Tracking, 배포와 메시지 규격
