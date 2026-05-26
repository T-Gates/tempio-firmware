from pathlib import Path

from fastapi import APIRouter, Depends, Request
from fastapi.responses import HTMLResponse
from fastapi.templating import Jinja2Templates
from domain.models import Command
from domain.services import SensorService
from dependencies import get_service, get_api_key_verifier

templates = Jinja2Templates(
    directory=str(Path(__file__).resolve().parent.parent.parent / "templates")
)

router = APIRouter()


@router.get("/", response_class=HTMLResponse)
def dashboard(request: Request, service: SensorService = Depends(get_service)):
    hub = service.get_latest_hub()
    sensors = service.get_latest_sensors()
    history = service.get_sensor_history(30)
    return templates.TemplateResponse(
        request,
        "dashboard.html",
        context={
            "hub": hub,
            "sensors": sensors,
            "sensor_history": [r.model_dump() for r in history],
        },
    )


@router.get("/api/hub-history")
def hub_history(service: SensorService = Depends(get_service)):
    return [r.model_dump() for r in service.get_hub_history()]


@router.get("/api/sensor-history")
def sensor_history(service: SensorService = Depends(get_service)):
    return [r.model_dump() for r in service.get_sensor_history()]


@router.post("/api/hub/{hub_id}/command", dependencies=[Depends(get_api_key_verifier)])
async def send_command(
    hub_id: str,
    commands: list[Command],
    service: SensorService = Depends(get_service),
):
    await service.send_command(hub_id, commands)
    return {"status": "ok"}
