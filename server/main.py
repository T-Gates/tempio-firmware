import asyncio
from contextlib import asynccontextmanager

from fastapi import FastAPI

from config import Settings
from domain.services import SensorService
from adapters.outbound.sqlite_repo import SqliteRepository
from adapters.outbound.mqtt_publisher import MqttPublisher
from adapters.inbound.mqtt_subscriber import MqttSubscriber
from adapters.inbound.http_routes import create_router

settings = Settings()

repo = SqliteRepository(settings.db_path)
publisher = MqttPublisher(settings.mqtt_broker_host, settings.mqtt_broker_port)
subscriber = MqttSubscriber(settings.mqtt_broker_host, settings.mqtt_broker_port)

service = SensorService(repo=repo, publisher=publisher)

subscriber.on_report(service.process_report)


@asynccontextmanager
async def lifespan(app: FastAPI):
    for attempt in range(10):
        try:
            await publisher.connect()
            break
        except Exception as e:
            print(f"MQTT connect attempt {attempt + 1}/10 failed: {e}")
            await asyncio.sleep(2)
    else:
        raise RuntimeError("Failed to connect to MQTT broker after 10 attempts")
    sub_task = asyncio.create_task(subscriber.run())
    print(f"MQTT connected: {settings.mqtt_broker_host}:{settings.mqtt_broker_port}")
    yield
    sub_task.cancel()
    try:
        await sub_task
    except asyncio.CancelledError:
        pass
    await publisher.disconnect()


app = FastAPI(title="서늘 API", lifespan=lifespan)
app.include_router(create_router(service, settings.api_key or None))
