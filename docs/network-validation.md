# Network / Control implementation and validation

Date: 2026-10-01. This records observed results, not an assertion that all 52 final validation items pass.

## Implemented behavior

- Single full-duplex TCP connection. One RX thread owns Control reads and connection/reconnection; one TX thread consumes the bounded queue.
- No Vision ACK, retry, persistence, or replay. Existing Vision JSON, device ID, message ID and timestamps remain unchanged.
- The state mutex covers PAUSE, queue clear, and the entire producer serialization/enqueue transaction. Frames carry an epoch captured before camera capture; a frame spanning a state transition is rejected before ID/JSON generation.
- TX rejects popped messages with an obsolete epoch. Resume waits for the prior admitted send to finish; PAUSE does not block inference on socket I/O. An already admitted/in-flight TCP frame may finish after receipt of PAUSE. Bytes already sent cannot be recalled. The queue and subsequent frames are discarded, not replayed.
- Connection success starts a new session in PAUSED with reason `pi_connection_restored`. Only current-session Control can resume. Reconnect runs independently of detections and queue occupancy.
- TcpClient snapshots one fd per framed operation, allows simultaneous TX/RX, calls shutdown before waiting for I/O locks, and closes only when both I/O users have exited. Connect/disconnect use a separate lifecycle lock. No socket-state lock is held across blocking receive.
- Connect attempt: at most 1 second; reconnect delay: 1 second. Send prefix and payload each have a 1-second deadline. Keepalive: idle 10s, interval 3s, probes 3, TCP_USER_TIMEOUT 20s. No per-Control idle timeout.
- Stop closes the queue, wakes retry waits, shuts down socket I/O, joins both threads, and clears pending data. Runtime exceptions in Vision are caught and take the same network cleanup path.

## Executed checks

1. Full CMake build on the Jetson succeeded.
2. `network_integration`: real localhost TCP, initial refused connection with empty queue, fragmented Control prefix/payload, invalid JSON/types/actions, 40 ACK-free messages with original JSON semantics, Control silence >1500ms, PAUSE/RESUME, old-frame rejection, EOF detection, reconnect and current-session synchronization, rejection of old-session Control, stalled-peer TX failure/queue clear/reconnect with no replay, blocked partial-prefix receive shutdown, separate overflow/discard counters, 100 producer-vs-PAUSE cycles.
3. `network_transport`: 1 MiB framed transfer with test-only forced short sends, EINTR and EAGAIN, and small receiver reads, non-reading peer/send deadline, 40 concurrent TX/RX/disconnect/reconnect cycles, invalid payload length.
4. Actual camera/TensorRT + localhost Gateway: 133 Vision messages across two sessions; pause, resume, disconnect, reconnect, new-session wait, recovery and SIGINT exit code 0. Stable segments near 12 FPS and 54.6–55 ms inference, Queue 0–1, no overflow. PAUSED full reporting windows showed Produced=Sent=0 while inference continued. GTK/imshow calls ran without errors; no independent visual quality review was performed.
5. Actual Pi Gateway `10.10.16.243:8000`: received `pause/upstream_drain_pending`, then `resume/server_ready`. RUNNING segments ~7.94–9.72 FPS, inference ~54.8–61.2 ms, Queue 0–1, overflow 0; Produced/Sent followed each other. SIGINT exit code 0. Short run, including model warm-up; not an endurance benchmark or proof of WSL/SQLite delivery.

Metrics windows can straddle a transition: a line displaying PAUSED may still include pre-pause production from that reporting interval. Sent means the entire frame was accepted by the local socket, not confirmed DB delivery.

## 2026-10-01 real-environment evidence

All event times below are KST (UTC+09:00). Observer log timestamps use Europe/Amsterdam (UTC+02:00); add seven hours. Observer timestamps indicate when the log follower read a line, not the precise application state-change instant. Packet timestamps identify captured traffic.

Evidence provenance:

