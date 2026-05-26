import asyncio
import json
import logging
from typing import Callable

import aiomqtt

from constants import MQTT_TOPIC_REPORT_PATTERN
from domain.models import HubReport

logger = logging.getLogger(__name__)


class MqttSubscriber:

    def __init__(
        self, broker_host: str = "localhost", broker_port: int = 1883
    ) -> None:
        self.broker_host = broker_host
        self.broker_port = broker_port
        self._callback: Callable[[HubReport], None] | None = None

    def on_report(self, callback: Callable[[HubReport], None]) -> None:
        self._callback = callback

    async def run(self) -> None:
        while True:
            try:
                async with aiomqtt.Client(
                    self.broker_host, self.broker_port
                ) as client:
                    await client.subscribe(MQTT_TOPIC_REPORT_PATTERN)
                    logger.info("mqtt connected, subscribed to %s", MQTT_TOPIC_REPORT_PATTERN)
                    async for message in client.messages:
                        try:
                            parts = str(message.topic).split("/")
                            if len(parts) != 3:
                                continue
                            hub_id = parts[1]

                            data: dict = json.loads(
                                message.payload.decode()
                            )
                            report = HubReport.from_mqtt_payload(hub_id, data)

                            if self._callback:
                                await asyncio.to_thread(
                                    self._callback, report
                                )
                        except Exception as e:
                            logger.error("mqtt parse error: %s", e)
            except aiomqtt.MqttError as e:
                logger.warning("mqtt connection lost: %s, reconnecting in 5s...", e)
                await asyncio.sleep(5)
