import logging
from contextlib import asynccontextmanager
from dotenv import load_dotenv

# Cargar variables de entorno del archivo .env
load_dotenv()

from fastapi import FastAPI
from fastapi.staticfiles import StaticFiles
from fastapi.responses import FileResponse, RedirectResponse
from pathlib import Path

from app.database   import init_db
from app.ws_manager import WSManager
from app.game.engine import GameEngine
from app.hardware.serial_bridge import SerialBridge
from app.routers import game, admin, payment, system

logging.basicConfig(level=logging.INFO,
    format="%(asctime)s %(levelname)s %(name)s: %(message)s")
log = logging.getLogger(__name__)

STATIC = Path(__file__).parent / "static"
IMAGENES = Path(__file__).parent.parent / "imagenes"
AUDIOS = Path(__file__).parent.parent / "audios"



@asynccontextmanager
async def lifespan(app: FastAPI):
    try:
        await init_db()
        
        # Cargar configuración desde base de datos SQLite
        from app.database import AsyncSessionLocal
        from app.models import ConfigEntry
        from app.config import update_config, get_config
        from sqlalchemy import select
        import json
        import random
        
        async with AsyncSessionLocal() as session:
            res = await session.execute(select(ConfigEntry).where(ConfigEntry.key == "app_config"))
            entry = res.scalar_one_or_none()
            if entry:
                try:
                    patch = json.loads(entry.value)
                    
                    # Asegurar que todos los sensores de DEFAULT_CONFIG existan en la BD
                    from app.config import DEFAULT_CONFIG
                    db_sensors = patch.get("sensors", [])
                    db_sensor_ids = {s["id"] for s in db_sensors}
                    
                    modified = False
                    for default_s in DEFAULT_CONFIG["sensors"]:
                        if default_s["id"] not in db_sensor_ids:
                            # Insertar antes de 'cero' si es posible para mantener el orden
                            cero_idx = next((idx for idx, s in enumerate(db_sensors) if s["id"] == "cero"), -1)
                            if cero_idx != -1:
                                db_sensors.insert(cero_idx, default_s)
                            else:
                                db_sensors.append(default_s)
                            db_sensor_ids.add(default_s["id"])
                            modified = True
                            
                    if modified:
                        patch["sensors"] = db_sensors
                        entry.value = json.dumps(patch)
                        session.add(entry)
                        await session.commit()
                        log.info("Base de datos de configuración auto-migrada con nuevos sensores.")
                        
                    update_config(patch)
                    log.info("Configuración cargada desde base de datos local SQLite.")
                except Exception as e:
                    log.error("Error al decodificar la configuración de la base de datos: %s", e)
            else:
                # Primer inicio: Generar identificador de máquina único
                rand_num = random.randint(10000, 99999)
                default_arcade_id = f"FUTSPO_{rand_num}"
                update_config({"arcade_id": default_arcade_id})
                
                # Guardar en SQLite
                new_entry = ConfigEntry(key="app_config", value=json.dumps(get_config()))
                session.add(new_entry)
                await session.commit()
                log.info("Primer inicio: se generó el identificador de máquina único: %s", default_arcade_id)

        # Inicializar audios por defecto desde /audios/llamados/ hacia /audios/attract/ si está vacío
        try:
            import shutil
            import re
            llamados_dir = Path(__file__).parent.parent / "audios" / "llamados"
            attract_dir = Path(__file__).parent.parent / "audios" / "attract"
            attract_dir.mkdir(parents=True, exist_ok=True)
            existing_attract = [f for f in attract_dir.iterdir() if f.is_file() and f.suffix.lower() in ('.mp3', '.wav', '.ogg')]
            if not existing_attract and llamados_dir.exists():
                for f in llamados_dir.iterdir():
                    if f.is_file() and f.suffix.lower() in ('.mp3', '.wav', '.ogg'):
                        clean_name = re.sub(r'[^a-zA-Z0-9_\-\.]', '', f.name.replace(' ', '_'))
                        shutil.copy2(f, attract_dir / clean_name)
                log.info("Audios por defecto copiados desde 'llamados' a 'attract'.")
        except Exception as ae:
            log.error("Error al copiar audios por defecto: %s", ae)

    except Exception as err:
        log.error("Fallo critico durante el arranque de la aplicacion: %s", err, exc_info=True)
        print(f"!!! STARTUP ERROR: {err}")
        import traceback
        traceback.print_exc()
        raise err

    async def _handle_hw_status(connected: bool) -> None:
        await app.state.ws.broadcast({"type": "hw_status", "connected": connected})

    is_cloud = get_config().get("cloud_mode")
    app.state.ws     = WSManager()
    app.state.engine = GameEngine(broadcast=app.state.ws.broadcast)
    app.state.bridge = SerialBridge(
        on_event=_handle_hw_event(app),
        on_status_change=_handle_hw_status
    )
    await app.state.bridge.start()
    
    if not is_cloud:
        from app.game.relay_client import CloudRelayClient
        app.state.relay = CloudRelayClient(app)
        app.state.engine.relay_client = app.state.relay
        app.state.ws.relay_client = app.state.relay
        # Registrar y conectar el relay con la sesión actual
        app.state.relay.start(app.state.engine.session.session_id)
        app.state.engine.last_relay_session_id = app.state.engine.session.session_id

    # Geolocalización asíncrona por IP de la máquina física al arrancar
    try:
        import asyncio
        from app.game.cloud_sync import detect_machine_location
        asyncio.create_task(detect_machine_location())
    except Exception as e:
        log.error("Fallo al iniciar geolocalización por IP: %s", e)
        
    log.info("Bolirana backend iniciado")
    yield
    if not is_cloud and hasattr(app.state, "relay"):
        app.state.relay.stop()
    await app.state.bridge.stop()
    log.info("Bolirana backend detenido")


