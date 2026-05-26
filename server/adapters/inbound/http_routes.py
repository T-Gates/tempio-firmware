import hmac
from pathlib import Path

from fastapi import APIRouter, Depends, Request, HTTPException, Header
from fastapi.responses import HTMLResponse
from fastapi.templating import Jinja2Templates
from domain.models import Command
from domain.services import SensorService

templates = Jinja2Templates(
    directory=str(Path(__file__).resolve().parent.parent.parent / "templates")
)


def create_router(service: SensorService, api_key: str = None) -> APIRouter:
    router = APIRouter()

    def verify_api_key(x_api_key: str = Header(None)):
        if api_key:
            if not x_api_key or not hmac.compare_digest(x_api_key, api_key):
                raise HTTPException(status_code=401, detail="Invalid API key")

    @router.get("/", response_class=HTMLResponse)
    def dashboard(request: Request):
        latest_hub = service.get_latest_hub()
        latest_sensors = service.get_latest_sensors()
        sensor_history = service.get_sensor_history(30)
        return templates.TemplateResponse(
            request,
            "dashboard.html",
            context={
                "hub": latest_hub,
                "sensors": latest_sensors,
                "sensor_history": sensor_history,
            },
        )

    @router.get("/api/hub-history")
    def hub_history():
        return service.get_hub_history()

    @router.get("/api/sensor-history")
    def sensor_history():
        return service.get_sensor_history()

    @router.post("/api/hub/{hub_id}/command", dependencies=[Depends(verify_api_key)])
    async def send_command(hub_id: str, commands: list[Command]):
        await service.send_command(hub_id, commands)
        return {"status": "ok"}

    return router
