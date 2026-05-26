# Phase 4 설계: 허브 WiFi + MQTT 서버 연동

## 개요

ESP32 허브에 WiFi + MQTT를 추가하여 BLE로 수집한 센서 데이터를 서버에 전달하고, 서버 명령을 노드에 중계한다.

## 결정 사항

| 항목 | 결정 | 이유 |
|------|------|------|
| 통신 프로토콜 | MQTT (Mosquitto) | 논블로킹 publish, 양방향 실시간, IoT 표준 |
| WiFi 설정 | NVS + 시리얼 입력 | 코드 재플래시 없이 WiFi 변경 가능 |
| 업로드 타이밍 | BLE notify 즉시 | 센서 데이터 도착 즉시 서버 전달 |
| BLE+WiFi 공존 | 플래그 + 메인루프 패턴 | BLE 콜백 블로킹 방지. PubSubClient가 스레드 안전하지 않으므로 메인 스레드에서만 publish |
| Topic 구조 | 평면 (`seonul/{hub_id}/...`) | 허브 1대 기준, 단순. 매장 확장 시 앞에 store_id 추가 |
| 재연결 | 자동 재연결 + 버퍼 보관 | WiFi/MQTT 끊겨도 BLE 수집은 계속, 복구 시 최신 데이터 전송 |
| 서버 | MQTT 수신 + 기존 HTTP 대시보드 유지 | 데이터 파이프는 MQTT, 조회/대시보드는 HTTP |
| MQTT 라이브러리 | PubSubClient | 가장 인기, 예제 많음, 안정적. AsyncMqttClient 대비 커뮤니티 자료 풍부 |

## 파일 구조

```
firmware/src/hub/
├── main.cpp            오케스트레이터 (setup/loop)
├── ble_central.cpp/h   BLE 스캔·연결·수신 (기존 + 버퍼 추가)
├── wifi_manager.cpp/h  WiFi 연결 + NVS SSID/PW 관리
└── mqtt_client.cpp/h   MQTT 연결·publish·subscribe·명령 파싱
```

## 데이터 흐름

```
센서노드 ──BLE notify──→ ble_central (onDataNotify)
                              │
                              ▼
                         공유 버퍼 (SensorReport 구조체) + 플래그
                              │
                              ▼
main.cpp loop() ──────→ mqtt_client.publish()
                              │
                              ▼
                      Mosquitto 브로커 (라즈베리파이 localhost:1883)
                              │
                              ▼
                      FastAPI 서버 (MQTT 구독 → 인메모리 저장)
```

```
서버 ──MQTT publish──→ Mosquitto ──→ mqtt_client (콜백)
                                          │
                                          ▼
                                     명령 큐에 저장
                                          │
                                          ▼
                              main.cpp loop()에서 처리
                                          │
                                          ▼
                                   ble_send_to_node()
```

## 모듈별 책임

### wifi_manager.cpp/h

- NVS에서 SSID/PW 로드 → WiFi 연결
- 시리얼에서 `wifi set <SSID> <PW>` 입력 → NVS 저장
- `WiFi.setAutoReconnect(true)`로 끊김 시 자동 복구
- 외부 API: `wifi_init()`, `wifi_is_connected()`, `wifi_process_serial()`

### mqtt_client.cpp/h

- WiFi 연결 후 Mosquitto 브로커에 MQTT 연결
- `seonul/{hub_id}/commands` 구독 → 명령 수신
- 명령 파싱 → 명령 큐에 저장 (메인루프에서 `ble_send_to_node()` 호출)
- 브로커 끊기면 5초 간격 자동 재연결
- 외부 API: `mqtt_init()`, `mqtt_loop()`, `mqtt_publish_report(SensorReport*)`, `mqtt_has_command()`, `mqtt_get_command(Command*)`

### ble_central.cpp/h (기존 + 최소 수정)

- `onDataNotify`에서 SENSOR_DATA 수신 시 → 공유 버퍼에 복사 + 플래그 ON
- 새로 추가: `ble_get_pending_report(SensorReport*)` — 버퍼에서 데이터 꺼내기
- 기존 BLE 로직 변경 없음

### main.cpp (오케스트레이터)

