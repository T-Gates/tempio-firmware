import sqlite3
import json
from contextlib import contextmanager
from datetime import datetime
from typing import Optional

from domain.models import DeviceInfo, HubReport, SensorReading, SensorReadingRecord
from ports.repository import SensorRepository


class SqliteRepository(SensorRepository):
    def __init__(self, db_path: str = "tempio.db"):
        self.db_path = db_path
        self._init_tables()

    @contextmanager
    def _conn(self):
        conn = sqlite3.connect(self.db_path, timeout=10)
        conn.row_factory = sqlite3.Row
        try:
            yield conn
            conn.commit()
        except Exception:
            conn.rollback()
            raise
        finally:
            conn.close()

    def _init_tables(self):
        with self._conn() as conn:
            conn.execute("PRAGMA journal_mode=WAL")
            conn.execute("""
                CREATE TABLE IF NOT EXISTS hub_reports (
                    id INTEGER PRIMARY KEY AUTOINCREMENT,
                    hub_id TEXT NOT NULL,
                    wifi_rssi INTEGER,
                    free_heap INTEGER,
                    uptime_ms INTEGER,
                    co2 INTEGER,
                    hub_temperature REAL,
                    hub_humidity REAL,
                    connected_devices TEXT,  -- JSON
                    timestamp TEXT NOT NULL
                )
            """)
            conn.execute("""
                CREATE TABLE IF NOT EXISTS sensor_readings (
                    id INTEGER PRIMARY KEY AUTOINCREMENT,
                    hub_id TEXT NOT NULL,
                    node_id TEXT NOT NULL,
                    temperature REAL NOT NULL,
                    humidity REAL NOT NULL,
                    lux REAL,
                    timestamp TEXT NOT NULL
                )
            """)
            conn.execute("CREATE INDEX IF NOT EXISTS idx_sensor_readings_node_id ON sensor_readings(node_id, id)")
            conn.execute("CREATE INDEX IF NOT EXISTS idx_sensor_readings_hub_id ON sensor_readings(hub_id)")
            conn.execute("CREATE INDEX IF NOT EXISTS idx_hub_reports_hub_id ON hub_reports(hub_id)")

    def save_hub_report(self, report: HubReport) -> None:
        ts = report.timestamp.isoformat() if report.timestamp else datetime.now().isoformat()
        devices_json = json.dumps([d.model_dump() for d in report.connected_devices])
        with self._conn() as conn:
            conn.execute(
                "INSERT INTO hub_reports (hub_id, wifi_rssi, free_heap, uptime_ms, co2, hub_temperature, hub_humidity, connected_devices, timestamp) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?)",
                (report.hub_id, report.wifi_rssi, report.free_heap, report.uptime_ms,
                 report.co2, report.hub_temperature, report.hub_humidity, devices_json, ts),
            )

    def save_sensor_reading(self, hub_id: str, reading: SensorReading, timestamp: Optional[datetime]) -> None:
        ts = timestamp.isoformat() if timestamp else datetime.now().isoformat()
        with self._conn() as conn:
            conn.execute(
                "INSERT INTO sensor_readings (hub_id, node_id, temperature, humidity, lux, timestamp) VALUES (?, ?, ?, ?, ?, ?)",
                (hub_id, reading.node_id, reading.temperature, reading.humidity, reading.lux, ts),
            )

    def _row_to_hub_report(self, row: sqlite3.Row) -> HubReport:
        d = dict(row)
        devices_raw = json.loads(d["connected_devices"]) if d["connected_devices"] else []
        return HubReport(
            hub_id=d["hub_id"],
            wifi_rssi=d["wifi_rssi"],
            free_heap=d["free_heap"],
            uptime_ms=d["uptime_ms"],
            co2=d["co2"],
            hub_temperature=d["hub_temperature"],
            hub_humidity=d["hub_humidity"],
            connected_devices=[DeviceInfo(**dev) for dev in devices_raw],
            timestamp=d["timestamp"],
        )

    def get_hub_reports(self, limit: int = 100) -> list[HubReport]:
        with self._conn() as conn:
            rows = conn.execute(
                "SELECT * FROM hub_reports ORDER BY id DESC LIMIT ?", (limit,)
            ).fetchall()
        result = [self._row_to_hub_report(row) for row in rows]
        result.reverse()
        return result

    def get_sensor_readings(self, limit: int = 100) -> list[SensorReadingRecord]:
        with self._conn() as conn:
            rows = conn.execute(
                "SELECT * FROM sensor_readings ORDER BY id DESC LIMIT ?", (limit,)
            ).fetchall()
        result = [SensorReadingRecord(**dict(row)) for row in rows]
        result.reverse()
        return result

    def get_latest_hub_report(self) -> Optional[HubReport]:
        with self._conn() as conn:
            row = conn.execute(
                "SELECT * FROM hub_reports ORDER BY id DESC LIMIT 1"
            ).fetchone()
        if not row:
            return None
        return self._row_to_hub_report(row)

    def get_latest_sensor_by_node(self) -> dict[str, SensorReadingRecord]:
        with self._conn() as conn:
            rows = conn.execute("""
                SELECT s.* FROM sensor_readings s
                INNER JOIN (
                    SELECT node_id, MAX(id) as max_id
                    FROM sensor_readings GROUP BY node_id
                ) latest ON s.id = latest.max_id
            """).fetchall()
        return {row["node_id"]: SensorReadingRecord(**dict(row)) for row in rows}
