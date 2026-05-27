# HTTP API

Base URL: `https://api.yuumi.wiki`

## 인증

API 키 방식 (`X-API-Key` 헤더). 서버는 `hmac.compare_digest`로 검증.

- 환경변수 `TEMPIO_API_KEY`로 키 설정
- `TEMPIO_API_KEY` 미설정 시 인증 비활성화 (개발 모드)
- 허브 펌웨어에는 NVS 또는 하드코딩으로 저장

## 명령 전송

### `POST /api/hub/{hub_id}/command`

서버가 허브에 즉시 명령을 전달. HTTP로 요청을 받아 MQTT 토픽 `tempio/{hub_id}/commands`로 퍼블리시.

**Path Parameters**

| 이름 | 타입 | 설명 |
|------|------|------|
| `hub_id` | string | 허브 식별자 (WiFi MAC, 콜론 제거 소문자) |

**Headers**

| 이름 | 필수 | 설명 |
|------|------|------|
| `X-API-Key` | Y* | API 키 (`TEMPIO_API_KEY` 미설정 시 비활성화) |

**Request Body**

```json
{
  "target": "aa:bb:cc:dd:ee:02",
  "type": "SET_INTERVAL",
  "payload": { "interval_sec": 1800 }
}
```

명령 타입별 상세는 [command-protocol.md](command-protocol.md) 참조.

**Response `200 OK`**

```json
{
  "status": "ok",
  "detail": "Command published to tempio/04b24793c230/commands"
}
```

## 조회 (대시보드·디버그용)

### `GET /`

실시간 대시보드 (HTML, Jinja2). 5초 자동 새로고침, Chart.js 온습도 추이 차트.

### `GET /api/hub-history`

허브 리포트 최근 100건.

**Response**: `HubReport[]` (timestamp 포함)

### `GET /api/sensor-history`

센서노드 측정값 최근 100건.

**Response**: `SensorReading[]` (hub_id, timestamp 포함)

## 예정 기능

| Phase | 내용 | 비고 |
|-------|------|------|
| 6 | CO2 데이터 | 기존 report payload에 포함 (필드 이미 존재) |
| 후기 | `GET /api/stores` | 매장 목록 조회 |
| 후기 | `PUT /api/stores/{store_id}` | 매장 설정 (영업시간 등) |
| 후기 | `POST /api/hub/{hub_id}/ota` | OTA 펌웨어 업데이트 트리거 |
