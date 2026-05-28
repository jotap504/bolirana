import json, logging
from fastapi import APIRouter, WebSocket, WebSocketDisconnect, Request

log = logging.getLogger(__name__)
router = APIRouter()


# ── WebSocket: display principal ──────────────────────────────────────────────
@router.websocket("/ws/display")
async def ws_display(ws: WebSocket):
    mgr    = ws.app.state.ws
    engine = ws.app.state.engine
    await mgr.connect_display(ws)
    await engine._sync_state()
    try:
        while True:
            raw = await ws.receive_text()
            msg = json.loads(raw)
            if msg.get("type") == "btn":
                await engine.handle_button(msg["id"])
    except WebSocketDisconnect:
        mgr.disconnect_display()


# ── WebSocket: celular jugador ────────────────────────────────────────────────
@router.websocket("/ws/player/{index}")
async def ws_player(ws: WebSocket, index: int):
    engine = ws.app.state.engine
    session_id = ws.query_params.get("session_id", "")
    
    # Validar si el session_id coincide con el de la sesión activa
    if session_id != engine.session.session_id:
        await ws.accept()
        await ws.send_json({"type": "error", "message": "Sesión vencida. Por favor escaneá el nuevo QR en la pantalla."})
        await ws.close()
        return

    mgr    = ws.app.state.ws
    await mgr.connect_player(ws, index)
    if index < len(engine.session.players):
        engine.session.players[index].connected = True
    await engine._sync_state()
    try:
        while True:
            raw = await ws.receive_text()
            msg = json.loads(raw)
            match msg.get("type"):
                case "player_name":
                    await engine.set_player_name(index, msg.get("name", ""), msg.get("google_id"), msg.get("avatar", ""))
                case "btn":
                    await engine.handle_button(msg["id"])
    except WebSocketDisconnect:
        mgr.disconnect_player(index)
        if index < len(engine.session.players):
            engine.session.players[index].connected = False
        await engine._sync_state()


# ── WebSocket: admin ──────────────────────────────────────────────────────────
@router.websocket("/ws/admin")
async def ws_admin(ws: WebSocket):
    mgr = ws.app.state.ws
    await mgr.connect_admin(ws)
    try:
        while True:
            await ws.receive_text()
    except WebSocketDisconnect:
        mgr.disconnect_admin(ws)


# ── REST: simular eventos (testing) ───────────────────────────────────────────
@router.post("/api/sim/sensor/{zone_id}")
async def sim_sensor(zone_id: str, request: Request):
    await request.app.state.engine.handle_sensor(zone_id)
    return {"ok": True}

@router.post("/api/sim/coin")
async def sim_coin(request: Request):
    await request.app.state.engine.handle_coin(1)
    return {"ok": True}

@router.post("/api/sim/button/{btn_id}")
async def sim_button(btn_id: str, request: Request):
    await request.app.state.engine.handle_button(btn_id)
    return {"ok": True}

@router.post("/api/sim/ball")
async def sim_ball(request: Request):
    await request.app.state.engine.handle_ball_consumed()
    return {"ok": True}

@router.post("/api/sim/reset")
async def sim_reset(request: Request):
    from app.game.session import GameState
    engine = request.app.state.engine
    engine.session.reset()
    await engine._transition(GameState.ATTRACT)
    return {"ok": True}

@router.post("/api/sim/qr")
async def sim_qr(request: Request):
    await request.app.state.engine.handle_qr_payment(credits=1, reference="SIM-QR")
    return {"ok": True}

@router.get("/api/state")
async def get_state(request: Request):
    return request.app.state.engine.session.to_dict()

@router.get("/api/supabase-config")
async def get_supabase_config(request: Request):
    from app.config import get_config
    cfg = get_config()
    sb = cfg.get("supabase", {})
    return {
        "url": sb.get("url", "https://your-project.supabase.co"),
        "anon_key": sb.get("anon_key", "your-anon-key"),
        "enabled": sb.get("enabled", False)
    }
