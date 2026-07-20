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
    import copy
    cfg = copy.deepcopy(get_config())
    # Enmascarar el access_token de mercadopago si existe en base de datos
    if "mercadopago" in cfg and cfg["mercadopago"].get("access_token"):
        from ..payments.crypto import decrypt_data
        decrypted = decrypt_data(cfg["mercadopago"]["access_token"])
        if decrypted:
            if len(decrypted) > 15:
                cfg["mercadopago"]["access_token"] = f"{decrypted[:10]}...****************"
            else:
                cfg["mercadopago"]["access_token"] = "****************"
        else:
            cfg["mercadopago"]["access_token"] = ""
    return cfg


async def push_radar_config_to_hardware(app) -> None:
    """Envía la configuración de proximidad del radar al hardware (ESP32) si está conectado."""
    cfg = get_config().get("anti_cheat", {})
    cmd = {
        "type": "config_proximity",
        "min_dist": int(cfg.get("min_distance_cm", 100)),
        "max_dist": int(cfg.get("max_distance_cm", 250)),
        "moving_th": int(cfg.get("moving_threshold", 55)),
        "static_th": int(cfg.get("static_threshold", 35)),
        "trigger_ms": int(cfg.get("trigger_delay_ms", 1000))
    }
    if hasattr(app, "state") and hasattr(app.state, "bridge") and app.state.bridge.is_connected():
        await app.state.bridge.send(cmd)
        log.info("Configuración de radar enviada al hardware: %s", cmd)


async def push_motor_config_to_hardware(app) -> None:
    """Envía la configuración del motor dispensador al hardware (ESP32) si está conectado."""
    cfg = get_config().get("motor", {})
    cmd = {
        "type": "config_motor",
        "speed": float(cfg.get("speed", 800.0)),
        "accel": float(cfg.get("acceleration", 400.0)),
        "dir": int(cfg.get("direction", 1)),
        "extra_steps": int(cfg.get("extra_steps", 150))
    }
    if hasattr(app, "state") and hasattr(app.state, "bridge") and app.state.bridge.is_connected():
        await app.state.bridge.send(cmd)
        log.info("Configuración de motor enviada al hardware: %s", cmd)


@router.patch("/config")
async def patch_config(request: Request, db: AsyncSession = Depends(get_db)):
    body = await request.json()
    
    # Procesamiento seguro de access_token de MercadoPago
    if "mercadopago" in body and "access_token" in body["mercadopago"]:
        token = body["mercadopago"]["access_token"]
        
        # Si el token enviado contiene asteriscos, significa que el usuario no lo modificó.
        # No sobreescribimos el valor que ya está guardado de forma segura en la base de datos.
        if "******" in token or token.endswith("****************") or "Placeholder" in token:
            del body["mercadopago"]["access_token"]
        else:
            # Token nuevo: viene encriptado en RSA desde la interfaz (tránsito seguro).
            # Lo desencriptamos con la clave privada RSA y lo guardamos encriptado con Fernet (seguro en reposo).
            if token.strip():
                try:
                    from ..payments.crypto import decrypt_rsa_payload, encrypt_data
                    decrypted_token = decrypt_rsa_payload(token)
                    encrypted_token = encrypt_data(decrypted_token)
                    body["mercadopago"]["access_token"] = encrypted_token
                except Exception as e:
                    raise HTTPException(status_code=400, detail=f"Fallo al desencriptar el token de MercadoPago: {str(e)}")
            else:
                body["mercadopago"]["access_token"] = ""

    update_config(body)
    await save_config_to_db(db)
    
    # Si se actualizó la configuración anti-trampas, enviarla al hardware (ESP32)
    if "anti_cheat" in body:
        try:
            await push_radar_config_to_hardware(request.app)
        except Exception as e:
            log.warning("No se pudo enviar la configuración del radar al hardware: %s", e)

    # Si se actualizó la configuración del motor dispensador, enviarla al hardware (ESP32)
    if "motor" in body:
        try:
            await push_motor_config_to_hardware(request.app)
        except Exception as e:
            log.warning("No se pudo enviar la configuración del motor al hardware: %s", e)
    
    # Devolver respuesta con el token enmascarado
    import copy
    cfg = copy.deepcopy(get_config())
    if "mercadopago" in cfg and cfg["mercadopago"].get("access_token"):
        from ..payments.crypto import decrypt_data
        decrypted = decrypt_data(cfg["mercadopago"]["access_token"])
        if decrypted:
            if len(decrypted) > 15:
                cfg["mercadopago"]["access_token"] = f"{decrypted[:10]}...****************"
            else:
                cfg["mercadopago"]["access_token"] = "****************"
        else:
            cfg["mercadopago"]["access_token"] = ""
    return cfg


