// Arduino 기본 함수들 (Serial, delay, pinMode, digitalWrite 등)
#include <Arduino.h>
// NimBLE 라이브러리 — ESP32용 BLE(저전력 블루투스) 통신 라이브러리.
// BLE Central(클라이언트) 역할: 주변 기기를 스캔하고 연결하는 쪽
#include <NimBLEDevice.h>
// 우리가 정의한 BLE 프로토콜 — UUID, 메시지 타입, 구조체 등
#include "protocol.h"
// 이 파일의 함수 선언 헤더
#include "ble_central.h"

// ESP32-C3 SuperMini 내장 LED 핀 번호. GPIO 8번에 연결돼 있음
// constexpr = 컴파일 시점에 값이 확정되는 상수. #define보다 타입 안전함
// static = 이 파일 안에서만 사용 가능 (다른 파일에서 접근 불가)
static constexpr uint8_t LED_PIN = 8;

// NimBLEClient 포인터 — BLE 서버(센서노드)에 연결하는 클라이언트 객체
// nullptr = 아직 생성 안 됨 (연결 전)
static NimBLEClient* pClient = nullptr;
// CONFIG 특성의 원격 참조. 이걸 통해 센서노드에 명령(ASSIGN_ID 등)을 보냄
static NimBLERemoteCharacteristic* pConfigChar = nullptr;
// 센서노드와 현재 연결되어 있는지 여부
static bool connected = false;
// 스캔에서 센서노드를 발견했고, 연결을 시도해야 하는지 여부 (플래그)
static bool doConnect = false;
// 스캔에서 발견한 센서노드의 BLE 주소 (MAC 주소 같은 것)
static NimBLEAddress targetAddr;
// 마지막으로 "scanning..." 메시지를 출력한 시각 (millis 기준)
static unsigned long lastPrint = 0;

// ──────────── notify 수신 ────────────

// 센서노드가 DATA 특성으로 notify를 보내면 이 함수가 자동 호출됨 (콜백 함수)
// c = 어떤 특성에서 왔는지, data = 받은 바이트 배열, len = 바이트 수, isNotify = notify인지 indicate인지
static void onDataNotify(NimBLERemoteCharacteristic* c,
                         uint8_t* data, size_t len, bool isNotify) {
    // 데이터가 1바이트도 없으면 무시
    if (len < 1) return;

    // 첫 바이트(data[0])를 MsgType enum으로 변환 — 어떤 종류의 메시지인지 판별
    // static_cast = C++ 타입 변환. uint8_t 숫자를 MsgType enum 값으로 해석
    auto type = static_cast<MsgType>(data[0]);
    // 메시지 타입에 따라 다른 처리를 하는 분기문
    switch (type) {
        // 센서노드가 연결 직후 보내는 자기소개 메시지
        case MsgType::NODE_INFO: {
            // 받은 데이터 크기가 NodeInfo 구조체보다 작으면 불완전 → 무시
            if (len < sizeof(NodeInfo)) break;
            // 바이트 배열을 NodeInfo 구조체 포인터로 재해석 (메모리 레이아웃이 같으므로 가능)
            // reinterpret_cast = 메모리를 다른 타입으로 "그대로" 읽겠다는 C++ 캐스팅
            auto* ni = reinterpret_cast<NodeInfo*>(data);
            // 노드 타입이 SENSOR면 "sensor", 아니면 "ir" 문자열
            const char* typeName = (ni->node_type == NodeType::SENSOR) ? "sensor" : "ir";
            // 시리얼 모니터에 노드 정보 출력
            // %s = 문자열, %u = 부호 없는 정수, %u.%u = 메이저.마이너 버전
            Serial.printf(">> NodeInfo: type=%s id=%u bat=%umV fw=%u.%u\n",
                          typeName, ni->node_id, ni->battery_mv,
                          ni->fw_major, ni->fw_minor);
            break;
        }
        // 센서노드가 주기적으로 보내는 센서 측정값
        case MsgType::SENSOR_DATA: {
            // 받은 데이터 크기가 SensorData 구조체보다 작으면 불완전 → 무시
            if (len < sizeof(SensorData)) break;
            // 바이트 배열을 SensorData 구조체로 재해석
            auto* sd = reinterpret_cast<SensorData*>(data);
            // 시리얼 모니터에 센서값 출력
            // %.1f = 소수점 1자리 실수, %u = 부호 없는 정수
            Serial.printf(">> sensor: %.1f C  %.1f %%  ldr=%u  bat=%umV\n",
                          sd->temp, sd->humidity, sd->ldr, sd->battery_mv);
            break;
        }
        // 알 수 없는 메시지 타입 → 타입 코드와 바이트 수만 출력
        default:
            // 0x%02x = 16진수 2자리 (예: 0x0A)
            Serial.printf(">> unknown: 0x%02x (%u bytes)\n", data[0], len);
    }
}

