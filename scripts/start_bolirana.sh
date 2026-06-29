#!/bin/bash

# Determinar dinámicamente la ruta raíz del proyecto
SCRIPT_DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"
PROJECT_ROOT="$( cd "$SCRIPT_DIR/.." && pwd )"

echo "=== Iniciando Bolirana Arcade ==="
echo "Ruta del proyecto: $PROJECT_ROOT"

# 1. Detener instancias previas de uvicorn y chromium (para evitar conflictos)
echo "Cerrando procesos antiguos..."
killall python 2>/dev/null
killall python3 2>/dev/null
killall chromium-browser 2>/dev/null
killall chromium 2>/dev/null
sleep 2

# 2. Iniciar el servidor backend en segundo plano
echo "Iniciando backend FastAPI..."
cd "$PROJECT_ROOT/backend"
# Usar el intérprete de python del entorno virtual de forma directa
./venv/bin/python -m uvicorn main:app --host 0.0.0.0 --port 8000 > "$PROJECT_ROOT/backend_server.log" 2>&1 &

# Guardar el PID del backend
BACKEND_PID=$!
echo "Backend iniciado en segundo plano (PID: $BACKEND_PID)"

# 3. Esperar a que el backend y el servidor estén listos dinámicamente
echo "Esperando a que el backend FastAPI esté listo..."
TIMEOUT=20
INTERVAL=1
ELAPSED=0

while ! curl -s -I http://127.0.0.1:8000/display/ >/dev/null; do
    sleep $INTERVAL
    ELAPSED=$((ELAPSED + INTERVAL))
    if [ $ELAPSED -ge $TIMEOUT ]; then
        echo "ERROR: El backend no inició dentro de los $TIMEOUT segundos de límite."
        exit 1
    fi
done
echo "¡Backend listo en $ELAPSED segundos! Preparando navegador..."

# 4. Detectar binario de Chromium disponible en Lubuntu
if command -v google-chrome >/dev/null 2>&1; then
    CHROMIUM_BIN="google-chrome"
elif command -v chromium-browser >/dev/null 2>&1; then
    CHROMIUM_BIN="chromium-browser"
elif command -v chromium >/dev/null 2>&1; then
    CHROMIUM_BIN="chromium"
else
    echo "ERROR: No se encontró un navegador compatible (Chrome/Chromium) en el sistema."
    exit 1
fi

# 3.5. Desactivar protector de pantalla y ocultar cursor
echo "Ocultando cursor y desactivando protector de pantalla..."
unclutter -idle 2 -root &
xset s off 2>/dev/null
xset -dpms 2>/dev/null
xset s noblank 2>/dev/null

echo "Iniciando $CHROMIUM_BIN en modo Kiosco..."
# Flags optimizados para cabinas arcade en Lubuntu:
# --autoplay-policy=no-user-gesture-required (Habilita el sonido automático sin clic del usuario)
# --kiosk (Pantalla completa bloqueada)
# --no-first-run --no-default-browser-check (Ocular alertas de primer uso)
# --disable-infobars --disable-session-crashed-bubble (Ocultar avisos de "apagado incorrecto")
# --check-for-update-interval=31536000 (Prevenir chequeo de actualizaciones de Chromium)
$CHROMIUM_BIN \
  --autoplay-policy=no-user-gesture-required \
  --kiosk \
  --no-first-run \
  --no-default-browser-check \
  --disable-infobars \
  --disable-session-crashed-bubble \
  --check-for-update-interval=31536000 \
  --password-store=basic \
  --disable-features=Translate \
  --start-maximized \
  --no-sandbox \
  "http://127.0.0.1:8000/" &

echo "=== Procesos de Bolirana iniciados con éxito ==="
