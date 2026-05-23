// Arduino 기본 함수들 (Serial, delay, pinMode, digitalWrite 등)
#include <Arduino.h>
// NimBLE 라이브러리 — ESP32용 BLE(저전력 블루투스) 통신 라이브러리.
// BLE Peripheral(서버) 역할: 자기를 광고하고, 연결 요청을 받아들이는 쪽
#include <NimBLEDevice.h>
// 우리가 정의한 BLE 프로토콜 — UUID, 메시지 타입, 구조체 등
#include "protocol.h"
// 이 파일의 함수 선언 헤더
#include "ble_peripheral.h"

// ESP32-C3 SuperMini 내장 LED 핀 번호. GPIO 8번에 연결돼 있음
static constexpr uint8_t LED_PIN = 8;

// DATA 특성 — 센서값(SensorData)이나 자기소개(NodeInfo)를 허브에 보내는 채널
// 포인터가 nullptr이면 아직 생성 안 됨
static NimBLECharacteristic* pDataChar = nullptr;
// CONFIG 특성 — 허브가 보내는 설정 명령(ASSIGN_ID 등)을 받는 채널
static NimBLECharacteristic* pConfigChar = nullptr;
// 허브와 현재 BLE 연결되어 있는지 여부
static bool deviceConnected = false;
// 연결 직후 NodeInfo를 한 번 보내야 하는지 여부 (플래그)
static bool sendNodeInfo = false;
// 마지막으로 "advertising..." 메시지를 출력한 시각
static unsigned long lastPrint = 0;

// ──────────── 콜백 ────────────

// BLE 서버 연결/해제 콜백 — 허브가 연결하거나 끊을 때 자동 호출
// NimBLEServerCallbacks를 상속받아 onConnect/onDisconnect를 재정의
class ServerCB : public NimBLEServerCallbacks {
    // 허브가 이 센서노드에 연결했을 때 호출
    // s = BLE 서버 객체, info = 연결 정보 (상대방 주소 등)
    void onConnect(NimBLEServer* s, NimBLEConnInfo& info) override {
        // 연결 상태를 true로 변경
        deviceConnected = true;
        // NodeInfo를 보내야 한다는 플래그 켜기
        sendNodeInfo = true;
        // LED 켜기 (active-low: LOW = ON) — 연결됨을 시각적으로 표시
        digitalWrite(LED_PIN, LOW);
        // 연결된 허브의 BLE 주소를 시리얼 모니터에 출력
        Serial.printf("connected: %s\n", info.getAddress().toString().c_str());
    }

    // 허브와 연결이 끊겼을 때 호출 (거리 벗어남, 허브 전원 꺼짐 등)
    // reason = 끊긴 이유 코드 (BLE 표준 정의)
    void onDisconnect(NimBLEServer* s, NimBLEConnInfo& info, int reason) override {
        // 연결 상태를 false로 변경
        deviceConnected = false;
        // LED 끄기 (active-low: HIGH = OFF)
        digitalWrite(LED_PIN, HIGH);
        // 끊긴 이유 출력
        Serial.printf("disconnected (reason=%d)\n", reason);
        // 다시 광고 시작 — 허브가 재연결할 수 있도록
        // Peripheral은 광고를 해야 Central(허브)이 찾을 수 있음
        NimBLEDevice::startAdvertising();
    }
};

