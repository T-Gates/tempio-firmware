# tempio 펌웨어 스펙

## BLE 프로토콜

### UUID 체계

128-bit UUID. 앞 8자리 `4c544d50` = ASCII "LTMP" (tempio).

| UUID | 용도 |
|------|------|
| `4c544d50-0001-...` | 서비스 |
| `4c544d50-0002-...` | DATA 특성 — 센서값(notify), IR 타이밍(write) |
| `4c544d50-0003-...` | CONFIG 특성 — 설정 명령(write only) |

### 메시지 타입

모든 메시지의 첫 바이트가 타입을 구분한다.

| 코드 | 이름 | 방향 | 특성 | 설명 |
|------|------|------|------|------|
| `0x01` | SENSOR_DATA | 센서노드 → 허브 | DATA (notify) | 온습도 + 조도 + 배터리 |
| `0x02` | IR_TIMING | 허브 → IR노드 | DATA (write) | raw IR 타이밍 배열 |
| `0x10` | ASSIGN_ID | 허브 → 노드 | CONFIG | 노드 ID 부여 |
| `0x11` | SET_INTERVAL | 허브 → 센서노드 | CONFIG | 측정 주기 변경 (초) |
| `0x12` | RESET_NODE | 허브 → 노드 | CONFIG | 리셋 (레벨별) |
| `0x20` | NODE_INFO | 노드 → 허브 | DATA (notify) | 연결 직후 자기 소개 |

### 데이터 구조체

모든 구조체는 packed, little-endian. 바이트 그대로 BLE 전송.

**SensorData (13바이트)**

| 오프셋 | 크기 | 필드 | 설명 |
|--------|------|------|------|
| 0 | 1 | type | `0x01` |
| 1 | 4 | temp | 온도 (°C), float |
| 5 | 4 | humidity | 습도 (%), float |
| 9 | 2 | ldr | 조도 raw ADC (0~4095) |
| 11 | 2 | battery_mv | 배터리 전압 (mV) |

**NodeInfo (7바이트)**

| 오프셋 | 크기 | 필드 | 설명 |
|--------|------|------|------|
| 0 | 1 | type | `0x20` |
| 1 | 1 | node_type | `0x01`=센서, `0x02`=IR |
| 2 | 1 | node_id | 허브 부여 ID (0=미할당) |
| 3 | 2 | battery_mv | 배터리 전압 (mV) |
| 5 | 1 | fw_major | 펌웨어 버전 major |
| 6 | 1 | fw_minor | 펌웨어 버전 minor |

**IR_TIMING (가변 길이)**

| 오프셋 | 크기 | 필드 | 설명 |
|--------|------|------|------|
| 0 | 1 | type | `0x02` |
| 1 | 2 | count | 타이밍 원소 개수 |
| 3 | count×2 | timings | mark/space 교대 (μs 단위) |

예: 삼성 냉방 26도 → count=199, 총 401바이트.

**AssignId (2바이트)**: `[0x10, node_id]`
**SetInterval (3바이트)**: `[0x11, interval_sec(2)]` — 기본 60초, 범위 10~3600
**ResetNode (2바이트)**: `[0x12, level]` — 0=재부팅, 1=페어링 삭제, 2=공장 초기화

### MTU

ESP32끼리 통신이므로 MTU 512바이트 협상. IR 타이밍 최대 ~600바이트도 한 번에 전송 가능. 패킷 분할 불필요.

Central(허브) 측에서 연결 후 `requestMTU(512)` 호출.

---

## 노드별 펌웨어 동작

### 허브 (ESP32-S3)

상시 전원(USB). WiFi + BLE central 동시 운영.

```
[부팅]
  WiFi 연결 (저장된 SSID/PW)
  실패 → BLE 프로비저닝 모드 (앱에서 WiFi 설정)

[메인 루프 — 1분 주기]
  SCD40 읽기 (CO2 + 온습도)
  BLE 연결된 센서노드 데이터 수집
  HTTP POST → 서버 (허브 + 센서노드 데이터 일괄)
  응답에 commands → BLE로 해당 노드에 전달
```

BLE 동시 연결: 안정 4~5대 (WiFi 병행). 소규모 매장(에어컨 1~3대 + 센서 1~2대) 충분.

### 센서노드 (ESP32-C3)

