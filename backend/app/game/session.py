from dataclasses import dataclass, field
from enum import Enum
from typing import Optional


class GameMode(str, Enum):
    CLASSIC  = "classic"
    TIMED    = "timed"
    BATTLE   = "battle"
    TEAM     = "team"


class GameState(str, Enum):
    ATTRACT        = "attract"
    SELECT_PLAYERS = "select_players"
    SELECT_MODE    = "select_mode"
    PAYMENT        = "payment"
    CONNECT_PHONE  = "connect_phone"
    WAITING_START  = "waiting_start"
    PLAYING        = "playing"
    TURN_CHANGE    = "turn_change"
    GAME_OVER      = "game_over"
    PAUSED         = "paused"


@dataclass
class Player:
    index:       int
    name:        str  = ""
    score:       int  = 0
    balls_left:  int  = 0
    connected:   bool = False   # celular conectado
    google_id:   Optional[str] = None # Google OAuth User ID
    avatar:      str  = ""      # URL de la foto de Google o ruta del avatar local

    def to_dict(self) -> dict:
        return {
            "index":      self.index,
            "name":       self.name or f"Jugador {self.index + 1}",
            "score":      self.score,
            "balls_left": self.balls_left,
            "connected":  self.connected,
            "google_id":  self.google_id,
            "avatar":     self.avatar,
        }


@dataclass
class Session:
    state:           GameState        = GameState.ATTRACT
    players:         list[Player]     = field(default_factory=list)
    current_player:  int              = 0
    mode:            GameMode         = GameMode.CLASSIC
    credits:         int              = 0
    credits_required: int             = 0
    balls_per_player: int             = 5
    time_left:       int              = 0    # segundos, modo TIMED
    paused_state:    Optional[GameState] = None
    session_id:      str              = ""

    # ── helpers ──────────────────────────────────────────────────────────────

    def reset(self) -> None:
        import uuid
        self.state           = GameState.ATTRACT
        self.players         = []
        self.current_player  = 0
        self.credits_required = 0
        self.paused_state    = None
        self.session_id      = uuid.uuid4().hex[:8]  # ID único de sesión de 8 caracteres

    def setup_players(self, count: int, balls: int) -> None:
        self.players = [Player(index=i, balls_left=balls) for i in range(count)]
        self.balls_per_player = balls

    def current(self) -> Optional[Player]:
        if self.players and 0 <= self.current_player < len(self.players):
            return self.players[self.current_player]
        return None

    def add_score(self, points: int) -> int:
        p = self.current()
        if p:
            p.score += points
        return p.score if p else 0

    def consume_ball(self) -> bool:
        """Descuenta una bola al jugador actual. Retorna True si aun le quedan."""
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
        return max(self.players, key=lambda p: p.score)

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

        return {
            "state":            self.state,
            "players":          [p.to_dict() for p in self.players],
            "current_player":   self.current_player,
            "mode":             self.mode,
            "credits":          self.credits,
            "credits_required": self.credits_required,
            "balls_per_player": self.balls_per_player,
            "time_left":        self.time_left,
            "session_id":       self.session_id,
            "local_ip":         get_local_ip(),
        }
