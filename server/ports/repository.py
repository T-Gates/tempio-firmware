from abc import ABC, abstractmethod
from datetime import datetime
from typing import Optional

from domain.models import HubReport, SensorReading, SensorReadingRecord


class SensorRepository(ABC):
    @abstractmethod
    def save_hub_report(self, report: HubReport) -> None: ...

    @abstractmethod
    def save_sensor_reading(
        self, hub_id: str, reading: SensorReading, timestamp: Optional[datetime]
    ) -> None: ...

    @abstractmethod
    def get_hub_reports(self, limit: int = 100) -> list[HubReport]: ...

    @abstractmethod
    def get_sensor_readings(self, limit: int = 100) -> list[SensorReadingRecord]: ...

    @abstractmethod
    def get_latest_hub_report(self) -> Optional[HubReport]: ...

    @abstractmethod
    def get_latest_sensor_by_node(self) -> dict[str, SensorReadingRecord]: ...
