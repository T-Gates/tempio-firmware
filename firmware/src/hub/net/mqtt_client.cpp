#include <Arduino.h>
#include <WiFi.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>
#include "mqtt_client.h"

// ──────────── hub_id ────────────
// WiFi MAC에서 콜론 제거 + 소문자. "aabbccddeeff" 형태.
static char hubId[16];

// ──────────── 토픽 ────────────
static char topicReport[48];   // tempio/{hub_id}/report
static char topicCommands[48]; // tempio/{hub_id}/commands

// ──────────── MQTT 클라이언트 ────────────
static WiFiClient wifiClient;
static PubSubClient mqttClient(wifiClient);

// ──────────── 재연결 타이머 ────────────
static unsigned long lastReconnectAttempt = 0;
static constexpr unsigned long RECONNECT_INTERVAL = 5000;

// ──────────── 명령 큐 (원형 버퍼) ────────────
static constexpr int CMD_QUEUE_MAX = 8;
static MqttCommand cmdQueue[CMD_QUEUE_MAX];
static volatile int cmdHead = 0;  // 다음 읽을 위치
static volatile int cmdTail = 0;  // 다음 쓸 위치
static volatile int cmdCount = 0; // 현재 큐에 있는 명령 수

static void buildHubId() {
    String mac = WiFi.macAddress(); // "AA:BB:CC:DD:EE:FF"
    int j = 0;
    for (int i = 0; i < (int)mac.length() && j < 12; i++) {
        if (mac[i] != ':') {
            hubId[j++] = tolower(mac[i]);
        }
    }
    hubId[j] = '\0';
}

static void onMessage(char* topic, byte* payload, unsigned int length) {
    if (cmdCount >= CMD_QUEUE_MAX) return; // 큐 가득 참

    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, payload, length);
    if (err) {
        Serial.printf("mqtt json err: %s\n", err.c_str());
        return;
    }

    JsonArray commands = doc["commands"].as<JsonArray>();
    if (commands.isNull()) return;

    for (JsonObject obj : commands) {
        if (cmdCount >= CMD_QUEUE_MAX) break;

        MqttCommand& cmd = cmdQueue[cmdTail];
        strlcpy(cmd.target, obj["target"] | "", sizeof(cmd.target));
        strlcpy(cmd.type, obj["type"] | "", sizeof(cmd.type));

        // payload는 중첩 JSON일 수 있으므로 직렬화
        if (obj["payload"].is<JsonObject>()) {
            serializeJson(obj["payload"], cmd.payload, sizeof(cmd.payload));
        } else {
            strlcpy(cmd.payload, obj["payload"] | "", sizeof(cmd.payload));
        }

        cmdTail = (cmdTail + 1) % CMD_QUEUE_MAX;
        cmdCount++;
    }
}

static bool reconnect() {
    String clientId = "tempio-hub-";
    clientId += hubId;

    if (mqttClient.connect(clientId.c_str())) {
        Serial.printf("mqtt connected as %s\n", clientId.c_str());
        mqttClient.subscribe(topicCommands);
        return true;
    }
    Serial.printf("mqtt connect failed, rc=%d\n", mqttClient.state());
    return false;
}

// ══════════════════════════════════════════════════════════════════════
// 공개 API
// ══════════════════════════════════════════════════════════════════════

void mqtt_init(const char* broker_ip, uint16_t port) {
    buildHubId();
    snprintf(topicReport, sizeof(topicReport), "tempio/%s/report", hubId);
    snprintf(topicCommands, sizeof(topicCommands), "tempio/%s/commands", hubId);

    mqttClient.setServer(broker_ip, port);
    mqttClient.setBufferSize(1024);
    mqttClient.setCallback(onMessage);

    Serial.printf("mqtt init: hub_id=%s broker=%s:%u\n", hubId, broker_ip, port);

    if (WiFi.isConnected()) {
        reconnect();
    }
}

void mqtt_loop() {
    if (!WiFi.isConnected()) return;

    if (!mqttClient.connected()) {
        unsigned long now = millis();
        if (now - lastReconnectAttempt >= RECONNECT_INTERVAL) {
            lastReconnectAttempt = now;
            reconnect();
        }
        return;
    }

    mqttClient.loop();
}

bool mqtt_is_connected() {
    return mqttClient.connected();
}

bool mqtt_publish_report(const char* json) {
    if (!WiFi.isConnected() || !mqttClient.connected()) return false;
    return mqttClient.publish(topicReport, json);
}

bool mqtt_has_command() {
    return cmdCount > 0;
}

bool mqtt_get_command(MqttCommand* cmd) {
    if (cmdCount <= 0) return false;

    *cmd = cmdQueue[cmdHead];
    cmdHead = (cmdHead + 1) % CMD_QUEUE_MAX;
    cmdCount--;
    return true;
}
