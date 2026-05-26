# 개발 노트

실기에서 터진 것들 정리. 다음에 또 터질 때 여기 먼저 확인.

---

## NimBLE 2.5.0 지뢰밭

### 1. deleteClient — 콜백 안에서 부르면 heap 크래시

- **증상**: `onDisconnect` 콜백 안에서 `NimBLEDevice::deleteClient(client)` 호출 → heap 크래시 (재부팅 루프)
- **원인**: NimBLE 콜백은 BLE 태스크 컨텍스트에서 실행됨. 그 안에서 클라이언트 메모리를 해제하면 아직 참조 중인 포인터를 밟음
- **해결**: `cleanupDisconnected()`처럼 **메인 루프(`loop()`)에서** deleteClient 호출. 콜백에서는 `used = false` 플래그만 세팅

### 2. 클라이언트 풀 누수 — createClient가 nullptr 반환

- **증상**: 한참 잘 되다가 갑자기 `createClient failed` 무한 반복. active=0인데도 생성 불가
- **원인**: disconnect 후 클라이언트 포인터만 nullptr로 밀고 `deleteClient()`를 안 부르면, NimBLE 내부 리스트에 죽은 클라이언트가 쌓임. MAX_CONNECTIONS(5)에 도달하면 끝
- **해결**: `cleanupDisconnected()`에서 `NimBLEDevice::deleteClient(client)` 호출 후 nullptr

### 3. NimBLEAddress 생성자 — v2.5.0에서 변경됨

- **증상**: `NimBLEAddress(string)` 컴파일 에러
- **원인**: v1.x는 `NimBLEAddress(std::string)`, v2.5.0은 `NimBLEAddress(std::string, uint8_t type)` — type 파라미터 필수
- **해결**: `NimBLEAddress(addrStr, 0)` 으로 호출 (0 = public address)

### 4. toString() 댕글링 포인터

- **증상**: 시리얼 출력에 깨진 문자열, 간헐적 크래시
- **원인**: `addr.toString().c_str()` → toString()이 임시 std::string 반환 → 그 줄(;) 끝에서 소멸 → c_str()이 시체 주소를 가리킴
- **해결**: string 자체를 변수에 담아야 포인터가 유효
```cpp
// ❌ 댕글링
const char* s = addr.toString().c_str();
// ✅ 안전
std::string s = addr.toString();
const char* p = s.c_str();
```

### 5. getClientListSize — v2.5.0에 없음

- **증상**: `getClientListSize is not a member of NimBLEDevice` 컴파일 에러
- **원인**: v2.5.0에서 API 이름 변경/제거
- **해결**: 직접 만든 `activeCount()` 같은 헬퍼로 대체

---

## ESP32 칩별 GPIO 주의

| 칩 | 플래시 전용 (건들면 크래시) | 내장 LED | LED 극성 |
|----|--------------------------|---------|---------|
| ESP32 오리지널 | GPIO 6~11 | GPIO2 | active-high |
| ESP32-C3 | GPIO 12~17 | GPIO8 | active-low |
| ESP32-S3 | GPIO 26~32 | 보드마다 다름 | 보드마다 다름 |

보드 바꿀 때 LED 핀 + 금지 GPIO 반드시 확인.

---

## BLE 디버깅 팁

- `createClient` 직전에 `activeCount()`, `CONFIG_BT_NIMBLE_MAX_CONNECTIONS`, `CONFIG_BT_NIMBLE_ROLE_CENTRAL` 출력해두면 원인 파악이 빠름
- disconnect reason: `reason - 512` = BLE HCI 표준 에러코드 (19=상대가 끊음, 8=타임아웃, 9=감시 타임아웃)
