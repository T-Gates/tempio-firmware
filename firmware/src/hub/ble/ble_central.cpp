// ═══════════════════════════════════════════════════════════════════════════
// ble_central.cpp — 허브(Hub) BLE Central 로직 (다중 연결)
// ═══════════════════════════════════════════════════════════════════════════
//
// 전체 흐름 (큰 그림):
//
//   1. init()에서 BLE 스캔을 시작한다.
//   2. 스캔 중 tempio 서비스 UUID를 광고하는 장치를 발견하면
//      → pending 큐(대기열)에 주소를 넣어둔다.
//   3. loop()가 매 프레임 돌면서:
//      a) 끊긴 연결 정리
//      b) pending 큐에서 주소를 하나 꺼내 연결 시도
//      c) 연결되면 → 서비스 탐색 → DATA 특성 구독 → HUB_READY 전송
//   4. 센서노드가 보내는 notify 데이터는 onDataNotify 콜백으로 수신.
//   5. 노드가 끊기면 슬롯 해제 → 재스캔.
//
// 노드 식별: BLE MAC 주소 (공장 고유). 별도 ID 부여 없음.
//
// 왜 pending 큐를 쓰나?
//   connect()는 blocking(완료될 때까지 멈춤)이라 스캔 콜백 안에서 부르면
//   BLE 스택이 꼬인다. 그래서 "발견"과 "연결"을 분리.

#include <Arduino.h>
#include <NimBLEDevice.h>     // NimBLE: ESP32용 경량 BLE 라이브러리
#include <cstring>            // memcpy 쓰려고
#include "protocol.h"         // 우리 프로토콜: UUID, MsgType, 패킷 구조체들
#include "ble_central.h"

// C3=GPIO8(active-low), ESP32=GPIO2(active-high)
#if defined(CONFIG_IDF_TARGET_ESP32C3)
static constexpr uint8_t LED_PIN = 8;
static constexpr bool LED_ON = LOW;
#else
static constexpr uint8_t LED_PIN = 2;
static constexpr bool LED_ON = HIGH;
#endif

// ──────────── 다중 연결 관리 ────────────

static constexpr int MAX_NODES = 5;  // 동시 연결 상한 (S3은 5대, C3은 3대 권장)

// 연결된 노드 하나의 정보를 묶는 구조체.
// nodes[0]~nodes[4]가 "슬롯" 역할. used==true면 활성 연결.
struct ConnectedNode {
    NimBLEClient* client = nullptr;                   // BLE 연결 객체 (전화기)
    NimBLERemoteCharacteristic* configChar = nullptr; // 노드의 CONFIG 특성 핸들 (명령 전송용)
    NimBLEAddress addr;                               // 노드의 BLE MAC 주소 (= 노드 식별자)
    NodeType nodeType = NodeType::SENSOR;              // 센서인지 IR인지
    bool     used     = false;                        // 이 슬롯이 사용 중인지
};

static ConnectedNode nodes[MAX_NODES];  // 슬롯 배열 — 이게 연결 관리의 핵심

// ──────────── 연결 대기 큐 (pending queue) ────────────
// 스캔에서 발견했지만 아직 연결 안 한 주소들.
// 파이썬의 list.append() → list.pop(0) 같은 역할인데, 고정 배열로 구현.
static constexpr int PENDING_MAX = 8;
static NimBLEAddress pendingAddrs[PENDING_MAX];  // 대기 중인 주소 배열
static int pendingCount = 0;                      // 현재 대기 수

// ──────────── 센서 데이터 공유 버퍼 ────────────
static SensorReport pendingReport;
static volatile bool reportReady = false;

// ──────────── 플래그 ────────────
static volatile bool doScan = false;       // true면 다음 loop()에서 스캔 재시작
static unsigned long lastPrint = 0;        // 상태 출력 타이머 (5초마다)

// ══════════════════════════════════════════════════════════════════════
// 헬퍼 함수 — nodes[] 배열을 검색/관리하는 유틸리티
// 파이썬이었으면 dict나 list comprehension으로 한 줄이지만,
// C++에서는 for문으로 배열을 직접 뒤진다.
// ══════════════════════════════════════════════════════════════════════