// 허브가 CONFIG 특성에 데이터를 쓰면(명령을 보내면) 이 콜백이 호출됨
// NimBLECharacteristicCallbacks를 상속받아 onWrite를 재정의
class ConfigCB : public NimBLECharacteristicCallbacks {
    // c = 쓰기가 발생한 특성, info = 쓴 쪽(허브)의 연결 정보
    void onWrite(NimBLECharacteristic* c, NimBLEConnInfo& info) override {
        // 허브가 보낸 데이터를 가져옴. val = 바이트 배열 (std::string 형태)
        auto val = c->getValue();
        // 데이터가 비어있으면 무시
        if (val.length() < 1) return;

        // 첫 바이트를 MsgType으로 변환 — 어떤 명령인지 판별
        auto type = static_cast<MsgType>(val[0]);
        // 명령 타입에 따라 다른 처리
        switch (type) {
            // 허브가 이 노드에 ID를 부여하는 명령
            case MsgType::ASSIGN_ID: {
                // 데이터 크기가 AssignId 구조체 이상인지 확인
                if (val.length() >= sizeof(AssignId)) {
                    // 바이트 데이터를 AssignId 구조체로 재해석
                    // const = 읽기 전용 (원본 데이터를 수정하지 않겠다)
                    auto* cmd = reinterpret_cast<const AssignId*>(val.data());
                    // 부여받은 ID를 시리얼에 출력
                    Serial.printf(">> assigned id: %u\n", cmd->node_id);
                    // TODO: NVS(비휘발성 저장소)에 ID 저장 → 재부팅 후에도 유지
                }
                break;
            }
            // 허브가 센서 측정 주기를 변경하는 명령
            case MsgType::SET_INTERVAL: {
                if (val.length() >= sizeof(SetInterval)) {
                    auto* cmd = reinterpret_cast<const SetInterval*>(val.data());
                    Serial.printf(">> interval: %u sec\n", cmd->interval_sec);
                    // TODO: 딥슬립 주기를 이 값으로 변경
                }
                break;
            }
            // 허브가 이 노드를 리셋하라는 명령
            case MsgType::RESET_NODE: {
                if (val.length() >= sizeof(ResetNode)) {
                    auto* cmd = reinterpret_cast<const ResetNode*>(val.data());
                    // level: 0=재부팅, 1=페어링 삭제, 2=공장초기화
                    Serial.printf(">> reset level: %u\n", cmd->level);
                    // TODO: 레벨에 따라 NVS 삭제 후 재부팅 등
                }
                break;
            }
            // 알 수 없는 명령
            default:
                Serial.printf(">> unknown config: 0x%02x\n", val[0]);
        }
    }
};

// ──────────── mock 센서 데이터 ────────────
// TODO: Phase 2에서 DHT22 실제 읽기로 교체

// 가짜 센서 데이터를 생성해서 허브에 notify로 전송하는 함수
static void sendSensorData() {
    // SensorData 구조체 생성 — type 필드는 기본값 SENSOR_DATA(0x01)가 자동 들어감
    SensorData data;
    // 가짜 온도: 24도 기준 ±3도 랜덤
    // random(-30, 30) = -30~29 사이 정수 반환, /10.0f로 소수점 만듦
    data.temp = 24.0f + random(-30, 30) / 10.0f;
    // 가짜 습도: 55% 기준 ±5% 랜덤
    data.humidity = 55.0f + random(-50, 50) / 10.0f;
    // 가짜 조도: 100~2999 사이 랜덤 (ADC raw 값, 0~4095 범위 중 일부)
    data.ldr = random(100, 3000);
    // 가짜 배터리 전압: 2900~3199mV 사이 랜덤 (AA 건전지 2개 ≈ 3.0V)
    data.battery_mv = 2900 + random(0, 300);

    // 구조체의 메모리를 바이트 배열로 재해석해서 DATA 특성에 설정
    // sizeof(data) = SensorData 구조체 크기 (13바이트)
    pDataChar->setValue(reinterpret_cast<uint8_t*>(&data), sizeof(data));
    // notify 전송 — 구독 중인 허브에 데이터가 자동으로 전달됨
    // notify = BLE에서 서버가 클라이언트에게 "새 데이터 있다"고 알리는 방식
    pDataChar->notify();

    // 시리얼 모니터에 보낸 데이터 출력 (<< = 이 노드가 보내는 데이터라는 표시)
    Serial.printf("<< %.1f C  %.1f %%  ldr=%u  bat=%umV\n",
                  data.temp, data.humidity, data.ldr, data.battery_mv);
}

// ──────────── 공개 API ────────────

