#pragma once
#include <stdint.h>

// tempio BLE 프로토콜 — 허브 ↔ 센서노드 ↔ IR노드 공유 헤더
//
// BLE 구조:
//   서비스 1개 (TEMPIO_SERVICE_UUID)
//   └── DATA 특성   — 센서값 수신(notify), IR 타이밍 송신(write)
//   └── CONFIG 특성 — 노드 설정 명령 (ID 부여, 주기 변경, 리셋)
//
// 모든 메시지의 첫 바이트는 MsgType enum으로 데이터 종류를 구분한다.
// 수신 측은 data[0]을 MsgType으로 캐스팅해서 파싱 분기.

// ──────────── BLE UUID ────────────
// 128-bit UUID. 앞 8자리 "4c544d50" = ASCII "LTMP" (tempio).
// 두 번째 그룹(0001/0002/0003)으로 서비스·특성을 구분한다.
constexpr const char* TEMPIO_SERVICE_UUID     = "4c544d50-0001-4654-b726-a8e800000000";  // 서비스
constexpr const char* TEMPIO_CHAR_DATA_UUID   = "4c544d50-0002-4654-b726-a8e800000000";  // 데이터 교환 (notify + write)
constexpr const char* TEMPIO_CHAR_CONFIG_UUID = "4c544d50-0003-4654-b726-a8e800000000";  // 설정 명령 (write only)

// ──────────── 메시지 타입 ────────────
// 0x01~0x0F: 데이터 (DATA 특성으로 전송)
// 0x10~0x1F: 설정 명령 (CONFIG 특성으로 전송)
// 0x20~0x2F: 노드 상태 보고 (DATA 특성, notify)
enum class MsgType : uint8_t {
    SENSOR_DATA    = 0x01,  // 센서노드 → 허브: 온습도 + 조도 + 배터리
    IR_TIMING      = 0x02,  // 허브 → IR노드: raw IR 타이밍 배열

    HUB_READY      = 0x10,  // 허브 → 노드: "subscribe 완료, 데이터 보내도 돼" (연결 직후)
    SET_INTERVAL   = 0x12,  // 허브 → 센서노드: 측정 주기 변경 (초 단위)
    RESET_NODE     = 0x13,  // 허브 → 노드: 리셋 명령 (레벨별)
    TEST_CMD       = 0x14,  // 허브 → 노드: E2E 테스트용 핑 (ACK 회신 검증)

    NODE_INFO      = 0x20,  // 노드 → 허브: 연결 직후 자기 소개 (타입, ID, 배터리, 펌웨어)
    CMD_ACK        = 0x21,  // 노드 → 허브: 서버 명령 실행 결과 회신
};

// ──────────── 노드 타입 ────────────
// 허브가 연결 후 NODE_INFO를 받아서 센서/IR 자동 구분
enum class NodeType : uint8_t {
    SENSOR = 0x01,  // 센서노드: SHT40 + LDR, 온습도·조도 측정
    IR     = 0x02,  // IR노드: IR LED, 에어컨 제어
};

// ──────────── 데이터 구조체 ────────────
// 모든 구조체는 packed — 바이트 그대로 BLE 전송/수신 가능.
// little-endian (ESP32 기본값).

// 센서노드 → 허브 (13바이트)
// 센서노드가 wake-up 후 SHT40 + LDR + ADC 읽어서 notify로 전송
struct SensorData {
    MsgType  type = MsgType::SENSOR_DATA;
    float    temp;        // 온도 (°C), SHT40
    float    humidity;    // 습도 (%), SHT40
    uint16_t ldr;         // 조도 raw ADC 값 (0~4095), LDR 분압회로
    uint16_t battery_mv;  // 배터리 전압 (mV), ADC로 측정
} __attribute__((packed));