def _handle_hw_event(app):
    async def _handler(msg: dict) -> None:
        engine = app.state.engine
        
        # Normalizar mensajes del firmware de la ESP32
        event = msg.get("event")
        if event:
            if event == "sensor":
                msg["t"] = "sensor"
                msg["id"] = msg.get("target")
            elif event == "coin":
                msg["t"] = "coin"
                msg["count"] = msg.get("pulses", 1)
            elif event == "button":
                msg["t"] = "btn"
                msg["id"] = msg.get("name")
            elif event == "proximity":
                msg["t"] = "proximity"
                msg["active"] = msg.get("active", False)

        t = msg.get("t")
        if t == "sensor":
            await app.state.ws.send_admin({"type": "sensor_test", "sensor_id": msg["id"]})
            await engine.handle_sensor(msg["id"])
        elif t == "coin":
            await engine.handle_coin(msg.get("count", 1))
        elif t == "btn":
            await app.state.ws.broadcast({"type": "physical_button", "button_id": msg["id"]})
            await engine.handle_button(msg["id"])
        elif t == "ball":
            await engine.handle_ball_consumed(from_hardware=True)
        elif t == "proximity":
            await engine.handle_proximity(msg.get("active", False))
    return _handler


app = FastAPI(title="Bolirana Arcade", lifespan=lifespan)

app.include_router(game.router)
app.include_router(admin.router)
app.include_router(payment.router)
app.include_router(system.router)

# Servir frontends estáticos
app.mount("/boot-menu", StaticFiles(directory=STATIC / "boot-menu", html=True), name="boot_menu")
app.mount("/display", StaticFiles(directory=STATIC / "display", html=True), name="display")
app.mount("/player",  StaticFiles(directory=STATIC / "player",  html=True), name="player")
app.mount("/admin",   StaticFiles(directory=STATIC / "admin",   html=True), name="admin_ui")
app.mount("/imagenes", StaticFiles(directory=IMAGENES), name="imagenes")
app.mount("/audios", StaticFiles(directory=AUDIOS), name="audios")

@app.get("/")
async def root():
    return RedirectResponse(url="/boot-menu/")
