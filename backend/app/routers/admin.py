import json
from fastapi import APIRouter, Request, HTTPException, Depends
from ..config import get_config, update_config, save_config_to_db
from ..database import get_db
from sqlalchemy.ext.asyncio import AsyncSession
import logging

log = logging.getLogger(__name__)
router = APIRouter(prefix="/api/admin")


@router.get("/config")
async def read_config():
    return get_config()


@router.patch("/config")
async def patch_config(request: Request, db: AsyncSession = Depends(get_db)):
    body = await request.json()
    update_config(body)
    await save_config_to_db(db)
    return get_config()


@router.post("/config/rename-machine")
async def rename_machine(request: Request, db: AsyncSession = Depends(get_db)):
    from datetime import datetime
    import urllib.request
    
    body = await request.json()
    new_id = body.get("arcade_id", "").strip()
    if not new_id:
        raise HTTPException(status_code=400, detail="El identificador no puede estar vacío.")
    
    current_config = get_config()
    old_id = current_config.get("arcade_id", "FUTSPO_01")
    
    if new_id == old_id:
        return {"status": "success", "message": "El nombre es el mismo."}
        
    cfg = current_config.get("supabase", {})
    if cfg.get("enabled"):
        url = cfg.get("url")
        anon_key = cfg.get("anon_key")
        
        # 1. Verificar si new_id ya existe en Supabase
        check_url = f"{url}/rest/v1/machines?arcade_id=eq.{new_id}&select=arcade_id"
        try:
            req = urllib.request.Request(
                check_url,
                headers={
                    "apikey": anon_key,
                    "Authorization": f"Bearer {anon_key}"
                }
            )
            with urllib.request.urlopen(req, timeout=5) as response:
                machines = json.loads(response.read().decode('utf-8'))
                if machines:
                    raise HTTPException(status_code=400, detail="Ese nombre de máquina ya está en uso.")
        except HTTPException as he:
            raise he
        except Exception as e:
            log.error("Error al verificar unicidad de máquina en Supabase: %s", e)
            raise HTTPException(status_code=500, detail="Error de comunicación con el servidor cloud.")

        # 2. Registrar la nueva máquina en Supabase
        from app.game.cloud_sync import MACHINE_LAT, MACHINE_LON, MACHINE_ZONE
        register_url = f"{url}/rest/v1/machines"
        payload = {
            "arcade_id": new_id,
            "name": new_id,
            "latitude": MACHINE_LAT,
            "longitude": MACHINE_LON,
            "zone": MACHINE_ZONE,
            "updated_at": datetime.utcnow().isoformat() + "Z"
        }
        
        try:
            req = urllib.request.Request(
                register_url,
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
                pass
        except Exception as e:
            log.error("Error al crear nueva máquina en Supabase: %s", e)
            raise HTTPException(status_code=500, detail="No se pudo registrar la nueva máquina en la nube.")

        # 3. Actualizar los rankings anteriores en Supabase para vincularlos al nuevo ID
        rankings_url = f"{url}/rest/v1/rankings?arcade_id=eq.{old_id}"
        try:
            req = urllib.request.Request(
                rankings_url,
                data=json.dumps({"arcade_id": new_id}).encode("utf-8"),
                headers={
                    "Content-Type": "application/json",
                    "apikey": anon_key,
                    "Authorization": f"Bearer {anon_key}"
                },
                method="PATCH"
            )
            with urllib.request.urlopen(req, timeout=5) as response:
                pass
        except Exception as e:
            log.warning("No se pudieron migrar los rankings anteriores en Supabase: %s", e)

        # 4. Eliminar el registro de la vieja máquina en Supabase
        delete_url = f"{url}/rest/v1/machines?arcade_id=eq.{old_id}"
        try:
            req = urllib.request.Request(
                delete_url,
                headers={
                    "apikey": anon_key,
                    "Authorization": f"Bearer {anon_key}"
                },
                method="DELETE"
            )
            with urllib.request.urlopen(req, timeout=5) as response:
                pass
        except Exception as e:
            log.warning("No se pudo eliminar la vieja máquina de Supabase: %s", e)

    # 5. Guardar el nuevo id en la configuración local y persistir en SQLite
    update_config({"arcade_id": new_id})
    await save_config_to_db(db)
    
    return {"status": "success", "arcade_id": new_id}


@router.get("/config/sensors")
async def get_sensors():
    return get_config()["sensors"]


@router.patch("/config/sensors")
async def patch_sensors(request: Request, db: AsyncSession = Depends(get_db)):
    sensors = await request.json()
    update_config({"sensors": sensors})
    await save_config_to_db(db)
    return get_config()["sensors"]


@router.get("/config/pricing")
async def get_pricing():
    return get_config()["pricing"]


@router.patch("/config/pricing")
async def patch_pricing(request: Request, db: AsyncSession = Depends(get_db)):
    body = await request.json()
    update_config({"pricing": body})
    await save_config_to_db(db)
    return get_config()["pricing"]


@router.get("/config/game")
async def get_game_cfg():
    return get_config()["game"]


@router.patch("/config/game")
async def patch_game_cfg(request: Request, db: AsyncSession = Depends(get_db)):
    body = await request.json()
    update_config({"game": body})
    await save_config_to_db(db)
    return get_config()["game"]


@router.get("/stats")
async def get_stats(request: Request):
    session = request.app.state.engine.session
    return {
        "state":         session.state,
        "players":       [p.to_dict() for p in session.players],
        "display_on":    request.app.state.ws.is_display_connected(),
        "phones_connected": request.app.state.ws.player_count(),
    }


@router.post("/hardware/command")
async def send_hardware_command(request: Request):
    body = await request.json()
    try:
        await request.app.state.bridge.send(body)
        return {"status": "ok", "command": body}
    except Exception as e:
        raise HTTPException(status_code=500, detail=str(e))
