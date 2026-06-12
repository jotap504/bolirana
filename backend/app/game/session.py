from dataclasses import dataclass, field
from enum import Enum
from typing import Optional


class GameMode(str, Enum):
    CLASSIC  = "classic"   # Gana mayor puntaje acumulado
    TIMED    = "timed"     # Gana mayor puntaje en tiempo por turno
    TEAM     = "team"      # Equipos A vs B, suma acumulada por equipo
    GOLEADOR = "goleador"  # Gana quien emboca MÁS bolas (sin importar zona)


class GameState(str, Enum):
    ATTRACT        = "attract"
    SELECT_PLAYERS = "select_players"
    SELECT_MODE    = "select_mode"
    PAYMENT        = "payment"
    CONNECT_PHONE  = "connect_phone"
    WAITING_START  = "waiting_start"
    SELECT_TEAM    = "select_team"   # Nuevo: elección de equipo en modo TEAM
    PLAYING        = "playing"
    TURN_CHANGE    = "turn_change"
    TIEBREAK       = "tiebreak"      # Nuevo: desempate con bola extra
    GAME_OVER      = "game_over"
    PAUSED         = "paused"


@dataclass
class Player:
    index:       int
    name:        str  = ""
    score:       int  = 0
    balls_left:  int  = 0
    balls_pocketed: int = 0   # Bolas embocadas en cualquier agujero (modo GOLEADOR)
    connected:   bool = False # Celular conectado
    google_id:   Optional[str] = None  # Google OAuth User ID
    avatar:      str  = ""    # URL de la foto de Google o ruta del avatar local
    jersey_primary_color:   str = "#ffffff"
    jersey_secondary_color: str = "#00ffcc"
    jersey_pattern:         str = "plain"
    club:                   str = ""
    team:        int  = 0     # 0=sin asignar, 1=Equipo A, 2=Equipo B (modo TEAM)

    def to_dict(self) -> dict:
        return {
            "index":         self.index,
            "name":          self.name or f"Jugador {self.index + 1}",
            "score":         self.score,
            "balls_left":    self.balls_left,
            "balls_pocketed": self.balls_pocketed,
            "connected":     self.connected,
            "google_id":     self.google_id,
            "avatar":        self.avatar,
            "jersey_primary_color":   self.jersey_primary_color,
            "jersey_secondary_color": self.jersey_secondary_color,
            "jersey_pattern":         self.jersey_pattern,
            "club":                   self.club,
            "team":                   self.team,
        }


@dataclass
class Session:
    state:            GameState       = GameState.ATTRACT
    players:          list[Player]   = field(default_factory=list)
    current_player:   int            = 0
    mode:             GameMode       = GameMode.CLASSIC
    credits:          int            = 0
    credits_required: int            = 0
    balls_per_player: int            = 5
    time_left:        int            = 0    # segundos, modo TIMED (por turno)
    paused_state:     Optional[GameState] = None
    session_id:       str            = ""

    # Modo TEAM
    team_scores:         dict        = field(default_factory=lambda: {1: 0, 2: 0})
    select_team_cursor:  int         = 0   # jugador que está siendo asignado

    # Sistema de desempate
    tiebreak_players:    list        = field(default_factory=list)  # índices de empatados
    tiebreak_cursor:     int         = 0   # a qué jugador le toca en el tiebreak

    # ── helpers ──────────────────────────────────────────────────────────────

    def reset(self) -> None:
        import uuid
        self.state            = GameState.ATTRACT
        self.players          = []
        self.current_player   = 0
        self.mode             = GameMode.CLASSIC
        self.credits_required = 0
        self.paused_state     = None
        self.session_id       = uuid.uuid4().hex[:8]
        self.team_scores      = {1: 0, 2: 0}
        self.select_team_cursor = 0
        self.tiebreak_players = []
        self.tiebreak_cursor  = 0

    def setup_players(self, count: int, balls: int) -> None:
        self.players = [Player(index=i, balls_left=balls) for i in range(count)]
        self.balls_per_player = balls
        self.team_scores = {1: 0, 2: 0}

    def current(self) -> Optional[Player]:
        if self.players and 0 <= self.current_player < len(self.players):
            return self.players[self.current_player]
        return None

    def add_score(self, points: int) -> int:
        """Suma puntos al jugador actual. En modo TEAM también suma al equipo."""
        p = self.current()
        if p:
            p.score += points
            # Modo equipo: acumular también en el score del equipo
            if self.mode == GameMode.TEAM and p.team in self.team_scores:
                self.team_scores[p.team] += points
        return p.score if p else 0

    def consume_ball(self) -> bool:
        """Descuenta una bola al jugador actual. Retorna True si aún le quedan."""
        p = self.current()
        if p and p.balls_left > 0:
            p.balls_left -= 1
            return p.balls_left > 0
        return False

    def next_player(self) -> Optional[Player]:
        """Avanza al siguiente jugador. Retorna None si la partida terminó."""
        for _ in range(len(self.players)):
            self.current_player = (self.current_player + 1) % len(self.players)
            p = self.current()
            if p and p.balls_left > 0:
                return p
        return None   # todos sin bolas → fin

    def game_finished(self) -> bool:
        return all(p.balls_left == 0 for p in self.players)

    def winner(self) -> Optional[Player]:
        if not self.players:
            return None
        if self.mode == GameMode.GOLEADOR:
            return max(self.players, key=lambda p: (p.balls_pocketed, p.score))
        return max(self.players, key=lambda p: p.score)

    def get_tied_players(self) -> list:
        """Retorna los índices de los jugadores empatados en la métrica principal."""
        if not self.players:
            return []
        if self.mode == GameMode.TEAM:
            return []  # En modo equipo el empate se resuelve diferente
        if self.mode == GameMode.GOLEADOR:
            max_val = max(p.balls_pocketed for p in self.players)
            return [p.index for p in self.players if p.balls_pocketed == max_val]
        else:
            max_val = max(p.score for p in self.players)
            return [p.index for p in self.players if p.score == max_val]

    def to_dict(self) -> dict:
        import socket
        def get_local_ip() -> str:
            s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
            try:
                s.connect(('8.8.8.8', 80))
                ip = s.getsockname()[0]
            except Exception:
                ip = '127.0.0.1'
            finally:
                s.close()
            return ip

        import os
        cloud_ws = os.getenv("CLOUD_WS_URL", "")
        cloud_host = ""
        if cloud_ws:
            parts = cloud_ws.replace("wss://", "").replace("ws://", "").split("/")
            if parts:
                cloud_host = parts[0]

        from app.config import get_config
        return {
            "state":             self.state,
            "arcade_id":         get_config().get("arcade_id", "FUTSPO_01"),
            "players":           [p.to_dict() for p in self.players],
            "current_player":    self.current_player,
            "mode":              self.mode,
            "credits":           self.credits,
            "credits_required":  self.credits_required,
            "balls_per_player":  self.balls_per_player,
            "time_left":         self.time_left,
            "session_id":        self.session_id,
            "local_ip":          get_local_ip(),
            "cloud_host":        cloud_host,
            "team_scores":       self.team_scores,
            "select_team_cursor": self.select_team_cursor,
            "tiebreak_players":  self.tiebreak_players,
            "tiebreak_cursor":   self.tiebreak_cursor,
        }
