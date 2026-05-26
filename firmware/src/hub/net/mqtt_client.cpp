// MQTT 클라이언트 — ESP-IDF esp_mqtt (WebSocket/WSS 네이티브 지원)
#include <Arduino.h>
#include <WiFi.h>
#include <ArduinoJson.h>
#include <mqtt_client.h>
#include "mqtt_client.h"

// ──────────── hub_id ────────────
// WiFi MAC에서 콜론 제거 + 소문자. "aabbccddeeff" 형태.
static char hubId[16];

// ──────────── 토픽 ────────────
static char topicReport[48];   // tempio/{hub_id}/report
static char topicCommands[48]; // tempio/{hub_id}/commands

// ──────────── ESP-IDF MQTT 핸들 ────────────
static esp_mqtt_client_handle_t client = nullptr;
static bool connected = false;

// ──────────── 명령 큐 (원형 버퍼) ────────────
static constexpr int CMD_QUEUE_MAX = 8;
static MqttCommand cmdQueue[CMD_QUEUE_MAX];
static volatile int cmdHead = 0;
static volatile int cmdTail = 0;
static volatile int cmdCount = 0;

static void buildHubId() {
    String mac = WiFi.macAddress();
    int j = 0;
    for (int i = 0; i < (int)mac.length() && j < 12; i++) {
        if (mac[i] != ':') {
            hubId[j++] = tolower(mac[i]);
        }
    }
    hubId[j] = '\0';
}

// 수신 메시지에서 commands 배열 파싱 → 큐에 적재
static void parseCommands(const char* data, int len) {
    if (cmdCount >= CMD_QUEUE_MAX) return;

    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, data, len);
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

        if (obj["payload"].is<JsonObject>()) {
            serializeJson(obj["payload"], cmd.payload, sizeof(cmd.payload));
        } else {
            strlcpy(cmd.payload, obj["payload"] | "", sizeof(cmd.payload));
        }

        cmdTail = (cmdTail + 1) % CMD_QUEUE_MAX;
        cmdCount++;
    }
}

// ESP-IDF MQTT 이벤트 핸들러 — 내부 태스크에서 호출됨
static void mqttEventHandler(void* arg, esp_event_base_t base, int32_t event_id, void* event_data) {
    auto* event = (esp_mqtt_event_handle_t)event_data;

    switch (event->event_id) {
        case MQTT_EVENT_CONNECTED:
            connected = true;
            esp_mqtt_client_subscribe(client, topicCommands, 0);
            Serial.printf("mqtt connected, subscribed: %s\n", topicCommands);
            break;

        case MQTT_EVENT_DISCONNECTED:
            connected = false;
            // esp_mqtt가 자동 재연결 처리함
            Serial.println("mqtt disconnected, auto-reconnecting...");
            break;

        case MQTT_EVENT_DATA:
            // topic 매칭 확인 후 명령 파싱
            if (event->topic_len > 0 && event->data_len > 0) {
                parseCommands(event->data, event->data_len);
            }
            break;

        case MQTT_EVENT_ERROR:
            Serial.println("mqtt error");
            break;

        default:
            break;
    }
}

// ══════════════════════════════════════════════════════════════════════
// 공개 API
// ══════════════════════════════════════════════════════════════════════

void mqtt_init(const char* broker_uri) {
    buildHubId();
    snprintf(topicReport, sizeof(topicReport), "tempio/%s/report", hubId);
    snprintf(topicCommands, sizeof(topicCommands), "tempio/%s/commands", hubId);

    // 클라이언트 ID: "tempio-hub-aabbccddeeff"
    static char clientId[32];
    snprintf(clientId, sizeof(clientId), "tempio-hub-%s", hubId);

    esp_mqtt_client_config_t cfg = {};
    cfg.uri = broker_uri;
    cfg.client_id = clientId;
    cfg.buffer_size = 1024;
    // Cloudflare 등 공인 인증서 검증용 — ESP32 내장 CA 번들 사용
    cfg.crt_bundle_attach = arduino_esp_crt_bundle_attach;

    client = esp_mqtt_client_init(&cfg);
    esp_mqtt_client_register_event(client, MQTT_EVENT_ANY, mqttEventHandler, nullptr);

    Serial.printf("mqtt init: hub_id=%s broker=%s\n", hubId, broker_uri);

    if (WiFi.isConnected()) {
        esp_mqtt_client_start(client);
    }
}

// esp_mqtt는 내부 FreeRTOS 태스크로 동작하므로 loop에서 할 일 적음
// WiFi 연결 상태 변화 시 start/stop만 관리
void mqtt_loop() {
    static bool wasConnected = false;
    bool wifiNow = WiFi.isConnected();

    if (wifiNow && !wasConnected) {
        esp_mqtt_client_start(client);
    } else if (!wifiNow && wasConnected) {
        esp_mqtt_client_stop(client);
        connected = false;
    }
    wasConnected = wifiNow;
}

bool mqtt_is_connected() {
    return connected;
}

bool mqtt_publish_report(const char* json) {
    if (!connected) return false;
    int msg_id = esp_mqtt_client_publish(client, topicReport, json, 0, 0, 0);
    return msg_id >= 0;
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
