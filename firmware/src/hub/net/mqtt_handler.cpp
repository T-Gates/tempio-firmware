// MQTT 클라이언트 — ESP-IDF esp_mqtt (WebSocket/WSS 네이티브 지원)
#include <Arduino.h>
#include <WiFi.h>
#include <ArduinoJson.h>
#include <mqtt_client.h>
#include "mqtt_handler.h"
#include "../config.h"

// GTS Root R4 (Google Trust Services) — Cloudflare 인증서 체인의 루트 CA
// Cloudflare가 CA를 변경하면 이 인증서도 교체 필요
static const char* root_ca = \
"-----BEGIN CERTIFICATE-----\n"
"MIIDejCCAmKgAwIBAgIQf+UwvzMTQ77dghYQST2KGzANBgkqhkiG9w0BAQsFADBX\n"
"MQswCQYDVQQGEwJCRTEZMBcGA1UEChMQR2xvYmFsU2lnbiBudi1zYTEQMA4GA1UE\n"
"CxMHUm9vdCBDQTEbMBkGA1UEAxMSR2xvYmFsU2lnbiBSb290IENBMB4XDTIzMTEx\n"
"NTAzNDMyMVoXDTI4MDEyODAwMDA0MlowRzELMAkGA1UEBhMCVVMxIjAgBgNVBAoT\n"
"GUdvb2dsZSBUcnVzdCBTZXJ2aWNlcyBMTEMxFDASBgNVBAMTC0dUUyBSb290IFI0\n"
"MHYwEAYHKoZIzj0CAQYFK4EEACIDYgAE83Rzp2iLYK5DuDXFgTB7S0md+8Fhzube\n"
"Rr1r1WEYNa5A3XP3iZEwWus87oV8okB2O6nGuEfYKueSkWpz6bFyOZ8pn6KY019e\n"
"WIZlD6GEZQbR3IvJx3PIjGov5cSr0R2Ko4H/MIH8MA4GA1UdDwEB/wQEAwIBhjAd\n"
"BgNVHSUEFjAUBggrBgEFBQcDAQYIKwYBBQUHAwIwDwYDVR0TAQH/BAUwAwEB/zAd\n"
"BgNVHQ4EFgQUgEzW63T/STaj1dj8tT7FavCUHYwwHwYDVR0jBBgwFoAUYHtmGkUN\n"
"l8qJUC99BM00qP/8/UswNgYIKwYBBQUHAQEEKjAoMCYGCCsGAQUFBzAChhpodHRw\n"
"Oi8vaS5wa2kuZ29vZy9nc3IxLmNydDAtBgNVHR8EJjAkMCKgIKAehhxodHRwOi8v\n"
"Yy5wa2kuZ29vZy9yL2dzcjEuY3JsMBMGA1UdIAQMMAowCAYGZ4EMAQIBMA0GCSqG\n"
"SIb3DQEBCwUAA4IBAQAYQrsPBtYDh5bjP2OBDwmkoWhIDDkic574y04tfzHpn+cJ\n"
"odI2D4SseesQ6bDrarZ7C30ddLibZatoKiws3UL9xnELz4ct92vID24FfVbiI1hY\n"
"+SW6FoVHkNeWIP0GCbaM4C6uVdF5dTUsMVs/ZbzNnIdCp5Gxmx5ejvEau8otR/Cs\n"
"kGN+hr/W5GvT1tMBjgWKZ1i4//emhA1JG1BbPzoLJQvyEotc03lXjTaCzv8mEbep\n"
"8RqZ7a2CPsgRbuvTPBwcOMBBmuFeU88+FSBX6+7iP0il8b4Z0QFqIwwMHfs/L6K1\n"
"vepuoxtGzi4CZ68zJpiq1UvSqTbFJjtbD4seiMHl\n"
"-----END CERTIFICATE-----\n";

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
static MqttCommand cmdQueue[CMD_QUEUE_MAX];
static volatile int cmdHead = 0;
static volatile int cmdTail = 0;
static volatile int cmdCount = 0;

// MQTT 이벤트 핸들러는 esp_mqtt 내부 태스크에서, mqtt_get_command는 Arduino loop 태스크에서 호출 — 크로스 태스크 보호 필요
static portMUX_TYPE cmdMux = portMUX_INITIALIZER_UNLOCKED;

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
    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, data, len);
    if (err) {
        Serial.printf("mqtt json err: %s\n", err.c_str());
        return;
    }

    JsonArray commands = doc["commands"].as<JsonArray>();
    if (commands.isNull()) return;

    for (JsonObject obj : commands) {
        portENTER_CRITICAL(&cmdMux);
        if (cmdCount >= CMD_QUEUE_MAX) {
            portEXIT_CRITICAL(&cmdMux);
            break;
        }

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
        portEXIT_CRITICAL(&cmdMux);
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
    cfg.cert_pem = root_ca;

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
    portENTER_CRITICAL(&cmdMux);
    if (cmdCount <= 0) {
        portEXIT_CRITICAL(&cmdMux);
        return false;
    }

    *cmd = cmdQueue[cmdHead];
    cmdHead = (cmdHead + 1) % CMD_QUEUE_MAX;
    cmdCount--;
    portEXIT_CRITICAL(&cmdMux);
    return true;
}
