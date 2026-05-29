# 하드웨어 스펙

## IR 모듈

이 문서는 Tempio IR 모듈의 현재 실험 회로와 설치 기준을 정리한다. 목적은 팀원이 같은 회로를 재현하고, 펌웨어 핀 설정과 실제 배선이 어긋나지 않도록 하는 것이다.

IR 모듈은 ESP32-C3 보드를 BLE peripheral로 사용하고, NPN 트랜지스터로 IR LED를 구동한다. 허브는 BLE로 IR 모듈에 명령을 보내고, IR 모듈은 에어컨 제어용 IR 신호를 송신한다.

## 보드

| 항목 | 값 |
| --- | --- |
| 보드 | ESP32-C3 계열 개발 보드 |
| 역할 | BLE peripheral + IR transmitter |
| IR 송신 방식 | GPIO 출력 → 2SC1815 NPN 트랜지스터 → IR LED |
| 버튼 입력 방식 | GPIO 내부 pull-up, 버튼을 누르면 GND로 떨어지는 active-low 입력 |

## 핀맵

| ESP32-C3 GPIO | 연결 대상 | 방향 | 설명 |
| --- | --- | --- | --- |
| GPIO4 | 1kΩ → 2SC1815 Base | Output | IR 송신 제어 |
| GPIO5 | 택트 스위치 → GND | Input pull-up | 에어컨 끄기 |
| GPIO6 | 택트 스위치 → GND | Input pull-up | 에어컨 켜기 / 냉방 24도 |
| GPIO7 | 택트 스위치 → GND | Input pull-up | 온도 1도 올리기 |
| GPIO8 | 택트 스위치 → GND | Input pull-up | 온도 1도 내리기 |
| GPIO9 | 저항 → 상태 LED → GND | Output | 송신/상태 확인 LED |

펌웨어의 IR 송신 핀은 이 문서 기준으로 `GPIO4`에 맞춰야 한다.

## IR 송신 회로

### 연결도

```text
                    +3.3V 또는 +5V
                          |
                         100Ω
                          |
                    IR LED Anode(+)
                    IR LED Cathode(-)
                          |
                          |
                    Collector (C)
                   ┌───────────┐
GPIO4 ─── 1kΩ ──── Base (B)    │  2SC1815 NPN
                   │           │
                   └─ Emitter(E)
                          |
                         GND
```

### 연결표

| 부품/핀 | 연결 |
| --- | --- |
| ESP32-C3 GPIO4 | 1kΩ 저항을 거쳐 2SC1815 Base |
| 2SC1815 Emitter | GND |
| 2SC1815 Collector | IR LED Cathode(-) |
| IR LED Anode(+) | 100Ω 저항을 거쳐 +3.3V 또는 +5V |
| ESP32-C3 GND | IR LED 전원 GND와 공통 접지 |

## 버튼 회로

버튼은 GPIO와 GND 사이에 연결한다. 펌웨어는 `INPUT_PULLUP`을 사용한다.

```text
GPIO5 ─── 택트 스위치 ─── GND   에어컨 끄기
GPIO6 ─── 택트 스위치 ─── GND   에어컨 켜기 / 냉방 24도
GPIO7 ─── 택트 스위치 ─── GND   온도 1도 올리기
GPIO8 ─── 택트 스위치 ─── GND   온도 1도 내리기
```

입력 논리는 다음과 같다.

```text
버튼을 누르지 않음 = HIGH
버튼을 누름       = LOW
```

GPIO에 5V를 직접 인가하지 않는다.

## 상태 LED 회로

권장 연결은 다음과 같다.

```text
GPIO9 ─── 전류 제한 저항 ─── LED Anode(+)
LED Cathode(-) ─── GND
```

이 연결에서는 GPIO9가 HIGH일 때 LED가 켜진다.

만약 LED를 다음처럼 반대로 연결하면 active-low가 된다.

```text
3.3V ─── 전류 제한 저항 ─── LED Anode(+)
LED Cathode(-) ─── GPIO9
```

이 경우 GPIO9가 LOW일 때 LED가 켜지므로, 펌웨어의 LED 논리와 맞춰야 한다.

## 부품 목록

| 부품 | 수량 | 비고 |
| --- | --- | --- |
| ESP32-C3 개발 보드 | 1 | IR 모듈 메인 보드 |
| 2SC1815 NPN 트랜지스터 | 1 | IR LED 구동용 low-side switch |
| IR LED 940nm | 1 이상 | 에어컨 수신부 방향으로 배치 |
| 1kΩ 저항 | 1 | GPIO4와 Base 사이 |
| 100Ω 저항 | 1 | IR LED 전류 제한 |
| 택트 스위치 | 4 | 현장/개발용 수동 입력 |
| 상태 LED | 1 | 송신/상태 확인 |
| 상태 LED용 저항 | 1 | 권장. 값은 사용하는 LED와 밝기에 맞춰 선택 |

## 주의사항

- 2SC1815 핀 배열은 제조사와 패키지에 따라 반드시 데이터시트로 확인한다.
- 일반적인 TO-92 2SC1815는 평평한 면을 보고 다리를 아래로 했을 때 `E-C-B`인 경우가 많다.
- ESP32-C3의 GND와 IR LED 전원 GND는 반드시 공통으로 묶는다.
- IR LED 극성을 확인한다. 일반적으로 긴 다리가 Anode(+)인 경우가 많지만 부품별로 확인한다.
- 버튼은 GPIO와 GND 사이에 연결한다. GPIO에 5V를 직접 넣지 않는다.
- GPIO9는 ESP32-C3 보드에 따라 부트 또는 특수 기능과 엮일 수 있으므로, 사용하는 보드의 핀맵을 확인한다.
- USB 전원으로 테스트할 때도 IR LED 전원과 ESP32-C3 GND는 공통 접지여야 한다.
- 에어컨 반응이 약하면 IR LED 방향, 수신부 위치, 거리, 주변 조명, IR LED 전류 제한 저항 값을 순서대로 확인한다.

## 펌웨어와 맞춰야 할 값

현재 회로 기준으로 펌웨어는 다음 값을 사용해야 한다.

```cpp
static constexpr uint8_t IR_PIN = 4;
```

개발용 버튼과 상태 LED를 함께 사용하는 펌웨어라면 다음 핀을 사용한다.

```cpp
static constexpr uint8_t BUTTON_OFF_PIN = 5;
static constexpr uint8_t BUTTON_ON_PIN = 6;
static constexpr uint8_t BUTTON_TEMP_UP_PIN = 7;
static constexpr uint8_t BUTTON_TEMP_DOWN_PIN = 8;
static constexpr uint8_t STATUS_LED_PIN = 9;
```
