# tempio -- 모노레포

매장 에어컨 온도 최적화 IoT 프로젝트 (SOMA 17기). 서비스명: 그린고래.

## 레포 구조
```
tempio/
├── firmware/           <- ESP32 펌웨어 (PlatformIO)
│   ├── platformio.ini  <- env 3개 (hub / sensor / ir)
│   ├── src/
│   │   ├── hub/        <- 허브 (ESP32, WiFi+BLE Central)
│   │   │   ├── config.h           <- 설정값 (MQTT 브로커 URI, 루프 주기)
│   │   │   ├── main.cpp           <- 오케스트레이터
│   │   │   ├── cmd/
│   │   │   │   ├── cmd_dispatcher.h/.cpp <- 명령 라우팅 + BLE 패킷 전송
│   │   │   │   └── hub_command.h/.cpp    <- 허브 자체 명령 (HUB_STATUS 등)
│   │   │   ├── dto/
│   │   │   │   └── sensor_report.h <- BLE·MQTT·main 간 공유 DTO
│   │   │   ├── ble/
│   │   │   │   ├── ble_central.h/.cpp <- BLE Central (스캔·연결·수신)
│   │   │   │   └── protocol.h      <- (lib/protocol/ 참조)
│   │   │   ├── net/
│   │   │   │   ├── wifi_manager.h/.cpp <- WiFi + NVS + 시리얼 설정
│   │   │   │   └── mqtt_handler.h/.cpp  <- MQTT WSS (ESP-IDF esp_mqtt)
│   │   │   └── util/
│   │   │       └── pending_pool.h  <- 노드별 펜딩 큐 (TTL 기반 만료)
│   │   ├── sensor/     <- 센서노드 (ESP32-C3, BLE Peripheral)
│   │   └── ir/         <- IR노드 (ESP32-C3, BLE Peripheral)
│   └── lib/
│       ├── protocol/   <- BLE 메시지 포맷 공유 헤더
│       └── storage/    <- NVS 영속 저장
├── server/             <- FastAPI 서버 (Python, 헥사고날 아키텍처)
│   ├── main.py              <- Composition root + lifespan DI
│   ├── config.py            <- Settings (env prefix: TEMPIO_)
│   ├── dependencies.py      <- FastAPI Depends() providers
│   ├── Dockerfile
│   ├── domain/
│   │   ├── models.py        <- Pydantic models (HubReport, Command 등)
│   │   └── services.py      <- SensorService (비즈니스 로직)
│   ├── ports/
│   │   ├── repository.py    <- SensorRepository ABC
│   │   └── message_broker.py <- CommandPublisher ABC
│   ├── adapters/
│   │   ├── inbound/
│   │   │   ├── http_routes.py    <- FastAPI router (대시보드, API)
│   │   │   └── mqtt_subscriber.py <- MQTT report 수신
│   │   └── outbound/
│   │       ├── sqlite_repo.py    <- SQLite + WAL
│   │       └── mqtt_publisher.py <- MQTT command 발행
│   └── templates/
│       └── dashboard.html
├── infra/                    <- 인프라 설정
│   ├── docker-compose.yml    <- Mosquitto + FastAPI 서버
│   └── mosquitto/config/
│       └── mosquitto.conf    <- TCP 1883 + WebSocket 9001
├── docs/               <- 스펙, 로드맵, 개발 노트
│   ├── spec/           <- 기술 스펙 (BLE, MQTT, 명령, 노드, API 등)
│   ├── ROADMAP.md
│   └── NOTES.md
└── CLAUDE.md
```

## 빌드/실행

### 펌웨어
PlatformIO 프로젝트 루트: `firmware/`
```bash
cd firmware
pio run -e hub    -t upload   # 허브 플래시
pio run -e sensor -t upload   # 센서노드 플래시
pio run -e ir     -t upload   # IR노드 플래시
pio device monitor            # 시리얼 모니터 (115200)
```

### 서버
```bash
# 직접 실행
cd server && uvicorn main:app --host 0.0.0.0 --port 8000

# Docker Compose (Mosquitto + 서버 함께)
cd infra && docker compose up -d
```

### Cloudflare 터널
| 도메인 | 대상 | 용도 |
|--------|------|------|
| `api.yuumi.wiki` | localhost:8000 | FastAPI 서버 |
| `mqtt.yuumi.wiki` | localhost:9001 | Mosquitto WebSocket |

systemd 서비스로 자동 실행. 허브는 `wss://mqtt.yuumi.wiki/mqtt` 로 MQTT 브로커에 접속한다.

## 시스템 아키텍처

```
서버(FastAPI) ←─ MQTT ─→ Mosquitto 브로커 ←─ MQTT over WSS ─→ 허브(ESP32)
  (RPi, 같은 호스트)         (RPi)         Cloudflare tunnel       (매장)
                                                                   ↕ BLE
                                                             센서노드(ESP32-C3)
                                                             IR노드(ESP32-C3)
```

- 허브는 매장에 배치되어 서버와 다른 네트워크에 있음
- Mosquitto 브로커가 RPi에서 실행되고, Cloudflare 터널이 WebSocket 리스너(9001)를 외부에 노출
- 허브는 WiFi로 `wss://mqtt.yuumi.wiki/mqtt` 에 접속하여 MQTT 통신
- 서버는 같은 호스트의 Mosquitto에 로컬 TCP(1883)로 연결

### 허브 (env:hub)
| 항목 | 내용 |
|------|------|
| 보드 | ESP32 |
| 센서 | SCD40 (CO2 + 온습도, I2C) -- 추후 연결 |
| 전원 | USB (콘센트) |
| 통신 | WiFi -> MQTT WSS (서버), BLE -> 센서노드·IR노드 |
| MQTT 라이브러리 | ESP-IDF esp_mqtt (WSS 네이티브 지원) |

### 센서노드 (env:sensor)
| 항목 | 내용 |
|------|------|
| 보드 | ESP32-C3 SuperMini |
| 센서 | DHT22 (테스트), SHT40 (프로덕션) + LDR (조도) |
| 전원 | AA 건전지 2개 + MT3608 부스트 컨버터 |
| 통신 | BLE -> 허브 |

### IR노드 (env:ir)
| 항목 | 내용 |
|------|------|
| 보드 | ESP32-C3 SuperMini |
| 액추에이터 | IR LED 940nm + 2N2222 트랜지스터 |
| 전원 | AA 건전지 2개 + MT3608 부스트 컨버터 |
| 통신 | BLE <- 허브 |

## 인증

API 인증은 `X-API-Key` 헤더 + `hmac.compare_digest` 검증 방식.

## 작업 시 주의
- `.cpp` 파일엔 `#include <Arduino.h>` 직접 포함 필요
- PlatformIO는 `firmware/src/` 아래 env별 서브폴더만 빌드 (`build_src_filter` 사용)
- `firmware/lib/protocol/protocol.h` -- 세 env 모두에서 공유, BLE 포맷 변경 시 여기만 수정
- NimBLE 2.5.0 주의사항은 `docs/NOTES.md` 참조
- 허브 MQTT 클라이언트는 ESP-IDF esp_mqtt 사용 (PubSubClient 아님, WSS 네이티브 지원)
- 서버 환경변수 prefix는 `TEMPIO_` (config.py의 Settings 참조)
- `server/.env`에 시크릿 포함 -- 커밋 금지
