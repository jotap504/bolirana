import json, logging
from fastapi import APIRouter, WebSocket, WebSocketDisconnect, Request

log = logging.getLogger(__name__)
router = APIRouter()

# Registros globales para retransmisión nube-local (solo activos en modo nube)
active_machines: dict[str, WebSocket] = {}
active_players: dict[str, dict[int, WebSocket]] = {}
last_machine_states: dict[str, dict] = {}


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
    from app.config import get_config
    is_cloud = get_config().get("cloud_mode")
    arcade_id = ws.query_params.get("arcade_id", "FUTSPO_01")
    session_id = ws.query_params.get("session_id", "")

    if is_cloud:
        # --- MODO NUBE (RELAY PROXY) ---
        await ws.accept()
        if arcade_id not in active_machines:
            await ws.send_json({"type": "error", "message": "La máquina no está en línea."})
            await ws.close()
            return

        if arcade_id not in active_players:
            active_players[arcade_id] = {}
        active_players[arcade_id][index] = ws
        log.info("Relay Nube: Jugador %d conectado a %s", index, arcade_id)

        # Avisar a la máquina local
        try:
            await active_machines[arcade_id].send_json({
                "type": "player_connected",
                "index": index
            })
        except Exception as e:
            log.warning("Fallo al avisar conexión a la máquina: %s", e)

        # Pedir al celular que reenvíe su perfil (google_id, nombre, avatar)
        # Esto es crítico cuando el jugador reconecta — la máquina local pierde el google_id
        try:
            await ws.send_json({"type": "resend_profile"})
        except Exception as e:
            log.warning("Fallo al solicitar re-envío de perfil al celular: %s", e)

        try:
            while True:
                raw = await ws.receive_text()
                msg = json.loads(raw)
                # Reenviar el mensaje del celular a la máquina local
                if arcade_id in active_machines:
                    await active_machines[arcade_id].send_json({
                        "type": "player_message",
                        "index": index,
                        "payload": msg
                    })
        except WebSocketDisconnect:
            log.info("Relay Nube: Jugador %d desconectado de %s", index, arcade_id)
            if arcade_id in active_players and index in active_players[arcade_id]:
                active_players[arcade_id].pop(index, None)
            if arcade_id in active_machines:
                try:
                    await active_machines[arcade_id].send_json({
                        "type": "player_disconnected",
                        "index": index
                    })
                except Exception:
                    pass
        return

    # --- MODO LOCAL (ESTÁNDAR) ---
    engine = ws.app.state.engine
    if session_id != engine.session.session_id:
        await ws.accept()
        await ws.send_json({"type": "error", "message": "Sesión vencida. Por favor escaneá el nuevo QR en la pantalla."})
        await ws.close()
        return

    mgr = ws.app.state.ws
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
                    await engine.set_player_name(
                        index,
                        msg.get("name", ""),
                        msg.get("google_id"),
                        msg.get("avatar", ""),
                        club=msg.get("club", ""),
                        jersey_primary_color=msg.get("jersey_primary_color", "#ffffff"),
                        jersey_secondary_color=msg.get("jersey_secondary_color", "#00ffcc"),
                        jersey_pattern=msg.get("jersey_pattern", "plain")
                    )
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


# ── WebSocket: máquina local en la nube (Relay) ──────────────────────────────
@router.websocket("/ws/machine_relay")
async def ws_machine_relay(ws: WebSocket):
    from app.config import get_config
    if not get_config().get("cloud_mode"):
        await ws.accept()
        await ws.send_json({"type": "error", "message": "La máquina local no debe conectarse en modo local."})
        await ws.close()
        return

    arcade_id = ws.query_params.get("arcade_id", "FUTSPO_01")
    session_id = ws.query_params.get("session_id", "")
    await ws.accept()

    active_machines[arcade_id] = ws
    log.info("Relay Nube: Máquina %s conectada para sesión %s", arcade_id, session_id)

    try:
        while True:
            raw = await ws.receive_text()
            msg = json.loads(raw)
            target = msg.get("target")
            payload = msg.get("payload")

            if target == "player":
                idx = msg.get("index")
                if arcade_id in active_players and idx in active_players[arcade_id]:
                    await active_players[arcade_id][idx].send_json(payload)
            elif target == "broadcast":
                if payload.get("type") == "state":
                    last_machine_states[arcade_id] = payload
                if arcade_id in active_players:
                    data = json.dumps(payload)
                    for player_ws in list(active_players[arcade_id].values()):
                        try:
                            await player_ws.send_text(data)
                        except Exception:
                            pass
    except WebSocketDisconnect:
        log.info("Relay Nube: Máquina %s desconectada", arcade_id)
        active_machines.pop(arcade_id, None)
        if arcade_id in active_players:
            for player_ws in list(active_players[arcade_id].values()):
                try:
                    await player_ws.send_json({"type": "error", "message": "Conexión perdida con la máquina física."})
                    await player_ws.close()
                except Exception:
                    pass
            active_players.pop(arcade_id, None)


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
    await request.app.state.engine.handle_ball_consumed(from_hardware=False)
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
async def get_state(request: Request, arcade_id: str = "FUTSPO_01"):
    from app.config import get_config
    if get_config().get("cloud_mode"):
        state = last_machine_states.get(arcade_id, {})
        if not state:
            return {"state": "attract", "players": []}
        return state
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


@router.get("/api/machine/info")
async def get_machine_info(request: Request):
    from app.config import get_config
    from app.game.cloud_sync import MACHINE_LAT, MACHINE_LON, MACHINE_ZONE
    cfg = get_config()
    return {
        "arcade_id": cfg.get("arcade_id", "FUTSPO_01"),
        "latitude": MACHINE_LAT,
        "longitude": MACHINE_LON,
        "zone": MACHINE_ZONE
    }
