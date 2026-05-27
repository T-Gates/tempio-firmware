// MQTT 클라이언트 — ESP-IDF esp_mqtt 기반, WebSocket(WSS) 전송
// Cloudflare 터널 경유: wss://mqtt.yuumi.wiki/mqtt → 로컬 Mosquitto
#pragma once
#include <stdint.h>

// 서버 → 허브로 오는 명령 하나. MQTT JSON에서 파싱됨.
struct MqttCommand {
    char target[18];    // 대상 노드 MAC "aa:bb:cc:dd:ee:ff"
    char type[16];      // 명령 타입: "SET_INTERVAL", "IR_TIMING", "RESET_NODE"
    char payload[256];  // JSON payload 문자열 (타입별 파라미터)
};

// MQTT 클라이언트 초기화. broker_uri 예: "wss://mqtt.yuumi.wiki/mqtt"
// WiFi 연결되면 자동으로 브로커 접속 시작.
void mqtt_init(const char* broker_uri);

// WiFi 연결 상태 변화에 따라 MQTT start/stop 관리. loop()에서 호출.
void mqtt_loop();

// 브로커에 연결된 상태인지.
bool mqtt_is_connected();

// 센서 리포트 JSON을 tempio/{hub_id}/report 토픽에 발행.
bool mqtt_publish_report(const char* json);

// 명령 큐에 대기 중인 명령이 있는지.
bool mqtt_has_command();

// 명령 큐에서 하나를 꺼낸다. 없으면 false.
// 파이썬의 queue.get_nowait()과 비슷.
bool mqtt_get_command(MqttCommand* cmd);
