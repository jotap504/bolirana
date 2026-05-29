import json
from fastapi import APIRouter, Request, HTTPException
from ..config import get_config, update_config

router = APIRouter(prefix="/api/admin")


@router.get("/config")
async def read_config():
    return get_config()


@router.patch("/config")
async def patch_config(request: Request):
    body = await request.json()
    update_config(body)
    return get_config()


@router.get("/config/sensors")
async def get_sensors():
    return get_config()["sensors"]


@router.patch("/config/sensors")
async def patch_sensors(request: Request):
    sensors = await request.json()
    update_config({"sensors": sensors})
    return get_config()["sensors"]


@router.get("/config/pricing")
async def get_pricing():
    return get_config()["pricing"]


@router.patch("/config/pricing")
async def patch_pricing(request: Request):
    body = await request.json()
    update_config({"pricing": body})
    return get_config()["pricing"]


@router.get("/config/game")
async def get_game_cfg():
    return get_config()["game"]


@router.patch("/config/game")
async def patch_game_cfg(request: Request):
    body = await request.json()
    update_config({"game": body})
    return get_config()["game"]


@router.get("/stats")
async def get_stats(request: Request):
    session = request.app.state.engine.session
    return {
        "state":         session.state,
        "players":       [p.to_dict() for p in session.players],
        "display_on":    request.app.state.ws.is_display_connected(),
        "phones_connected": request.app.state.ws.player_count(),
    }


@router.post("/hardware/command")
async def send_hardware_command(request: Request):
    body = await request.json()
    try:
        await request.app.state.bridge.send(body)
        return {"status": "ok", "command": body}
    except Exception as e:
        raise HTTPException(status_code=500, detail=str(e))
