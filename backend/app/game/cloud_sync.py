import json
import logging
import urllib.request
import asyncio
from ..config import get_config

log = logging.getLogger(__name__)

# Ubicación por defecto de la máquina recreativa
MACHINE_ZONE = "Buenos Aires, AR"

async def detect_machine_location() -> None:
    """Detecta la ubicación de la máquina recreativa por IP al arrancar."""
    global MACHINE_ZONE
    def _fetch():
        try:
            req = urllib.request.Request(
                "http://ip-api.com/json",
                headers={"User-Agent": "Mozilla/5.0"}
            )
            with urllib.request.urlopen(req, timeout=4) as response:
                data = json.loads(response.read().decode())
                if data.get("status") == "success":
                    return f"{data.get('city')}, {data.get('region')}"
        except Exception as e:
            log.warning("No se pudo geolocalizar la máquina por IP: %s", e)
        return "Buenos Aires, AR"

    MACHINE_ZONE = await asyncio.to_thread(_fetch)
    log.info("Ubicación geográfica de la máquina configurada en: %s", MACHINE_ZONE)


async def sync_scores_to_supabase(players: list, mode: str) -> None:
    """Envía los resultados de la partida de forma asíncrona a Supabase Cloud."""
    cfg = get_config().get("supabase", {})
    if not cfg.get("enabled"):
        log.info("Sincronización Supabase desactivada.")
        return

    url = f"{cfg.get('url')}/rest/v1/rankings"
    anon_key = cfg.get("anon_key")

    # Mapear los scores de los jugadores
    payload = []
    for p in players:
        name = p.name or f"Jugador {p.index + 1}"
        is_guest = not hasattr(p, "google_id") or p.google_id is None
        google_id = getattr(p, "google_id", None)
        
        payload.append({
            "arcade_id": "FUTSPO_01",
            "player_name": name,
            "score": p.score,
            "zone": MACHINE_ZONE,
            "is_guest": is_guest,
            "google_id": google_id or None
        })

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
        except Exception as e:
            log.error("Error al sincronizar partida con Supabase: %s", e)

    # Ejecutar de forma asíncrona no bloqueante
    asyncio.create_task(asyncio.to_thread(_send))
