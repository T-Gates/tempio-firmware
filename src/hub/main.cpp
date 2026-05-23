#include <Arduino.h>
#include <NimBLEDevice.h>
#include "protocol.h"

// Phase 1: BLE Central — 센서노드 스캔 → 연결 → 데이터 수신
// WiFi, SCD40은 Phase 3~4에서 추가

static constexpr uint8_t LED_PIN = 8;

static NimBLEClient* pClient = nullptr;
static NimBLERemoteCharacteristic* pConfigChar = nullptr;
static bool connected = false;
static bool doConnect = false;
static NimBLEAddress targetAddr;

static void onDataNotify(NimBLERemoteCharacteristic* c,
                         uint8_t* data, size_t len, bool isNotify) {
    if (len < 1) return;

    auto type = static_cast<MsgType>(data[0]);
    switch (type) {
        case MsgType::NODE_INFO: {
            if (len < sizeof(NodeInfo)) break;
            auto* ni = reinterpret_cast<NodeInfo*>(data);
            const char* typeName = (ni->node_type == NodeType::SENSOR) ? "sensor" : "ir";
            Serial.printf(">> NodeInfo: type=%s id=%u bat=%umV fw=%u.%u\n",
                          typeName, ni->node_id, ni->battery_mv,
                          ni->fw_major, ni->fw_minor);
            break;
        }
        case MsgType::SENSOR_DATA: {
            if (len < sizeof(SensorData)) break;
            auto* sd = reinterpret_cast<SensorData*>(data);
            Serial.printf(">> sensor: %.1f C  %.1f %%  ldr=%u  bat=%umV\n",
                          sd->temp, sd->humidity, sd->ldr, sd->battery_mv);
            break;
        }
        default:
            Serial.printf(">> unknown: 0x%02x (%u bytes)\n", data[0], len);
    }
}

class ScanCB : public NimBLEScanCallbacks {
    void onResult(const NimBLEAdvertisedDevice* dev) override {
        if (dev->isAdvertisingService(NimBLEUUID(TEMPIO_SERVICE_UUID))) {
            Serial.printf("found: %s  rssi=%d\n",
                          dev->getAddress().toString().c_str(), dev->getRSSI());
            NimBLEDevice::getScan()->stop();
            targetAddr = dev->getAddress();
            doConnect = true;
        }
    }
};

class ClientCB : public NimBLEClientCallbacks {
    void onConnect(NimBLEClient* c) override {
        Serial.println("connected");
    }

    void onDisconnect(NimBLEClient* c, int reason) override {
        connected = false;
        pConfigChar = nullptr;
        digitalWrite(LED_PIN, HIGH);
        Serial.printf("disconnected (reason=%d) — restarting scan\n", reason);
        NimBLEDevice::getScan()->start(0, false);
    }
};

bool connectToServer() {
    pClient = NimBLEDevice::createClient();
    pClient->setClientCallbacks(new ClientCB());

    if (!pClient->connect(targetAddr)) {
        Serial.println("connect failed");
        return false;
    }

    auto* pService = pClient->getService(TEMPIO_SERVICE_UUID);
    if (!pService) {
        Serial.println("service not found");
        pClient->disconnect();
        return false;
    }

    auto* pDataChar = pService->getCharacteristic(TEMPIO_CHAR_DATA_UUID);
    if (pDataChar && pDataChar->canNotify()) {
        pDataChar->subscribe(true, onDataNotify);
        Serial.println("subscribed to DATA");
    }

    pConfigChar = pService->getCharacteristic(TEMPIO_CHAR_CONFIG_UUID);

    connected = true;
    digitalWrite(LED_PIN, LOW);
    return true;
}

void sendCommand(const void* data, size_t len) {
    if (!connected || !pConfigChar) return;
    pConfigChar->writeValue(reinterpret_cast<const uint8_t*>(data), len);
}

void startScan() {
    auto* pScan = NimBLEDevice::getScan();
    pScan->setScanCallbacks(new ScanCB());
    pScan->setActiveScan(true);
    pScan->setInterval(100);
    pScan->setWindow(99);
    pScan->start(0, false);
    Serial.println("scanning...");
}

void setup() {
    Serial.begin(115200);
    delay(2000);
    pinMode(LED_PIN, OUTPUT);
    digitalWrite(LED_PIN, HIGH);

    Serial.println("=== TEMPIO HUB ===");
    Serial.println("BLE init...");
    NimBLEDevice::init("tempio-hub");
    NimBLEDevice::setMTU(512);
    Serial.println("BLE ok");

    startScan();
    Serial.println("hub central ready");
}

void loop() {
    if (doConnect) {
        doConnect = false;
        if (connectToServer()) {
            delay(1000);
            AssignId cmd;
            cmd.node_id = 1;
            sendCommand(&cmd, sizeof(cmd));
            Serial.println("<< ASSIGN_ID: 1");
        } else {
            startScan();
        }
    }

    delay(100);
}
