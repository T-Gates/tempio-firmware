// MQTT 클라이언트 — ESP-IDF esp_mqtt (WebSocket/WSS 네이티브 지원)
//
// 이 파일의 역할:
//   1. MQTT 브로커(Mosquitto)에 WSS로 연결하고, 끊기면 자동 재연결
//   2. 센서 리포트를 서버에 publish (허브 → 서버)
//   3. 서버에서 내려온 명령을 수신해서 dispatch_command()로 전달 (서버 → 허브)
//
// esp_mqtt는 내부 FreeRTOS 태스크를 가지고 있어서,
// 연결/수신/재연결을 알아서 처리한다. Arduino loop()에서는 WiFi 상태만 감시하면 됨.
//
// 명령 수신 흐름:
//   esp_mqtt 태스크 → mqttEventHandler() → parseCommands() → dispatch_command()
//   dispatch_command 내부에서 노드별 펜딩 큐로 관리됨 (cmd_dispatcher.cpp 참조)
#include <Arduino.h>
#include <WiFi.h>
#include <ArduinoJson.h>
#include <mqtt_client.h>
#include "mqtt_handler.h"
#include "../cmd/cmd_dispatcher.h"
#include "../config.h"

// GTS Root R4 (Google Trust Services) — Cloudflare 인증서 체인의 루트 CA
// WSS 연결 시 서버 인증서를 검증하는 데 사용.
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
// 이 허브의 고유 식별자. WiFi MAC에서 콜론 제거 + 소문자. "aabbccddeeff" 형태.
// MQTT 토픽과 클라이언트 ID에 사용됨.
static char hubId[16];

// ──────────── MQTT 토픽 ────────────
// 한 허브가 두 개의 토픽을 사용:
//   report   — 허브가 센서 데이터를 서버에 올림 (publish)
//   commands — 서버가 허브에 명령을 내림 (subscribe)
static char topicReport[48];   // tempio/{hub_id}/report
static char topicCommands[48]; // tempio/{hub_id}/commands

// ──────────── ESP-IDF MQTT 핸들 ────────────
static esp_mqtt_client_handle_t client = nullptr;
// volatile: esp_mqtt 태스크에서 쓰고, Arduino loop에서 읽으니까
// 컴파일러가 최적화로 캐시하지 않게 강제
static volatile bool connected = false;

// 글로벌 명령 큐 없음 — 수신된 명령은 즉시 dispatch_command()로 전달.
// dispatch_command() 내부에서 노드별 펜딩 큐로 관리됨 (cmd_dispatcher.cpp 참조).

// WiFi MAC "AA:BB:CC:DD:EE:FF" → hub_id "aabbccddeeff"
// 콜론 빼고 소문자로 변환해서 MQTT 토픽에 쓸 수 있는 형태로 만듦
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

// 서버에서 온 JSON을 파싱해서 dispatch_command()로 넘김
// esp_mqtt 내부 태스크에서 호출됨 — Arduino loop가 아닌 다른 태스크!
//
// 수신 JSON 예시:
// { "commands": [
//     { "target": "aa:bb:cc:dd:ee:ff", "type": "SET_INTERVAL", "payload": {"sec": 10} }
// ]}
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
        MqttCommand cmd;
        strlcpy(cmd.target, obj["target"] | "", sizeof(cmd.target));
        strlcpy(cmd.type, obj["type"] | "", sizeof(cmd.type));

        if (obj["payload"].is<JsonObject>()) {
            serializeJson(obj["payload"], cmd.payload, sizeof(cmd.payload));
        } else {
            strlcpy(cmd.payload, obj["payload"] | "", sizeof(cmd.payload));
        }

        // 글로벌 큐 없이 바로 디스패치 — 연결 안 된 노드면 내부에서 펜딩 큐에 보관됨
        dispatch_command(cmd);
    }
}

