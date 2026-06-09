import asyncio
import json
import logging
import websockets
from app.config import get_config

log = logging.getLogger(__name__)


class RelayPlayerWebSocket:
    def __init__(self, index: int, relay_client):
        self.index = index
        self.relay_client = relay_client

    async def accept(self) -> None:
        pass

    async def send_text(self, data: str) -> None:
        # Enviar el mensaje al servidor en la nube para que se lo mande al celular del jugador
        try:
            payload = json.loads(data)
            await self.relay_client.send({
                "target": "player",
                "index": self.index,
                "payload": payload
            })
        except Exception as e:
            log.warning("Fallo al enviar mensaje de relay a la nube para jugador %d: %s", self.index, e)

    async def close(self) -> None:
        pass


class CloudRelayClient:
    def __init__(self, app):
        self.app = app
        self.ws = None
        self.running = False
        self.task = None
        self.arcade_id = "FUTSPO_01"

    def start(self, session_id: str):
        self.stop()
        self.running = True
        self.task = asyncio.create_task(self._loop(session_id))
        log.info("RelayClient iniciado para sesión %s", session_id)

    def stop(self):
        self.running = False
        if self.task:
            self.task.cancel()
            self.task = None
        self.ws = None
        log.info("RelayClient detenido")

    async def send(self, data: dict):
        if self.ws and self.running:
            try:
                await self.ws.send(json.dumps(data))
            except Exception as e:
                log.warning("RelayClient failed to send: %s", e)

    async def _loop(self, session_id: str):
        import os
        cloud_url = os.getenv("CLOUD_WS_URL", "wss://bolirana.onrender.com/ws/machine_relay")
        url = f"{cloud_url}?arcade_id={self.arcade_id}&session_id={session_id}"

        if url.startswith("http://"):
            url = url.replace("http://", "ws://")
        elif url.startswith("https://"):
            url = url.replace("https://", "wss://")

        while self.running:
            try:
                log.info("RelayClient conectando a %s...", url)
                async with websockets.connect(url) as ws:
                    self.ws = ws
                    log.info("RelayClient conectado a la nube con éxito.")

                    while self.running:
                        raw = await ws.recv()
                        msg = json.loads(raw)
                        msg_type = msg.get("type")
                        idx = msg.get("index")

                        engine = self.app.state.engine
                        mgr = self.app.state.ws

                        if msg_type == "player_connected":
                            log.info("Relay Nube -> Local: Jugador %d conectado", idx)
                            fake_ws = RelayPlayerWebSocket(idx, self)
                            await mgr.connect_player(fake_ws, idx)
                            if idx < len(engine.session.players):
                                engine.session.players[idx].connected = True
                            await engine._sync_state()

                        elif msg_type == "player_disconnected":
                            log.info("Relay Nube -> Local: Jugador %d desconectado", idx)
                            mgr.disconnect_player(idx)
                            if idx < len(engine.session.players):
                                engine.session.players[idx].connected = False
                            await engine._sync_state()

                        elif msg_type == "player_message":
                            payload = msg.get("payload", {})
                            payload_type = payload.get("type")
                            log.info("Relay Nube -> Local: Mensaje de jugador %d: %s", idx, payload)

                            if payload_type == "player_name":
                                await engine.set_player_name(
                                    idx,
                                    payload.get("name", ""),
                                    payload.get("google_id"),
                                    payload.get("avatar", ""),
                                    club=payload.get("club", ""),
                                    jersey_primary_color=payload.get("jersey_primary_color", "#ffffff"),
                                    jersey_secondary_color=payload.get("jersey_secondary_color", "#00ffcc"),
                                    jersey_pattern=payload.get("jersey_pattern", "plain")
                                )
                            elif payload_type == "btn":
                                await engine.handle_button(payload.get("id"))

            except asyncio.CancelledError:
                break
            except Exception as e:
                log.error("RelayClient error de conexión: %s. Reintentando en 5s...", e)
                self.ws = None
                await asyncio.sleep(5)
