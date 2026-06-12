#include <WiFi.h>
#include <WebServer.h>
#include <AccelStepper.h>

// ==========================================
// CONFIGURACIÓN DE RED WIFI (Modificar)
// ==========================================
const char* ssid     = "Fibrasky";
const char* password = "corsa000";

// ==========================================
// CONFIGURACIÓN DE HARDWARE (PINES LIMPIOS)
// ==========================================
#define IN1 26
#define IN2 25
#define IN3 33
#define IN4 32

#define IR_SENSOR_PIN 34

// Configuración del motor (Secuencia directa)
AccelStepper stepper(AccelStepper::HALF4WIRE, IN1, IN3, IN2, IN4);

WebServer server(80);

// ==========================================
// INTERFAZ DASHBOARD (HTML / JS)
// ==========================================
const char index_html[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0, user-scalable=no">
    <title>Dashboard Dispensador</title>
    <style>
        body { font-family: -apple-system, BlinkMacSystemFont, "Segoe UI", Roboto, sans-serif; text-align: center; background-color: #121212; color: #e0e0e0; padding: 15px; margin: 0; touch-action: manipulation; }
        .container { max-width: 400px; margin: 20px auto; background: #1e1e1e; padding: 25px; border-radius: 12px; box-shadow: 0 4px 15px rgba(0,0,0,0.6); }
        h2 { color: #00adb5; margin-bottom: 20px; font-size: 1.5em; }
        .section { background: #2a2a2a; padding: 15px; border-radius: 8px; margin-bottom: 20px; }
        .section-title { font-size: 0.85em; color: #888; text-transform: uppercase; letter-spacing: 1px; margin-bottom: 15px; text-align: left; border-bottom: 1px solid #444; padding-bottom: 5px; }
        input { width: 90%; padding: 12px; margin-bottom: 15px; border: 1px solid #333; background: #222; color: #fff; border-radius: 6px; text-align: center; font-size: 1.2em; }
        .btn-group { display: flex; justify-content: space-between; gap: 10px; }
        button { flex: 1; padding: 15px; border: none; border-radius: 6px; font-weight: bold; font-size: 1em; cursor: pointer; transition: background 0.2s; user-select: none; -webkit-user-select: none; }
        
        /* Botones de Pasos */
        .btn-step { background-color: #00adb5; color: #fff; }
        .btn-step:active { background-color: #008288; }
        
        /* Botones Continuos */
        .btn-hold { background-color: #e6a817; color: #fff; }
        .btn-hold:active { background-color: #c58e0f; }

        .status { padding: 15px; border-radius: 6px; background: #2d2d2d; font-size: 1.1em; border-left: 5px solid #4caf50; }
        .detected { color: #ff5722; font-weight: bold; }
        .clear { color: #4caf50; font-weight: bold; }
    </style>
</head>
<body>
    <div class="container">
        <h2>Control de Metegol</h2>
        
        <div class="section">
            <div class="section-title">Movimiento por Pasos</div>
            <input type="number" id="stepsInput" value="512" min="1">
            <div class="btn-group">
                <button class="btn-step" onclick="moveSteps('forward')">+ Pasos</button>
                <button class="btn-step" onclick="moveSteps('reverse')">- Pasos</button>
            </div>
        </div>

        <div class="section">
            <div class="section-title">Movimiento Continuo (Mantener)</div>
            <div class="btn-group">
                <button class="btn-hold" id="btnHoldFwd">Avanzar</button>
                <button class="btn-hold" id="btnHoldRev">Retroceder</button>
            </div>
        </div>
        
        <div id="statusBox" class="status">
            Sensor IR: <span id="irState" class="clear">Detectando...</span>
        </div>
    </div>

    <script>
        // 1. Lógica para botones de pasos exactos
        function moveSteps(direction) {
            const steps = document.getElementById('stepsInput').value;
            fetch(`/step?dir=${direction}&steps=${steps}`).catch(err => console.error(err));
        }

        // 2. Lógica para botones de mantener presionado
        function setupHoldButton(buttonId, direction) {
            const btn = document.getElementById(buttonId);
            let isPressing = false;

            const startMove = (e) => {
                e.preventDefault(); // Evita scroll o zoom en mobile
                if (!isPressing) {
                    isPressing = true;
                    fetch(`/start?dir=${direction}`).catch(err => console.error(err));
                }
            };

            const stopMove = (e) => {
                e.preventDefault();
                if (isPressing) {
                    isPressing = false;
                    fetch('/stop').catch(err => console.error(err));
                }
            };

            // Eventos para Celular (Touch)
            btn.addEventListener('touchstart', startMove);
            btn.addEventListener('touchend', stopMove);
            btn.addEventListener('touchcancel', stopMove); // Por si entra una llamada o algo interrumpe

            // Eventos para PC (Mouse)
            btn.addEventListener('mousedown', startMove);
            btn.addEventListener('mouseup', stopMove);
            btn.addEventListener('mouseleave', stopMove); // Por si movés el mouse fuera del botón
        }

        setupHoldButton('btnHoldFwd', 'forward');
        setupHoldButton('btnHoldRev', 'reverse');

        // 3. Polling del Sensor IR
        setInterval(() => {
            fetch('/sensor')
                .then(response => response.text())
                .then(data => {
                    const label = document.getElementById('irState');
                    const box = document.getElementById('statusBox');
                    if (data === "1") {
                        label.innerText = "¡PELOTA DETECTADA!";
                        label.className = "detected";
                        box.style.borderLeftColor = "#ff5722";
                    } else {
                        label.innerText = "Despejado";
                        label.className = "clear";
                        box.style.borderLeftColor = "#4caf50";
                    }
                });
        }, 200);
    </script>
</body>
</html>
)rawliteral";

// ==========================================
// MANEJADORES DE LAS RUTAS DEL SERVIDOR
// ==========================================
void handleRoot() {
  server.send_P(200, "text/html", index_html);
}

// Ruta para movimiento exacto (por pasos)
void handleStep() {
  if (server.hasArg("steps") && server.hasArg("dir")) {
    long steps = server.arg("steps").toInt();
    String dir = server.arg("dir");
    
    if (dir == "reverse") {
      stepper.move(-steps);
    } else {
      stepper.move(steps);
    }
  }
  server.send(200, "text/plain", "OK");
}

// Ruta para INICIAR movimiento continuo
void handleStartContinuous() {
  if (server.hasArg("dir")) {
    String dir = server.arg("dir");
    // Le seteamos un objetivo larguísimo (1 millón de pasos)
    // Se moverá hasta que reciba la orden de frenar
    if (dir == "reverse") {
      stepper.move(-1000000);
    } else {
      stepper.move(1000000);
    }
  }
  server.send(200, "text/plain", "OK");
}

// Ruta para DETENER movimiento continuo
void handleStop() {
  // stepper.stop() recalcula la posición final basándose en la desaceleración actual
  stepper.stop(); 
  server.send(200, "text/plain", "OK");
}

void handleSensor() {
  String state = (digitalRead(IR_SENSOR_PIN) == LOW) ? "1" : "0";
  server.send(200, "text/plain", state);
}

// ==========================================
// SETUP & LOOP
// ==========================================
void setup() {
  Serial.begin(115200);
  
  pinMode(IR_SENSOR_PIN, INPUT);

  stepper.setMaxSpeed(800.0);
  stepper.setAcceleration(400.0);

  WiFi.begin(ssid, password);
  Serial.print("Conectando al WiFi");
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  
  Serial.println("\nConectado!");
  Serial.print("Ingresá esta IP en el navegador de tu celular/PC: ");
  Serial.println(WiFi.localIP());

  // Definición de las nuevas rutas
  server.on("/", HTTP_GET, handleRoot);
  server.on("/step", HTTP_GET, handleStep);
  server.on("/start", HTTP_GET, handleStartContinuous);
  server.on("/stop", HTTP_GET, handleStop);
  server.on("/sensor", HTTP_GET, handleSensor);

  server.begin();
}

void loop() {
  server.handleClient();
  stepper.run();
}