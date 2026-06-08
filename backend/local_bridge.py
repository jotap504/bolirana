#!/usr/bin/env python
"""
local_bridge.py
Puente de hardware local para conectar la ESP32 (Serial) con el Servidor en la Nube (WebSocket).
Debe correr únicamente en la PC de la máquina recreativa física.
"""

import asyncio
import json
import logging
import os
import sys
import serial
import serial.tools.list_ports
import websockets
from dotenv import load_dotenv

# Cargar variables locales
load_dotenv()

logging.basicConfig(
    level=logging.INFO,
    format="%(asctime)s %(levelname)s %(name)s: %(message)s"
)
log = logging.getLogger("local_bridge")

# Configuración
BAUD_RATE = int(os.getenv("SERIAL_BAUD", "115200"))
PORT_OVERRIDE = os.getenv("SERIAL_PORT", None)
# URL por defecto al servidor en la nube o local
DEFAULT_CLOUD_URL = "ws://127.0.0.1:8000/ws/hardware"
CLOUD_WS_URL = os.getenv("CLOUD_WS_URL", DEFAULT_CLOUD_URL)


def detect_serial_port() -> str:
    """Busca y detecta automáticamente el puerto serie donde se encuentra la ESP32."""
    if PORT_OVERRIDE:
        return PORT_OVERRIDE
    try:
        ports = list(serial.tools.list_ports.comports())
        # Buscar descriptores comunes de ESP32 (CP210x, CH340, CH341, USB-to-Serial, etc.)
        for p in ports:
            desc = p.description.lower()
            if any(k in desc for k in ["cp210", "ch340", "ch341", "usb-to-serial", "usb to uart", "ch341"]):
                log.info("🎯 ESP32 Auto-detectado: %s (%s)", p.device, p.description)
                return p.device
        if ports:
            log.info("🔌 Usando primer puerto USB-Serial disponible: %s (%s)", ports[0].device, ports[0].description)
            return ports[0].device
    except Exception as e:
        log.warning("Fallo al listar puertos serie: %s", e)
    return None


class LocalBridge:
    def __init__(self, ws_url: str, baud: int):
        self.ws_url = ws_url
        self.baud = baud
        self.serial_conn = None
        self.ws_conn = None
        self.running = False
        self.serial_to_ws_queue = asyncio.Queue()

    async def run(self):
        self.running = True
        log.info("Iniciando puente local hacia %s", self.ws_url)
        
        # Correr loops en paralelo
        await asyncio.gather(
            self.serial_loop(),
            self.websocket_loop(),
            self.dispatcher_loop()
        )

    async def serial_loop(self):
        """Lee mensajes del puerto serial y los pone en la cola para enviar por WS."""
        while self.running:
            port = detect_serial_port()
            if not port:
                log.error("No se detectó ningún puerto USB-Serial activo. Reintentando en 5s...")
                await asyncio.sleep(5)
                continue

            try:
                log.info("Abriendo puerto serie %s @ %d...", port, self.baud)
                self.serial_conn = serial.Serial(port, self.baud, timeout=1)
                log.info("Puerto serie %s conectado con éxito.", port)
                
                while self.running:
                    # Lectura no bloqueante delegada a un hilo ejecutor
                    line = await asyncio.get_event_loop().run_in_executor(
                        None, self.serial_conn.readline
                    )
                    if line:
                        decoded = line.decode().strip()
                        if not decoded:
                            continue
                        log.info("ESP32 ➡️ %s", decoded)
                        try:
                            # Validar que sea JSON
                            msg = json.loads(decoded)
                            await self.serial_to_ws_queue.put(decoded)
                        except json.JSONDecodeError:
                            log.warning("Línea serial ignorada (no es JSON): %s", decoded)
            except Exception as e:
                log.error("Error en puerto serie (%s): %s. Reintentando en 5s...", port, e)
                if self.serial_conn:
                    try:
                        self.serial_conn.close()
                    except Exception:
                        pass
                    self.serial_conn = None
                await asyncio.sleep(5)

    async def websocket_loop(self):
        """Mantiene la conexión WebSocket activa y procesa comandos recibidos desde la nube."""
        while self.running:
            try:
                log.info("Conectando al WebSocket de la Nube: %s...", self.ws_url)
                async with websockets.connect(self.ws_url) as ws:
                    log.info("Conexión WebSocket establecida con la Nube.")
                    self.ws_conn = ws
                    
                    while self.running:
                        raw_msg = await ws.recv()
                        log.info("Nube ➡️ ESP32: %s", raw_msg.strip())
                        
                        # Enviar el comando al puerto serial
                        if self.serial_conn and self.serial_conn.is_open:
                            try:
                                self.serial_conn.write((raw_msg + "\n").encode())
                                self.serial_conn.flush()
                            except Exception as e:
                                log.error("Error al escribir en puerto serie: %s", e)
                        else:
                            log.warning("Comando descartado: Puerto serie no conectado o cerrado.")
            except Exception as e:
                log.error("Fallo o desconexión del WebSocket: %s. Reintentando en 5s...", e)
                self.ws_conn = None
                await asyncio.sleep(5)

    async def dispatcher_loop(self):
        """Toma eventos de la cola serial y los envía por el WebSocket activo."""
        while self.running:
            data = await self.serial_to_ws_queue.get()
            sent = False
            while not sent and self.running:
                if self.ws_conn:
                    try:
                        await self.ws_conn.send(data)
                        sent = True
                    except Exception as e:
                        log.error("Fallo al enviar mensaje por WebSocket: %s", e)
                        await asyncio.sleep(1)
                else:
                    # Esperar a que el WebSocket se conecte
                    await asyncio.sleep(1)
            self.serial_to_ws_queue.task_done()


if __name__ == "__main__":
    bridge = LocalBridge(CLOUD_WS_URL, BAUD_RATE)
    try:
        asyncio.run(bridge.run())
    except KeyboardInterrupt:
        log.info("Puente local detenido por el usuario.")
        sys.exit(0)