// 이미 연결된 주소인지 확인 (중복 연결 방지)
static bool isAlreadyConnected(const NimBLEAddress& addr) {
    for (int i = 0; i < MAX_NODES; i++) {
        if (nodes[i].used && nodes[i].addr == addr) return true;
    }
    return false;
}

// 이미 대기 큐에 들어간 주소인지 확인 (중복 큐잉 방지)
static bool isAlreadyPending(const NimBLEAddress& addr) {
    for (int i = 0; i < pendingCount; i++) {
        if (pendingAddrs[i] == addr) return true;
    }
    return false;
}

// 비어있는 슬롯 번호 반환. 다 차면 -1.
static int findEmptySlot() {
    for (int i = 0; i < MAX_NODES; i++) {
        if (!nodes[i].used) return i;
    }
    return -1;
}

// NimBLEClient 포인터로 슬롯 찾기 (콜백에서 "이 연결이 누구인지" 알아낼 때)
static int findSlotByClient(const NimBLEClient* c) {
    for (int i = 0; i < MAX_NODES; i++) {
        if (nodes[i].used && nodes[i].client == c) return i;
    }
    return -1;
}

// MAC 주소로 슬롯 찾기 (특정 노드에 명령 보낼 때)
static int findSlotByAddr(const NimBLEAddress& addr) {
    for (int i = 0; i < MAX_NODES; i++) {
        if (nodes[i].used && nodes[i].addr == addr) return i;
    }
    return -1;
}

// 현재 활성 연결 수 (파이썬: sum(1 for n in nodes if n.used))
static int activeCount() {
    int count = 0;
    for (int i = 0; i < MAX_NODES; i++) {
        if (nodes[i].used) count++;
    }
    return count;
}

// 끊긴 슬롯의 configChar만 정리. client 객체는 유지해서 재사용.
// deleteClient()는 NimBLE 2.5.0에서 heap 크래시를 일으키므로 쓰지 않는다.
static void cleanupDisconnected() {
    for (int i = 0; i < MAX_NODES; i++) {
        if (!nodes[i].used && nodes[i].client) {
            nodes[i].configChar = nullptr;
        }
    }
}

// 기존 끊긴 클라이언트 객체를 찾아 반환. 없으면 nullptr.
static NimBLEClient* findReusableClient() {
    for (int i = 0; i < MAX_NODES; i++) {
        if (!nodes[i].used && nodes[i].client) {
            NimBLEClient* c = nodes[i].client;
            nodes[i].client = nullptr;
            return c;
        }
    }
    return nullptr;
}

// ══════════════════════════════════════════════════════════════════════
// notify 수신 콜백 — 센서노드가 데이터를 보내면 여기로 온다
// ══════════════════════════════════════════════════════════════════════
//
// BLE에서 notify란?
//   센서노드(peripheral)가 허브(central)에게 "나 데이터 있어!" 하고 밀어주는 것.
//   허브가 요청하는 게 아니라, 센서가 능동적으로 보내는 push 방식.
//   아래 함수는 NimBLE가 notify를 받을 때마다 자동으로 호출해주는 콜백.