// BLE Peripheral 초기화 — setup()에서 한 번만 호출
void ble_peripheral_init() {
    // LED 핀을 출력 모드로 설정
    pinMode(LED_PIN, OUTPUT);
    // LED 끄기 (active-low: HIGH = OFF). 연결 전이니까 꺼둠
    digitalWrite(LED_PIN, HIGH);

    // NimBLE 라이브러리 초기화 + 이 기기의 BLE 이름을 "tempio-sensor"로 설정
    // 허브가 스캔할 때 이 이름이 보임
    NimBLEDevice::init("tempio-sensor");
    // MTU 512바이트로 설정 (허브와 동일하게 맞춤)
    NimBLEDevice::setMTU(512);

    // BLE 서버 생성 — Peripheral은 "서버" 역할 (데이터를 제공하는 쪽)
    // Central(허브)이 "클라이언트" (데이터를 요청/수신하는 쪽)
    auto* pServer = NimBLEDevice::createServer();
    // 서버 콜백 등록 — 연결/해제 시 ServerCB 클래스의 함수가 호출됨
    pServer->setCallbacks(new ServerCB());

    // BLE 서비스 생성 — TEMPIO_SERVICE_UUID로 식별되는 서비스
    // 서비스 = 관련 특성(Characteristic)들을 묶어놓은 그룹
    auto* pService = pServer->createService(TEMPIO_SERVICE_UUID);

    // DATA 특성 생성 — 센서값을 허브에 보내는 채널
    // NOTIFY = 서버가 값 변경 시 클라이언트에 자동 알림
    // READ = 클라이언트가 직접 값을 읽을 수도 있음
    // | = 비트 OR 연산자. 두 속성을 동시에 설정
    pDataChar = pService->createCharacteristic(
        TEMPIO_CHAR_DATA_UUID,
        NIMBLE_PROPERTY::NOTIFY | NIMBLE_PROPERTY::READ
    );

    // CONFIG 특성 생성 — 허브가 명령을 보내는 채널
    // WRITE = 클라이언트(허브)가 이 특성에 데이터를 쓸 수 있음
    pConfigChar = pService->createCharacteristic(
        TEMPIO_CHAR_CONFIG_UUID,
        NIMBLE_PROPERTY::WRITE
    );
    // CONFIG 특성에 콜백 등록 — 허브가 데이터를 쓰면 ConfigCB::onWrite() 호출
    pConfigChar->setCallbacks(new ConfigCB());

    // BLE 서버 시작 — 이 호출이 없으면 서비스가 활성화되지 않아서
    // 허브가 스캔해도 이 노드의 서비스 UUID를 못 찾음
    pServer->start();

    // BLE 광고(Advertising) 설정 — "나 여기 있어요" 신호를 주기적으로 브로드캐스트
    auto* pAdv = NimBLEDevice::getAdvertising();
    // 광고 패킷에 서비스 UUID를 포함시킴 → 허브가 이 UUID로 우리 기기를 식별
    pAdv->addServiceUUID(TEMPIO_SERVICE_UUID);
    // 광고 시작 — 이제부터 허브가 스캔하면 이 센서노드가 발견됨
    pAdv->start();
}

// BLE Peripheral 매 프레임 처리 — loop()에서 반복 호출
void ble_peripheral_loop() {
    // 허브와 연결 안 된 상태일 때
    if (!deviceConnected) {
        // 5초마다 "advertising..." 출력 (살아있다는 표시)
        if (millis() - lastPrint > 5000) {
            lastPrint = millis();
            Serial.println("advertising...");
        }
        // 500ms 쉬고 함수 종료 (return). 연결 안 됐으니 센서값 보낼 필요 없음
        delay(500);
        return;
    }

    // --- 여기부터는 허브와 연결된 상태 ---

    // 연결 직후 한 번만 NodeInfo(자기소개)를 보냄
    if (sendNodeInfo) {
        // 플래그 끄기 (한 번만 보내도록)
        sendNodeInfo = false;
        // NodeInfo 구조체 생성 — type 필드는 기본값 NODE_INFO(0x20)가 자동 들어감
        NodeInfo info;
        // 이 노드는 센서노드임을 표시
        info.node_type = NodeType::SENSOR;
        // 아직 ID를 못 받은 상태이므로 0 (허브가 ASSIGN_ID로 부여해줌)
        // TODO: NVS에서 저장된 ID 읽기
        info.node_id = 0;
        // 배터리 전압 (임시 고정값)
        info.battery_mv = 3100;
        // 펌웨어 버전 0.1
        info.fw_major = 0;
        info.fw_minor = 1;
        // NodeInfo 구조체를 바이트로 변환해서 DATA 특성에 설정
        pDataChar->setValue(reinterpret_cast<uint8_t*>(&info), sizeof(info));
        // notify로 허브에 전송
        pDataChar->notify();
        Serial.println("<< NodeInfo sent");
        // NodeInfo 전송 후 잠깐 대기 (허브가 처리할 시간)
        delay(500);
    }

    // 센서 데이터(현재는 가짜)를 허브에 전송
    sendSensorData();
    // 3초 대기 후 다시 loop() 진입 → 3초마다 센서값 전송
    delay(3000);
}

// 현재 BLE 연결 상태 반환
bool ble_is_connected() {
    return deviceConnected;
}
