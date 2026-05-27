# 노드별 동작

## 허브 (ESP32)

상시 전원(USB). WiFi + BLE central 동시 운영.

```
[부팅]
  WiFi 연결 (NVS에서 SSID/PW 로드, 최대 10초)
  ├── 성공 → MQTT 브로커 연결 (WSS)
  └── 실패 → BLE만 동작, 시리얼에서 wifi set 대기
  BLE 스캔 시작 (WiFi 성공/실패 무관)

[메인 루프 — 시분할(cooperative multitasking)]
  wifi_process_serial()   ← 시리얼 WiFi 명령 처리
  ble_central_loop()      ← 끊긴 연결 정리, 재스캔, 대기 노드 연결 (하나씩)
  mqtt_loop()             ← WiFi 상태 변화 시 MQTT start/stop
  센서 리포트 큐 → MQTT publish (즉시)
  명령 큐 → dispatch_command() → BLE write (하나씩)
  delay(100ms)
```

BLE 동시 연결: 안정 4~5대 (WiFi 병행). 소규모 매장(에어컨 1~3대 + 센서 1~2대) 충분.

### 허브 파일 구조

```
firmware/src/hub/
├── config.h                설정값 (MQTT 브로커 URI, 루프 주기, 큐 사이즈)
├── main.cpp                오케스트레이터 (setup/loop — 로직 없음)
├── cmd_dispatcher.cpp/h    MQTT 명령 → BLE 바이너리 패킷 변환·전달
├── dto/
│   └── sensor_report.h     BLE·MQTT·main 간 공유 DTO
├── ble/
│   ├── ble_internal.h      공유 타입 (ConnectedNode, nodes[], 헬퍼)
│   ├── ble_central.cpp/h   슬롯 관리 + 오케스트레이터 (공개 API)
│   ├── ble_connection.cpp/h 스캔 + 연결 + NimBLE 콜백
│   └── ble_data.cpp/h      notify 수신 + 센서 리포트 링 버퍼
└── net/
    ├── wifi_manager.cpp/h  WiFi 연결 + NVS SSID/PW 관리
    └── mqtt_handler.cpp/h  MQTT WSS 클라이언트 (ESP-IDF esp_mqtt)
```

## 센서노드 (ESP32-C3)

AA 건전지 구동. 대부분 딥슬립.

```
[딥슬립] → 1분 타이머 (서버에서 SET_INTERVAL로 변경 가능)
[wake-up]
  BLE peripheral 광고 (TEMPIO_SERVICE_UUID)
  허브 연결 대기 (5초 타임아웃)
  HUB_READY 대기 (3초 타임아웃)
  NODE_INFO notify 전송 (타입, 배터리, 펌웨어 버전)
  SensorData notify 전송
  BLE 해제 → 딥슬립
```

## IR노드 (ESP32-C3)

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

## 라이브러리

| 노드 | 라이브러리 | 용도 |
|------|-----------|------|
| 공통 | NimBLE-Arduino | BLE (ESP32 기본보다 가볍고 안정적) |
| 허브 | Sensirion I2C SCD4x | CO2 + 온습도 |
| 허브 | ESP-IDF esp_mqtt | MQTT (WSS 네이티브 지원, Cloudflare 터널 경유) |
| 허브 | ArduinoJson | JSON 직렬화/역직렬화 |
| 센서 | DHT sensor library (테스트) / Sensirion I2C SHT4x (프로덕션) | 온습도 |
| IR | IRremoteESP8266 | IR 프로토콜 (67개 프로토콜, 85+ 브랜드) |
