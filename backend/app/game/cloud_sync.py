import json
import logging
import urllib.request
import urllib.error
import asyncio
from ..config import get_config

log = logging.getLogger(__name__)

# Ubicación por defecto de la máquina recreativa
MACHINE_ZONE = "Buenos Aires"
MACHINE_LAT = -34.6037
MACHINE_LON = -58.3816

async def detect_machine_location() -> None:
    """Detecta la ubicación de la máquina recreativa por IP al arrancar y la registra en la nube."""
    global MACHINE_ZONE, MACHINE_LAT, MACHINE_LON
    from datetime import datetime
    
    def _fetch():
        try:
            req = urllib.request.Request(
                "http://ip-api.com/json",
                headers={"User-Agent": "Mozilla/5.0"}
            )
            with urllib.request.urlopen(req, timeout=4) as response:
                data = json.loads(response.read().decode())
                if data.get("status") == "success":
                    return {
                        "zone": data.get("regionName", "Buenos Aires"),
                        "lat": float(data.get("lat", -34.6037)),
                        "lon": float(data.get("lon", -58.3816))
                    }
        except Exception as e:
            log.warning("No se pudo geolocalizar la máquina por IP: %s", e)
        return {"zone": "Buenos Aires", "lat": -34.6037, "lon": -58.3816}

    result = await asyncio.to_thread(_fetch)
    MACHINE_ZONE = result["zone"]
    MACHINE_LAT = result["lat"]
    MACHINE_LON = result["lon"]
    log.info("Ubicación geográfica de la máquina configurada en: %s (Lat: %s, Lon: %s)", MACHINE_ZONE, MACHINE_LAT, MACHINE_LON)

    # Registrar la máquina en Supabase
    cfg = get_config().get("supabase", {})
    if cfg.get("enabled"):
        arcade_id = get_config().get("arcade_id", "FUTSPO_01")
        url = f"{cfg.get('url')}/rest/v1/machines"
        anon_key = cfg.get("anon_key")
        
        payload = {
            "arcade_id": arcade_id,
            "name": arcade_id,
            "latitude": MACHINE_LAT,
            "longitude": MACHINE_LON,
            "zone": MACHINE_ZONE,
            "updated_at": datetime.utcnow().isoformat() + "Z"
        }
        
        def _register():
            try:
                # Upsert de PostgREST
                req = urllib.request.Request(
                    url,
                    data=json.dumps(payload).encode("utf-8"),
                    headers={
                        "Content-Type": "application/json",
                        "apikey": anon_key,
                        "Authorization": f"Bearer {anon_key}",
                        "Prefer": "resolution=merge-duplicates"
                    },
                    method="POST"
                )
                with urllib.request.urlopen(req, timeout=5) as response:
                    log.info("Máquina registrada en Supabase Cloud. Status: %s", response.status)
            except Exception as e:
                log.error("Error al registrar máquina en Supabase: %s", e)
                
        asyncio.create_task(asyncio.to_thread(_register))


async def sync_scores_to_supabase(players: list, mode: str) -> None:
    """Envía los resultados de la partida de forma asíncrona a Supabase Cloud."""
    cfg = get_config().get("supabase", {})
    if not cfg.get("enabled"):
        log.info("Sincronización Supabase desactivada.")
        return

    url = f"{cfg.get('url')}/rest/v1/rankings"
    anon_key = cfg.get("anon_key")
    arcade_id = get_config().get("arcade_id", "FUTSPO_01")

    # Mapear los scores de los jugadores
    payload = []
    game_cfg = get_config().get("game", {})
    is_goleador = str(mode) == "goleador"

    if is_goleador:
        # En modo GOLEADOR: el score para ranking = bolas_embocadas × puntos_por_bola
        points_per_ball = game_cfg.get("goleador_points_per_ball", 500)
        max_balls = max((getattr(p, "balls_pocketed", 0) for p in players), default=0) if players else 0
    else:
        max_score = max((p.score for p in players), default=0) if players else 0

    for p in players:
        google_id = getattr(p, "google_id", None)
        is_guest = not hasattr(p, "google_id") or google_id is None

        # Sincronizar en rankings solo si es un jugador registrado
        if not is_guest and google_id is not None:
            name = p.name or f"Jugador {p.index + 1}"
            avatar = getattr(p, "avatar", None)
            balls = getattr(p, "balls_pocketed", 0)

            if is_goleador:
                # Score para ranking = bolas embocadas × puntos configurados
                sync_score = balls * points_per_ball
                is_winner = (len(players) == 1) or (balls == max_balls and max_balls >= 0)
            else:
                sync_score = p.score
                is_winner = (len(players) == 1) or (p.score == max_score and max_score > 0)

            payload.append({
                "arcade_id":   arcade_id,
                "player_name": name,
                "score":       sync_score,
                "zone":        MACHINE_ZONE,
                "is_guest":    False,
                "google_id":   google_id,
                "avatar_url":  avatar or None,
                "is_winner":   is_winner,
            })

    if not payload:
        log.info("Sincronización Supabase omitida: no hay jugadores registrados en la partida.")
        return


    def _send():
        try:
            req = urllib.request.Request(
                url,
                data=json.dumps(payload).encode("utf-8"),
                headers={
                    "Content-Type": "application/json",
                    "apikey": anon_key,
                    "Authorization": f"Bearer {anon_key}",
                    "Prefer": "return=minimal"
                },
                method="POST"
            )
            with urllib.request.urlopen(req, timeout=5) as response:
                log.info("Partida sincronizada con Supabase. Status: %s", response.status)
        except urllib.error.HTTPError as e:
            err_body = e.read().decode("utf-8")
            log.error("Error HTTP al sincronizar partida con Supabase (%s): %s", e.code, err_body)
        except Exception as e:
            log.error("Error al sincronizar partida con Supabase: %s", e)

    # Ejecutar de forma asíncrona no bloqueante
    asyncio.create_task(asyncio.to_thread(_send))