```cpp
setup():
  wifi_init()         // NVS에서 WiFi 로드 → 연결 (최대 10초)
  ble_central_init()  // BLE 스캔 시작
  mqtt_init()         // 브로커 연결 + subscribe

loop():
  wifi_process_serial()                   // 시리얼 WiFi 설정 입력 처리
  ble_central_loop()                      // BLE 처리
  mqtt_loop()                             // MQTT keepalive + 재연결
  if (ble_get_pending_report(&report))    // 새 센서 데이터?
      mqtt_publish_report(&report)        //   → publish
  if (mqtt_has_command())                 // 서버 명령?
      mqtt_get_command(&cmd)              //   → BLE로 노드에 전달
      ble_send_to_node(cmd.target, ...)
```

## MQTT 메시지 포맷

### Topic 구조

| 방향 | topic | QoS |
|------|-------|-----|
| 허브 → 서버 | `seonul/{hub_id}/report` | 1 |
| 서버 → 허브 | `seonul/{hub_id}/commands` | 1 |

hub_id = ESP32 WiFi MAC 주소, 콜론 제거 소문자 (예: `aabbccddeeff`). 자동 생성.

### Report payload (허브 → 서버)

```json
{
  "wifi_rssi": -45,
  "free_heap": 180000,
  "uptime_ms": 360000,
  "connected_devices": [
    { "node_id": "aa:bb:cc:dd:ee:01", "node_type": "sensor", "battery_voltage": 2.85, "rssi": -62 }
  ],
  "sensor_readings": [
    { "node_id": "aa:bb:cc:dd:ee:01", "temperature": 26.3, "humidity": 55.2, "lux": null }
  ]
}
```

### Command payload (서버 → 허브)

```json
{
  "commands": [
    { "target": "aa:bb:cc:dd:ee:01", "type": "SET_INTERVAL", "payload": { "interval_sec": 1800 } },
    { "target": "aa:bb:cc:dd:ee:02", "type": "IR_TIMING", "payload": { "timings": [4500, 4500, 560, 1690] } },
    { "target": "aa:bb:cc:dd:ee:01", "type": "RESET_NODE", "payload": { "level": 0 } }
  ]
}
```

## WiFi 설정 플로우

### 시리얼 입력

```
> wifi set MySSID MyPassword123
"WiFi credentials saved. Connecting..."
"WiFi connected: 192.168.0.10"
```

NVS에 저장 → 재부팅 후에도 유지.

### 부팅 시퀀스

```
1. WiFi 연결 시도 (NVS에서 로드, 최대 10초 대기)
   ├── 성공 → MQTT 연결 → 정상 동작
   └── 실패 → BLE만 동작 (센서 데이터 수집 계속)
              시리얼에서 wifi set 입력 대기

2. BLE 스캔 시작 (WiFi 성공/실패 무관하게 항상)
```

WiFi 없어도 BLE는 돌아감. WiFi가 나중에 연결되면 그때부터 publish 시작.

### 재연결

- WiFi 끊김 → `WiFi.setAutoReconnect(true)` 자동 복구
- MQTT 끊김 → `mqtt_loop()`에서 5초 간격 재연결 시도
- 끊긴 동안 BLE 데이터 → 버퍼에 최신 1건만 유지 (덮어쓰기)

## 인프라

### Mosquitto (라즈베리파이)

```bash
sudo apt install mosquitto mosquitto-clients
```

- 포트: 1883 (로컬)
- 인증: 없음 (테스트 단계)
- systemd 자동 시작

### FastAPI 서버 변경

- `aiomqtt` 라이브러리 추가
- MQTT 구독 (`seonul/+/report`) → 데이터 수신 → 기존 인메모리 저장소에 저장
- 기존 HTTP 엔드포인트 (대시보드, 히스토리 조회) 유지
- 명령 전송: `POST /api/hub/{hub_id}/command` → MQTT publish → 허브 수신

### platformio.ini 변경

```ini
lib_deps =
    ...
    knolleary/PubSubClient   ; MQTT 클라이언트
```

## 향후 확장

- BLE 프로비저닝: 앱에서 WiFi SSID/PW 설정 (시리얼 대체)
- MQTT 인증: username/password 또는 TLS
- 매장 확장: topic에 store_id 추가 (`seonul/{store_id}/{hub_id}/...`)
- QoS 2: 중복 방지가 필요한 IR 명령에 적용
