#!/bin/bash
SCRIPT_DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"
PROJECT_ROOT="$( cd "$SCRIPT_DIR/.." && pwd )"

AUTOSTART_DIR="$HOME/.config/autostart"
mkdir -p "$AUTOSTART_DIR"

cat <<EOF > "$AUTOSTART_DIR/bolirana.desktop"
[Desktop Entry]
Type=Application
Name=Bolirana Arcade
Exec=/bin/bash "$PROJECT_ROOT/scripts/start_bolirana.sh"
Terminal=false
X-GNOME-Autostart-enabled=true
EOF

chmod +x "$PROJECT_ROOT/scripts/start_bolirana.sh"
chmod +x "$AUTOSTART_DIR/bolirana.desktop"

echo "¡Autostart configurado con éxito en $AUTOSTART_DIR/bolirana.desktop!"
echo "La máquina se iniciará de forma automática en cada arranque."
