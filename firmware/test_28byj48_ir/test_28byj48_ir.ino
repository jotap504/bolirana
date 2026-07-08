/*
=============================================================================
SKETCH DE DIAGNÓSTICO: PRUEBA INTEGRADA 28BYJ-48 (ULN2003) + SENSOR IR
=============================================================================
Este sketch permite probar el funcionamiento conjunto del motor paso a paso
unipolar 28BYJ-48 (con placa controladora ULN2003) y el sensor IR (GPIO 15).

Conexiones recomendadas:
- ULN2003 IN1 -> GPIO 13 de la ESP32
- ULN2003 IN2 -> GPIO 12 de la ESP32
- ULN2003 IN3 -> GPIO 14 de la ESP32
- ULN2003 IN4 -> GPIO 27 de la ESP32
- ULN2003 VCC -> 5V de la ESP32 (El motor funciona con 5V)
- ULN2003 GND -> GND común con la ESP32
- Microswitch -> GPIO 32 (a GND)
- Sensor IR   -> GPIO 15 (Alimentado a 3.3V)
=============================================================================
*/

#include <WiFi.h>
#include <WebServer.h>
#include <AccelStepper.h>

// Credenciales WiFi
const char* ssid     = "Fibrasky";
const char* password = "corsa000";

// Instancia del servidor web en puerto 80
WebServer server(80);

// Prototipos de funciones para evitar errores de compilación del preprocesador de Arduino
void processCommand(String cmd, String val);
void homeStepper();
void stopMotorEmergency();
void startBallRelease(int count);
void stopMotorCoils();
void readSerialCommand();
void webLog(String line);

// Pines para el motor unipolar 28BYJ-48 en la placa ULN2003
#define MOTOR_IN1 13
#define MOTOR_IN2 12
#define MOTOR_IN3 14
#define MOTOR_IN4 27

// Pin de carrera y de señal del receptor IR
#define MOTOR_LIMIT_SWITCH_PIN 32 
#define IR_SENSOR_PIN 15  

// Led indicador integrado
#define LED_INDICATOR_PIN 2

// Configuración del sensor IR
const bool IR_ACTIVE_STATE = HIGH; 

// Inicializar AccelStepper en modo unipolar de 4 hilos (HALF4WIRE / 8 pasos)
// OJO: La secuencia de pines para este motor debe ser IN1, IN3, IN2, IN4 para que gire correctamente.
AccelStepper stepper(AccelStepper::HALF4WIRE, MOTOR_IN1, MOTOR_IN3, MOTOR_IN2, MOTOR_IN4);

// Variables dinámicas (El 28BYJ-48 tiene reducción interna 1:64 y gira más lento)
// Valores típicos: velocidad max ~800, aceleración ~400 en modo HALF4WIRE (4096 pasos/vuelta)
float releaseSpeed = 800.0;
float releaseAccel = 400.0;
int directionMultiplier = 1; // 1 = Giro normal, -1 = Giro invertido
int extraStepsAfterDetect = 150; // Pasos extras para despejar canal
bool keepLockedOnHalt = false; // false = libre y frío (recomendado para este motor), true = trabado

// Estados de la prueba
bool releasingActive = false;
bool finishingRelease = false;
int targetBallCount = 0;
int detectedBallCount = 0;
bool ballInSensor = false;
unsigned long lastBallDetectTime = 0;
const unsigned long BALL_COOLDOWN_MS = 300;

// Modo de monitoreo del sensor IR sin mover el motor
bool sensorMonitorActive = false;

// Historial de logs para transmitir vía Web
String logHistory = "";
int logLinesCount = 0;

// Función para loggear tanto en puerto serie como en el buffer web
void webLog(String line) {
  Serial.println(line);
  
  unsigned long sec = millis() / 1000;
  String timeStr = "[" + String(sec) + "s] ";
  
  logHistory += timeStr + line + "\n";
  logLinesCount++;
  
  // Limitar buffer a las últimas 25 líneas
  if (logLinesCount > 25) {
    int firstNewLine = logHistory.indexOf('\n');
    if (firstNewLine != -1) {
      logHistory = logHistory.substring(firstNewLine + 1);
      logLinesCount--;
    }
  }
}

