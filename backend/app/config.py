import os
from pathlib import Path

if os.getenv("RENDER") == "true":
    default_db_path = "/tmp/bolirana.db"
else:
    default_db_path = str(Path(__file__).parent.parent / "bolirana.db")

DB_PATH = Path(os.getenv("DATABASE_PATH", default_db_path))
ASSETS_DIR = Path(__file__).parent.parent / "static" / "display" / "assets"

DEFAULT_CONFIG = {
    "arcade_id": "FUTSPO_01",
    "game": {
        "balls_default": 5,
        "ball_options": [3, 5, 7, 10],
        "time_per_player_seconds": 60,     # Segundos por turno completo (modo TIMED)
        "goleador_points_per_ball": 500,   # Puntos al ranking por cada bola embocada (modo GOLEADOR)
        "attract_timeout_seconds": 30,
        "turn_change_seconds": 2,
        "rotation_mode": "sequential",  # "sequential" o "alternate"
    },
    "pricing": {
        "base_credits_per_player": 1,
        "mode_extra": {"classic": 0, "timed": 0, "team": 0, "goleador": 0},
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
    "cloud_mode": os.getenv("CLOUD_MODE", "False").lower() in ("true", "1", "yes"),
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

def apply_env_overrides(cfg: dict) -> None:
    # Supabase config overrides from environment
    if "SUPABASE_URL" in os.environ:
        if "supabase" not in cfg:
            cfg["supabase"] = {}
        cfg["supabase"]["url"] = os.environ["SUPABASE_URL"]
    if "SUPABASE_ANON_KEY" in os.environ:
        if "supabase" not in cfg:
            cfg["supabase"] = {}
        cfg["supabase"]["anon_key"] = os.environ["SUPABASE_ANON_KEY"]
    if "SUPABASE_ENABLED" in os.environ:
        if "supabase" not in cfg:
            cfg["supabase"] = {}
        cfg["supabase"]["enabled"] = os.environ["SUPABASE_ENABLED"].lower() in ("true", "1", "yes")
    if "CLOUD_MODE" in os.environ:
        cfg["cloud_mode"] = os.environ["CLOUD_MODE"].lower() in ("true", "1", "yes")
    if "MP_ACCESS_TOKEN" in os.environ:
        if "mercadopago" not in cfg:
            cfg["mercadopago"] = {}
        cfg["mercadopago"]["access_token"] = os.environ["MP_ACCESS_TOKEN"]
    if "SERIAL_MOCK" in os.environ:
        if "serial" not in cfg:
            cfg["serial"] = {}
        cfg["serial"]["mock"] = os.environ["SERIAL_MOCK"].lower() in ("true", "1", "yes")
    if "SERIAL_PORT" in os.environ:
        if "serial" not in cfg:
            cfg["serial"] = {}
        cfg["serial"]["port"] = os.environ["SERIAL_PORT"]


def get_config() -> dict:
    cfg = _runtime if _runtime else DEFAULT_CONFIG
    apply_env_overrides(cfg)
    return cfg

def update_config(patch: dict) -> None:
    import copy
    global _runtime
    base = copy.deepcopy(get_config())
    _deep_merge(base, patch)
    apply_env_overrides(base)
    _runtime = base

def _deep_merge(target: dict, source: dict) -> None:
    for k, v in source.items():
        if k in target and isinstance(target[k], dict) and isinstance(v, dict):
            _deep_merge(target[k], v)
        else:
            target[k] = v


async def save_config_to_db(session) -> None:
    from .models import ConfigEntry
    from sqlalchemy import select
    import json
    res = await session.execute(select(ConfigEntry).where(ConfigEntry.key == "app_config"))
    entry = res.scalar_one_or_none()
    if entry:
        entry.value = json.dumps(get_config())
    else:
        entry = ConfigEntry(key="app_config", value=json.dumps(get_config()))
        session.add(entry)
    await session.commit()