// 노드 → 허브 (6바이트)
// 연결 직후 자기 소개용. 허브는 이걸로 센서/IR 구분 + 상태 파악.
// 노드 식별은 BLE MAC 주소로 — 별도 ID 부여 불필요.
struct NodeInfo {
    MsgType  type = MsgType::NODE_INFO;
    NodeType node_type;   // SENSOR 또는 IR
    uint16_t battery_mv;  // 배터리 전압 (mV)
    uint8_t  fw_major;    // 펌웨어 버전 (major)
    uint8_t  fw_minor;    // 펌웨어 버전 (minor)
} __attribute__((packed));

// ──────────── IR 타이밍 (가변 길이) ────────────
// 서버가 생성한 raw IR 타이밍을 허브가 IR노드에 그대로 전달.
// 에어컨 프로토콜마다 200~600바이트 — 고정 구조체로 못 담아서 직접 파싱.
//
// 바이트 레이아웃:
//   [0]      MsgType::IR_TIMING (0x02)
//   [1..2]   uint16_t cmd_id    서버 명령 ID (ACK에서 에코)
//   [3..4]   uint16_t count     타이밍 배열 원소 개수 (little-endian)
//   [5..]    uint16_t[]         mark/space 교대 값 (마이크로초 단위)
//                               짝수 인덱스 = mark(IR ON), 홀수 = space(IR OFF)
//
// 예: 삼성 에어컨 냉방 26도 → count=199, 총 401바이트
//
// BLE MTU 기본 20바이트로는 부족.
// MTU 협상(최대 512)으로 해결하거나, 안 되면 패킷 분할 전송 구현 필요.

// 노드 → 허브: 명령 실행 결과 (4바이트)
// 노드가 서버 명령(SET_INTERVAL 등)을 받아 실행한 뒤 결과를 회신.
// 허브는 이걸 서버에 MQTT로 전달해서 명령 도달 여부를 확인.
struct CmdAck {
    MsgType  type = MsgType::CMD_ACK;
    uint16_t cmd_id;   // 서버가 부여한 명령 ID (에코)
    uint8_t  success;  // 1=성공, 0=실패
} __attribute__((packed));

// 허브 → 노드: E2E 테스트 핑 (4바이트)
// 노드가 수신하면 CMD_ACK를 회신. led=1이면 LED도 한 번 깜빡임.
// 서버에서 통신 경로 전체(서버→허브→노드→허브→서버)를 검증할 때 사용.
struct TestCmd {
    MsgType  type = MsgType::TEST_CMD;
    uint16_t cmd_id;  // 서버 명령 ID (ACK에서 에코)
    uint8_t  led;     // 1이면 LED 깜빡임, 0이면 핑퐁만
} __attribute__((packed));

// ──────────── 설정 명령 ────────────

// 허브 → 노드: subscribe 완료 신호 (1바이트)
// 허브가 서비스 탐색 + DATA 구독을 끝낸 뒤 전송.
// 노드는 이걸 받으면 NODE_INFO를 보내도 안전하다는 뜻.
struct HubReady {
    MsgType type = MsgType::HUB_READY;
} __attribute__((packed));

// 허브 → 센서노드: 측정 주기 변경 (5바이트)
// 서버가 허브에 명령 → 허브가 센서노드에 전달.
// 센서노드는 이 값으로 딥슬립 타이머를 재설정.
struct SetInterval {
    MsgType  type = MsgType::SET_INTERVAL;
    uint16_t cmd_id;        // 서버 명령 ID (ACK에서 에코)
    uint16_t interval_sec;  // 딥슬립 주기 (초). 기본 60, 범위 10~3600
} __attribute__((packed));

// 허브 → 노드: 리셋 명령 (4바이트)
// 원격 초기화용. BOOT 버튼 롱프레스의 원격 버전.
struct ResetNode {
    MsgType  type = MsgType::RESET_NODE;
    uint16_t cmd_id;  // 서버 명령 ID (ACK에서 에코)
    uint8_t  level;   // 0=재부팅(설정 유지), 1=페어링+ID 삭제, 2=공장 초기화(전부 삭제)
} __attribute__((packed));