static void onDataNotify(NimBLERemoteCharacteristic* c,
                         uint8_t* data, size_t len, bool isNotify) {
    if (len < 1) return;

    // 이 데이터를 보낸 노드가 누구인지 찾는다.
    // 특성(c) → 서비스 → 클라이언트 → nodes[] 슬롯 순으로 역추적.
    int slot = -1;
    auto* svc = c->getRemoteService();
    if (svc) slot = findSlotByClient(svc->getClient());
    // toString()은 임시 std::string을 반환 → c_str()만 저장하면 댕글링 포인터.
    // string 자체를 보관해야 포인터가 유효.
    std::string srcAddrStr = (slot >= 0)
        ? nodes[slot].addr.toString() : "??";
    const char* srcAddr = srcAddrStr.c_str();

    // data[0]이 메시지 타입 — protocol.h의 MsgType enum 값.
    auto type = static_cast<MsgType>(data[0]);

    switch (type) {
        case MsgType::NODE_INFO: {
            // 센서노드의 자기소개: 타입, 배터리, 펌웨어 버전
            if (len < sizeof(NodeInfo)) break;
            NodeInfo ni;
            memcpy(&ni, data, sizeof(ni));

            if (slot >= 0) {
                nodes[slot].nodeType = ni.node_type;
            }

            const char* typeName = (ni.node_type == NodeType::SENSOR) ? "sensor" : "ir";
            Serial.printf(">> [%s] NodeInfo: type=%s bat=%umV fw=%u.%u\n",
                          srcAddr, typeName, ni.battery_mv, ni.fw_major, ni.fw_minor);
            break;
        }
        case MsgType::SENSOR_DATA: {
            // 센서 측정값: 온도, 습도, 조도, 배터리
            if (len < sizeof(SensorData)) break;
            SensorData sd;
            memcpy(&sd, data, sizeof(sd));
            Serial.printf(">> [%s] sensor: %.1f C  %.1f %%  ldr=%u  bat=%umV\n",
                          srcAddr, sd.temp, sd.humidity, sd.ldr, sd.battery_mv);

            // 공유 버퍼에 복사 — WiFi 모듈이 ble_get_pending_report()로 꺼내감
            strncpy(pendingReport.node_id, srcAddr, sizeof(pendingReport.node_id) - 1);
            pendingReport.node_id[sizeof(pendingReport.node_id) - 1] = '\0';

            const char* typeStr = (slot >= 0 && nodes[slot].nodeType == NodeType::IR)
                ? "ir" : "sensor";
            strncpy(pendingReport.node_type_str, typeStr, sizeof(pendingReport.node_type_str) - 1);
            pendingReport.node_type_str[sizeof(pendingReport.node_type_str) - 1] = '\0';

            pendingReport.temperature = sd.temp;
            pendingReport.humidity    = sd.humidity;
            pendingReport.ldr         = sd.ldr;
            pendingReport.battery_mv  = sd.battery_mv;
            pendingReport.ble_rssi    = (slot >= 0) ? nodes[slot].client->getRssi() : 0;
            reportReady = true;
            break;
        }
        default:
            Serial.printf(">> [%s] unknown: 0x%02x (%u bytes)\n",
                          srcAddr, data[0], len);
    }
}

// ══════════════════════════════════════════════════════════════════════
// BLE 콜백 클래스
// ══════════════════════════════════════════════════════════════════════
//
// BLE에서 "콜백"이란?
//   NimBLE가 이벤트(장치 발견, 연결, 끊김)를 감지하면
//   우리가 등록한 함수를 자동으로 호출해주는 것.
//   파이썬의 on_message, on_connect 핸들러와 같은 개념.

// ── 스캔 콜백 ──
// BLE 스캔 중 주변 장치가 발견될 때마다 onResult()가 호출된다.
// 우리 서비스 UUID를 광고하는 놈만 걸러서 pending 큐에 넣는다.
class ScanCB : public NimBLEScanCallbacks {
    void onResult(const NimBLEAdvertisedDevice* dev) override {
        // 우리 tempio 서비스가 아니면 무시
        if (!dev->isAdvertisingService(NimBLEUUID(TEMPIO_SERVICE_UUID))) return;

        auto addr = dev->getAddress();

        // 4가지 조건 중 하나라도 걸리면 무시:
        if (isAlreadyConnected(addr)) return;     // 이미 연결된 놈
        if (isAlreadyPending(addr))   return;     // 이미 큐에 들어간 놈
        if (findEmptySlot() < 0)      return;     // 슬롯 다 찼음
        if (pendingCount >= PENDING_MAX) return;   // 큐 다 찼음

        Serial.printf("found: %s  rssi=%d\n",
                      addr.toString().c_str(), dev->getRSSI());

        pendingAddrs[pendingCount++] = addr;  // 큐에 추가
    }
};

// ── 클라이언트 콜백 ──
// 연결/끊김 이벤트를 처리한다.
class ClientCB : public NimBLEClientCallbacks {
    void onConnect(NimBLEClient* c) override {
        Serial.printf("connected: %s\n", c->getPeerAddress().toString().c_str());
    }

