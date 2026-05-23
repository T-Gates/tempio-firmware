#pragma once
#include <stdint.h>

// tempio BLE 메시지 포맷 — 허브 ↔ 센서노드 ↔ IR노드 공유

enum class MsgType : uint8_t {
    SENSOR_DATA = 0x01,  // 센서노드 → 허브: 온습도 + 조도
    AC_CMD      = 0x02,  // 허브 → IR노드: 에어컨 명령
};

struct SensorData {
    MsgType type = MsgType::SENSOR_DATA;
    float   temp;
    float   humidity;
    uint16_t ldr;
} __attribute__((packed));

struct AcCmd {
    MsgType type = MsgType::AC_CMD;
    uint8_t power;     // 0=off, 1=on
    uint8_t setpoint;  // 목표 온도 (°C)
    uint8_t mode;      // 0=cool, 1=fan, 2=auto
} __attribute__((packed));
