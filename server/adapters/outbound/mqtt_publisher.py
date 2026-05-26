import json

import aiomqtt

from constants import MQTT_TOPIC_COMMANDS_TEMPLATE
from domain.models import CommandEnvelope
from ports.message_broker import CommandPublisher


class MqttPublisher(CommandPublisher):

    def __init__(
        self, broker_host: str = "localhost", broker_port: int = 1883
    ) -> None:
        self.broker_host = broker_host
        self.broker_port = broker_port
        self._client: aiomqtt.Client | None = None

    async def connect(self) -> None:
        self._client = aiomqtt.Client(self.broker_host, self.broker_port)
        await self._client.__aenter__()

    async def disconnect(self) -> None:
        if self._client:
            await self._client.__aexit__(None, None, None)
            self._client = None

    async def publish_commands(self, envelope: CommandEnvelope) -> None:
        if not self._client:
            raise RuntimeError("MQTT publisher not connected")
        topic = MQTT_TOPIC_COMMANDS_TEMPLATE.format(hub_id=envelope.hub_id)
        payload = json.dumps(
            {"commands": [cmd.model_dump() for cmd in envelope.commands]}
        )
        await self._client.publish(topic, payload)