    void onDisconnect(NimBLEClient* c, int reason) override {
        // 끊긴 클라이언트가 어떤 슬롯인지 찾아서 used=false로만 표시.
        // 실제 메모리 해제(deleteClient)는 loop()의 cleanupDisconnected()에서.
        int slot = findSlotByClient(c);
        if (slot >= 0) {
            Serial.printf("%s disconnected (reason=%d)\n",
                          nodes[slot].addr.toString().c_str(), reason);
            nodes[slot].used = false;
            nodes[slot].configChar = nullptr;
        }
        // LED: 연결된 노드가 하나라도 있으면 켜짐, 없으면 꺼짐
        digitalWrite(LED_PIN, activeCount() > 0 ? LED_ON : !LED_ON);
        doScan = true;  // 다음 loop()에서 스캔 재시작
    }
};

static ScanCB  scanCb;    // 스캔 콜백 인스턴스 (전역 — 한 번만 생성)
static ClientCB clientCb;  // 클라이언트 콜백 인스턴스

// ══════════════════════════════════════════════════════════════════════
// 연결 함수 — 센서노드 하나와의 연결을 처음부터 끝까지 처리
// ══════════════════════════════════════════════════════════════════════
//
// 연결 시퀀스:
//   1) NimBLE 클라이언트 생성 (createClient)
//   2) 상대 주소로 BLE 연결 (connect — blocking, 수초 걸릴 수 있음)
//   3) 서비스 탐색 (getService) — "tempio 서비스 있냐?" 물어보는 것
//   4) DATA 특성 구독 (subscribe) — "너가 notify 보내면 나한테 알려줘"
//   5) 슬롯에 저장 (MAC 주소가 곧 식별자)
//   6) HUB_READY 전송 — "나 준비됐어, 데이터 보내도 돼"

// 클라이언트를 빈 슬롯에 보관 (재사용용). 실패 경로에서 호출.
// NimBLE 2.5.0에서 deleteClient()는 heap 크래시를 일으키므로,
// 삭제 대신 슬롯에 넣어두고 findReusableClient()가 재활용하게 한다.
static void stashClient(NimBLEClient* c) {
    for (int i = 0; i < MAX_NODES; i++) {
        if (!nodes[i].used && !nodes[i].client) {
            nodes[i].client = c;
            return;
        }
    }
}

static bool connectToNode(const NimBLEAddress& addr) {
    int slot = findEmptySlot();
    if (slot < 0) {
        Serial.println("no empty slot");
        return false;
    }

    // 1) 끊긴 클라이언트 재사용 시도 → 없으면 새로 생성
    auto* client = findReusableClient();
    if (!client) {
        client = NimBLEDevice::createClient();
    }
    if (!client) {
        Serial.println("createClient failed");
        return false;
    }
    client->setClientCallbacks(&clientCb);

    // 2) BLE 연결 — 실패하면 클라이언트를 슬롯에 보관 (누수 방지)
    if (!client->connect(addr)) {
        Serial.printf("connect failed: %s\n", addr.toString().c_str());
        client->disconnect();
        stashClient(client);
        return false;
    }

    // 3) 서비스 탐색
    auto* svc = client->getService(TEMPIO_SERVICE_UUID);
    if (!svc) {
        Serial.println("service not found");
        client->disconnect();
        stashClient(client);
        return false;
    }

    // 4) DATA 특성 구독
    auto* dataChar = svc->getCharacteristic(TEMPIO_CHAR_DATA_UUID);
    if (dataChar && dataChar->canNotify()) {
        dataChar->subscribe(true, onDataNotify);
    } else {
        Serial.println("DATA subscribe failed");
        client->disconnect();
        stashClient(client);
        return false;
    }

    // 5) 슬롯에 저장 — MAC 주소가 곧 노드 식별자.
    nodes[slot].client     = client;
    nodes[slot].configChar = svc->getCharacteristic(TEMPIO_CHAR_CONFIG_UUID);
    nodes[slot].addr       = addr;
    nodes[slot].used       = true;

    // 6) HUB_READY 전송 — "subscribe 끝남, 너 누구야?"
    delay(200);
    HubReady ready;
    if (nodes[slot].configChar) {
        nodes[slot].configChar->writeValue(
            reinterpret_cast<const uint8_t*>(&ready), sizeof(ready));
    }
    Serial.printf("<< HUB_READY → %s\n", addr.toString().c_str());

    digitalWrite(LED_PIN, LED_ON);  // 연결 성공 → LED 켜기
    return true;
}