// MQTT 이벤트 핸들러 — esp_mqtt 내부 태스크에서 호출됨 (Arduino loop가 아님!)
// mqtt_init()에서 register_event로 등록해둠.
// 파이썬으로 치면 client.on("*", mqtt_event_handler) 같은 콜백.
static void mqttEventHandler(void* arg, esp_event_base_t base, int32_t event_id, void* event_data) {
    auto* event = (esp_mqtt_event_handle_t)event_data;

    switch (event->event_id) {
        case MQTT_EVENT_CONNECTED:
            connected = true;
            // 연결되면 commands 토픽 구독 시작 — 서버 명령을 받기 위해
            esp_mqtt_client_subscribe(client, topicCommands, 0);
            Serial.printf("mqtt connected, subscribed: %s\n", topicCommands);
            break;

        case MQTT_EVENT_DISCONNECTED:
            connected = false;
            // esp_mqtt가 알아서 재연결 시도함 — 여기서 할 건 없음
            Serial.println("mqtt disconnected, auto-reconnecting...");
            break;

        case MQTT_EVENT_DATA:
            // 메시지 수신. 큰 메시지는 여러 번에 나눠서 오는데(분할),
            // data_len == total_data_len일 때만 한 번에 온 완전한 메시지.
            if (event->topic_len > 0 && event->data_len > 0
                && event->data_len == event->total_data_len) {
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

// MQTT 클라이언트 초기화. setup()에서 한 번 호출.
// 하는 일: hub_id 생성 → 토픽 조립 → esp_mqtt 클라이언트 생성 → 이벤트 핸들러 등록
// WiFi가 이미 연결되어 있으면 즉시 브로커 접속 시작.
void mqtt_init(const char* broker_uri) {
    buildHubId();
    // snprintf = 파이썬의 f"tempio/{hub_id}/report". 버퍼 크기만큼만 안전하게 씀.
    snprintf(topicReport, sizeof(topicReport), "tempio/%s/report", hubId);
    snprintf(topicCommands, sizeof(topicCommands), "tempio/%s/commands", hubId);

    // 클라이언트 ID: 브로커에서 이 허브를 식별하는 고유 이름
    // static — 함수 끝나도 메모리가 유지됨. cfg.client_id가 이 포인터를 참조하니까.
    static char clientId[32];
    snprintf(clientId, sizeof(clientId), "tempio-hub-%s", hubId);

    // esp_mqtt 설정. uri가 "wss://"로 시작하면 자동으로 WebSocket+TLS 모드.
    esp_mqtt_client_config_t cfg = {};
    cfg.uri = broker_uri;
    cfg.client_id = clientId;
    cfg.buffer_size = 1024;
    cfg.cert_pem = root_ca; // TLS 검증용 루트 CA 인증서

    client = esp_mqtt_client_init(&cfg);
    // 모든 MQTT 이벤트를 mqttEventHandler 함수로 라우팅
    esp_mqtt_client_register_event(client, MQTT_EVENT_ANY, mqttEventHandler, nullptr);

    Serial.printf("mqtt init: hub_id=%s broker=%s\n", hubId, broker_uri);

    if (WiFi.isConnected()) {
        esp_mqtt_client_start(client); // 내부 FreeRTOS 태스크 시작
    }
}

// WiFi 상태 변화 감시 — loop()에서 매 사이클 호출.
// esp_mqtt가 통신은 알아서 하니까, 여기서는 WiFi 연결/끊김에 따라
// MQTT 태스크를 시작/중지만 해주면 됨.
void mqtt_loop() {
    static bool wasConnected = false; // static: 호출 사이에 값이 유지됨 (이전 상태 기억)
    bool wifiNow = WiFi.isConnected();

    if (wifiNow && !wasConnected) {
        esp_mqtt_client_start(client);       // WiFi 복귀 → MQTT 태스크 시작
    } else if (!wifiNow && wasConnected) {
        esp_mqtt_client_stop(client);        // WiFi 끊김 → MQTT 태스크 중지
        connected = false;
    }
    wasConnected = wifiNow;
}

bool mqtt_is_connected() {
    return connected;
}

// 센서 리포트 JSON을 브로커에 publish.
// main.cpp에서 BLE 데이터 수신할 때마다 호출됨.
// 연결 안 되어 있으면 false 리턴하고 데이터는 유실됨 (재전송 안 함).
bool mqtt_publish_report(const char* json) {
    if (!connected) return false;
    // 마지막 세 인자: len=0(자동계산), qos=0, retain=0
    int msg_id = esp_mqtt_client_publish(client, topicReport, json, 0, 0, 0);
    return msg_id >= 0; // 음수면 실패
}

// mqtt_get_command, mqtt_has_command 삭제됨
// 명령은 parseCommands에서 직접 dispatch_command()로 전달.
// 노드별 펜딩 큐는 cmd_dispatcher.cpp에서 관리.
