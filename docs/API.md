# 서늘 HTTP API 명세

Base URL: `https://api.yuumi.wiki`

---

## 허브 → 서버

### `POST /api/hub/{hub_id}/report`

허브가 주기적(1분)으로 센서 데이터를 서버에 업로드. 응답에 명령을 피기백.

**Path Parameters**

| 이름 | 타입 | 설명 |
|------|------|------|
| `hub_id` | string | 허브 식별자 (BLE MAC 주소) |

**Request Body**

```json
{
  "wifi_rssi": -45,
  "free_heap": 180000,
  "uptime_ms": 360000,
  "co2": null,
  "hub_temperature": null,
  "hub_humidity": null,
  "connected_devices": [
    {
      "node_id": "aa:bb:cc:dd:ee:01",
      "node_type": "sensor",
      "battery_voltage": 2.85,
      "rssi": -62
    }
  ],
  "sensor_readings": [
    {
      "node_id": "aa:bb:cc:dd:ee:01",
      "temperature": 26.3,
      "humidity": 55.2,
      "lux": null
    }
  ]
}
```

| 필드 | 타입 | 필수 | 설명 |
|------|------|------|------|
| `wifi_rssi` | int? | N | WiFi 신호세기 (dBm) |
| `free_heap` | int? | N | ESP32 남은 힙 (바이트) |
| `uptime_ms` | int? | N | 부팅 후 경과시간 (ms) |
| `co2` | int? | N | SCD40 CO2 (ppm), Phase 6 |
| `hub_temperature` | float? | N | SCD40 온도 (°C), Phase 6 |
| `hub_humidity` | float? | N | SCD40 습도 (%), Phase 6 |
| `connected_devices` | DeviceInfo[] | N | BLE 연결 디바이스 목록 |
| `sensor_readings` | SensorReading[] | N | 센서노드 측정값 목록 |

**DeviceInfo**

| 필드 | 타입 | 필수 | 설명 |
|------|------|------|------|
| `node_id` | string | Y | 노드 BLE MAC 주소 |
| `node_type` | string | Y | `"sensor"` 또는 `"ir"` |
| `battery_voltage` | float? | N | 배터리 전압 (V) |
| `rssi` | int? | N | BLE RSSI (dBm) |

**SensorReading**

| 필드 | 타입 | 필수 | 설명 |
|------|------|------|------|
| `node_id` | string | Y | 센서노드 BLE MAC 주소 |
| `temperature` | float | Y | 온도 (°C) |
| `humidity` | float | Y | 습도 (%) |
| `lux` | float? | N | 조도 (lux) |

**Response `200 OK`**

```json
{
  "status": "ok",
  "commands": [
    {
      "target": "aa:bb:cc:dd:ee:02",
      "type": "SET_INTERVAL",
      "payload": { "interval_sec": 1800 }
    },
    {
      "target": "aa:bb:cc:dd:ee:03",
      "type": "IR_TIMING",
      "payload": { "timings": [4500, 4500, 560, 1690, ...] }
    }
  ]
}
```

**Command 타입**

| type | target | payload | 설명 |
|------|--------|---------|------|
| `SET_INTERVAL` | 센서노드 MAC | `{ "interval_sec": int }` | 측정 주기 변경 (10~3600) |
| `IR_TIMING` | IR노드 MAC | `{ "timings": int[] }` | IR raw 타이밍 (μs) 발사 |
| `RESET_NODE` | 노드 MAC | `{ "level": int }` | 0=재부팅, 1=페어링삭제, 2=공장초기화 |

---

## 조회 (대시보드·디버그용)

### `GET /api/hub-history`

허브 리포트 최근 100건.

**Response**: `HubReport[]` (timestamp 포함)

### `GET /api/sensor-history`

센서노드 측정값 최근 100건.

**Response**: `SensorReading[]` (hub_id, timestamp 포함)

### `GET /`

실시간 대시보드 (HTML). 5초 자동 새로고침, Chart.js 온습도 추이 차트.

---

## 예정 엔드포인트

| Phase | 엔드포인트 | 용도 |
|-------|-----------|------|
| 5 | `POST /api/hub/{hub_id}/command` | 즉시 명령 전달 (피기백 지연 불가 시) |
| 6 | — | CO2 데이터는 기존 report에 포함 |
| 후기 | `GET /api/stores` | 매장 목록 조회 |
| 후기 | `PUT /api/stores/{store_id}` | 매장 설정 (영업시간 등) |
| 후기 | `POST /api/hub/{hub_id}/ota` | OTA 펌웨어 업데이트 트리거 |

---

## 인증

현재: 없음 (테스트 단계)
계획: API key 헤더 (`X-API-Key`) — 허브 펌웨어에 하드코딩 or NVS 저장
