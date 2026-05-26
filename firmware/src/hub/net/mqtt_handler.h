// MQTT 클라이언트 — WebSocket 기반, ESP-IDF esp_mqtt 사용
#pragma once
#include <stdint.h>

struct MqttCommand {
    char target[18];    // 노드 MAC 주소 "aa:bb:cc:dd:ee:ff"
    char type[16];      // "SET_INTERVAL", "IR_TIMING", "RESET_NODE"
    char payload[256];  // JSON payload 문자열
};

// broker_uri: "ws://host:port/mqtt" 또는 "wss://host/mqtt"
void mqtt_init(const char* broker_uri);
void mqtt_loop();
bool mqtt_is_connected();
bool mqtt_publish_report(const char* json);
bool mqtt_has_command();
bool mqtt_get_command(MqttCommand* cmd);
