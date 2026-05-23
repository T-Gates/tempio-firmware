#include <Arduino.h>
#include <NimBLEDevice.h>
#include "protocol.h"

// Phase 1: BLE Peripheral — mock 센서 데이터 전송
// 실제 센서(SHT40, LDR)는 Phase 2에서 연결

static constexpr uint8_t LED_PIN = 8;

static NimBLECharacteristic* pDataChar = nullptr;
static NimBLECharacteristic* pConfigChar = nullptr;
static bool deviceConnected = false;
static bool sendNodeInfo = false;

class ServerCB : public NimBLEServerCallbacks {
    void onConnect(NimBLEServer* s, NimBLEConnInfo& info) override {
        deviceConnected = true;
        sendNodeInfo = true;
        digitalWrite(LED_PIN, LOW);
        Serial.printf("connected: %s\n", info.getAddress().toString().c_str());
    }

    void onDisconnect(NimBLEServer* s, NimBLEConnInfo& info, int reason) override {
        deviceConnected = false;
        digitalWrite(LED_PIN, HIGH);
        Serial.printf("disconnected (reason=%d)\n", reason);
        NimBLEDevice::startAdvertising();
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
                    auto* cmd = reinterpret_cast<const AssignId*>(val.data());
                    Serial.printf(">> assigned id: %u\n", cmd->node_id);
                }
                break;
            }
            case MsgType::SET_INTERVAL: {
                if (val.length() >= sizeof(SetInterval)) {
                    auto* cmd = reinterpret_cast<const SetInterval*>(val.data());
                    Serial.printf(">> interval: %u sec\n", cmd->interval_sec);
                }
                break;
            }
            case MsgType::RESET_NODE: {
                if (val.length() >= sizeof(ResetNode)) {
                    auto* cmd = reinterpret_cast<const ResetNode*>(val.data());
                    Serial.printf(">> reset level: %u\n", cmd->level);
                }
                break;
            }
            default:
                Serial.printf(">> unknown config: 0x%02x\n", val[0]);
        }
    }
};

void setup() {
    Serial.begin(115200);
    pinMode(LED_PIN, OUTPUT);
    digitalWrite(LED_PIN, HIGH);

    NimBLEDevice::init("tempio-sensor");
    NimBLEDevice::setMTU(512);

    auto* pServer = NimBLEDevice::createServer();
    pServer->setCallbacks(new ServerCB());

    auto* pService = pServer->createService(TEMPIO_SERVICE_UUID);

    pDataChar = pService->createCharacteristic(
        TEMPIO_CHAR_DATA_UUID,
        NIMBLE_PROPERTY::NOTIFY | NIMBLE_PROPERTY::READ
    );

    pConfigChar = pService->createCharacteristic(
        TEMPIO_CHAR_CONFIG_UUID,
        NIMBLE_PROPERTY::WRITE
    );
    pConfigChar->setCallbacks(new ConfigCB());

    pService->start();

    auto* pAdv = NimBLEDevice::getAdvertising();
    pAdv->addServiceUUID(TEMPIO_SERVICE_UUID);
    pAdv->setScanResponse(true);
    pAdv->start();

    Serial.println("sensor peripheral ready");
}

void loop() {
    if (!deviceConnected) {
        delay(500);
        return;
    }

    if (sendNodeInfo) {
        sendNodeInfo = false;
        NodeInfo info;
        info.node_type = NodeType::SENSOR;
        info.node_id = 0;
        info.battery_mv = 3100;
        info.fw_major = 0;
        info.fw_minor = 1;
        pDataChar->setValue(reinterpret_cast<uint8_t*>(&info), sizeof(info));
        pDataChar->notify();
        Serial.println("<< NodeInfo sent");
        delay(500);
    }

    SensorData data;
    data.temp = 24.0f + random(-30, 30) / 10.0f;
    data.humidity = 55.0f + random(-50, 50) / 10.0f;
    data.ldr = random(100, 3000);
    data.battery_mv = 2900 + random(0, 300);

    pDataChar->setValue(reinterpret_cast<uint8_t*>(&data), sizeof(data));
    pDataChar->notify();

    Serial.printf("<< %.1f C  %.1f %%  ldr=%u  bat=%umV\n",
                  data.temp, data.humidity, data.ldr, data.battery_mv);

    delay(3000);
}
