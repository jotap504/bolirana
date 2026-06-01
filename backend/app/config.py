import os
from pathlib import Path

DB_PATH   = Path(__file__).parent.parent / "bolirana.db"
ASSETS_DIR = Path(__file__).parent.parent / "static" / "display" / "assets"

DEFAULT_CONFIG = {
    "game": {
        "balls_default": 5,
        "ball_options": [3, 5, 7, 10],
        "time_limit_seconds": 120,
        "attract_timeout_seconds": 30,
        "turn_change_seconds": 2,
        "rotation_mode": "sequential",  # "sequential" (todas las bolas juntas) o "alternate" (1 bola cada uno)
    },
    "pricing": {
        "base_credits_per_player": 1,
        "mode_extra": {"classic": 0, "timed": 0, "battle": 1, "team": 0},
        "coin_to_credits": 1,
        "pesos_per_credit": 200,
        "group_discount": {"5": 1, "6": 2},
    },
    "sensors": [
        {"id": "rana", "name": "Rana", "points": 1000, "enabled": True},
        {"id": "sapo", "name": "Sapo", "points": 500, "enabled": True},
        {"id": "fosa_1", "name": "Fosa 1", "points": 100, "enabled": True},
        {"id": "fosa_2", "name": "Fosa 2", "points": 50, "enabled": True},
        {"id": "fosa_3", "name": "Fosa 3", "points": 20, "enabled": True},
        {"id": "fosa_4", "name": "Fosa 4", "points": 10, "enabled": True},
        {"id": "cero", "name": "Cero Puntos", "points": 0, "enabled": True},
    ],
    "serial": {
        "port": "/dev/ttyUSB0",
        "baud": 115200,
        "mock": False,
    },
    "mercadopago": {
        "access_token": os.getenv("MP_ACCESS_TOKEN", ""),
        "enabled": False,
    },
    "supabase": {
        "url": os.getenv("SUPABASE_URL", "https://your-project.supabase.co"),
        "anon_key": os.getenv("SUPABASE_ANON_KEY", "your-anon-key"),
        "enabled": os.getenv("SUPABASE_ENABLED", "False").lower() in ("true", "1", "yes"),
    },
    "attract": {
        "videos": [],
        "images": [],
    },
    "anti_cheat": {
        "front_enabled": True,
        "left_enabled": False,
        "right_enabled": False,
        "alert_duration_seconds": 3,
        "invalidate_throws": False,
    },
}

_runtime: dict = {}

def get_config() -> dict:
    return _runtime if _runtime else DEFAULT_CONFIG

def update_config(patch: dict) -> None:
    import copy
    global _runtime
    base = copy.deepcopy(get_config())
    _deep_merge(base, patch)
    _runtime = base

def _deep_merge(target: dict, source: dict) -> None:
    for k, v in source.items():
        if k in target and isinstance(target[k], dict) and isinstance(v, dict):
            _deep_merge(target[k], v)
        else:
            target[k] = v
