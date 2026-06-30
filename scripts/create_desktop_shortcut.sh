#!/bin/bash
SCRIPT_DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"
PROJECT_ROOT="$( cd "$SCRIPT_DIR/.." && pwd )"
DESKTOP_DIR="$HOME/Desktop"
mkdir -p "$DESKTOP_DIR"
cat <<EOF > "$DESKTOP_DIR/bolirana.desktop"
[Desktop Entry]
Type=Application
Name=Iniciar Bolirana
Comment=Inicia el juego Bolirana Arcade
Exec=/bin/bash "$PROJECT_ROOT/scripts/start_bolirana.sh"
Icon=games-sport
Terminal=false
Categories=Game;
EOF
chmod +x "$PROJECT_ROOT/scripts/start_bolirana.sh"
chmod +x "$DESKTOP_DIR/bolirana.desktop"
if [ -d "$DESKTOP_DIR" ]; then
    gio set "$DESKTOP_DIR/bolirana.desktop" metadata::trusted true 2>/dev/null
    dbus-launch gio set "$DESKTOP_DIR/bolirana.desktop" metadata::trusted true 2>/dev/null
fi
echo "============================================="
echo "¡Acceso directo creado con éxito en tu Escritorio!"
echo "Archivo: $DESKTOP_DIR/bolirana.desktop"
echo "============================================="