// ==========================================
// PLANTILLA HTML DEL CONTROL WEB (FLASH)
// ==========================================
const char index_html[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html lang="es">
<head>
  <meta charset="UTF-8">
  <title>Futsapo - Panel de Diagnóstico WiFi</title>
  <link rel="preconnect" href="https://fonts.googleapis.com">
  <link rel="preconnect" href="https://fonts.gstatic.com" crossorigin>
  <link href="https://fonts.googleapis.com/css2?family=Outfit:wght@300;400;600;800&family=Share+Tech+Mono&display=swap" rel="stylesheet">
  <style>
    * { box-sizing: border-box; margin: 0; padding: 0; }
    body {
      font-family: 'Outfit', sans-serif;
      background: radial-gradient(circle at 50% 50%, #11112b 0%, #070714 100%);
      color: #e2e2ff;
      min-height: 100vh;
      padding: 20px;
    }
    .container { max-width: 1000px; margin: 0 auto; display: grid; grid-template-columns: 1fr 1.2fr; gap: 20px; }
    @media (max-width: 800px) { .container { grid-template-columns: 1fr; } }
    .card {
      background: rgba(18, 18, 38, 0.7);
      backdrop-filter: blur(10px);
      border: 1px solid rgba(0, 240, 255, 0.15);
      border-radius: 12px;
      padding: 20px;
      box-shadow: 0 8px 32px rgba(0,0,0,0.5);
    }
    .card-title {
      font-family: 'Share Tech Mono', monospace;
      font-size: 15px;
      color: #f0e000;
      margin-bottom: 15px;
      border-left: 4px solid #f0e000;
      padding-left: 10px;
      text-transform: uppercase;
      font-weight: 800;
    }
    .card-title.cyan { color: #00f0ff; border-left-color: #00f0ff; }
    .btn {
      font-family: inherit;
      padding: 10px 15px;
      border: 1px solid rgba(0, 240, 255, 0.15);
      border-radius: 6px;
      font-size: 12px;
      font-weight: 700;
      cursor: pointer;
      text-transform: uppercase;
      transition: all 0.2s;
      background: rgba(255,255,255,0.03);
      color: #e2e2ff;
    }
    .btn:hover {
      background: rgba(0, 240, 255, 0.15);
      border-color: #00f0ff;
      transform: translateY(-1px);
    }
    .btn-danger {
      background: linear-gradient(135deg, #ff0055 0%, #cc0044 100%);
      color: #fff;
      border: none;
    }
    .btn-danger:hover { background: #ff1a66; }
    .btn-success {
      background: rgba(0, 255, 170, 0.08);
      border-color: rgba(0, 255, 170, 0.3);
      color: #00ffaa;
    }
    .btn-success:hover { background: #00ffaa; color: #000; }
    .grid-balls { display: grid; grid-template-columns: repeat(5, 1fr); gap: 8px; margin-bottom: 12px; }
    .btn-ball {
      padding: 12px 0;
      font-size: 16px;
      background: rgba(255,255,255,0.05);
      border: 1px solid rgba(0, 240, 255, 0.15);
      border-radius: 8px;
      font-weight: 800;
      cursor: pointer;
      color: #00f0ff;
      transition: all 0.2s;
    }
    .btn-ball:hover { background: #00f0ff; color: #000; }
    .slider-row { display: flex; align-items: center; gap: 10px; margin-bottom: 12px; }
    input[type=range] { flex-grow: 1; accent-color: #00f0ff; }
    .terminal {
      background: #020206;
      border: 1px solid rgba(0, 240, 255, 0.15);
      border-radius: 8px;
      padding: 12px;
      font-family: 'Share Tech Mono', monospace;
      font-size: 12px;
      color: #00ffaa;
      height: 380px;
      overflow-y: auto;
      white-space: pre-wrap;
      box-shadow: inset 0 0 10px rgba(0,0,0,0.8);
    }
  </style>
</head>
<body>
  <div style="max-width: 1000px; margin: 0 auto 20px auto; display:flex; justify-content:space-between; align-items:center;">
    <h1 style="font-family:'Share Tech Mono'; font-size:24px; color:#00f0ff;">FUTSAPO WiFi DIAGS</h1>
    <div style="font-size:12px; color:#8493a8;">Conectado a: Fibrasky</div>
  </div>
  
  <div class="container">
    <div style="display:flex; flex-direction:column; gap:20px;">
      <!-- PANEL CONTROL -->
      <div class="card">
        <div class="card-title">Dispensador 28BYJ-48</div>
        <div class="grid-balls">
          <button class="btn-ball" onclick="send('cmd', 1)">1</button>
          <button class="btn-ball" onclick="send('cmd', 2)">2</button>
          <button class="btn-ball" onclick="send('cmd', 3)">3</button>
          <button class="btn-ball" onclick="send('cmd', 4)">4</button>
          <button class="btn-ball" onclick="send('cmd', 5)">5</button>
        </div>
        <div style="display:grid; grid-template-columns:1fr 1fr; gap:8px;">
          <button class="btn" onclick="send('cmd', 'H')">🏠 Homing</button>
          <button class="btn" onclick="send('cmd', 'I')">🔄 Invertir</button>
          <button class="btn btn-success" id="btn-t" onclick="send('cmd', 'T')">🔍 Monitor IR: OFF</button>
          <button class="btn btn-danger" onclick="send('cmd', 'S')">🚨 STOP</button>
        </div>
      </div>
      
      <!-- CONFIG -->
      <div class="card">
        <div class="card-title cyan">Parámetros</div>
        
        <div style="margin-bottom:12px;">
          <div style="font-size:11px; color:#8493a8; margin-bottom:4px;">Velocidad: <span id="lbl-v">800</span> pasos/s</div>
          <div class="slider-row">
            <input type="range" id="sld-v" min="100" max="1200" value="800" onchange="send('V', this.value)">
          </div>
        </div>
        
        <div style="margin-bottom:12px;">
          <div style="font-size:11px; color:#8493a8; margin-bottom:4px;">Aceleración: <span id="lbl-a">400</span> pasos/s²</div>
          <div class="slider-row">
            <input type="range" id="sld-a" min="50" max="800" value="400" onchange="send('A', this.value)">
          </div>
        </div>
        
        <div style="margin-bottom:12px;">
          <div style="font-size:11px; color:#8493a8; margin-bottom:4px;">Pasos Extras: <span id="lbl-p">150</span> pasos</div>
          <div class="slider-row">
            <input type="range" id="sld-p" min="0" max="3000" value="150" onchange="send('P', this.value)">
          </div>
        </div>
        
        <div>
          <div style="font-size:11px; color:#8493a8; margin-bottom:6px;">Traba en Reposo:</div>
          <div style="display:flex; gap:8px;">
            <button class="btn" id="lock-l0" onclick="send('L', 0)" style="flex:1;">🔓 Libre (L0)</button>
            <button class="btn" id="lock-l1" onclick="send('L', 1)" style="flex:1;">🔒 Trabado (L1)</button>
          </div>
        </div>
      </div>
    </div>
    
    <!-- TERMINAL -->
    <div class="card" style="display:flex; flex-direction:column; height:100%;">
      <div class="card-title cyan" style="display:flex; justify-content:space-between; align-items:center;">
        Terminal de Actividad
        <div style="display:flex; gap:8px; align-items:center;">
          <input type="checkbox" id="chk-scroll" checked>
          <span style="font-size:10px; font-family:sans-serif; color:#8493a8;">Auto-scroll</span>
        </div>
      </div>
      <div class="terminal" id="term"></div>
    </div>
  </div>
  
  <script>
    async function send(cmd, val) {
      await fetch(`/action?cmd=${cmd}&val=${val}`);
    }
    
    async function updateStatus() {
      try {
        const r = await fetch('/status');
        const d = await r.json();
        
        document.getElementById('lbl-v').textContent = d.speed;
        document.getElementById('sld-v').value = d.speed;
        document.getElementById('lbl-a').textContent = d.accel;
        document.getElementById('sld-a').value = d.accel;
        document.getElementById('lbl-p').textContent = d.steps;
        document.getElementById('sld-p').value = d.steps;
        
        const btnT = document.getElementById('btn-t');
        if (d.monitor === 1) {
          btnT.textContent = "🔍 Monitor IR: ON";
          btnT.style.background = "#00ffaa";
          btnT.style.color = "#000";
        } else {
          btnT.textContent = "🔍 Monitor IR: OFF";
          btnT.style.background = "";
          btnT.style.color = "";
        }
        
        const l0 = document.getElementById('lock-l0');
        const l1 = document.getElementById('lock-l1');
        if (d.lock === 1) {
          l1.style.background = "#f0e000";
          l1.style.color = "#000";
          l0.style.background = "";
          l0.style.color = "";
        } else {
          l0.style.background = "#00f0ff";
          l0.style.color = "#000";
          l1.style.background = "";
          l1.style.color = "";
        }
        
        const term = document.getElementById('term');
        term.textContent = d.logs;
        if (document.getElementById('chk-scroll').checked) {
          term.scrollTop = term.scrollHeight;
        }
      } catch (e) {}
    }
    
    setInterval(updateStatus, 500);
    updateStatus();
  </script>
</body>
</html>
)rawliteral";

// ==========================================
// ENDPOINTS DEL SERVIDOR WEB
// ==========================================
void handleRoot() {
  server.send_P(200, "text/html", index_html);
}

void handleStatus() {
  String json = "{";
  json += "\"speed\":" + String(releaseSpeed);
  json += ",\"accel\":" + String(releaseAccel);
  json += ",\"steps\":" + String(extraStepsAfterDetect);
  json += ",\"lock\":" + String(keepLockedOnHalt ? 1 : 0);
  json += ",\"monitor\":" + String(sensorMonitorActive ? 1 : 0);
  
  // Escapar logs para JSON
  String escapedLogs = logHistory;
  escapedLogs.replace("\\", "\\\\");
  escapedLogs.replace("\"", "\\\"");
  escapedLogs.replace("\n", "\\n");
  escapedLogs.replace("\r", "");
  
  json += ",\"logs\":\"" + escapedLogs + "\"";
  json += "}";
  server.send(200, "application/json", json);
}

void handleAction() {
  if (server.hasArg("cmd")) {
    String cmd = server.arg("cmd");
    String val = server.hasArg("val") ? server.arg("val") : "";
    
    processCommand(cmd, val);
  }
  server.send(200, "text/plain", "OK");
}

void setup() {
  Serial.begin(115200);
  delay(1000);
  
  Serial.println("\n=======================================================");
  Serial.println("  DIAGNÓSTICO INTERACTIVO: MOTOR 28BYJ-48 + SENSOR IR  ");
  Serial.println("=======================================================");
  Serial.printf("Pines Motor: IN1=%d, IN2=%d, IN3=%d, IN4=%d\n", MOTOR_IN1, MOTOR_IN2, MOTOR_IN3, MOTOR_IN4);
  Serial.printf("Final de carrera: GPIO %d | Sensor IR: GPIO %d\n", MOTOR_LIMIT_SWITCH_PIN, IR_SENSOR_PIN);
  Serial.println("-------------------------------------------------------");

  pinMode(MOTOR_LIMIT_SWITCH_PIN, INPUT_PULLUP);
  pinMode(IR_SENSOR_PIN, INPUT_PULLUP);
  pinMode(LED_INDICATOR_PIN, OUTPUT);
  digitalWrite(LED_INDICATOR_PIN, LOW);

  // Conectar WiFi
  WiFi.begin(ssid, password);
  Serial.print("Conectando a WiFi Fibrasky...");
  int wifiTimeout = 0;
  while (WiFi.status() != WL_CONNECTED && wifiTimeout < 20) {
    delay(500);
    Serial.print(".");
    wifiTimeout++;
  }
  
  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\n[WiFi] ¡Conectado con éxito!");
    Serial.print("[WiFi] IP local: ");
    Serial.println(WiFi.localIP());
  } else {
    Serial.println("\n[WiFi] No se pudo conectar.");
  }
  Serial.println();

  // Iniciar servidor web y configurar rutas
  server.on("/", HTTP_GET, handleRoot);
  server.on("/status", HTTP_GET, handleStatus);
  server.on("/action", HTTP_GET, handleAction);
  server.begin();
  
  webLog("[SYSTEM] Servidor Web iniciado en puerto 80.");

  // Calibración inicial automática
  homeStepper();
}

void loop() {
  // Procesar peticiones Web
  server.handleClient();

  // Leer y procesar comandos seriales
  readSerialCommand();

  // 2. Ejecutar movimiento si está activo
  if (releasingActive) {
    if (stepper.distanceToGo() != 0) {
      stepper.run();
      
      // Fase de pasos extras
      if (finishingRelease) {
        if (stepper.distanceToGo() == 0) {
          stopMotorCoils();
          releasingActive = false;
          finishingRelease = false;
          
          long stepsTaken = stepper.currentPosition();
          char buf[120];
          sprintf(buf, "[ FIN ] Giro completo con %d pasos extras. Pasos avanzados totales: %ld", extraStepsAfterDetect, stepsTaken);
          webLog(buf);
          
          delay(1000);
          digitalWrite(LED_INDICATOR_PIN, LOW);
        }
        return;
      }
      
      bool currentIrState = (digitalRead(IR_SENSOR_PIN) == IR_ACTIVE_STATE);
      unsigned long now = millis();
      
      // A. La bola ingresa al sensor
      if (currentIrState && !ballInSensor) {
        if (now - lastBallDetectTime > BALL_COOLDOWN_MS) {
          ballInSensor = true;
          digitalWrite(LED_INDICATOR_PIN, HIGH);
          webLog(" [ SENSANDO ] Bola cruzando la barrera...");
        }
      }
      
      // B. La bola sale y libera el sensor
      if (!currentIrState && ballInSensor) {
        ballInSensor = false;
        lastBallDetectTime = now;
        detectedBallCount++;
        
        char buf[80];
        sprintf(buf, "[ DETECCIÓN ] ¡Bola %d liberada! (%d de %d)", detectedBallCount, detectedBallCount, targetBallCount);
        webLog(buf);
        
        if (detectedBallCount >= targetBallCount) {
          if (extraStepsAfterDetect > 0) {
            long targetPos = stepper.currentPosition() + (extraStepsAfterDetect * directionMultiplier);
            stepper.moveTo(targetPos);
            finishingRelease = true;
            char b[80];
            sprintf(b, "[INFO] Avanzando %d pasos extras antes de frenar...", extraStepsAfterDetect);
            webLog(b);
          } else {
            // Parar inmediatamente
            stepper.moveTo(stepper.currentPosition());
            stopMotorCoils();
            releasingActive = false;
            
            long stepsTaken = stepper.currentPosition();
            char b[120];
            sprintf(b, "[ FIN ] Se soltaron las %d bolas programadas. Pasos avanzados totales: %ld", targetBallCount, stepsTaken);
            webLog(b);
            
            delay(1000);
            digitalWrite(LED_INDICATOR_PIN, LOW);
          }
        } else {
          digitalWrite(LED_INDICATOR_PIN, LOW);
        }
      }
    } else {
      // Límite de seguridad alcanzado
      stopMotorCoils();
      releasingActive = false;
      finishingRelease = false;
      char buf[120];
      sprintf(buf, "[ ERROR ] Límite alcanzado. Solo se detectaron %d de %d bolas.", detectedBallCount, targetBallCount);
      webLog(buf);
    }
  }

  // 3. Monitor del sensor IR en vivo (si está inactivo el motor)
  if (sensorMonitorActive && !releasingActive) {
    static unsigned long lastMonitorPrint = 0;
    static int lastMonitorVal = -1;
    int currentVal = digitalRead(IR_SENSOR_PIN);
    
    if (currentVal != lastMonitorVal || millis() - lastMonitorPrint > 250) {
      lastMonitorVal = currentVal;
      lastMonitorPrint = millis();
      
      bool isDetected = (currentVal == IR_ACTIVE_STATE);
      digitalWrite(LED_INDICATOR_PIN, isDetected ? HIGH : LOW);
      
      char buf[120];
      sprintf(buf, "[MONITOR IR] Pin 15: %s (%s) | Haz: %s",
              currentVal == HIGH ? "HIGH (~3.3V)" : "LOW (0V / GND)",
              isDetected ? "OBSTRUIDO" : "LIBRE",
              isDetected ? "████████████" : "------------");
      webLog(buf);
    }
  }
}

// ==========================================
// CONTROL DE BOBINAS (FRENADO / REPOSO)
// ==========================================
void stopMotorCoils() {
  if (keepLockedOnHalt) {
    // Mantener las salidas energizadas en el último paso para bloquear el motor
    stepper.enableOutputs(); 
  } else {
    // Escribir LOW en todos los pines IN1-IN4 (apaga los transistores ULN2003)
    stepper.disableOutputs(); 
  }
}

// ==========================================
// PROCESADO DE COMANDOS COMPARTIDO (SERIAL & WEB)
// ==========================================
void processCommand(String cmd, String val) {
  cmd.toUpperCase();
  char cmdChar = cmd[0];
  
  if (cmdChar >= '1' && cmdChar <= '5') {
    if (!releasingActive) {
      int count = cmdChar - '0';
      startBallRelease(count);
    } else {
      webLog("[!] El motor ya está girando.");
    }
  }
  else if (cmd == "CMD" && val.length() > 0) {
    char subCmd = toupper(val[0]);
    if (subCmd >= '1' && subCmd <= '5') {
      if (!releasingActive) {
        int count = subCmd - '0';
        startBallRelease(count);
      } else {
        webLog("[!] El motor ya está girando.");
      }
    }
    else if (subCmd == 'H') {
      homeStepper();
    }
    else if (subCmd == 'S') {
      stopMotorEmergency();
    }
    else if (subCmd == 'I') {
      directionMultiplier = -directionMultiplier;
      char buf[100];
      sprintf(buf, "[CONFIG] Dirección invertida. Multiplicador: %d", directionMultiplier);
      webLog(buf);
    }
    else if (subCmd == 'T') {
      sensorMonitorActive = !sensorMonitorActive;
      if (sensorMonitorActive) {
        webLog("[MONITOR IR] Modo monitor INICIADO. Pasá un objeto para probar.");
      } else {
        webLog("[MONITOR IR] Modo monitor APAGADO.");
        digitalWrite(LED_INDICATOR_PIN, LOW);
      }
    }
  }
  else if (cmdChar == 'H') {
    homeStepper();
  } 
  else if (cmdChar == 'S') {
    stopMotorEmergency();
  }
  else if (cmdChar == 'I') {
    directionMultiplier = -directionMultiplier;
    char buf[100];
    sprintf(buf, "[CONFIG] Dirección invertida. Multiplicador: %d", directionMultiplier);
    webLog(buf);
  }
  else if (cmdChar == 'V') {
    float fVal = val.toFloat();
    if (fVal >= 100.0 && fVal <= 1200.0) {
      releaseSpeed = fVal;
      char buf[100];
      sprintf(buf, "[CONFIG] Velocidad máxima: %.1f pasos/seg", releaseSpeed);
      webLog(buf);
    } else {
      webLog("[ERROR] Rango de velocidad permitido: 100 a 1200.");
    }
  }
  else if (cmdChar == 'A') {
    float fVal = val.toFloat();
    if (fVal >= 50.0 && fVal <= 800.0) {
      releaseAccel = fVal;
      char buf[100];
      sprintf(buf, "[CONFIG] Aceleración: %.1f pasos/seg^2", releaseAccel);
      webLog(buf);
    } else {
      webLog("[ERROR] Rango de aceleración permitido: 50 a 800.");
    }
  }
  else if (cmdChar == 'P') {
    int iVal = val.toInt();
    if (iVal >= 0 && iVal <= 3000) {
      extraStepsAfterDetect = iVal;
      char buf[100];
      sprintf(buf, "[CONFIG] Pasos extras: %d pasos", extraStepsAfterDetect);
      webLog(buf);
    } else {
      webLog("[ERROR] Rango permitido: 0 a 3000.");
    }
  }
  else if (cmdChar == 'L') {
    int iVal = val.toInt();
    keepLockedOnHalt = (iVal == 1);
    if (!releasingActive) {
      stopMotorCoils();
    }
    webLog(keepLockedOnHalt ? "[CONFIG] Traba de motor: ACTIVADA" : "[CONFIG] Traba de motor: DESACTIVADA");
  }
}

// ==========================================
// COMANDOS DEL MONITOR SERIAL
// ==========================================
void readSerialCommand() {
  if (Serial.available() > 0) {
    String input = Serial.readStringUntil('\n');
    input.trim();
    if (input.length() == 0) return;
    
    String cmd = input.substring(0, 1);
    String val = input.substring(1);
    
    // Si la entrada es un número del 1 al 5
    if (cmd[0] >= '1' && cmd[0] <= '5') {
      processCommand(cmd, "");
    } else {
      processCommand(cmd, val);
    }
  }
}

// ==========================================
// CALIBRACIÓN (HOMING)
// ==========================================
void homeStepper() {
  webLog("[HOMING] Iniciando calibración del dispensador...");
  
  if (digitalRead(MOTOR_LIMIT_SWITCH_PIN) == LOW) {
    webLog("[HOMING] Aspa ya alineada. Posición puesta a CERO.");
    stepper.setCurrentPosition(0);
    stopMotorCoils();
    return;
  }

  stepper.enableOutputs();
  delay(50);
  
  stepper.setMaxSpeed(releaseSpeed * 0.5);
  stepper.setAcceleration(releaseAccel * 0.5);
  
  // Buscar en reversa (un rango amplio ya que este motor da más pasos)
  stepper.move(-12000 * directionMultiplier); 
  
  int stepsTaken = 0;
  int safetyLimit = 12000;
  
  while (digitalRead(MOTOR_LIMIT_SWITCH_PIN) == HIGH && stepsTaken < safetyLimit && stepper.distanceToGo() != 0) {
    stepper.run();
    stepsTaken++;
  }
  
  stepper.stop();
  while (stepper.distanceToGo() != 0) {
    stepper.run();
  }
  
  stepper.setCurrentPosition(0);
  stopMotorCoils();
  
  if (stepsTaken >= safetyLimit) {
    webLog("[ ALERTA ] Homing falló por límite de seguridad.");
  } else {
    webLog("[HOMING] ¡Calibración exitosa! Aspa alineada en posición CERO.");
  }
}

// ==========================================
// INICIO DE AVANCE CONTINUO
// ==========================================
void startBallRelease(int count) {
  targetBallCount = count;
  detectedBallCount = 0;
  finishingRelease = false;
  
  ballInSensor = (digitalRead(IR_SENSOR_PIN) == IR_ACTIVE_STATE);
  lastBallDetectTime = 0;
  
  char buf[120];
  sprintf(buf, "[TEST] Soltando %d bola(s)... (V=%.1f, A=%.1f, P=%d)", 
          targetBallCount, releaseSpeed, releaseAccel, extraStepsAfterDetect);
  webLog(buf);
  
  stepper.enableOutputs();
  delay(20);
  
  stepper.setMaxSpeed(releaseSpeed);
  stepper.setAcceleration(releaseAccel);
  stepper.setCurrentPosition(0);
  
  // Le damos un rango amplio al motor incluyendo los pasos extras
  // Este motor unipolar requiere muchos más pasos por revolución (4096 en HALF4WIRE)
  long totalSteps = (12000 * targetBallCount) + extraStepsAfterDetect;
  stepper.move(totalSteps * directionMultiplier); 
  
  releasingActive = true;
}

// ==========================================
// PARADA DE EMERGENCIA
// ==========================================
void stopMotorEmergency() {
  if (releasingActive) {
    stepper.moveTo(stepper.currentPosition());
    stopMotorCoils();
    releasingActive = false;
    finishingRelease = false;
    webLog("[!] DETENCIÓN DE EMERGENCIA EJECUTADA por el usuario.");
  }
}
