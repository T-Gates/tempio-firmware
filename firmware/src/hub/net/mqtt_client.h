#pragma once
#include <stdint.h>

struct MqttCommand {
    char target[18];    // 노드 MAC 주소 "aa:bb:cc:dd:ee:ff"
    char type[16];      // "SET_INTERVAL", "IR_TIMING", "RESET_NODE"
    char payload[256];  // JSON payload 문자열
};

void mqtt_init(const char* broker_ip, uint16_t port = 1883);
void mqtt_loop();
bool mqtt_is_connected();
bool mqtt_publish_report(const char* json);
bool mqtt_has_command();
bool mqtt_get_command(MqttCommand* cmd);
