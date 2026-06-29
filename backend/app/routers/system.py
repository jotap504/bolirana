import subprocess
import logging
from fastapi import APIRouter, Request, HTTPException
from pydantic import BaseModel

log = logging.getLogger(__name__)
router = APIRouter(prefix="/api/system")

class ConnectRequest(BaseModel):
    ssid: str
    password: str = None

@router.get("/wifi/list")
async def get_wifi_list():
    try:
        # Ejecutar nmcli para listar wifis en formato tabulado/limpio
        res = subprocess.run(
            ["nmcli", "-t", "-f", "SSID,SIGNAL,SECURITY", "dev", "wifi", "list"],
            capture_output=True,
            text=True,
            timeout=6
        )
        if res.returncode != 0:
            log.warning("nmcli dev wifi list falló con código %d: %s", res.returncode, res.stderr)
            return []
        
        networks = []
        seen_ssids = set()
        for line in res.stdout.splitlines():
            # nmcli -t separa campos por ':' pero escapa los ':' internos con '\'
            # para simplificar, dividimos por ':' sin preceder de '\'
            parts = line.split(":")
            if len(parts) >= 3:
                ssid = parts[0].strip()
                signal = parts[1].strip()
                security = parts[2].strip()
                if ssid and ssid not in seen_ssids:
                    seen_ssids.add(ssid)
                    networks.append({
                        "ssid": ssid,
                        "signal": int(signal) if signal.isdigit() else 50,
                        "secured": bool(security) and ("WPA" in security or "WEP" in security or "802.1X" in security)
                    })
        # Ordenar por fuerza de señal
        networks.sort(key=lambda x: x["signal"], reverse=True)
        return networks
    except FileNotFoundError:
        # Si nmcli no está instalado (por ejemplo en Windows de desarrollo)
        log.info("nmcli no encontrado, retornando redes mock.")
        return [
            {"ssid": "Mock_Red_Bolirana_95", "signal": 95, "secured": True},
            {"ssid": "Mock_Publica_Invitados", "signal": 75, "secured": False},
            {"ssid": "Mock_Señal_Debil", "signal": 30, "secured": True}
        ]
    except Exception as e:
        log.error("Error al escanear wifi: %s", e)
        raise HTTPException(status_code=500, detail=str(e))

@router.post("/wifi/connect")
async def connect_wifi(req: ConnectRequest):
    try:
        cmd = ["nmcli", "dev", "wifi", "connect", req.ssid]
        if req.password:
            cmd.extend(["password", req.password])
        
        log.info("Intentando conectar a WiFi SSID: %s", req.ssid)
        res = subprocess.run(cmd, capture_output=True, text=True, timeout=20)
        
        if res.returncode == 0:
            log.info("Conexión exitosa a %s", req.ssid)
            return {"success": True, "message": f"Conectado a la red {req.ssid} correctamente."}
        else:
            error_msg = res.stderr.strip() or res.stdout.strip()
            log.warning("Fallo de conexión a %s: %s", req.ssid, error_msg)
            return {"success": False, "message": f"Error de conexión: {error_msg}"}
            
    except FileNotFoundError:
        return {"success": False, "message": "nmcli no está instalado en este sistema de desarrollo."}
    except Exception as e:
        log.error("Error al conectar wifi: %s", e)
        return {"success": False, "message": str(e)}

@router.post("/exit-kiosk")
async def exit_kiosk():
    try:
        log.info("Cerrando navegador kiosk por comando del administrador...")
        # Intentar cerrar Google Chrome o Chromium
        subprocess.Popen(["killall", "google-chrome"])
        subprocess.Popen(["killall", "chromium-browser"])
        subprocess.Popen(["killall", "chromium"])
        return {"success": True, "message": "Navegador cerrado."}
    except Exception as e:
        log.error("Error al cerrar navegador: %s", e)
        raise HTTPException(status_code=500, detail=str(e))
