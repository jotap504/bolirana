# Bolirana Arcade Memory & Guidelines

## 🛠️ Stack Tecnológico
1. **Firmware (ESP32)**: C++ ([firmware.ino](file:///c:/Users/user/Documents/bolirana/firmware/firmware.ino)) con la librería `Adafruit_NeoPixel` para efectos de iluminación. Comunicación bidireccional serial a 115200 baudios.
2. **Backend (Python)**: FastAPI ([main.py](file:///c:/Users/user/Documents/bolirana/backend/main.py)), routers en `backend/app/routers/`, base de datos SQLite (`bolirana.db`) mediante `app/database.py`, integración de pagos con MercadoPago (`app/payments/mercadopago.py`).
3. **Frontend (HTML/CSS/JS)**:
   - **Display (Pantalla Principal)**: `/display` (estáticos en `backend/static/display/`).
   - **Player (Control Celular)**: `/player` (estáticos en `backend/static/player/`).
   - **Admin (Consola de Configuración)**: `/admin` (estáticos en `backend/static/admin/`).

---

## 🔌 Configuración de Pines y Hardware (ESP32)
* **Sensores de Acierto (PULLUP interno)**:
  * Rana: GPIO 12 | Sapo: GPIO 14 | Fosa 1: GPIO 27 | Fosa 2: GPIO 26 | Fosa 3: GPIO 25 | Fosa 4: GPIO 33 | Fosa Cero (Sin acierto): GPIO 5
* **Seguridad y Control**:
  * Radar de proximidad anti-trampa (HLK-LD2410C): GPIO 16 (PULLDOWN)
  * Monedero (Entrada de fichas): GPIO 18 (Interrupción por flanco de bajada)
  * Botón Start: GPIO 19 | Botón Pause: GPIO 21
* **Actuadores**:
  * Solenoide / Campana física: GPIO 13 | LED indicador de aciertos (Azul en placa): GPIO 2
  * Tira NeoPixel (WS2812B): GPIO 4 (60 LEDs)
* **Motor del Arquero Móvil**:
  * Dirección A (IN1): GPIO 17 | Dirección B (IN2): GPIO 15
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