// ══════════════════════════════════════════════════════════════════════
// 공개 API — 외부에서 호출하는 함수들 (ble_central.h에 선언)
// ══════════════════════════════════════════════════════════════════════

// setup()에서 한 번 호출. BLE 스택 초기화 + 스캔 시작.
void ble_central_init() {
    pinMode(LED_PIN, OUTPUT);
    digitalWrite(LED_PIN, !LED_ON);   // LED 끔

    NimBLEDevice::init("tempio-hub");  // BLE 장치 이름 설정
    NimBLEDevice::setMTU(512);          // 한 번에 보낼 수 있는 최대 바이트 (기본 23)

    auto* pScan = NimBLEDevice::getScan();
    pScan->setScanCallbacks(&scanCb);   // 장치 발견 시 ScanCB::onResult() 호출
    pScan->setActiveScan(true);         // active scan: 더 많은 정보를 요청 (느리지만 정확)
    pScan->setInterval(100);            // 스캔 간격 (단위: 0.625ms → 100 = 62.5ms)
    pScan->setWindow(99);               // 실제 스캔 시간 (interval에 가까울수록 꼼꼼)
    pScan->start(0, false);             // 0 = 무한 스캔, false = 중복 콜백 허용
}

// loop()에서 반복 호출. 매 프레임의 BLE 처리를 4단계로 수행.
void ble_central_loop() {
    // 1단계: 끊긴 클라이언트 메모리 해제 (NimBLE 풀 반환)
    cleanupDisconnected();

    // 2단계: disconnect 콜백이 재스캔을 요청했으면 스캔 재시작
    if (doScan) {
        doScan = false;
        NimBLEDevice::getScan()->start(0, false);
    }

    // 3단계: pending 큐에서 주소를 하나 꺼내 연결 시도
    // 한 루프에 하나만 — connect()가 blocking이라 여러 개 하면 멈춤
    if (pendingCount > 0) {
        // 큐 맨 앞 꺼내기 (pop front — 배열이라 뒤를 앞으로 당김)
        NimBLEAddress addr = pendingAddrs[0];
        for (int i = 1; i < pendingCount; i++) {
            pendingAddrs[i - 1] = pendingAddrs[i];
        }
        pendingCount--;

        NimBLEDevice::getScan()->stop();  // 연결 중에는 스캔 못 함 (NimBLE 제약)

        connectToNode(addr);

        NimBLEDevice::getScan()->start(0, false);  // 성공/실패 무관 스캔 재시작
    }

    // 4단계: 시리얼 상태 출력 (5초마다)
    if (millis() - lastPrint > 5000) {
        lastPrint = millis();
        int count = activeCount();
        if (count == 0) {
            Serial.println("scanning...");
        } else {
            Serial.printf("nodes: %d connected\n", count);
        }
    }
}

// 현재 연결된 노드 수
int ble_connected_count() {
    return activeCount();
}

// 특정 노드에 명령 전송. MAC 주소로 슬롯 찾고 CONFIG 특성에 write.
// 예: SET_INTERVAL, RESET_NODE 등을 서버→허브→노드로 내릴 때 사용.
bool ble_send_to_node(const char* addrStr, const void* data, size_t len) {
    NimBLEAddress addr(std::string(addrStr), 0);
    int slot = findSlotByAddr(addr);
    if (slot < 0 || !nodes[slot].configChar) return false;

    nodes[slot].configChar->writeValue(
        reinterpret_cast<const uint8_t*>(data), len);
    return true;
}

// 가장 최근 센서 리포트를 꺼내간다. 새 데이터가 없으면 false.
bool ble_get_pending_report(SensorReport* out) {
    if (!reportReady) return false;
    *out = pendingReport;
    reportReady = false;
    return true;
}
