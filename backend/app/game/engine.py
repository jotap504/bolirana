"""
Motor del juego — máquina de estados.
Recibe eventos (botones, monedas, sensores, pagos) y actualiza la sesión.
Emite mensajes al WebSocket manager para sincronizar display y celulares.
"""
import asyncio
import logging
from typing import Callable, Awaitable

from ..config import get_config
from .session import GameMode, GameState, Player, Session

log = logging.getLogger(__name__)

# Tipo del callback para broadcast
Broadcaster = Callable[[dict], Awaitable[None]]


class GameEngine:
    def __init__(self, broadcast: Broadcaster) -> None:
        self._broadcast = broadcast
        self.session    = Session()
        self.session.reset()
        self._attract_task: asyncio.Task | None = None
        self._timer_task:   asyncio.Task | None = None
        self.proximity_active = False
        self.last_relay_session_id = ""
        self.relay_client = None

    # ── API pública ──────────────────────────────────────────────────────────

    async def handle_button(self, btn_id: str) -> None:
        s = self.session
        cfg = get_config()

        if btn_id == "pause" and s.state == GameState.PLAYING:
            await self._pause()
            return
        if btn_id == "pause" and s.state == GameState.PAUSED:
            await self._resume()
            return

        match s.state:
            case GameState.ATTRACT:
                await self._transition(GameState.SELECT_PLAYERS)

            case GameState.SELECT_PLAYERS:
                count = len(s.players) if s.players else 1
                match btn_id:
                    case "next": count = min(6, count + 1)
                    case "prev": count = max(1, count - 1)
                    case "ok":
                        balls = cfg["game"]["balls_default"]
                        s.setup_players(count, balls)
                        s.credits_required = self._calc_credits(count, s.mode)
                        await self._transition(GameState.SELECT_MODE)
                        return
                s.setup_players(count, s.balls_per_player)
                s.credits_required = self._calc_credits(count, s.mode)
                await self._sync_state()

            case GameState.SELECT_MODE:
                modes = list(GameMode)
                idx   = modes.index(s.mode)
                match btn_id:
                    case "next": s.mode = modes[(idx + 1) % len(modes)]
                    case "prev": s.mode = modes[(idx - 1) % len(modes)]
                    case "ok":
                        s.credits_required = self._calc_credits(len(s.players), s.mode)
                        await self._transition(GameState.PAYMENT)
                        return
                s.credits_required = self._calc_credits(len(s.players), s.mode)
                await self._sync_state()

            case GameState.PAYMENT:
                if btn_id == "back":
                    await self._transition(GameState.SELECT_MODE)

            case GameState.CONNECT_PHONE:
                if btn_id in ("start", "ok"):
                    await self._transition(GameState.WAITING_START)

            case GameState.WAITING_START:
                if btn_id == "start":
                    await self._start_game()

            case GameState.PAUSED:
                if btn_id == "start":
                    await self._resume()
                elif btn_id == "back":
                    await self._end_game()

            case GameState.GAME_OVER:
                if btn_id in ("start", "ok"):
                    # Verificar si hay créditos suficientes para iniciar una revancha
                    if s.credits >= s.credits_required:
                        # Deducir créditos
                        s.credits -= s.credits_required
                        # REVANCHA RÁPIDA: Mantener mismos jugadores, reiniciar puntajes e iniciar partida
                        for p in s.players:
                            p.score = 0
                            p.balls_left = s.balls_per_player
                        s.current_player = 0
                        await self._transition(GameState.PLAYING)
                        p = s.current()
                        if p:
                            await self._broadcast({"type": "turn", "current_player": 0,
                                                   "player_name": p.name or f"Jugador 1",
                                                   "balls_left":  p.balls_left})
                    else:
                        # Si no hay créditos suficientes, volver a la pantalla de pago
                        # pero conservando el listado de jugadores
                        await self._transition(GameState.PAYMENT)
                elif btn_id == "back":
                    self.session.reset()
                    await self._transition(GameState.ATTRACT)

    async def handle_coin(self, count: int = 1) -> None:
        cfg = get_config()
        credits_added = count * cfg["pricing"]["coin_to_credits"]
        self.session.credits += credits_added
        await self._broadcast({"type": "credits", "total": self.session.credits,
                               "required": self.session.credits_required,
                               "added": credits_added})

        if self.session.state == GameState.ATTRACT:
            await self._transition(GameState.SELECT_PLAYERS)
        elif self.session.state == GameState.PAYMENT:
            await self._check_payment_complete()

    async def handle_sensor(self, zone_id: str) -> None:
        if self.session.state != GameState.PLAYING:
            return
        if self.proximity_active:
            log.warning("Sensor de puntaje ignorado: ¡Jugador cometiendo trampa!")
            return
        cfg     = get_config()
        sensors = {s["id"]: s for s in cfg["sensors"]}
        zone    = sensors.get(zone_id)
        if not zone or not zone.get("enabled"):
            return

        points    = zone["points"]
        new_score = self.session.add_score(points)
        player    = self.session.current()

        await self._broadcast({
            "type":         "score",
            "player_index": self.session.current_player,
            "player_name":  player.name or f"Jugador {player.index + 1}",
            "delta":        points,
            "total":        new_score,
            "zone_id":      zone_id,
            "zone_name":    zone.get("name", zone_id),
        })

        # Animación LED / servo via hardware bridge
        await self._broadcast({"type": "effect", "name": "goal",
                               "zone": zone_id, "player_index": self.session.current_player})

        if self.session.mode == GameMode.BATTLE:
            target = get_config()["game"].get("battle_target", 500)
            if new_score >= target:
                await self._end_game()
                return

        await self.handle_ball_consumed()

    async def handle_ball_consumed(self) -> None:
        """Llamado cuando el sensor detecta que la bola salió del campo."""
        if self.session.state != GameState.PLAYING:
            return

        cfg = get_config()
        rotation_mode = cfg["game"].get("rotation_mode", "sequential")

        if rotation_mode == "alternate":
            # Modo alternado: 1 bola por jugador cada vez
            self.session.consume_ball()
            next_p = self.session.next_player()
            if next_p is None:
                await self._end_game()
            else:
                await self._turn_change(next_p)
        else:
            # Modo secuencial: tirar todas sus bolas consecutivas
            has_balls = self.session.consume_ball()
            if not has_balls:
                next_p = self.session.next_player()
                if next_p is None:
                    await self._end_game()
                else:
                    await self._turn_change(next_p)

    async def handle_qr_payment(self, credits: int, reference: str) -> None:
        self.session.credits += credits
        await self._broadcast({"type": "credits", "total": self.session.credits,
                               "required": self.session.credits_required,
                               "added": credits, "reference": reference})
        if self.session.state == GameState.PAYMENT:
            await self._check_payment_complete()

    async def set_player_name(self, index: int, name: str, google_id: str = None, avatar: str = None, club: str = "", jersey_primary_color: str = "#ffffff", jersey_secondary_color: str = "#00ffcc", jersey_pattern: str = "plain") -> None:
        if 0 <= index < len(self.session.players):
            self.session.players[index].name = name[:20]
            if google_id:
                self.session.players[index].google_id = google_id
            if avatar:
                self.session.players[index].avatar = avatar
            if club:
                self.session.players[index].club = club
            if jersey_primary_color:
                self.session.players[index].jersey_primary_color = jersey_primary_color
            if jersey_secondary_color:
                self.session.players[index].jersey_secondary_color = jersey_secondary_color
            if jersey_pattern:
                self.session.players[index].jersey_pattern = jersey_pattern
            await self._sync_state()

    async def set_balls_config(self, balls: int) -> None:
        cfg_opts = get_config()["game"]["ball_options"]
        if balls in cfg_opts:
            for p in self.session.players:
                p.balls_left = balls
            self.session.balls_per_player = balls
            await self._sync_state()

    async def handle_proximity(self, active: bool) -> None:
        cfg = get_config()
        ac = cfg.get("anti_cheat", {})
        if not (ac.get("front_enabled") or ac.get("left_enabled") or ac.get("right_enabled")):
            return

        if self.session.state != GameState.PLAYING and self.session.state != GameState.PAUSED:
            return

        if active:
            if not self.proximity_active:
                self.proximity_active = True
                log.warning("¡ALERTA PROXIMIDAD ACTIVA! Jugador cometiendo trampa.")
                await self._broadcast({"type": "proximity_alert", "active": True})
                if self.session.state == GameState.PLAYING:
                    await self._pause()
        else:
            if self.proximity_active:
                self.proximity_active = False
                log.info("Alerta de proximidad desactivada. Jugador en distancia reglamentaria.")
                await self._broadcast({"type": "proximity_alert", "active": False})
                if self.session.state == GameState.PAUSED:
                    await self._resume()

    # ── internos ─────────────────────────────────────────────────────────────

    def _calc_credits(self, count: int, mode: GameMode) -> int:
        cfg      = get_config()["pricing"]
        base     = cfg["base_credits_per_player"] * count
        extra    = cfg["mode_extra"].get(mode, 0)
        discount = cfg["group_discount"].get(str(count), 0)
        return max(1, base + extra - discount)

    async def _transition(self, new_state: GameState) -> None:
        # Si transicionamos a PAYMENT pero ya tenemos créditos acumulados suficientes,
        # hacemos bypass directo a WAITING_START de manera silenciosa y segura.
        if new_state == GameState.PAYMENT and self.session.credits >= self.session.credits_required:
            log.info("Bypass PAYMENT: Créditos suficientes (%s/%s). Transicionando a WAITING_START.", 
                     self.session.credits, self.session.credits_required)
            await self._transition(GameState.WAITING_START)
            return

        log.info("State: %s → %s", self.session.state, new_state)
        self.session.state = new_state

        if new_state == GameState.ATTRACT:
            self._start_attract_timer()
        elif new_state == GameState.PLAYING:
            if self.session.mode == GameMode.TIMED:
                self._start_timer()

        # Actualizar conexión del relay si cambió la sesión (solo en local)
        if self.relay_client and self.session.session_id != self.last_relay_session_id:
            self.last_relay_session_id = self.session.session_id
            self.relay_client.start(self.last_relay_session_id)

        await self._sync_state()

    async def _sync_state(self) -> None:
        await self._broadcast({"type": "state", **self.session.to_dict()})

    async def _check_payment_complete(self) -> None:
        if self.session.credits >= self.session.credits_required:
            await self._transition(GameState.WAITING_START)

    async def _start_game(self) -> None:
        self.session.credits -= self.session.credits_required
        # Reiniciar puntajes y bolas para todos los jugadores para empezar una partida limpia
        for p in self.session.players:
            p.score = 0
            p.balls_left = self.session.balls_per_player
        self.session.current_player = 0
        await self._transition(GameState.PLAYING)
        p = self.session.current()
        if p:
            await self._broadcast({"type": "turn", "current_player": 0,
                                   "player_name": p.name or f"Jugador 1",
                                   "balls_left":  p.balls_left})

    async def _turn_change(self, next_player: Player) -> None:
        await self._transition(GameState.TURN_CHANGE)
        await self._broadcast({"type": "turn",
                               "current_player": self.session.current_player,
                               "player_name":    next_player.name or f"Jugador {next_player.index+1}",
                               "balls_left":     next_player.balls_left})
        await asyncio.sleep(get_config()["game"]["turn_change_seconds"])
        await self._transition(GameState.PLAYING)

    async def _end_game(self) -> None:
        if self._timer_task:
            self._timer_task.cancel()
        scores = [{"name": p.name or f"Jugador {p.index+1}", "score": p.score,
                   "index": p.index} for p in self.session.players]
        scores.sort(key=lambda x: x["score"], reverse=True)
        winner = self.session.winner()
        await self._transition(GameState.GAME_OVER)
        await self._broadcast({"type": "game_over", "scores": scores,
                               "winner_index": winner.index if winner else 0})
        
        # Sincronizar asíncronamente a Supabase Cloud
        try:
            from .cloud_sync import sync_scores_to_supabase
            await sync_scores_to_supabase(self.session.players, self.session.mode)
        except Exception as e:
            log.error("Fallo al iniciar sincronización Supabase: %s", e)

    async def _pause(self) -> None:
        self.session.paused_state = self.session.state
        await self._transition(GameState.PAUSED)

    async def _resume(self) -> None:
        target = self.session.paused_state or GameState.PLAYING
        await self._transition(target)

    def _start_attract_timer(self) -> None:
        if self._attract_task:
            self._attract_task.cancel()
        async def _reset():
            await asyncio.sleep(get_config()["game"]["attract_timeout_seconds"])
            if self.session.state == GameState.ATTRACT:
                self.session.reset()
                await self._sync_state()
        self._attract_task = asyncio.create_task(_reset())

    def _start_timer(self) -> None:
        cfg = get_config()["game"]
        self.session.time_left = cfg["time_limit_seconds"]
        async def _tick():
            while self.session.time_left > 0 and self.session.state == GameState.PLAYING:
                await asyncio.sleep(1)
                self.session.time_left -= 1
                await self._broadcast({"type": "timer", "seconds_left": self.session.time_left})
            if self.session.state == GameState.PLAYING:
                await self._end_game()
        self._timer_task = asyncio.create_task(_tick())
