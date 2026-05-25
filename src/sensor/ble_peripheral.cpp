// ═══════════════════════════════════════════════════════════════════════════
// ble_peripheral.cpp — 센서노드 BLE Peripheral (딥슬립 one-shot 버전)
// ═══════════════════════════════════════════════════════════════════════════

#include <Arduino.h>
#include <NimBLEDevice.h>
#include <cstring>
#include "protocol.h"
#include "node_state.h"
#include "ble_peripheral.h"

static constexpr uint8_t LED_PIN = 8;

// ──────────── 상태 ────────────
static NodeState state;

static NimBLECharacteristic* pDataChar   = nullptr;
static NimBLECharacteristic* pConfigChar = nullptr;
static volatile bool     deviceConnected = false;
static volatile bool     configReceived  = false;
static volatile uint16_t hubConnHandle   = 0;

// ══════════════════════════════════════════════════════════════════════
// BLE 콜백
// ══════════════════════════════════════════════════════════════════════

class ServerCB : public NimBLEServerCallbacks {
    void onConnect(NimBLEServer* s, NimBLEConnInfo& info) override {
        deviceConnected = true;
        hubConnHandle = info.getConnHandle();
        digitalWrite(LED_PIN, LOW);
        Serial.printf("connected: %s\n", info.getAddress().toString().c_str());
    }

    void onDisconnect(NimBLEServer* s, NimBLEConnInfo& info, int reason) override {
        deviceConnected = false;
        digitalWrite(LED_PIN, HIGH);
        Serial.printf("disconnected (reason=%d)\n", reason);
    }
};

class ConfigCB : public NimBLECharacteristicCallbacks {
    void onWrite(NimBLECharacteristic* c, NimBLEConnInfo& info) override {
        auto val = c->getValue();
        if (val.length() < 1) return;

        auto type = static_cast<MsgType>(val[0]);

        switch (type) {
            case MsgType::ASSIGN_ID: {
                if (val.length() >= sizeof(AssignId)) {
                    AssignId cmd;
                    memcpy(&cmd, val.data(), sizeof(cmd));
                    state.setNodeId(cmd.node_id);
                    Serial.printf(">> assigned id: %u\n", cmd.node_id);
                }
                break;
            }
            case MsgType::SET_INTERVAL: {
                if (val.length() >= sizeof(SetInterval)) {
                    SetInterval cmd;
                    memcpy(&cmd, val.data(), sizeof(cmd));
                    state.setSleepInterval(cmd.interval_sec);
                    Serial.printf(">> interval: %u sec\n", state.sleepInterval());
                }
                break;
            }
            case MsgType::RESET_NODE: {
                if (val.length() >= sizeof(ResetNode)) {
                    ResetNode cmd;
                    memcpy(&cmd, val.data(), sizeof(cmd));
                    Serial.printf(">> reset level: %u\n", cmd.level);
                    if (cmd.level >= 1) state.clear();
                    state.end();
                    ESP.restart();
                }
                break;
            }
            default:
                Serial.printf(">> unknown config: 0x%02x\n", val[0]);
        }

        configReceived = true;
    }
};

static ServerCB  serverCb;
static ConfigCB  configCb;

// ══════════════════════════════════════════════════════════════════════
// 공개 API
// ══════════════════════════════════════════════════════════════════════

void ble_peripheral_init() {
    pinMode(LED_PIN, OUTPUT);
    digitalWrite(LED_PIN, HIGH);

    state.begin();
    Serial.printf("state: node_id=%u interval=%u\n",
                  state.nodeId(), state.sleepInterval());

    NimBLEDevice::init("tempio-sensor");
    NimBLEDevice::setMTU(512);

    auto* pServer = NimBLEDevice::createServer();
    pServer->setCallbacks(&serverCb);

    auto* pService = pServer->createService(TEMPIO_SERVICE_UUID);

    pDataChar = pService->createCharacteristic(
        TEMPIO_CHAR_DATA_UUID,
        NIMBLE_PROPERTY::NOTIFY | NIMBLE_PROPERTY::READ
    );

    pConfigChar = pService->createCharacteristic(
        TEMPIO_CHAR_CONFIG_UUID,
        NIMBLE_PROPERTY::WRITE
    );
    pConfigChar->setCallbacks(&configCb);

    pService->start();
    pServer->start();

    auto* pAdv = NimBLEDevice::getAdvertising();
    pAdv->addServiceUUID(TEMPIO_SERVICE_UUID);
    pAdv->start();
}

bool ble_wait_connect(uint32_t timeout_ms) {
    unsigned long start = millis();
    while (!deviceConnected) {
        if (millis() - start >= timeout_ms) return false;
        delay(10);
    }
    return true;
}

bool ble_wait_config(uint32_t timeout_ms) {
    unsigned long start = millis();
    while (!configReceived) {
        if (!deviceConnected) return false;
        if (millis() - start >= timeout_ms) return false;
        delay(10);
    }
    return true;
}

void ble_send_node_info() {
    if (!pDataChar || !deviceConnected) return;

    NodeInfo info;
    info.node_type  = NodeType::SENSOR;
    info.node_id    = state.nodeId();
    info.battery_mv = 3100;  // TODO: ADC 실측
    info.fw_major   = 0;
    info.fw_minor   = 1;

    pDataChar->setValue(reinterpret_cast<uint8_t*>(&info), sizeof(info));
    pDataChar->notify();
    Serial.printf("<< NodeInfo: id=%u\n", state.nodeId());
}

void ble_send_sensor_data() {
    if (!pDataChar || !deviceConnected) return;

    SensorData data;
    data.temp       = 24.0f + random(-30, 30) / 10.0f;  // TODO: 실제 센서
    data.humidity   = 55.0f + random(-50, 50) / 10.0f;
    data.ldr        = random(100, 3000);
    data.battery_mv = 2900 + random(0, 300);

    pDataChar->setValue(reinterpret_cast<uint8_t*>(&data), sizeof(data));
    pDataChar->notify();
    Serial.printf("<< %.1f C  %.1f %%  ldr=%u  bat=%umV\n",
                  data.temp, data.humidity, data.ldr, data.battery_mv);
}

void ble_disconnect() {
    if (deviceConnected) {
        auto* pServer = NimBLEDevice::createServer();
        pServer->disconnect(hubConnHandle);
        unsigned long start = millis();
        while (deviceConnected && millis() - start < 500) delay(10);
    }

    state.end();
    NimBLEDevice::deinit(false);
    delay(50);
}

bool ble_is_connected() {
    return deviceConnected;
}

uint16_t ble_get_sleep_interval() {
    return state.sleepInterval();
}
