"""
Bridge entre el backend y el ESP32 via USB Serial.
En modo mock genera eventos de prueba para desarrollo sin hardware.
"""
import asyncio, json, logging, random
from typing import Callable, Awaitable
from ..config import get_config

log = logging.getLogger(__name__)
EventHandler = Callable[[dict], Awaitable[None]]


class SerialBridge:
    def __init__(self, on_event: EventHandler):
        self._on_event  = on_event
        self._serial    = None
        self._running   = False

    async def start(self) -> None:
        cfg = get_config()["serial"]
        self._running = True
        if cfg.get("mock"):
            log.info("Serial bridge en modo MOCK")
            asyncio.create_task(self._mock_loop())
        else:
            asyncio.create_task(self._serial_loop(cfg["port"], cfg["baud"]))

    async def stop(self) -> None:
        self._running = False
        if self._serial:
            self._serial.close()

    async def send(self, cmd: dict) -> None:
        """Envía comando al ESP32."""
        data = json.dumps(cmd) + "\n"
        if self._serial:
            try:
                self._serial.write(data.encode())
            except Exception as e:
                log.warning("Serial write error: %s", e)
        else:
            log.debug("Mock send: %s", data.strip())

    # ── loops ─────────────────────────────────────────────────────────────────

    async def _serial_loop(self, port: str, baud: int) -> None:
        import serial
        while self._running:
            try:
                self._serial = serial.Serial(port, baud, timeout=1)
                log.info("Serial abierto: %s @ %d", port, baud)
                while self._running:
                    line = await asyncio.get_event_loop().run_in_executor(
                        None, self._serial.readline)
                    if line:
                        await self._parse(line.decode().strip())
            except Exception as e:
                log.error("Serial error: %s — reintentando en 3s", e)
                await asyncio.sleep(3)

    async def _parse(self, line: str) -> None:
        try:
            msg = json.loads(line)
            await self._on_event(msg)
        except json.JSONDecodeError:
            log.warning("Serial malformed: %s", line)

    async def _mock_loop(self) -> None:
        """Genera eventos de prueba periódicos y simula salidas de bola."""
        cfg = get_config()
        zones = [s["id"] for s in cfg["sensors"] if s.get("enabled")]
        while self._running:
            await asyncio.sleep(random.uniform(4, 8))
            if zones:
                zone = random.choice(zones)
                # 1. Simular impacto en sensor
                await self._on_event({"t": "sensor", "id": zone})
                
                # 2. Esperar 1.8 segundos y simular salida de bola del campo (consume ball / cambio de turno)
                await asyncio.sleep(1.8)
                await self._on_event({"t": "ball"})