- **Jetson direct evidence:** [DB runtime log](/tmp/jetson-db-observation-20261001-082139/runtime-live.log), [DB pcap](/tmp/jetson-db-observation-20261001-082139/wire.pcap), [link-fault runtime log](/tmp/jetson-fault-observation-20261001-084558/runtime-live.log), [link-fault pcap](/tmp/jetson-fault-observation-20261001-084558/wire.pcap). These are local temporary observation files, not versioned artifacts. Results below refer to the inspected windows, not later appended data.
- **Pi/WSL evidence supplied by the user:** SQLite INSERT failure trigger and its removal, SQLite storage/count recovery, Pi Control reception/state transitions/forwarding, and Pi/WSL counters. Those remote raw logs were not accessible from this Jetson session. Their results are attributed to the user, not claimed as independently inspected here.
- The earlier short Pi run in Executed checks #5 did not establish DB delivery by itself. The additional user-supplied 3-node evidence below establishes storage for the reported test, not for every local socket send.

### E1 — Normal 3-node E2E

User-supplied Pi/WSL results confirm Jetson → Pi → WSL → SQLite storage, Pi Received≈Forwarded and WSL Received≈Saved during normal intervals, stable queues and overflow=0. Jetson logs/wire independently show continuous Vision TX, Produced≈Sent and no overflow in the inspected windows. No endurance test was performed. Queue stability does not mean a universal Queue≤1 guarantee: DB recovery reached Queue=4.

### E2 — Same-event DB failure / PAUSE

The user reports an actual SQLite INSERT failure trigger causing WSL `pause/database_write_failed`, Pi receipt/PAUSED transition and forwarding of the same Control to Jetson.

Jetson wire captured **15:25:06.736524**, message_id **`server-wsl-01-000000000042`**, action `pause`, reason `database_write_failed`. Control processing was observed at **15:25:06.812505**, and the first PAUSED Metrics line at **15:25:06.913040**. Starting **15:25:07.915473**, complete PAUSED reporting intervals had Produced=Sent=0. The inspected failure snapshot contained 663 PAUSED Metrics windows: the first included pre-transition activity; the other 662 had zero production/send. Queue and overflow stayed 0. Camera/inference/Metrics continued, averaging ~12.22 FPS and ~54.65 ms inference. Independent Display quality was not assessed.

The packet reconstruction through the subsequent DB resume contained **zero Jetson→Pi Vision frames during PAUSE**. Additional Pi `pause/wsl_connection_lost`, `pause/wsl_connection_restored`, and repeated DB-failure Controls occurred; Jetson remained PAUSED. These observed reason strings do not establish an independently diagnosed downstream cause. `Discarded on PAUSE=0` means this interval does not prove removal of a non-empty pending queue.

### E3 — Same-event DB recovery / RESUME

The user reports WSL `resume/database_recovered`, Pi reception/RUNNING and forwarding with original message_id/reason preserved. Jetson captured **15:37:55.286723**, message_id **`server-wsl-01-000000000112`**, action `resume`, reason `database_recovered`; processing was observed at **15:37:55.326465**. RUNNING appeared in Metrics at **15:37:56.228622**. First resumed Vision was captured at **15:37:55.433122**.

| Boundary | Last pre-PAUSE Vision | First post-RESUME Vision |
|---|---|---|
| message_id sequence (boot_id 000019) | `00010878` | `00010879` |
| frame_id | `13258` | `22643` |
| Jetson frame timestamp, KST | 15:25:06.590 | 15:37:55.348 |

The first resumed frame timestamp is after Control receipt. Frame processing continued while the Vision sequence did not advance across this observed pause boundary. Through the inspected recovery window ending **15:43:57 KST**, **6,043 Vision** frames had zero timestamps preceding resume receipt, zero duplicate IDs and zero replay of captured pre-pause IDs. All 341 recovery Metrics windows were RUNNING; mean reported Produced/Sent rates were ~16.665/~16.659 msg/s, Queue=0–4, overflow=0. This is evidence for the inspected interval, not a global replay/endurance guarantee.

