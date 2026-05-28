"""
WebSocket manager — hub central de conexiones.
Canales:
  display  → pantalla principal (1 conexión)
  player   → celulares de jugadores (hasta 6)
  admin    → panel de administración
"""
import json
import logging
from enum import Enum
from fastapi import WebSocket

log = logging.getLogger(__name__)


class Channel(str, Enum):
    DISPLAY = "display"
    PLAYER  = "player"
    ADMIN   = "admin"


class WSManager:
    def __init__(self):
        self._display:  WebSocket | None        = None
        self._players:  dict[int, WebSocket]    = {}   # index → ws
        self._admin:    list[WebSocket]         = []

    # ── conexiones ────────────────────────────────────────────────────────────

    async def connect_display(self, ws: WebSocket) -> None:
        await ws.accept()
        self._display = ws
        log.info("Display conectado")

    async def connect_player(self, ws: WebSocket, index: int) -> None:
        await ws.accept()
        self._players[index] = ws
        log.info("Jugador %d conectado", index)

    async def connect_admin(self, ws: WebSocket) -> None:
        await ws.accept()
        self._admin.append(ws)

    def disconnect_display(self)  -> None: self._display = None
    def disconnect_player(self, index: int) -> None: self._players.pop(index, None)
    def disconnect_admin(self, ws: WebSocket) -> None:
        try: self._admin.remove(ws)
        except ValueError: pass

    # ── broadcast ─────────────────────────────────────────────────────────────

    async def broadcast(self, message: dict) -> None:
        """Envía a display + todos los jugadores + admin."""
        data = json.dumps(message)
        await self._send_display(data)
        for ws in list(self._players.values()):
            await self._safe_send(ws, data)
        for ws in list(self._admin):
            await self._safe_send(ws, data)

    async def send_display(self, message: dict) -> None:
        if self._display:
            await self._safe_send(self._display, json.dumps(message))

    async def send_player(self, index: int, message: dict) -> None:
        ws = self._players.get(index)
        if ws:
            await self._safe_send(ws, json.dumps(message))

    async def send_admin(self, message: dict) -> None:
        data = json.dumps(message)
        for ws in list(self._admin):
            await self._safe_send(ws, data)

    async def _send_display(self, data: str) -> None:
        if self._display:
            await self._safe_send(self._display, data)

    @staticmethod
    async def _safe_send(ws: WebSocket, data: str) -> None:
        try:
            await ws.send_text(data)
        except Exception as e:
            log.warning("WS send error: %s", e)

    def player_count(self) -> int:
        return len(self._players)

    def is_display_connected(self) -> bool:
        return self._display is not None
