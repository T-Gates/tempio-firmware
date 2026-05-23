# tempio-firmware

매장 냉난방 최적화 IoT 프로젝트 **서늘**의 ESP32 펌웨어 모노레포.

## 구조

```
허브 (ESP32-S3)  ←── WiFi ──→  서버 (FastAPI)
     ↕ BLE
센서노드 (ESP32-C3)    온습도 + 조도 측정
IR노드 (ESP32-C3)      에어컨 IR 제어
```

세 노드의 펌웨어를 하나의 PlatformIO 프로젝트로 관리한다.

```
src/
├── hub/main.cpp        허브 — WiFi + BLE central + SCD40 (CO2)
├── sensor/main.cpp     센서노드 — BLE peripheral + SHT40 + LDR
└── ir/main.cpp         IR노드 — BLE peripheral + IR LED
lib/
└── protocol/protocol.h BLE 메시지 포맷 (세 env 공유)
```

## 빌드 & 플래시

[PlatformIO](https://platformio.org/) 필요.

```bash
pio run -e hub    -t upload   # 허브
pio run -e sensor -t upload   # 센서노드
pio run -e ir     -t upload   # IR노드
pio device monitor            # 시리얼 모니터 (115200)
```

## 하드웨어

| 노드 | 보드 | 센서/출력 | 전원 |
|------|------|-----------|------|
| 허브 | ESP32-S3 | SCD40 (CO2 + 온습도) | USB |
| 센서노드 | ESP32-C3 SuperMini | SHT40 (온습도) + LDR (조도) | AA x2 + 부스트 |
| IR노드 | ESP32-C3 SuperMini | IR LED 940nm + 2N2222 | AA x2 + 부스트 |

## 현재 상태

- [x] 센서 읽기 (SCD40, SHT40, LDR)
- [ ] BLE 통신 (NimBLE)
- [ ] WiFi + 서버 연동
- [ ] 딥슬립
- [ ] IR 발사

자세한 개발 계획은 [ROADMAP.md](ROADMAP.md) 참조.