### E4 — Actual Jetson↔Pi link failure

The user applied iptables DROP for Jetson↔Pi TCP 8000 only and reports Pi `jetson_connection_lost` → jetson=DOWN / pi=PAUSED, forwarding stopped and Queue=0/overflow=0.

Jetson `pi_connection_lost` was observed at **15:48:57.711965**, followed by PAUSED/Pi Link DOWN at **15:48:58.113072**. Through **15:50:43.534039**, all 100 inspected Metrics intervals had Produced=Sent=0, Queue=0 and overflow=0. Camera/inference/Metrics continued (~12.18 FPS, ~54.73 ms mean inference).

No new SYN was visible in the captured DROP interval, and runtime does not log each failed connect attempt. Thus continuous retry attempts during DROP were **not directly observed**; local OUTPUT filtering may prevent packets reaching capture. Reconnect count=0 is a successful-reconnection counter, not an attempt counter. Recovery in E5 demonstrates an autonomous connection attempt while PAUSED once traffic was permitted.

### E5 — Actual link recovery / new session synchronization

After the user removed DROP, SYN **15:52:38.852018** and SYN-ACK **15:52:38.876689** established a new connection `10.10.16.153:59160 → 10.10.16.243:8000` (previous Jetson port: 58562). `pi_connection_restored; awaiting control` was observed at **15:52:38.896459**.

| Jetson wire receipt, KST | action / reason | message_id |
|---|---|---|
| 15:52:38.919361 | pause / upstream_drain_pending | `gateway-pi-01-1790826626374-20388-000000000242` |
| 15:52:38.919403 | resume / server_ready | `server-wsl-01-000000000129` |

RUNNING and Reconnect count=1 appeared at **15:52:39.297857**. First resumed Vision was captured at **15:52:40.382166**, frame_id **33367**, frame timestamp **15:52:40.293**, message_id `vision-pi-01-000019-00019500`.

Through **15:53:57 KST**, the inspected new session contained **196 Vision** frames: zero before RESUME receipt, zero old timestamps, zero duplicate IDs and zero replay of captured old-session IDs. Across 75 Metrics intervals, Produced/Sent means were ~2.4594/~2.4593 msg/s, Queue=0–1 and overflow=0. The user separately reports Pi **Received=177 / Forwarded=177** in its recovery observation window. These are different observation windows; 177 is not asserted to equal the Jetson 196-frame sample. Packets hidden by DROP were not reconstructed or claimed to be fully accounted for.

## Final 52-item evidence map

Each item has exactly one primary evidence classification:

- **Observed:** the relevant behavior was observed in the stated real-environment window, including explicitly attributed user-supplied Pi/WSL evidence. This is a scoped result, not an unconditional PASS.
- **Local:** the decisive verification is a synthetic Gateway, standalone or fault-injected local test. Real-environment evidence may support it but does not independently prove the full condition.
- **Open:** the stated condition still lacks sufficient verification. Partial/stress evidence is retained, not promoted into proof.

Neither source inspection nor build success alone grants PASS. Counts below apply to the 52 numbered items only; cross-cutting limitations follow separately.

