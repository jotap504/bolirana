# Bolirana Arcade Memory & Guidelines

## 🛠️ Stack Tecnológico
1. **Firmware (ESP32)**: C++ ([firmware.ino](file:///c:/Users/user/Documents/bolirana/firmware/firmware.ino)) con la librería `Adafruit_NeoPixel` para efectos de iluminación. Comunicación bidireccional serial a 115200 baudios.
2. **Backend (Python)**: FastAPI ([main.py](file:///c:/Users/user/Documents/bolirana/backend/main.py)), routers en `backend/app/routers/`, base de datos SQLite (`bolirana.db`) mediante `app/database.py`, integración de pagos con MercadoPago (`app/payments/mercadopago.py`).
3. **Frontend (HTML/CSS/JS)**:
   - **Display (Pantalla Principal)**: `/display` (estáticos en `backend/static/display/`).
   - **Player (Control Celular)**: `/player` (estáticos en `backend/static/player/`).
   - **Admin (Consola de Configuración)**: `/admin` (estáticos en `backend/static/admin/`).

---

## 🔌 Configuración de Pines y Hardware (ESP32 con Expansor MCP23017)
* **Conexión de Expansor I2C (MCP23017, Dirección 0x20)**:
  * SDA: GPIO 21 | SCL: GPIO 22
* **Mapeo de Sensores en MCP23017 (Puertos A y B / PA0-PA7 y PB0-PB7)**:
  * PA0 (GPA0 / Pin 21): Rana (1000 pts)
  * PA1 (GPA1 / Pin 22): Sapo (500 pts)
  * PA2 (GPA2 / Pin 23): Fosa 1 (100 pts)
  * PA3 (GPA3 / Pin 24): Fosa 2 (50 pts)
  * PA4 (GPA4 / Pin 25): Fosa 3 (20 pts)
  * PA5 (GPA5 / Pin 26): Fosa 4 (10 pts)
  * PA6 (GPA6 / Pin 27): Fosa 5 (Opcional)
  * PA7 (GPA7 / Pin 28): Fosa 6 (Opcional)
  * PB0 (GPB0 / Pin 1): Fosa 7 (Opcional)
  * PB1 (GPB1 / Pin 2): Fosa 8 (Opcional)
  * PB2 (GPB2 / Pin 3): Fosa 9 (Opcional / 5 pts por defecto)
  * PB7 (GPB7 / Pin 8): Fosa Cero (Sin acierto / 0 pts)
* **Seguridad y Control**:
  * Radar de proximidad anti-trampa (HLK-LD2410C): GPIO 16 (PULLDOWN)
  * Monedero (Entrada de fichas): GPIO 18 (Interrupción por flanco de bajada)
  * Botón Start: GPIO 19 | Botón Pause: GPIO 27 (Relocalizado del GPIO 21 para evitar conflicto con SDA)
* **Actuadores**:
  * Solenoide / Campana física: GPIO 13 | LED indicador de aciertos (Azul en placa): GPIO 2
  * Tira NeoPixel (WS2812B/W): GPIO 4 (60 LEDs, controlado directamente por ESP32)
* **Motor del Arquero Móvil**:
  * Dirección A (IN1): GPIO 26 | Dirección B (IN2): GPIO 25 | IN3: GPIO 33 | IN4: GPIO 32 (Conforme a firmware.ino)
  * Final de carrera izquierdo: GPIO 34 | Final de carrera derecho: GPIO 35

---

## 💻 Comandos Útiles

### Servidor de Desarrollo (FastAPI Backend)
```powershell
cd backend
python -m venv venv
.\venv\Scripts\activate
pip install -r requirements.txt
uvicorn main:app --reload --host 0.0.0.0 --port 8000
```

---

## 📁 Estructura del Directorio
```text
/bolirana
├── .gitignore
├── skills-lock.json
├── CLAUDE.md                      # Este archivo
├── firmware/
│   └── firmware.ino               # Código fuente de la ESP32
└── backend/
    ├── main.py                    # Inicialización de FastAPI y SerialBridge
    ├── requirements.txt           # Dependencias de Python
    ├── schema.sql                 # Estructura de la BD SQLite
    └── app/
        ├── database.py            # Conexión SQLite
        ├── models.py              # Definición de tablas
        ├── ws_manager.py          # Gestor de conexiones WebSockets
        ├── game/
        │   ├── cloud_sync.py      # Sincronización en la nube y geolocalización
        │   ├── engine.py          # Motor de lógica del juego
        │   └── session.py         # Modelo de datos de sesión y jugador
        ├── hardware/
        │   └── serial_bridge.py   # Conexión Serial con la ESP32
        └── routers/
            ├── admin.py
            ├── game.py
            └── payment.py
```

---

## 🔊 Audio y Políticas de Autoplay
* **Políticas del Navegador (Autoplay)**: En una cabina física o pantalla arcade, el navegador bloquea la reproducción de audios nativos y suspende el `AudioContext` de la pantalla principal (`/display`) a menos que haya una interacción física inicial en la pantalla (click, touch, tecla).
  * **Solución de Lanzamiento (Kiosk/Cabinas)**: Configurar el inicio de Google Chrome/Chromium con los siguientes flags para omitir restricciones de interacción:
    ```bash
    chrome.exe --autoplay-policy=no-user-gesture-required --kiosk http://localhost:8000/display/
    ```
  * **Correcciones de Código Realizadas**:
    * **`audio.js`**: Se modificó el inicializador de `AudioContext` para que si se llama a `init()` durante un gesto del usuario y el contexto ya fue creado de forma suspendida, se invoque `ctx.resume()` y se active el audio.
    * **`playRana()`**: Se creó un método dedicado en `AudioFX` para reproducir el archivo de sonido `/audios/rana.mp3` (Celebración de Messi) con fallback sintetizado.
    * **`app.js`**: Se configuró la escucha de WebSocket para que al detectar el evento de score en la zona `"rana"`, reproduzca `AudioFX.playRana()` independientemente de los puntos (`delta`) de la jugada, asegurando que suene en todos los modos de juego (ej. modo Goleador, donde delta es 1 en vez de 1000).

