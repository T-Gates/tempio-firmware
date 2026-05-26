import hmac

from fastapi import Request, Header, HTTPException

from domain.services import SensorService


def get_service(request: Request) -> SensorService:
    return request.app.state.service


def get_api_key_verifier(request: Request):
    api_key = request.app.state.api_key

    def verify(x_api_key: str = Header(None)):
        if api_key:
            if not x_api_key or not hmac.compare_digest(x_api_key, api_key):
                raise HTTPException(status_code=401, detail="Invalid API key")

    return verify
