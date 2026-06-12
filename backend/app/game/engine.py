"""
Motor del juego — máquina de estados.
Recibe eventos (botones, monedas, sensores, pagos) y actualiza la sesión.
Emite mensajes al WebSocket manager para sincronizar display y celulares.
"""
import asyncio
import logging
import random
import time
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
        self.last_sensor_time = 0.0

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
                # Siempre step=1 y rango 1-6 en SELECT_PLAYERS
                # (el modo siempre se elige en SELECT_MODE, no aquí)
                step = 1
                min_count, max_count = 1, 6

                match btn_id:
                    case "next": count = min(max_count, count + step)
                    case "prev": count = max(min_count, count - step)
                    case "ok":
                        balls = cfg["game"]["balls_default"]
                        s.setup_players(count, balls)
                        s.mode = GameMode.CLASSIC  # Resetear modo al confirmar jugadores
                        s.credits_required = self._calc_credits(count, s.mode)
                        await self._transition(GameState.SELECT_MODE)
                        return
                s.setup_players(count, s.balls_per_player)
                s.mode = GameMode.CLASSIC  # Resetear modo al navegar jugadores
                s.credits_required = self._calc_credits(count, s.mode)
                await self._sync_state()

            case GameState.SELECT_MODE:
                count = len(s.players)
                if count % 2 != 0 or count < 4:
                    modes = [GameMode.CLASSIC, GameMode.TIMED, GameMode.GOLEADOR]
                else:
                    modes = [GameMode.CLASSIC, GameMode.TIMED, GameMode.GOLEADOR, GameMode.TEAM]

                if s.mode not in modes:
                    s.mode = GameMode.CLASSIC

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
                    if s.mode == GameMode.TEAM:
                        # Asignar equipos por defecto (alternado) antes de SELECT_TEAM
                        self._assign_teams_alternating()
                        await self._transition(GameState.SELECT_TEAM)
                    else:
                        await self._start_game()

            case GameState.SELECT_TEAM:
                t1_count = sum(1 for p in s.players if p.team == 1)
                t2_count = sum(1 for p in s.players if p.team == 2)
                is_balanced = (t1_count == t2_count)

                match btn_id:
                    case "next" | "prev":
                        # Cambiar equipo del jugador en el cursor
                        cursor = s.select_team_cursor
                        if 0 <= cursor < len(s.players):
                            p = s.players[cursor]
                            p.team = 2 if p.team == 1 else 1
                        await self._sync_state()
                    case "ok":
                        # Avanzar al siguiente jugador o confirmar si ya están todos
                        s.select_team_cursor += 1
                        if s.select_team_cursor >= len(s.players):
                            s.select_team_cursor = 0
                            if is_balanced:
                                await self._start_game()
                            else:
                                log.warning("Intento de iniciar partida con equipos desbalanceados por boton OK.")
                                await self._sync_state()
                        else:
                            await self._sync_state()
                    case "random":
                        self._assign_teams_random()
                        await self._sync_state()
                    case "start":
                        # START confirma directamente con equipos actuales si estan balanceados
                        if is_balanced:
                            s.select_team_cursor = 0
                            await self._start_game()
                        else:
                            log.warning("Intento de iniciar partida con equipos desbalanceados por boton START.")
                            await self._sync_state()

            case GameState.PAUSED:
                if btn_id == "start":
                    await self._resume()
                elif btn_id == "back":
                    await self._end_game()

            case GameState.TIEBREAK:
                pass  # El tiebreak avanza con sensores/bolas, no con botones

            case GameState.GAME_OVER:
                if btn_id in ("start", "ok"):
                    if s.credits >= s.credits_required:
                        s.credits -= s.credits_required
                        for p in s.players:
                            p.score = 0
                            p.balls_left = s.balls_per_player
                            p.balls_pocketed = 0
                        s.current_player = 0
                        s.team_scores = {1: 0, 2: 0}
                        s.tiebreak_players = []
                        s.tiebreak_cursor = 0
                        if s.mode == GameMode.TEAM:
                            self._assign_teams_alternating()
                            await self._transition(GameState.SELECT_TEAM)
                        else:
                            await self._transition(GameState.PLAYING)
                            p = s.current()
                            if p:
                                await self._broadcast({"type": "turn", "current_player": 0,
                                                       "player_name": p.name or "Jugador 1",
                                                       "balls_left":  p.balls_left})
                    else:
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
        if self.session.state not in (GameState.PLAYING, GameState.TIEBREAK):
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
        self.last_sensor_time = time.time()
        new_score = self.session.add_score(points)
        player    = self.session.current()

        if self.session.mode == GameMode.GOLEADOR:
            # En modo GOLEADOR: incrementar bolas embocadas y mostrar ese valor en display
            player.balls_pocketed += 1
            await self._broadcast({
                "type":          "score",
                "player_index":  self.session.current_player,
                "player_name":   player.name or f"Jugador {player.index + 1}",
                "delta":         1,                     # 1 bola embocada
                "total":         player.balls_pocketed,  # Display: número de bolas
                "total_pts":     new_score,              # Score real en puntos (sin mostrar)
                "zone_id":       zone_id,
                "zone_name":     zone.get("name", zone_id),
                "mode":          "goleador",
            })
        else:
            await self._broadcast({
                "type":          "score",
                "player_index":  self.session.current_player,
                "player_name":   player.name or f"Jugador {player.index + 1}",
                "delta":         points,
                "total":         new_score,
                "zone_id":       zone_id,
                "zone_name":     zone.get("name", zone_id),
            })

        # Animación LED / servo via hardware bridge
        await self._broadcast({"type": "effect", "name": "goal",
                               "zone": zone_id, "player_index": self.session.current_player})

        await self.handle_ball_consumed()

    async def handle_ball_consumed(self, from_hardware: bool = False) -> None:
        """Llamado cuando el sensor detecta que la bola salió del campo."""
        s = self.session
        cfg = get_config()
        is_mock = cfg.get("serial", {}).get("mock", False)

        if from_hardware and not is_mock and (time.time() - self.last_sensor_time < 2.0):
            log.info("Ignorando evento físico 'ball' duplicado (dentro de 2s desde el último sensor).")
            return

        if s.state == GameState.TIEBREAK:
            await self._handle_tiebreak_ball_consumed()
            return

        if s.state != GameState.PLAYING:
            return

        cfg = get_config()
        rotation_mode = cfg["game"].get("rotation_mode", "sequential")

        if rotation_mode == "alternate":
            s.consume_ball()
            next_p = s.next_player()
            if next_p is None:
                await self._check_and_start_tiebreak()
            else:
                await self._turn_change(next_p)
        else:
            has_balls = s.consume_ball()
            if not has_balls:
                next_p = s.next_player()
                if next_p is None:
                    await self._check_and_start_tiebreak()
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

        if self.session.state not in (GameState.PLAYING, GameState.PAUSED, GameState.TIEBREAK):
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

    def _assign_teams_alternating(self) -> None:
        """Asigna equipos alternando: jugadores pares → A, impares → B."""
        for i, p in enumerate(self.session.players):
            p.team = 1 if i % 2 == 0 else 2

    def _assign_teams_random(self) -> None:
        """Asigna equipos de forma aleatoria balanceada."""
        n = len(self.session.players)
        teams = [1] * (n // 2) + [2] * (n - n // 2)
        random.shuffle(teams)
        for p, t in zip(self.session.players, teams):
            p.team = t

    async def _check_and_start_tiebreak(self) -> None:
        """Verifica si hay empate y entra en estado TIEBREAK, o termina la partida."""
        s = self.session

        # Modo equipo: empate entre equipos
        if s.mode == GameMode.TEAM:
            scores = list(s.team_scores.values())
            if len(set(scores)) == 1 and len(scores) > 1:
                # Empate de equipos: dar 1 bola extra a todos
                for p in s.players:
                    p.balls_left = 1
                s.tiebreak_players = [p.index for p in s.players]
                s.tiebreak_cursor = 0
                await self._transition(GameState.TIEBREAK)
                await self._broadcast({
                    "type": "tiebreak",
                    "tied_teams": True,
                    "team_scores": s.team_scores
                })
                return
            else:
                await self._end_game()
                return

        tied = s.get_tied_players()
        if len(tied) > 1:
            # Hay empate → preparar desempate
            s.tiebreak_players = tied
            s.tiebreak_cursor = 0
            # Dar 1 bola extra a cada empatado
            for idx in tied:
                s.players[idx].balls_left = 1
            # Poner al primero en el cursor
            s.current_player = tied[0]
            await self._transition(GameState.TIEBREAK)
            await self._broadcast({
                "type":           "tiebreak",
                "tied_players":   tied,
                "player_names":   [s.players[i].name or f"Jugador {i+1}" for i in tied],
                "tiebreak_round": 1
            })
        else:
            await self._end_game()

    async def _handle_tiebreak_ball_consumed(self) -> None:
        """Maneja la bola extra del desempate."""
        s = self.session
        p = s.current()
        log.info("[TIEBREAK] Consumed ball request for player index %s. Current balls_left: %s", s.current_player, p.balls_left if p else None)
        if not p or p.balls_left <= 0:
            log.info("[TIEBREAK] Ball already consumed or player invalid. Ignoring duplicate trigger.")
            return

        p.balls_left = 0  # Consumir la bola del desempate
        await self._sync_state()  # Mostrar en la pantalla de inmediato el cambio de puntaje y consumo de bola

        # Avanzar al siguiente empatado
        s.tiebreak_cursor += 1
        log.info("[TIEBREAK] Advanced tiebreak_cursor to %s. Players in tiebreak: %s", s.tiebreak_cursor, s.tiebreak_players)
        if s.tiebreak_cursor < len(s.tiebreak_players):
            # Turno del siguiente jugador empatado
            s.current_player = s.tiebreak_players[s.tiebreak_cursor]
            next_p = s.current()
            log.info("[TIEBREAK] Next tiebreak player is index %s (name: %s)", s.current_player, next_p.name if next_p else "?")
            await self._broadcast({
                "type":         "turn",
                "current_player": s.current_player,
                "player_name":  next_p.name if next_p else "?",
                "balls_left":   1,
                "is_tiebreak":  True,
            })
            await self._sync_state()  # Sincronizar de nuevo para refrescar la interfaz (cursor de turno activo)
        else:
            # Todos tiraron → re-evaluar
            tied_after = s.get_tied_players()
            still_tied = [i for i in s.tiebreak_players if i in tied_after]
            log.info("[TIEBREAK] All players threw. tied_after: %s, still_tied: %s", tied_after, still_tied)
            if len(still_tied) > 1:
                # Sigue el empate → otra ronda
                s.tiebreak_players = still_tied
                s.tiebreak_cursor = 0
                for idx in still_tied:
                    s.players[idx].balls_left = 1
                s.current_player = still_tied[0]
                log.info("[TIEBREAK] Tie continues. Initiating new round. tied_players: %s", still_tied)
                await self._sync_state()
                await self._broadcast({
                    "type":           "tiebreak",
                    "tied_players":   still_tied,
                    "player_names":   [s.players[i].name or f"Jugador {i+1}" for i in still_tied],
                    "tiebreak_round": 2
                })
            else:
                # Se resolvió el empate
                log.info("[TIEBREAK] Tie resolved. Winner index: %s. Transitioning to end_game.", still_tied[0] if still_tied else None)
                await self._end_game()

    async def _transition(self, new_state: GameState) -> None:
        if new_state == GameState.PAYMENT and self.session.credits >= self.session.credits_required:
            log.info("Bypass PAYMENT: Créditos suficientes (%s/%s).",
                     self.session.credits, self.session.credits_required)
            await self._transition(GameState.WAITING_START)
            return

        log.info("State: %s → %s", self.session.state, new_state)
        self.session.state = new_state

        if new_state == GameState.ATTRACT:
            self._start_attract_timer()
        elif new_state == GameState.PLAYING:
            if self.session.mode == GameMode.TIMED:
                self._start_player_timer()

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
        for p in self.session.players:
            p.score = 0
            p.balls_left = self.session.balls_per_player
            p.balls_pocketed = 0
        self.session.current_player = 0
        self.session.team_scores = {1: 0, 2: 0}
        self.session.tiebreak_players = []
        self.session.tiebreak_cursor = 0
        await self._transition(GameState.PLAYING)
        p = self.session.current()
        if p:
            await self._broadcast({"type": "turn", "current_player": 0,
                                   "player_name": p.name or "Jugador 1",
                                   "balls_left":  p.balls_left})

    async def _turn_change(self, next_player: Player) -> None:
        # Cancelar timer del turno anterior
        if self._timer_task:
            self._timer_task.cancel()
            self._timer_task = None
        await self._transition(GameState.TURN_CHANGE)
        await self._broadcast({"type": "turn",
                               "current_player": self.session.current_player,
                               "player_name":    next_player.name or f"Jugador {next_player.index+1}",
                               "balls_left":     next_player.balls_left})
        await asyncio.sleep(get_config()["game"]["turn_change_seconds"])
        await self._transition(GameState.PLAYING)
        # Reiniciar timer para el nuevo jugador en modo TIMED
        if self.session.mode == GameMode.TIMED:
            self._start_player_timer()

    async def _end_game(self) -> None:
        if self._timer_task:
            self._timer_task.cancel()
            self._timer_task = None
        scores = [{
            "name":           p.name or f"Jugador {p.index+1}",
            "score":          p.score,
            "balls_pocketed": p.balls_pocketed,
            "index":          p.index,
            "team":           p.team,
        } for p in self.session.players]

        # Ordenar según el modo
        if self.session.mode == GameMode.GOLEADOR:
            scores.sort(key=lambda x: (x["balls_pocketed"], x["score"]), reverse=True)
        else:
            scores.sort(key=lambda x: x["score"], reverse=True)

        winner = self.session.winner()
        await self._transition(GameState.GAME_OVER)
        await self._broadcast({
            "type":          "game_over",
            "scores":        scores,
            "winner_index":  winner.index if winner else 0,
            "session_id":    self.session.session_id,
            "mode":          self.session.mode,
            "team_scores":   self.session.team_scores,
        })

        # Sincronizar asíncronamente a Supabase Cloud
        try:
            from .cloud_sync import sync_scores_to_supabase
            await sync_scores_to_supabase(self.session.players, self.session.mode)
        except Exception as e:
            log.error("Fallo al iniciar sincronización Supabase: %s", e)

    async def _pause(self) -> None:
        self.session.paused_state = self.session.state
        if self._timer_task:
            self._timer_task.cancel()
            self._timer_task = None
        await self._transition(GameState.PAUSED)

    async def _resume(self) -> None:
        target = self.session.paused_state or GameState.PLAYING
        await self._transition(target)
        if target == GameState.PLAYING and self.session.mode == GameMode.TIMED:
            self._start_player_timer()

    def _start_attract_timer(self) -> None:
        if self._attract_task:
            self._attract_task.cancel()
        async def _reset():
            await asyncio.sleep(get_config()["game"]["attract_timeout_seconds"])
            if self.session.state == GameState.ATTRACT:
                self.session.reset()
                await self._sync_state()
        self._attract_task = asyncio.create_task(_reset())

    def _start_player_timer(self) -> None:
        """Timer por turno completo del jugador (modo TIMED). Solo afecta el display."""
        if self._timer_task:
            self._timer_task.cancel()
        cfg = get_config()["game"]
        seconds = cfg.get("time_per_player_seconds", 60)
        self.session.time_left = seconds

        async def _tick():
            while self.session.time_left > 0 and self.session.state == GameState.PLAYING:
                await asyncio.sleep(1)
                self.session.time_left -= 1
                # Solo broadcast al display (type=timer)
                await self._broadcast({"type": "timer", "seconds_left": self.session.time_left})
            if self.session.state == GameState.PLAYING and self.session.time_left <= 0:
                # Se agotó el tiempo → consumir todas las bolas restantes del jugador y avanzar
                p = self.session.current()
                if p:
                    p.balls_left = 0
                log.info("Tiempo agotado para %s. Avanzando al siguiente jugador.", 
                         self.session.current().name if self.session.current() else "?")
                next_p = self.session.next_player()
                if next_p is None:
                    await self._check_and_start_tiebreak()
                else:
                    await self._turn_change(next_p)

        self._timer_task = asyncio.create_task(_tick())
