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
    def __init__(self, on_event: EventHandler, on_status_change: Callable[[bool], Awaitable[None]] = None):
        self._on_event  = on_event
        self._on_status_change = on_status_change
        self._serial    = None
        self._running   = False
        self.hardware_ws = None  # Para el modo híbrido en la nube
        self._last_connected = False

    def is_connected(self) -> bool:
        cfg = get_config()
        if cfg.get("cloud_mode"):
            return self.hardware_ws is not None
        if cfg.get("serial", {}).get("mock"):
            return True
        return self._serial is not None and self._serial.is_open

    async def _update_status(self, connected: bool) -> None:
        if connected != self._last_connected:
            self._last_connected = connected
            if self._on_status_change:
                try:
                    await self._on_status_change(connected)
                except Exception as e:
                    log.warning("Error in on_status_change callback: %s", e)

    async def start(self) -> None:
        cfg = get_config()
        self._running = True
        if cfg.get("cloud_mode"):
            log.info("Serial bridge iniciado en modo NUBE (esperando WebSocket /ws/hardware)")
            await self._update_status(False)
            return
        serial_cfg = cfg["serial"]
        if serial_cfg.get("mock"):
            log.info("Serial bridge en modo MOCK")
            await self._update_status(True)
            asyncio.create_task(self._mock_loop())
        else:
            await self._update_status(False)
            asyncio.create_task(self._serial_loop(serial_cfg["port"], serial_cfg["baud"]))

    async def stop(self) -> None:
        self._running = False
        if self._serial:
            self._serial.close()
        await self._update_status(False)

    async def send(self, cmd: dict) -> None:
        """Envía comando al ESP32."""
        data = json.dumps(cmd)
        if self.hardware_ws:
            try:
                await self.hardware_ws.send_text(data)
            except Exception as e:
                log.warning("Hardware WebSocket write error: %s", e)
        elif self._serial:
            try:
                self._serial.write((data + "\n").encode())
            except Exception as e:
                log.warning("Serial write error: %s", e)
        else:
            log.debug("Mock/Nube sin cliente send: %s", data)

    # ── loops ─────────────────────────────────────────────────────────────────

    def _detect_port(self) -> str:
        import serial.tools.list_ports
        try:
            ports = list(serial.tools.list_ports.comports())
            # Buscar descriptores comunes de placas de desarrollo ESP32 (CP210x o CH340)
            for p in ports:
                desc = p.description.lower()
                if "cp210" in desc or "ch340" in desc or "usb-to-serial" in desc or "usb to uart" in desc or "ch341" in desc:
                    log.info("🎯 ESP32 Auto-detectado: %s (%s)", p.device, p.description)
                    return p.device
            # Si no se detecta un ESP32 por descriptor, NO usar un fallback genérico.
            # COM1 u otros puertos de sistema no son el ESP32 y causarían problemas.
            log.warning("No se detectó ningún puerto ESP32 (CP210x/CH340). Reintentando en 3s...")
        except Exception as e:
            log.warning("Fallo al listar puertos serie para auto-detección: %s", e)
        return None

    async def _serial_loop(self, port: str, baud: int) -> None:
        import serial
        while self._running:
            active_port = port
            try:
                detected = self._detect_port()
                if detected:
                    active_port = detected
                else:
                    await self._update_status(False)
                    # Si no se encontró el ESP32, esperar antes de volver a buscar
                    await asyncio.sleep(3)
                    continue
                self._serial = serial.Serial(active_port, baud, timeout=1)
                log.info("Serial abierto con éxito: %s @ %d", active_port, baud)
                await self._update_status(True)
                try:
                    while self._running:
                        line = await asyncio.get_event_loop().run_in_executor(
                            None, self._serial.readline)
                        if line:
                            await self._parse(line.decode().strip())
                finally:
                    # Asegurar que el puerto se cierre siempre al salir del loop interno
                    try:
                        self._serial.close()
                    except Exception:
                        pass
                    self._serial = None
                    await self._update_status(False)
            except Exception as e:
                log.error("Serial error (Puerto: %s): %s — reintentando en 3s", active_port, e)
                if self._serial:
                    try:
                        self._serial.close()
                    except Exception:
                        pass
                    self._serial = None
                await self._update_status(False)
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
        import os
        while self._running:
            sleep_time = random.uniform(60, 120) if os.getenv("SLOW_MOCK") else random.uniform(4, 8)
            await asyncio.sleep(sleep_time)
            if zones:
                zone = random.choice(zones)
                # 1. Simular impacto en sensor
                await self._on_event({"t": "sensor", "id": zone})
                
                # 2. Esperar 1.8 segundos y simular salida de bola del campo (consume ball / cambio de turno)
                await asyncio.sleep(1.8)
                await self._on_event({"t": "ball"})
