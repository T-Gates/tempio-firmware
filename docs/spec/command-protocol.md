# 명령 규약 (서버 → 노드)

## 전체 흐름

```
서버 (FastAPI)
  │  MQTT publish: tempio/{hub_id}/commands
  │  JSON: {"commands": [{target, type, payload}, ...]}
  ▼
Mosquitto (RPi, localhost:1883)
  │  Cloudflare tunnel (WSS :9001)
  ▼
허브 (ESP32, mqtt_handler.cpp)
  │  parseCommands() → MqttCommand 링버퍼에 적재
  │  portMUX 크리티컬 섹션 보호 (MQTT 태스크 → Arduino loop 태스크)
  ▼
허브 (main.cpp loop)
  │  mqtt_get_command() → 큐에서 하나씩 꺼냄
  │  dispatch_command() → type별 핸들러 호출
  ▼
허브 (cmd_dispatcher.cpp)
  │  JSON payload → BLE 바이너리 패킷 변환
  │  ble_send_to_node(target, packet, len)
  ▼
노드 (BLE CONFIG 특성 write)
  │  구조체 그대로 수신 → 동작 실행
  ▼
완료
```

## MQTT Command payload

```json
{
  "commands": [
    { "target": "aa:bb:cc:dd:ee:01", "type": "SET_INTERVAL", "payload": { "interval_sec": 1800 } },
    { "target": "aa:bb:cc:dd:ee:02", "type": "IR_TIMING", "payload": { "timings": [4500, 4500, 560, 1690] } },
    { "target": "aa:bb:cc:dd:ee:01", "type": "RESET_NODE", "payload": { "level": 0 } }
  ]
}
```

## 명령 타입별 상세

### SET_INTERVAL — 센서노드 측정 주기 변경

| 항목 | 값 |
|------|-----|
| type 문자열 | `"SET_INTERVAL"` (서버·펌웨어 정확히 일치) |
| payload | `{"interval_sec": N}` |
| 기본값 | 60 (필드 없으면) |
| 유효 범위 | 10 ~ 3600초 |
| BLE 패킷 | `SetInterval` 구조체 (3바이트): `[0x12, interval_sec(2B LE)]` |
| 대상 | 센서노드 |
| 노드 동작 | NVS에 저장, 다음 딥슬립부터 적용 |

### RESET_NODE — 노드 리셋

| 항목 | 값 |
|------|-----|
| type 문자열 | `"RESET_NODE"` |
| payload | `{"level": N}` |
| 기본값 | 0 (필드 없으면) |
| BLE 패킷 | `ResetNode` 구조체 (2바이트): `[0x13, level]` |
| 대상 | 모든 노드 |
| level 의미 | 0=재부팅, 1=페어링 삭제, 2=공장 초기화 |

### IR_TIMING — IR 신호 발사

| 항목 | 값 |
|------|-----|
| type 문자열 | `"IR_TIMING"` |
| payload | `{"timings": [9000, 4500, 560, 1690, ...]}` |
| BLE 패킷 | 가변 길이: `[0x02, count(2B LE), timing값들(count×2B LE)]` |
| 크기 제한 | 500바이트 (BLE MTU 512 - 여유분). 초과 시 거부 + 시리얼 로그 |
| 대상 | IR노드 |
| timings 의미 | IR LED on/off 교대 마이크로초. mark(on) → space(off) → mark → ... |
| 노드 동작 | 38kHz PWM 캐리어 생성 + 타이밍대로 GPIO 토글 |

## 규칙

1. **type 문자열 매칭**: 서버 `constants.py`와 펌웨어 `cmd_dispatcher.cpp`의 문자열이 정확히 일치해야 함. 오타 시 `unknown cmd` 로그 출력 후 무시.
2. **기본값**: payload에 필드가 없으면 ArduinoJson `|` 연산자로 기본값 적용 (파이썬 `dict.get(key, default)`).
3. **한 번에 여러 명령**: `commands` 배열에 여러 명령 가능. 큐(`CMD_QUEUE_MAX=8`)에 순서대로 적재, loop에서 하나씩 처리.
4. **큐 만석**: 큐가 다 차면 새 명령 버림 (오래된 명령 우선 처리).
5. **대상 미연결**: `ble_send_to_node()`가 false 반환 → 시리얼 "fail" 로그. 재시도 없음.
6. **분할 메시지**: MQTT 메시지가 버퍼(1024B)를 초과하면 무시 (`data_len != total_data_len` 체크).