@router.get("/crypto-key")
async def get_crypto_key():
    """Sirve la clave pública RSA (SPKI) para la encriptación asimétrica en el frontend."""
    from ..payments.crypto import get_rsa_public_pem
    return {"public_key": get_rsa_public_pem()}


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
    # Transmitir el nuevo estado / volumen de forma inmediata a las pantallas conectadas
    engine = request.app.state.engine
    await engine._broadcast({"type": "state", **engine.session.to_dict()})
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


from fastapi import UploadFile, File
from pathlib import Path
import os
import shutil

AUDIOS_DIR = Path(__file__).parent.parent.parent.parent / "audios"
ATTRACT_DIR = AUDIOS_DIR / "attract"

@router.get("/attract/audios")
async def get_attract_audios():
    if not ATTRACT_DIR.exists():
        return []
    try:
        files = [f.name for f in ATTRACT_DIR.iterdir() if f.is_file() and f.suffix.lower() in ('.mp3', '.wav', '.ogg')]
        files.sort()
        return files
    except Exception as e:
        log.error("Error listando audios de atraccion: %s", e)
        raise HTTPException(status_code=500, detail="Error al listar archivos de audio.")

@router.post("/attract/upload")
async def upload_attract_audio(file: UploadFile = File(...)):
    # Crear carpeta si no existe
    ATTRACT_DIR.mkdir(parents=True, exist_ok=True)
    
    # Contar archivos existentes
    existing_files = [f for f in ATTRACT_DIR.iterdir() if f.is_file() and f.suffix.lower() in ('.mp3', '.wav', '.ogg')]
    if len(existing_files) >= 10:
        raise HTTPException(status_code=400, detail="Límite alcanzado: Máximo 10 archivos de audio.")
        
    # Validar extensión
    ext = Path(file.filename).suffix.lower()
    if ext not in ('.mp3', '.wav', '.ogg'):
        raise HTTPException(status_code=400, detail="Formato no soportado. Subir archivos .mp3, .wav o .ogg.")
        
    # Limpiar nombre de archivo (reemplazar espacios y caracteres raros)
    import re
    clean_name = re.sub(r'[^a-zA-Z0-9_\-\.]', '', file.filename.replace(' ', '_'))
    if not clean_name:
        clean_name = f"attract_{len(existing_files) + 1}{ext}"
    target_path = ATTRACT_DIR / clean_name
    
    # Guardar archivo
    try:
        with open(target_path, "wb") as f:
            shutil.copyfileobj(file.file, f)
        return {"status": "success", "filename": clean_name}
    except Exception as e:
        log.error("Error guardando archivo subido: %s", e)
        raise HTTPException(status_code=500, detail="No se pudo guardar el archivo de audio.")

@router.delete("/attract/audios/{filename}")
async def delete_attract_audio(filename: str):
    # Prevenir path traversal
    if ".." in filename or "/" in filename or "\\" in filename:
        raise HTTPException(status_code=400, detail="Nombre de archivo inválido.")
        
    target_path = ATTRACT_DIR / filename
    if not target_path.exists():
        raise HTTPException(status_code=404, detail="El archivo no existe.")
        
    try:
        os.remove(target_path)
        return {"status": "success", "message": f"Archivo {filename} eliminado."}
    except Exception as e:
        log.error("Error eliminando archivo: %s", e)
        raise HTTPException(status_code=500, detail="No se pudo eliminar el archivo de audio.")