AA 건전지 구동. 대부분 딥슬립.

```
[딥슬립] → 1분 타이머 (서버에서 변경 가능)
[wake-up]
  BLE peripheral 광고
  허브 연결 대기
  SHT40 + LDR + 배터리 ADC 읽기
  SensorData notify 전송
  BLE 해제 → 딥슬립
```

### IR노드 (ESP32-C3)

AA 건전지 구동. IR 명령 대기.

```
[딥슬립] → 5분 타이머
[wake-up]
  BLE peripheral 광고
  허브 연결 대기
  명령 있음 → IR 타이밍 수신 → 38kHz PWM + GPIO로 IR 발사
  명령 없음 → 배터리 전압만 보고
  BLE 해제 → 딥슬립
```

IR 발사: `ledcWrite`로 38kHz 캐리어 생성, 타이밍 배열대로 on/off 토글.

---

## 연결 흐름

### 최초 페어링

1. 노드 전원 ON → NODE_INFO(node_id=0) 광고
2. 허브가 스캔 → TEMPIO_SERVICE_UUID 발견 → 연결
3. NODE_INFO 수신 → node_id=0 확인 → 신규 노드
4. ASSIGN_ID(node_id=N) 전송
5. 노드가 NVS에 ID 저장

### 일상 통신

1. 노드 wake-up → BLE 광고 (NODE_INFO에 할당된 ID 포함)
2. 허브 연결 → MTU 협상(512)
3. 센서노드: SensorData notify → 허브 수신
4. IR노드: 허브가 대기 중인 IR_TIMING write → 노드가 IR 발사
5. 연결 해제 → 노드 딥슬립

---

## 영업시간 기반 절전 (Scheduled Power Saving)

센서노드·IR노드는 건전지 구동이므로, 영업 외 시간에 측정 빈도를 낮춰 배터리 수명을 연장한다.

### 동작 방식

서버가 매장별 영업시간을 관리하고, 허브 리포트 응답(피기백)으로 `SET_INTERVAL` 명령을 내린다.

| 시간대 | 센서노드 주기 | IR노드 주기 | 이유 |
|--------|--------------|------------|------|
| 영업 중 | 1분 (기본) | 5분 | 실시간 모니터링 + 에어컨 제어 |
| 영업 외 | 30분 | 깨우지 않음 | 건물 열특성 데이터 수집 (RL 학습용) |

### 왜 완전 차단이 아닌 저빈도 모니터링인가

에어컨이 꺼진 뒤 매장 온도가 올라가는 속도 = 건물의 열특성.
이 데이터가 있어야 RL 모델이 "에어컨을 몇 시에 미리 켜야 영업 시작 시 쾌적한지" 예측할 수 있다.

### 구현

- **서버**: 매장별 영업시간 설정 (예: 09:00~22:00). 허브 리포트 응답에 `SET_INTERVAL` 포함.
- **허브**: 서버 응답의 `SET_INTERVAL`을 해당 노드에 BLE로 전달 (기존 프로토콜 그대로).
- **노드**: NVS에 interval 저장, 다음 딥슬립부터 적용. 추가 펌웨어 변경 불필요.
- **전환 타이밍**: 영업 시작/종료 시각 직전 리포트에서 interval 변경 명령 발송.

---

## OTA

허브만 해당. 센서노드/IR노드는 BLE OTA 하지 않음.

- **Phase 1 (MVP)**: 수동 USB 플래시
- **Phase 2 (5개 매장+)**: 서버 → WiFi HTTP OTA
  - 허브가 부팅 시(또는 매 N시간) 서버에 현재 버전 전송
  - 서버가 새 버전이면 바이너리 URL 응답
  - 허브가 다운로드 → OTA 파티션에 기록 → 재부팅
  - 실패 시 자동 롤백 (ESP32 기본 기능)

---

## 라이브러리

| 노드 | 라이브러리 | 용도 |
|------|-----------|------|
| 공통 | NimBLE-Arduino | BLE (ESP32 기본보다 가볍고 안정적) |
| 허브 | Sensirion I2C SCD4x | CO2 + 온습도 |
| 허브 | WiFi.h / HTTPClient | 서버 통신 |
| 센서 | Sensirion I2C SHT4x | 온습도 |
| IR | (직접 구현) | 38kHz PWM + 타이밍 토글 |
