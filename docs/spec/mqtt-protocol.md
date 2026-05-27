# MQTT 프로토콜

## 설계 판단

| 항목 | 결정 | 이유 |
|------|------|------|
| 프로토콜 | MQTT (Mosquitto) | 논블로킹 publish, 양방향 실시간, IoT 표준 |
| 전송 계층 | WSS (WebSocket Secure) | Cloudflare 터널 경유에 필요. TCP 1883은 터널 불가 |
| MQTT 라이브러리 | ESP-IDF esp_mqtt | WSS 네이티브 지원. PubSubClient는 TCP만 지원하여 교체 |
| WiFi 설정 | NVS + 시리얼 입력 | 코드 재플래시 없이 WiFi 변경 가능 |
| 업로드 타이밍 | BLE notify 즉시 | 센서 데이터 도착 즉시 서버 전달 |
| BLE+WiFi 공존 | 공유 버퍼 + 플래그 + 메인루프 패턴 | BLE 콜백에서 직접 publish 안 함 |
| Topic 구조 | 평면 (`tempio/{hub_id}/...`) | 허브 1대 기준. 매장 확장 시 앞에 store_id 추가 |
| QoS | 0 | 현재 구현 기준. 센서 데이터는 주기적이므로 유실 허용 |

## 토픽 구조

| 방향 | topic | QoS |
|------|-------|-----|
| 허브 → 서버 | `tempio/{hub_id}/report` | 0 |
| 서버 → 허브 | `tempio/{hub_id}/commands` | 0 |

hub_id = ESP32 WiFi MAC 주소, 콜론 제거 소문자 (예: `aabbccddeeff`).
서버는 `tempio/+/report`를 구독하여 자동 처리.

## Report payload (허브 → 서버)

```json
{
  "wifi_rssi": -45,
  "free_heap": 180000,
  "uptime_ms": 360000,
  "co2": null,
  "hub_temperature": null,
  "hub_humidity": null,
  "connected_devices": [
    { "node_id": "aa:bb:cc:dd:ee:01", "node_type": "sensor", "battery_voltage": 2.85, "rssi": -62 }
  ],
  "sensor_readings": [
    { "node_id": "aa:bb:cc:dd:ee:01", "temperature": 26.3, "humidity": 55.2, "lux": null }
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

## 데이터 흐름

### 센서 데이터 (노드 → 서버)

```
센서노드 ──BLE notify──→ ble_data.cpp (onDataNotify)
                              │
                         리포트 링 버퍼 (portMUX 보호)
                              │
main.cpp loop() ──────→ mqtt_publish_report()
                              │
                      esp_mqtt → wss://mqtt.yuumi.wiki/mqtt
                              │
                      Cloudflare tunnel → Mosquitto (RPi, :9001)
                              │
                      FastAPI 서버 (aiomqtt, localhost:1883 구독)
```

### 명령 (서버 → 노드)

```
서버 ──MQTT publish──→ Mosquitto (localhost:1883)
                            │
                      Cloudflare tunnel (WSS :9001)
                            │
                      esp_mqtt 콜백 (허브, mqtt_handler.cpp)
                            │
                       명령 링버퍼 (portMUX 보호)
                            │
                main.cpp loop()에서 처리
                            │
                cmd_dispatcher.cpp → ble_send_to_node()
```

## 인프라

| 구성요소 | 주소 | 비고 |
|----------|------|------|
| Mosquitto (TCP) | `localhost:1883` | 서버 → 브로커, 로컬 전용 |
| Mosquitto (WebSocket) | `localhost:9001` | 허브 → 브로커, Cloudflare 경유 |
| Cloudflare 터널 (MQTT) | `mqtt.yuumi.wiki` → `localhost:9001` | ESP32 WSS 접속 |
| Cloudflare 터널 (HTTP) | `api.yuumi.wiki` → `localhost:8000` | 대시보드·API |
| Docker Compose | `infra/docker-compose.yml` | Mosquitto + FastAPI 함께 구동 |

## WiFi 설정

시리얼 모니터에서 `wifi set <SSID> <PW>` 입력 → NVS 저장. 재부팅 후에도 유지.

WiFi 없어도 BLE는 동작. WiFi 복구 시 esp_mqtt가 자동으로 start되어 publish 재개.

## 재연결

- WiFi 끊김 → `WiFi.setAutoReconnect(true)` 자동 복구
- MQTT 끊김 → esp_mqtt가 자동 재연결 처리 (내부 FreeRTOS 태스크)
- WiFi 끊긴 동안 BLE 데이터 → 버퍼에 최신 1건만 유지 (덮어쓰기)