| # | Validation item | Status | Evidence / scope |
|---|---|---|---|
| 1 | Existing TensorRT Vision works | Observed | Actual camera/TensorRT runs; E1–E5. |
| 2 | Effective FPS maintains ~7.4 baseline | Observed | Short-run warm intervals above baseline; E2/E4 ~12 FPS. No endurance/comparable long benchmark. |
| 3 | No per-message Vision ACK | Observed | E1/E3/E5 wire progress without application ACK; local 40-message test supports. |
| 4 | Continuous Vision TX | Observed | E1 normal traffic and E3/E5 recovery traffic. |
| 5 | 4-byte big-endian framing | Observed | Real Control/Vision pcap reconstructed with this framing. |
| 6 | Partial write handling | Local | Forced short send, EINTR/EAGAIN and full 1 MiB reconstruction in network_transport. |
| 7 | Control receive | Observed | Exact E2/E3 IDs and E5 current-session Controls. |
| 8 | Shared Vision/Control envelope | Observed | Captured real payloads use version/type/device_id/message_id/data. |
| 9 | Existing Vision JSON semantics | Observed | Captured classes, confidence, frame/timestamp/bbox; local serializer checks support. |
| 10 | Existing message_id policy | Observed | Real boot/sequence IDs; E3 00010878 → 00010879. |
| 11 | Original timestamp_ms meaning | Observed | Captured Unix-ms frame timestamps; E3/E5 post-resume frames. |
| 12 | Direct Pi connection-loss detection | Observed | E4 local pi_connection_lost during targeted DROP. |
| 13 | pi_connection_lost reason | Observed | E4 exact runtime reason. |
| 14 | No device-down/crash cause inferred from TCP loss | Observed | E4 logs report link loss without claiming a physical cause. |
| 15 | Pi connection restoration detected | Observed | E5 handshake and restored log. |
| 16 | pi_connection_restored reason | Observed | E5 exact runtime reason. |
| 17 | Self-PAUSE on Pi link failure | Observed | E4 PAUSED with Pi Link DOWN. |
| 18 | Immediate pending Queue CLEAR on PAUSE | Local | Local non-empty queue clear/race tests; E2/E4 Queue=0 but no positive discard evidence. |
| 19 | No new Vision JSON while PAUSED | Local | Producer callback rejection tested locally; real zero Produced and sequence continuity corroborate, no JSON-call trace. |
| 20 | No Queue push while PAUSED | Local | Producer/pause race test; real Queue=0 corroborates, no push-call trace. |
| 21 | No Vision backlog during PAUSE | Observed | E2 Queue=0/zero wire Vision; E3/E5 no stale frames in sampled recovery. |
| 22 | Camera continues during PAUSE | Observed | E2/E4 continuing frame/FPS processing; E3 frame_id advances across PAUSE. |
| 23 | TensorRT continues during PAUSE | Observed | E2/E4 continuing inference measurements. |
| 24 | Display continues during PAUSE | Open | imshow/GTK ran without errors, but independent visual quality/continuity verification is incomplete. |
| 25 | Metrics continues during PAUSE | Observed | E2/E4 uninterrupted PAUSED Metrics windows. |
| 26 | Receive wsl_connection_lost Control | Observed | Repeated real Pi pause/wsl_connection_lost in E2 observation. |
| 27 | Receive wsl_connection_restored Control | Observed | Repeated real Pi pause/wsl_connection_restored in E2; reason alone does not mean resume. |
| 28 | Receive WSL-originated DB failure Control | Observed | E2 exact server-wsl-01-000000000042; remote trigger/forwarding evidence user-supplied. |
| 29 | Preserve Control reason | Observed | E2/E3 exact reason/ID matching against user-supplied Pi evidence. |
| 30 | Queue CLEAR on Control PAUSE | Local | Non-empty local queue clear test; real E2 queue stayed empty without measurable discarded items. |
| 31 | Control RESUME handling | Observed | E3 database_recovered and E5 server_ready. |
| 32 | Only new Detection after RESUME | Observed | E3/E5 first frame timestamps after resume wire receipt. |
| 33 | No replay of failure-period Detection | Observed | E3 6,043-frame and E5 196-frame samples have no old timestamps; capture limits apply. |
| 34 | No application-level Vision retry | Observed | Observed recovery samples contain no duplicate IDs or captured-old-ID replay; TCP retransmissions are distinct. |
| 35 | Separate TX / Control RX roles | Observed | Controls processed during active Vision TX; real PAUSE/RUNNING transitions and local duplex tests support. |
| 36 | Only one socket receive owner | Local | Local RX/TX lifecycle tests plus implementation inspection; no runtime all-thread recv trace. |
| 37 | No concurrent TX/RX socket race | Open | 40 lifecycle stress cycles passed; no exhaustive/race-detector proof. |
| 38 | Thread-safe connect/disconnect | Local | Concurrent lifecycle interruption stress passed; not proof of every scheduling case. |
| 39 | Reconnect operates while PAUSED | Observed | E5 autonomous SYN/new session while PAUSED; local empty-queue retry test. Failed attempts during DROP not directly observed. |
| 40 | New-session downstream synchronization | Observed | E5 new session pause then resume; local stale-session rejection test supports. |
| 41 | No Vision before synchronization | Observed | E5 zero new-session Vision before RESUME. |
| 42 | Produced Vision msg/s measured | Observed | E1/E3/E5 real Metrics. |
| 43 | Sent Vision msg/s measured | Observed | E1/E3/E5 real Metrics; local socket acceptance, not DB ACK. |
| 44 | Produced≈Sent in normal operation | Observed | E3 ~16.665/~16.659; E5 ~2.4594/~2.4593 within sampled windows. |
| 45 | No sustained normal Queue saturation | Observed | E3 Queue=0–4, E5 0–1, overflow=0; endurance remains Open separately. |
| 46 | Separate overflow and PAUSE discard | Local | Local overflow=1/discard=2 test; real counters present but discard=0 in cited failure intervals. |
| 47 | ACK bottleneck not reintroduced | Observed | E1/E3/E5 continuous traffic without per-message ACK; local no-ACK test supports. |
| 48 | No crash | Observed | No crash in executed short-run/E2–E5 observations; no endurance claim. |
| 49 | No deadlock | Observed | Progress through executed pause/recovery/shutdown tests; no universal absence proof. |
| 50 | No socket race | Open | Stress and recovery observations passed; race-detector/exhaustive coverage outstanding. |
| 51 | Graceful shutdown | Observed | Earlier actual camera/Pi SIGINT exited 0; local blocked/partial RX shutdown passed. Current fault-test process was retained. |
| 52 | Normal recovery after reconnect | Observed | E5 real new session, READY/RESUME, fresh Vision; user reports Pi Received=Forwarded=177. |