// ──────────── 콜백 ────────────

// BLE 스캔 결과 콜백 — 주변에서 BLE 광고를 보내는 기기가 발견될 때마다 호출됨
// NimBLEScanCallbacks를 상속받아 onResult를 오버라이드(재정의)
class ScanCB : public NimBLEScanCallbacks {
    // dev = 발견된 BLE 기기 정보 (이름, 주소, 신호 세기, 광고하는 서비스 UUID 등)
    // override = 부모 클래스의 함수를 재정의한다는 표시
    void onResult(const NimBLEAdvertisedDevice* dev) override {
        // 발견된 기기가 우리 TEMPIO 서비스 UUID를 광고하고 있는지 확인
        // 다른 BLE 기기(이어폰, 시계 등)는 여기서 걸러짐
        if (dev->isAdvertisingService(NimBLEUUID(TEMPIO_SERVICE_UUID))) {
            // 발견! 주소와 RSSI(신호 세기, 가까울수록 0에 가까움) 출력
            // toString() = BLE 주소를 "AA:BB:CC:DD:EE:FF" 문자열로 변환
            // c_str() = C++ string을 C 문자열(char*)로 변환 (printf가 요구)
            Serial.printf("found: %s  rssi=%d\n",
                          dev->getAddress().toString().c_str(), dev->getRSSI());
            // 원하는 기기를 찾았으니 스캔 중지 (불필요한 전력 소모 방지)
            NimBLEDevice::getScan()->stop();
            // 찾은 기기의 BLE 주소를 저장해둠 (나중에 연결할 때 사용)
            targetAddr = dev->getAddress();
            // "연결해야 함" 플래그 켜기 → loop()에서 connectToServer() 호출됨
            doConnect = true;
        }
    }
};

// BLE 클라이언트 연결/해제 콜백 — 연결 상태가 바뀔 때 자동 호출
class ClientCB : public NimBLEClientCallbacks {
    // 센서노드에 성공적으로 연결됐을 때 호출
    void onConnect(NimBLEClient* c) override {
        Serial.println("connected");
    }

    // 센서노드와 연결이 끊겼을 때 호출 (거리 벗어남, 센서 전원 꺼짐 등)
    // reason = 끊긴 이유 코드 (BLE 표준 정의)
    void onDisconnect(NimBLEClient* c, int reason) override {
        // 연결 상태를 false로 변경
        connected = false;
        // CONFIG 특성 참조도 초기화 (연결 끊기면 쓸 수 없으므로)
        pConfigChar = nullptr;
        // LED 끄기 (active-low이므로 HIGH = LED OFF)
        digitalWrite(LED_PIN, HIGH);
        // 끊긴 이유 출력
        Serial.printf("disconnected (reason=%d) — restarting scan\n", reason);
        // 다시 스캔 시작 — 센서노드가 재부팅되면 다시 찾아서 연결하기 위해
        // 0 = 시간 제한 없이 계속 스캔, false = 중복 결과도 보고
        NimBLEDevice::getScan()->start(0, false);
    }
};

// ──────────── 연결·명령 ────────────

// 스캔에서 발견한 센서노드(targetAddr)에 실제로 BLE 연결을 수행하는 함수
// 성공하면 true, 실패하면 false 반환
static bool connectToServer() {
    // BLE 클라이언트 객체 생성 — 이걸로 서버(센서노드)에 연결함
    pClient = NimBLEDevice::createClient();
    // 연결/해제 콜백 등록 — 상태 변화 시 ClientCB 클래스의 함수가 호출됨
    // new ClientCB() = ClientCB 객체를 힙 메모리에 동적 생성
    pClient->setClientCallbacks(new ClientCB());

    // 아까 스캔에서 저장한 주소로 연결 시도. 실패하면 false 반환
    if (!pClient->connect(targetAddr)) {
        Serial.println("connect failed");
        return false;
    }

    // 연결 성공 → 센서노드의 TEMPIO 서비스를 가져옴
    // BLE 서비스 = 관련 기능을 묶어놓은 컨테이너 (특성들의 그룹)
    auto* pService = pClient->getService(TEMPIO_SERVICE_UUID);
    // 서비스를 못 찾으면 (UUID가 다르거나 서비스가 없으면) 연결 해제
    if (!pService) {
        Serial.println("service not found");
        pClient->disconnect();
        return false;
    }

    // 서비스 안에서 DATA 특성을 가져옴 — 센서값을 받는 채널
    auto* pDataChar = pService->getCharacteristic(TEMPIO_CHAR_DATA_UUID);
    // DATA 특성이 존재하고, notify 기능을 지원하면
    if (pDataChar && pDataChar->canNotify()) {
        // notify 구독 등록. 센서노드가 값을 보낼 때마다 onDataNotify 함수가 호출됨
        // true = notify 활성화, onDataNotify = 데이터 수신 시 호출할 콜백 함수
        pDataChar->subscribe(true, onDataNotify);
        Serial.println("subscribed to DATA");
    }

    // CONFIG 특성 참조를 저장 — 나중에 센서노드에 명령을 보낼 때 사용
    pConfigChar = pService->getCharacteristic(TEMPIO_CHAR_CONFIG_UUID);

    // 연결 상태를 true로 변경
    connected = true;
    // LED 켜기 (active-low이므로 LOW = LED ON) — 연결됨을 시각적으로 표시
    digitalWrite(LED_PIN, LOW);
    return true;
}

