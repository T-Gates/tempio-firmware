# BLE 프로토콜

## UUID 체계

128-bit UUID. 앞 8자리 `4c544d50` = ASCII "LTMP" (tempio).

| UUID | 용도 |
|------|------|
| `4c544d50-0001-...` | 서비스 |
| `4c544d50-0002-...` | DATA 특성 — 센서값(notify), IR 타이밍(write) |
| `4c544d50-0003-...` | CONFIG 특성 — 설정 명령(write only) |

## 메시지 타입

모든 메시지의 첫 바이트가 타입을 구분한다.

| 코드 | 이름 | 방향 | 특성 | 설명 |
|------|------|------|------|------|
| `0x01` | SENSOR_DATA | 센서노드 → 허브 | DATA (notify) | 온습도 + 조도 + 배터리 |
| `0x02` | IR_TIMING | 허브 → IR노드 | DATA (write) | raw IR 타이밍 배열 |
| `0x10` | HUB_READY | 허브 → 노드 | CONFIG | subscribe 완료 신호 ("데이터 보내도 돼") |
| `0x12` | SET_INTERVAL | 허브 → 센서노드 | CONFIG | 측정 주기 변경 (초) |
| `0x13` | RESET_NODE | 허브 → 노드 | CONFIG | 리셋 (레벨별) |
| `0x20` | NODE_INFO | 노드 → 허브 | DATA (notify) | 연결 직후 자기 소개 |

## 데이터 구조체

모든 구조체는 packed, little-endian. 바이트 그대로 BLE 전송.

**SensorData (13바이트)**

| 오프셋 | 크기 | 필드 | 설명 |
|--------|------|------|------|
| 0 | 1 | type | `0x01` |
| 1 | 4 | temp | 온도 (°C), float |
| 5 | 4 | humidity | 습도 (%), float |
| 9 | 2 | ldr | 조도 raw ADC (0~4095) |
| 11 | 2 | battery_mv | 배터리 전압 (mV) |

**NodeInfo (6바이트)**

| 오프셋 | 크기 | 필드 | 설명 |
|--------|------|------|------|
| 0 | 1 | type | `0x20` |
| 1 | 1 | node_type | `0x01`=센서, `0x02`=IR |
| 2 | 2 | battery_mv | 배터리 전압 (mV) |
| 4 | 1 | fw_major | 펌웨어 버전 major |
| 5 | 1 | fw_minor | 펌웨어 버전 minor |

노드 식별은 BLE MAC 주소로 — 별도 ID 부여 불필요.

**IR_TIMING (가변 길이)**

| 오프셋 | 크기 | 필드 | 설명 |
|--------|------|------|------|
| 0 | 1 | type | `0x02` |
| 1 | 2 | count | 타이밍 원소 개수 |
| 3 | count×2 | timings | mark/space 교대 (μs 단위) |

예: 삼성 냉방 26도 → count=199, 총 401바이트.

**HubReady (1바이트)**: `[0x10]` — 허브가 서비스 탐색 + DATA 구독 완료 후 전송. 노드는 이걸 받으면 NODE_INFO를 보내도 안전.
**SetInterval (3바이트)**: `[0x12, interval_sec(2)]` — 기본 60초, 범위 10~3600
**ResetNode (2바이트)**: `[0x13, level]` — 0=재부팅, 1=페어링 삭제, 2=공장 초기화

## MTU

ESP32끼리 통신이므로 MTU 512바이트 협상. IR 타이밍 최대 ~600바이트도 한 번에 전송 가능. 패킷 분할 불필요.

Central(허브) 측에서 `NimBLEDevice::setMTU(512)` 호출.

## 연결 흐름

### 최초 연결

1. 노드 전원 ON → BLE peripheral 광고 (TEMPIO_SERVICE_UUID 포함)
2. 허브가 스캔 → TEMPIO_SERVICE_UUID 발견 → pending 큐에 주소 추가
3. loop()에서 큐에서 주소를 꺼내 연결 시도 (한 번에 하나 — 시분할)
4. 서비스 탐색 → DATA 특성 구독 (subscribe)
5. 허브가 CONFIG 특성으로 HUB_READY(`0x10`) 전송
6. 노드가 HUB_READY 수신 → NODE_INFO notify 전송 (타입, 배터리, 펌웨어 버전)
7. 노드는 BLE MAC 주소로 식별 — 별도 ID 부여 없음

### 일상 통신

1. 노드 wake-up → BLE 광고 (TEMPIO_SERVICE_UUID)
2. 허브 연결 → MTU 협상(512) → HUB_READY 전송
3. 노드: NODE_INFO → SensorData notify (센서노드), 또는 IR_TIMING 수신 대기 (IR노드)
4. 연결 해제 → 노드 딥슬립
