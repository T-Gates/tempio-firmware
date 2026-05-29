// MQTT 클라이언트 — ESP-IDF esp_mqtt (WebSocket/WSS 네이티브 지원)
//
// 이 파일의 역할:
//   1. MQTT 브로커(Mosquitto)에 WSS로 연결하고, 끊기면 자동 재연결
//   2. 센서 리포트를 서버에 publish (허브 → 서버)
//   3. 서버에서 내려온 raw 데이터를 cmd 레이어에 넘김 (서버 → 허브)
#include <Arduino.h>
#include <WiFi.h>
#include <mqtt_client.h>
#include "mqtt_handler.h"
#include "root_ca.h"
#include "../cmd/cmd_dispatcher.h"
#include "../util/hub_id.h"

static char topicReport[48];
static char topicCommands[48];

// ──────────── ESP-IDF MQTT 핸들 ────────────
static esp_mqtt_client_handle_t client = nullptr;
static volatile bool connected = false;

// ──────────── 이벤트 디스패치 테이블 ────────────

using MqttHandler = void(*)(esp_mqtt_event_handle_t);

struct MqttEventEntry {
    esp_mqtt_event_id_t id;
    MqttHandler handler;
};

static void onConnected(esp_mqtt_event_handle_t) {
    connected = true;
    esp_mqtt_client_subscribe(client, topicCommands, 0);
    Serial.printf("mqtt connected, subscribed: %s\n", topicCommands);
}

static void onDisconnected(esp_mqtt_event_handle_t) {
    connected = false;
    Serial.println("mqtt disconnected, auto-reconnecting...");
}

static void onData(esp_mqtt_event_handle_t event) {
    if (event->topic_len > 0 && event->data_len > 0
        && event->data_len == event->total_data_len) {
        parseAndDispatchCommands(event->data, event->data_len);
    }
}

static void onError(esp_mqtt_event_handle_t) {
    Serial.println("mqtt error");
}

static constexpr MqttEventEntry eventHandlers[] = {
    { MQTT_EVENT_CONNECTED,    onConnected },
    { MQTT_EVENT_DISCONNECTED, onDisconnected },
    { MQTT_EVENT_DATA,         onData },
    { MQTT_EVENT_ERROR,        onError },
};

static void mqttEventDispatch(void*, esp_event_base_t, int32_t event_id, void* event_data) {
    auto* event = static_cast<esp_mqtt_event_handle_t>(event_data);
    for (const auto& entry : eventHandlers) {
        if (entry.id == static_cast<esp_mqtt_event_id_t>(event_id)) {
            entry.handler(event);
            return;
        }
    }
}

// ══════════════════════════════════════════════════════════════════════
// 공개 API
// ══════════════════════════════════════════════════════════════════════

void mqttInit(const char* broker_uri) {
    hubIdInit();
    const char* id = getHubId();
    snprintf(topicReport, sizeof(topicReport), "tempio/%s/report", id);
    snprintf(topicCommands, sizeof(topicCommands), "tempio/%s/commands", id);

    static char clientId[32];
    snprintf(clientId, sizeof(clientId), "tempio-hub-%s", id);

    esp_mqtt_client_config_t cfg = {};
    cfg.uri = broker_uri;
    cfg.client_id = clientId;
    cfg.buffer_size = 1024;
    cfg.cert_pem = root_ca;

    client = esp_mqtt_client_init(&cfg);
    esp_mqtt_client_register_event(client, MQTT_EVENT_ANY, mqttEventDispatch, nullptr);

    Serial.printf("mqtt init: hub_id=%s broker=%s\n", id, broker_uri);

    if (WiFi.isConnected()) {
        esp_mqtt_client_start(client); // 내부 FreeRTOS 태스크 시작
    }
}

void mqttLoop() {
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

bool mqttIsConnected() {
    return connected;
}

bool mqttPublishReport(const char* json) {
    if (!connected) return false;
    int msg_id = esp_mqtt_client_publish(client, topicReport, json, 0, 0, 0);
    return msg_id >= 0;
}