**52-item totals: Observed 41 / Local 8 / Open 3. This is not 52/52 PASS.**

## Open limitations and remaining validation

- **Long-duration endurance: Open, not performed.** Short observation intervals are not an endurance substitute; this limitation applies across FPS, throughput, queue stability, crash and deadlock claims.
- **Race-detector/exhaustive concurrency proof: Open.** Passing local stress and real recovery does not prove every race absent.
- Real PAUSE intervals with an already empty queue and Discarded=0 do not separately prove deletion of pending items. The non-empty discard evidence remains Local (#18/#30).
- JSON creation/enqueue calls are not individually traced in the real process. Metrics/wire/sequence evidence corroborates the policy; direct producer rejection tests remain Local (#19/#20).
- Display quality/continuity was not independently visually verified in some tests (#24).
- No claim covers the contents of every packet hidden by DROP. No replay was observed in the reconstructed samples; this is not proof for uncaptured bytes.
- Reconnect attempts throughout DROP were not directly captured. The runtime counts successful reconnects, and the new SYN after DROP removal proves autonomous recovery but not the complete attempt history.
- Pi/WSL results above are explicitly user-supplied. Their windows/counters are not silently equated with independently inspected Jetson samples.

## Reproduction

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j2
(cd build && ctest --output-on-failure)
DISPLAY=:1 ./build/bin/edge_vision 10.10.16.243 8000
```

The commands above are retained as reproduction instructions; none were executed for this documentation update. Real DB failure/recovery and targeted DROP/recovery evidence is now recorded above. Endurance, independent Display inspection and stronger concurrency verification remain outstanding. `/opt`, `pirun`, commit and push were not performed.
