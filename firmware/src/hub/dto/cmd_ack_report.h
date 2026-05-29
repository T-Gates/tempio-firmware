// CMD_ACK 리포트 DTO — BLE에서 수신한 명령 응답을 main.cpp 경유로 MQTT 발행
#pragma once
#include <stdint.h>
#include <ArduinoJson.h>

struct CmdAckReport {
    char node_id[18] = {};
    uint16_t cmd_id = 0;
    bool success = false;

    int toJson(char* buf, size_t bufSize) const {
        JsonDocument doc;
        doc["type"] = "cmd_ack";
        doc["node_id"] = node_id;
        doc["cmd_id"] = cmd_id;
        doc["success"] = success;
        return serializeJson(doc, buf, bufSize);
    }
};
