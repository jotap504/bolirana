import logging
from contextlib import asynccontextmanager
from dotenv import load_dotenv

# Cargar variables de entorno del archivo .env
load_dotenv()

from fastapi import FastAPI
from fastapi.staticfiles import StaticFiles
from fastapi.responses import FileResponse
from pathlib import Path

from app.database   import init_db
from app.ws_manager import WSManager
from app.game.engine import GameEngine
from app.hardware.serial_bridge import SerialBridge
from app.routers import game, admin, payment

logging.basicConfig(level=logging.INFO,
    format="%(asctime)s %(levelname)s %(name)s: %(message)s")
log = logging.getLogger(__name__)

STATIC = Path(__file__).parent / "static"


@asynccontextmanager
async def lifespan(app: FastAPI):
    await init_db()
    app.state.ws     = WSManager()
    app.state.engine = GameEngine(broadcast=app.state.ws.broadcast)
    app.state.bridge = SerialBridge(on_event=_handle_hw_event(app))
    await app.state.bridge.start()
    
    # Geolocalización asíncrona por IP de la máquina física al arrancar
    try:
        import asyncio
        from app.game.cloud_sync import detect_machine_location
        asyncio.create_task(detect_machine_location())
    except Exception as e:
        log.error("Fallo al iniciar geolocalización por IP: %s", e)
        
    log.info("Bolirana backend iniciado")
    yield
    await app.state.bridge.stop()
    log.info("Bolirana backend detenido")


def _handle_hw_event(app):
    async def _handler(msg: dict) -> None:
        engine = app.state.engine
        t = msg.get("t")
        if t == "sensor":
            await engine.handle_sensor(msg["id"])
        elif t == "coin":
            await engine.handle_coin(msg.get("count", 1))
        elif t == "btn":
            await engine.handle_button(msg["id"])
        elif t == "ball":
            await engine.handle_ball_consumed()
    return _handler


app = FastAPI(title="Bolirana Arcade", lifespan=lifespan)

app.include_router(game.router)
app.include_router(admin.router)
app.include_router(payment.router)

# Servir frontends estáticos
app.mount("/display", StaticFiles(directory=STATIC / "display", html=True), name="display")
app.mount("/player",  StaticFiles(directory=STATIC / "player",  html=True), name="player")
app.mount("/admin",   StaticFiles(directory=STATIC / "admin",   html=True), name="admin_ui")

@app.get("/")
async def root():
    return FileResponse(STATIC / "display" / "index.html")
