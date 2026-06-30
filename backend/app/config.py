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
        "volume": 80,
    },
    "pricing": {
        "base_credits_per_player": 1,
        "mode_extra": {"classic": 0, "timed": 0, "team": 0, "goleador": 0},
        "coin_to_credits": 1,
        "pesos_per_credit": 200,
        "group_discount": {},
    },
    "sensors": [
        {"id": "rana", "name": "Rana", "points": 1000, "enabled": True},
        {"id": "sapo", "name": "Sapo", "points": 500, "enabled": True},
        {"id": "fosa_1", "name": "Fosa 1", "points": 100, "enabled": True},
        {"id": "fosa_2", "name": "Fosa 2", "points": 50, "enabled": True},
        {"id": "fosa_3", "name": "Fosa 3", "points": 20, "enabled": True},
        {"id": "fosa_4", "name": "Fosa 4", "points": 10, "enabled": True},
        {"id": "fosa_5", "name": "Fosa 5", "points": 10, "enabled": True},
        {"id": "fosa_6", "name": "Fosa 6", "points": 10, "enabled": True},
        {"id": "fosa_7", "name": "Fosa 7", "points": 10, "enabled": True},
        {"id": "fosa_8", "name": "Fosa 8", "points": 10, "enabled": True},
        {"id": "fosa_9", "name": "Fosa 9", "points": 10, "enabled": True},
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
    "attract_players": {
        "enabled": False,
        "idle_timeout_seconds": 60,
        "cooldown_seconds": 30,
        "volume": 80,
    },
    "anti_cheat": {
        "front_enabled": True,
        "left_enabled": False,
        "right_enabled": False,
        "alert_duration_seconds": 3,
        "invalidate_throws": False,
    },
    "promotions": [
        {
            "id": "happy_hour",
            "title": "HAPPY HOUR FUTSAPO",
            "desc": "2x1 en créditos de juego de lunes a jueves de 18:00 a 20:00 hs.",
            "badge": "2x1 CRÉDITOS",
            "icon": "⚡",
            "days": [1, 2, 3, 4],
            "start_hour": 18,
            "end_hour": 20
        },
        {
            "id": "crazy_wednesday",
            "title": "MIÉRCOLES LOCOS",
            "desc": "Todos los miércoles, obtené un 25% de descuento en la carga de fichas.",
            "badge": "25% OFF FICHAS",
            "icon": "🎉",
            "days": [3],
            "start_hour": 0,
            "end_hour": 24
        },
        {
            "id": "late_night",
            "title": "TRASNOCHE CYBER",
            "desc": "Viernes y sábados de 23:00 a 02:00 hs, jugá 3 partidas por sólo $500.",
            "badge": "DESCUENTO DE NOCHE",
            "icon": "🌙",
            "days": [5, 6, 0],
            "start_hour": 23,
            "end_hour": 2
        },
        {
            "id": "weekend_champions",
            "title": "FIN DE SEMANA",
            "desc": "Sábados y domingos sumás el doble para el Ranking Provincial y Global.",
            "badge": "DOBLE PUNTOS RANKING",
            "icon": "🏆",
            "days": [6, 0],
            "start_hour": 0,
            "end_hour": 24
        }
    ]
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

# Trigger reload
