from pydantic import BaseModel
from datetime import datetime
from typing import Optional


class DeviceInfo(BaseModel):
    node_id: str
    node_type: str  # "sensor" | "ir"
    battery_voltage: Optional[float] = None
    rssi: Optional[int] = None


class SensorReading(BaseModel):
    node_id: str
    temperature: float
    humidity: float
    lux: Optional[float] = None


class SensorReadingRecord(BaseModel):
    id: int
    hub_id: str
    node_id: str
    temperature: float
    humidity: float
    lux: Optional[float] = None
    timestamp: str


class HubReport(BaseModel):
    hub_id: str
    wifi_rssi: Optional[int] = None
    free_heap: Optional[int] = None
    uptime_ms: Optional[int] = None
    co2: Optional[int] = None
    hub_temperature: Optional[float] = None
    hub_humidity: Optional[float] = None
    connected_devices: list[DeviceInfo] = []
    sensor_readings: list[SensorReading] = []
    timestamp: Optional[datetime] = None

    @classmethod
    def from_mqtt_payload(cls, hub_id: str, data: dict) -> "HubReport":
        return cls(
            hub_id=hub_id,
            timestamp=datetime.now(),
            **{k: v for k, v in data.items() if k in cls.model_fields and k not in ("hub_id", "timestamp")},
        )


class Command(BaseModel):
    target: str       # 노드 MAC 주소
    type: str         # "SET_INTERVAL", "IR_TIMING", "RESET_NODE"
    payload: dict     # {"interval_sec": 1800} 등


class CommandEnvelope(BaseModel):
    hub_id: str
    commands: list[Command]
