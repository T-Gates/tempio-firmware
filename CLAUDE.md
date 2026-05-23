# tempio — ESP32 펌웨어 모노레포

## 이 저장소
매장 에어컨 온도 최적화 IoT 프로젝트 (SOMA 17기)의 ESP32 펌웨어.
세 노드(허브·센서노드·IR노드) 펌웨어를 하나의 PlatformIO 프로젝트로 관리.

> 프로젝트 이름 미확정 — 임시로 "tempio" 사용 중

## 개발 환경
- **PlatformIO** + Arduino framework
- 빌드/플래시: PlatformIO IDE 사이드바 "Project Tasks" 패널의 env별 Upload 버튼

```powershell
pio run -e hub    -t upload   # 허브 플래시
pio run -e sensor -t upload   # 센서노드 플래시
pio run -e ir     -t upload   # IR노드 플래시
pio device monitor            # 시리얼 모니터 (115200)
```

## 코드 구조
```
tempio/
├── platformio.ini          ← env 3개 (hub / sensor / ir)
├── src/
│   ├── hub/main.cpp        ← 허브 펌웨어
│   ├── sensor/main.cpp     ← 센서노드 펌웨어
│   └── ir/main.cpp         ← IR노드 펌웨어
├── lib/
│   └── protocol/
│       └── protocol.h      ← BLE 메시지 포맷 공유 헤더
├── CLAUDE.md
├── ROADMAP.md
└── NOTES.md
```

## 시스템 아키텍처

### 허브 (env:hub)
| 항목 | 내용 |
|------|------|
| 보드 | ESP32-S3 |
| 센서 | SCD40 (CO2 + 온습도, I2C) |
| 전원 | USB (콘센트) |
| 통신 | WiFi → 서버 HTTP, BLE → 센서노드·IR노드 |
| 라이브러리 | `sensirion/Sensirion I2C SCD4x` |

### 센서노드 (env:sensor)
| 항목 | 내용 |
|------|------|
| 보드 | ESP32-C3 SuperMini |
| 센서 | SHT40 (온습도, I2C) + LDR (조도, ADC GPIO2) |
| 전원 | AA 건전지 2개 + MT3608 부스트 컨버터 |
| 통신 | BLE → 허브 |
| 라이브러리 | `sensirion/Sensirion I2C SHT4x` |

### IR노드 (env:ir)
| 항목 | 내용 |
|------|------|
| 보드 | ESP32-C3 SuperMini |
| 액추에이터 | IR LED 940nm + 2N2222 트랜지스터 (GPIO3) |
| 전원 | AA 건전지 2개 + MT3608 부스트 컨버터 |
| 통신 | BLE ← 허브 |
| 라이브러리 | `crankyoldgit/IRremoteESP8266` |

## 통신 구조
```
서버(FastAPI) ←─ WiFi HTTP ─→ 허브(ESP32-S3)
                                   ↕ BLE 5.0
                             센서노드(ESP32-C3)
                             IR노드(ESP32-C3)
```

## ESP32 하드웨어 메모

### ESP32-C3 SuperMini (sensor / ir 공통)
- 내장 LED: **GPIO8, active-low**
- BOOT 버튼: **GPIO9, active-low** — 부팅 중 누르면 다운로드 모드
- ADC: GPIO0~4 (DAC·터치센서 없음)
- GPIO 로직 **3.3V 전용** (5V 인가 금지)

### ESP32-S3 (hub)
- USB CDC 활성화 필요: `-DARDUINO_USB_MODE=1 -DARDUINO_USB_CDC_ON_BOOT=1`
- I2C 기본 핀: SDA=GPIO8, SCL=GPIO9 (보드마다 다를 수 있음 — 확인 필요)

## 작업 시 주의
- `.cpp` 파일엔 `#include <Arduino.h>` 직접 포함 필요
- PlatformIO는 `src/` 아래 env별 서브폴더만 빌드 (`build_src_filter` 사용)
- `lib/protocol/protocol.h` — 세 env 모두에서 공유, BLE 포맷 변경 시 여기만 수정
- 프로젝트 경로에 한글·공백 포함 → PlatformIO가 드물게 빌드 오류를 낼 수 있음
