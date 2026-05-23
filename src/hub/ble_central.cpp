#include <Arduino.h>
#include <NimBLEDevice.h>
#include <cstring>
#include "protocol.h"
#include "ble_central.h"

// ESP32-C3 SuperMini 내장 LED. GPIO8, active-low (LOW=켜짐, HIGH=꺼짐)
static constexpr uint8_t LED_PIN = 8;

// 센서노드에 연결하는 BLE 클라이언트 객체. 이걸로 connect/disconnect/서비스 탐색을 함.
// 연결 전이나 정리 후에는 nullptr
static NimBLEClient* pClient = nullptr;

// 센서노드의 CONFIG 특성(0003) 원격 참조.
// 이걸 통해 센서노드에 명령(ASSIGN_ID, SET_INTERVAL 등)을 write로 보냄.
// 연결 끊기면 nullptr로 초기화됨
static NimBLERemoteCharacteristic* pConfigChar = nullptr;

// 센서노드와 현재 BLE 연결이 되어있는지 여부.
// true=연결됨, false=끊김 또는 미연결.
// volatile: BLE 콜백(onDisconnect)과 loop()가 서로 다른 태스크에서 읽고 쓰기 때문에 필요
static volatile bool connected = false;

// 스캔에서 센서노드를 발견했을 때 true로 설정됨.
// loop()에서 이걸 보고 connectToServer()를 호출함.
// volatile: 스캔 콜백(BLE 태스크)에서 쓰고, loop()(Arduino 태스크)에서 읽기 때문
static volatile bool doConnect = false;

// 연결이 끊겼을 때 true로 설정됨.
// onDisconnect 콜백 안에서 직접 스캔을 시작하면 NimBLE 내부 상태가 불안정할 수 있어서,
// loop()에서 이 플래그를 보고 스캔을 재시작함
static volatile bool doScan = false;

// 스캔에서 발견한 센서노드의 BLE 주소 (MAC 주소 같은 것).
// ScanCB::onResult()에서 저장하고, connectToServer()에서 이 주소로 연결함
static NimBLEAddress targetAddr;

// 마지막으로 "scanning..." 메시지를 시리얼에 출력한 시각 (millis 기준).
// 5초마다 한 번 출력하기 위한 타이머
static unsigned long lastPrint = 0;

// ──────────── notify 수신 ────────────

static void onDataNotify(NimBLERemoteCharacteristic* c,
                         uint8_t* data, size_t len, bool isNotify) {
    if (len < 1) return;

    auto type = static_cast<MsgType>(data[0]);
    switch (type) {
        case MsgType::NODE_INFO: {
            if (len < sizeof(NodeInfo)) break;
            // memcpy로 정렬 안전하게 복사 (reinterpret_cast 대신)
            NodeInfo ni;
            memcpy(&ni, data, sizeof(ni));
            const char* typeName = (ni.node_type == NodeType::SENSOR) ? "sensor" : "ir";
            Serial.printf(">> NodeInfo: type=%s id=%u bat=%umV fw=%u.%u\n",
                          typeName, ni.node_id, ni.battery_mv,
                          ni.fw_major, ni.fw_minor);
            break;
        }
        case MsgType::SENSOR_DATA: {
            if (len < sizeof(SensorData)) break;
            SensorData sd;
            memcpy(&sd, data, sizeof(sd));
            Serial.printf(">> sensor: %.1f C  %.1f %%  ldr=%u  bat=%umV\n",
                          sd.temp, sd.humidity, sd.ldr, sd.battery_mv);
            break;
        }
        default:
            Serial.printf(">> unknown: 0x%02x (%u bytes)\n", data[0], len);
    }
}

// ──────────── 콜백 (static 인스턴스로 힙 할당 방지) ────────────

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
        Serial.printf("disconnected (reason=%d)\n", reason);
        // 콜백 안에서 직접 스캔 시작하지 않고 loop에 위임
        doScan = true;
    }
};

// 콜백 객체를 static으로 한 번만 생성해서 재사용.
// new로 매번 만들면 힙 메모리가 쌓이고 안 지워짐
static ScanCB scanCb;    // 스캔 결과 콜백 — BLE 기기 발견 시 호출됨
static ClientCB clientCb; // 연결/해제 콜백 — 센서노드와 연결 상태 변할 때 호출됨

// ──────────── 연결·명령 ────────────

static bool connectToServer() {
    // 기존 클라이언트가 있으면 정리 (풀 소진 방지)
    if (pClient) {
        NimBLEDevice::deleteClient(pClient);
        pClient = nullptr;
    }

    pClient = NimBLEDevice::createClient();
    if (!pClient) {
        Serial.println("createClient failed");
        return false;
    }
    pClient->setClientCallbacks(&clientCb);

    if (!pClient->connect(targetAddr)) {
        Serial.println("connect failed");
        // 실패 시 클라이언트 정리 (풀 슬롯 반환)
        NimBLEDevice::deleteClient(pClient);
        pClient = nullptr;
        return false;
    }

    auto* pService = pClient->getService(TEMPIO_SERVICE_UUID);
    if (!pService) {
        Serial.println("service not found");
        pClient->disconnect();
        NimBLEDevice::deleteClient(pClient);
        pClient = nullptr;
        return false;
    }

    auto* pDataChar = pService->getCharacteristic(TEMPIO_CHAR_DATA_UUID);
    if (pDataChar && pDataChar->canNotify()) {
        pDataChar->subscribe(true, onDataNotify);
        Serial.println("subscribed to DATA");
    } else {
        // DATA 구독 실패 — 센서값을 받을 수 없으므로 연결 의미 없음
        Serial.println("DATA subscribe failed — disconnecting");
        pClient->disconnect();
        NimBLEDevice::deleteClient(pClient);
        pClient = nullptr;
        return false;
    }

    pConfigChar = pService->getCharacteristic(TEMPIO_CHAR_CONFIG_UUID);

    connected = true;
    digitalWrite(LED_PIN, LOW);
    return true;
}

static void sendCommand(const void* data, size_t len) {
    if (!connected || !pConfigChar) return;
    pConfigChar->writeValue(reinterpret_cast<const uint8_t*>(data), len);
}

// ──────────── 공개 API ────────────

void ble_central_init() {
    pinMode(LED_PIN, OUTPUT);
    digitalWrite(LED_PIN, HIGH);

    NimBLEDevice::init("tempio-hub");
    NimBLEDevice::setMTU(512);

    auto* pScan = NimBLEDevice::getScan();
    pScan->setScanCallbacks(&scanCb);
    pScan->setActiveScan(true);
    pScan->setInterval(100);
    pScan->setWindow(99);
    pScan->start(0, false);
}

void ble_central_loop() {
    // 끊김 후 재스캔 요청 처리 (콜백에서 위임받음)
    if (doScan) {
        doScan = false;
        NimBLEDevice::getScan()->start(0, false);
    }

    if (doConnect) {
        doConnect = false;
        if (connectToServer()) {
            delay(1000);
            AssignId cmd;
            cmd.node_id = 1; // TODO: 동적 ID 부여
            sendCommand(&cmd, sizeof(cmd));
            Serial.println("<< ASSIGN_ID: 1");
        } else {
            NimBLEDevice::getScan()->start(0, false);
        }
    }

    if (!connected && millis() - lastPrint > 5000) {
        lastPrint = millis();
        Serial.println("scanning...");
    }
}

bool ble_is_connected() {
    return connected;
}