// 센서노드에 설정 명령을 보내는 함수
// data = 보낼 데이터(구조체)의 메모리 주소, len = 바이트 수
static void sendCommand(const void* data, size_t len) {
    // 연결 안 됐거나 CONFIG 특성이 없으면 아무것도 안 함
    if (!connected || !pConfigChar) return;
    // CONFIG 특성에 바이트 배열을 씀 → 센서노드의 ConfigCB::onWrite()가 호출됨
    // reinterpret_cast = void* 포인터를 uint8_t* (바이트 배열)로 변환
    pConfigChar->writeValue(reinterpret_cast<const uint8_t*>(data), len);
}

// ──────────── 공개 API ────────────

// BLE Central 초기화 — setup()에서 한 번만 호출
void ble_central_init() {
    // LED 핀을 출력 모드로 설정 (전류를 보내서 LED를 제어하겠다)
    // OUTPUT = 출력 모드, INPUT = 입력 모드 (버튼 읽기 등)
    pinMode(LED_PIN, OUTPUT);
    // LED 끄기 (active-low: HIGH = OFF). 연결 전이니까 꺼둠
    digitalWrite(LED_PIN, HIGH);

    // NimBLE 라이브러리 초기화 + 이 기기의 BLE 이름을 "tempio-hub"로 설정
    // 다른 기기가 스캔할 때 이 이름이 보임
    NimBLEDevice::init("tempio-hub");
    // MTU(Maximum Transmission Unit) 설정 = 한 번에 보낼 수 있는 최대 바이트 수
    // BLE 기본값은 23바이트인데, ESP32끼리는 512까지 협상 가능
    // IR 타이밍 데이터가 수백 바이트라서 크게 설정
    NimBLEDevice::setMTU(512);

    // BLE 스캔 객체를 가져옴
    auto* pScan = NimBLEDevice::getScan();
    // 스캔 콜백 등록 — BLE 기기가 발견될 때마다 ScanCB::onResult() 호출
    pScan->setScanCallbacks(new ScanCB());
    // 액티브 스캔 활성화 — 기기 발견 시 추가 정보(서비스 UUID 등)를 요청함
    // false(패시브 스캔)면 광고 패킷만 받아서 UUID를 못 볼 수 있음
    pScan->setActiveScan(true);
    // 스캔 간격 = 100 * 0.625ms = 62.5ms마다 스캔 윈도우 시작
    pScan->setInterval(100);
    // 스캔 윈도우 = 99 * 0.625ms = 61.875ms 동안 실제로 라디오 수신
    // 간격 ≈ 윈도우이므로 거의 쉬지 않고 계속 스캔 (전력 많이 씀, 허브는 USB 전원이라 괜찮)
    pScan->setWindow(99);
    // 스캔 시작. 0 = 시간 제한 없이 계속, false = 중복 기기도 보고
    pScan->start(0, false);
}

// BLE Central 매 프레임 처리 — loop()에서 반복 호출
void ble_central_loop() {
    // doConnect = true면 스캔에서 센서노드를 발견한 상태 → 연결 시도
    if (doConnect) {
        // 플래그 리셋 (한 번만 시도하도록)
        doConnect = false;
        // 센서노드에 연결 시도
        if (connectToServer()) {
            // 연결 성공 후 1초 대기 — 센서노드가 서비스 디스커버리를 완료할 시간
            delay(1000);
            // ASSIGN_ID 명령 구조체 생성 — 센서노드에 ID 1번을 부여
            AssignId cmd;
            cmd.node_id = 1; // TODO: 동적 ID 부여
            // 명령을 CONFIG 특성으로 전송
            sendCommand(&cmd, sizeof(cmd));
            Serial.println("<< ASSIGN_ID: 1");
        } else {
            // 연결 실패 → 다시 스캔 시작해서 재시도
            NimBLEDevice::getScan()->start(0, false);
        }
    }

    // 연결 안 된 상태에서 5초마다 "scanning..." 출력 (살아있다는 표시)
    // millis() = 보드 부팅 후 경과한 밀리초. unsigned long으로 약 50일까지 셈
    if (!connected && millis() - lastPrint > 5000) {
        lastPrint = millis();
        Serial.println("scanning...");
    }
}

// 현재 BLE 연결 상태 반환
bool ble_is_connected() {
    return connected;
}
