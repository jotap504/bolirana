#include <WiFi.h>
#include <WebServer.h>
#include <AccelStepper.h>
#include <Wire.h>
#include <Adafruit_NeoPixel.h>
#include <Adafruit_MCP23X17.h>
#include <ArduinoJson.h>

/*
=============================================================================
DEPENDENCIAS (Instalar desde el Gestor de Librerías de Arduino):
1. "Adafruit MCP23017 Arduino Library" (o Adafruit MCP23X17)
2. "Adafruit NeoPixel"
3. "ArduinoJson" (Compatible con v6 y v7)
=============================================================================
*/
// Prototipos de funciones optimizadas para I2C
void readSensors(uint16_t gpioState);
void readButtons(uint16_t gpioState);

// ==========================================
// CONFIGURACIÓN DE RED WIFI
// ==========================================
const char* ssid     = "Fibrasky";
// Mantener la contraseña original del usuario
const char* password = "corsa000";

// ==========================================
// CONFIGURACIÓN DE HARDWARE Y PINES (ESP32)
// ==========================================
// Pines para el motor unipolar 28BYJ-48 con placa ULN2003 (Dispensador en máquina de producción)
#define MOTOR_IN1 26
#define MOTOR_IN2 25
#define MOTOR_IN3 33
#define MOTOR_IN4 32
#define MOTOR_LIMIT_SWITCH_PIN 27 // Pin del microswitch de tope/calibración si aplica (INPUT_PULLUP)

// Pin físico dedicado a la Tira NeoPixel (WS2812B/W) en el ESP32
#define NEOPIXEL_PIN 4
#define NUM_LEDS     60 // Cantidad de LEDs en la tira

// Pines del Bus I2C
#define I2C_SDA      21
#define I2C_SCL      22
#define MCP_ADDRESS  0x20 // Dirección I2C por defecto (A0, A1, A2 a GND)

// Pines de botones físicos en el Expansor MCP23017 (Puerto B)
#define BTN_START_PIN 11  // PB3 (index 11)
#define BTN_PAUSE_PIN 12  // PB4 (index 12)
#define COIN_PIN 18       // Sigue en GPIO directo del ESP32 por soporte de interrupción/rapidez

// Sensor de proximidad radar HLK-LD2410 (Caso A: salida digital en GPIO 16/RX2)
#define PROXIMITY_PIN 16

// ==========================================
// CONFIGURACIÓN DE SENSORES EN MCP23017
// ==========================================
// Tipo de Entrada: INPUT (si tienes pull-downs/pull-ups externos de 10k) o INPUT_PULLUP
#define SENSOR_PIN_MODE    INPUT
// Polaridad del Sensor: HIGH (activo al pasar bola, normalmente LOW) o LOW (activo en bajo)
#define SENSOR_ACTIVE_STATE HIGH
// Tiempo de enfriamiento / debounce por sensor (en milisegundos)
// Reducido a 250ms para permitir conteo rápido de bolas sucesivas
#define SENSOR_COOLDOWN_MS 250

// Cantidad de sensores
#define NUM_SENSORS 12

// Mapeo de pines del MCP23017 para cada sensor
const int SENSOR_PINS[NUM_SENSORS] = {
  0,  // "rana"   -> PA0 (0)
  1,  // "sapo"   -> PA1 (1)
  2,  // "fosa_1" -> PA2 (2)
  3,  // "fosa_2" -> PA3 (3)
  4,  // "fosa_3" -> PA4 (4)
  5,  // "fosa_4" -> PA5 (5)
  6,  // "fosa_5" -> PA6 (6)
  7,  // "fosa_6" -> PA7 (7)
  8,  // "fosa_7" -> PB0 (8)
  9,  // "fosa_8" -> PB1 (9)
  10, // "fosa_9" -> PB2 (10)
  15  // "cero"   -> PB7 (15)
};

// Lista de zonas y su correspondencia con los pines del MCP23017
const char* SENSOR_IDS[NUM_SENSORS] = {
  "rana",
  "sapo",
  "fosa_1",
  "fosa_2",
  "fosa_3",
  "fosa_4",
  "fosa_5",
  "fosa_6",
  "fosa_7",
  "fosa_8",
  "fosa_9",
  "cero"
};

// ==========================================
// ESTADOS E ILUMINACIÓN NEOPIXEL (Máquina de Estados)
// ==========================================
enum LedState {
  LED_IDLE,
  LED_SCORING,
  LED_PROXIMITY_ALERT,
  LED_GAME_OVER
};

LedState currentLedState = LED_IDLE;
unsigned long ledStateStartTime = 0;
unsigned long ledStateDuration = 0;
String currentScoringZone = "";

// Inicialización de componentes principales
Adafruit_MCP23X17 mcp;
bool mcpInitialized = false; // Bandera de estado para evitar llamadas colgadas de I2C
Adafruit_NeoPixel strip(NUM_LEDS, NEOPIXEL_PIN, NEO_GRB + NEO_KHZ800);
// Inicialización del motor 28BYJ-48 en modo unipolar HALF4WIRE (Idéntico a test_28byj48_ir.ino)
// Secuencia requerida por AccelStepper: IN1, IN3, IN2, IN4
AccelStepper stepper(AccelStepper::HALF4WIRE, MOTOR_IN1, MOTOR_IN3, MOTOR_IN2, MOTOR_IN4);
WebServer server(80);

// ==========================================
// CONFIGURACIÓN DE DISPENSADOR DE BOLAS (MOTOR 28BYJ-48 / ULN2003)
// ==========================================
// Configuración dinámica del motor dispensador (recibida por JSON desde Backend)
float motorSpeed = 800.0;
float motorAccel = 400.0;
int motorDirection = 1;       // 1 = Horario (Normal), -1 = Antihorario (Invertido)
int motorExtraSteps = 50;     // Pasos adicionales a avanzar tras detectar el clic de tope (50 pasos)

void stopMotorCoils() {
  digitalWrite(MOTOR_IN1, LOW);
  digitalWrite(MOTOR_IN2, LOW);
  digitalWrite(MOTOR_IN3, LOW);
  digitalWrite(MOTOR_IN4, LOW);
  stepper.disableOutputs();
}

void homeStepper() {
  Serial.println("{\"event\":\"homing_start\"}");
  
  // 1. Si ya está activo el microswitch (LOW), no hace nada y permanece en cero
  if (digitalRead(MOTOR_LIMIT_SWITCH_PIN) == LOW) {
    Serial.println("{\"event\":\"homing_done\",\"steps_taken\":0}");
    stepper.setCurrentPosition(0);
    stopMotorCoils();
    return;
  }

  // 2. Si no está activo, esperar 1 segundo de estabilización
  Serial.println("[HOMING] Buscando final de carrera...");
  delay(1000);
  
  // 3. Retroceder despacio buscando el microswitch
  stepper.enableOutputs();
  delay(50);
  
  stepper.setMaxSpeed(motorSpeed * 0.5);
  stepper.setAcceleration(motorAccel * 0.5);
  stepper.move(-12000 * motorDirection); // Reversa (rango de pasos para 28BYJ-48 en HALF4WIRE)
  
  unsigned long startTime = millis();
  unsigned long timeoutMs = 15000;
  
  while (digitalRead(MOTOR_LIMIT_SWITCH_PIN) == HIGH && (millis() - startTime < timeoutMs) && stepper.distanceToGo() != 0) {
    stepper.run();
  }
  
  stepper.stop();
  while (stepper.distanceToGo() != 0) {
    stepper.run();
  }
  
  long actualSteps = abs(stepper.currentPosition());
  stepper.setCurrentPosition(0);
  stopMotorCoils();
  
  if (digitalRead(MOTOR_LIMIT_SWITCH_PIN) == LOW) {
    Serial.print("{\"event\":\"homing_done\",\"steps_taken\":");
    Serial.print(actualSteps);
    Serial.println("}");
  } else {
    Serial.println("{\"event\":\"homing_failed\",\"reason\":\"timeout\"}");
  }
}

void releaseBalls(int count) {
  Serial.print("[COMMAND] Dispensando ");
  Serial.print(count);
  Serial.println(" bola(s)...");
  
  Serial.print("{\"event\":\"motor_start\",\"balls\":");
  Serial.print(count);
  Serial.println("}");
  
  stepper.enableOutputs();
  delay(50);
  
  stepper.setMaxSpeed(motorSpeed);
  stepper.setAcceleration(motorAccel);
  
  int clicks = 0;
  bool lastState = digitalRead(MOTOR_LIMIT_SWITCH_PIN);
  unsigned long lastClickTime = millis();
  unsigned long releaseStartTime = millis();
  unsigned long maxDurationMs = (unsigned long)count * 15000;
  
  stepper.setCurrentPosition(0);
  stepper.move(15000 * count * motorDirection); // Mover adelante
  
  while (clicks < count && (millis() - releaseStartTime < maxDurationMs) && stepper.distanceToGo() != 0) {
    stepper.run();
    
    bool currentState = digitalRead(MOTOR_LIMIT_SWITCH_PIN);
    
    // Flanco de bajada: de HIGH a LOW (aspa entra al haz del sensor IR)
    if (lastState == HIGH && currentState == LOW) {
      if (millis() - lastClickTime >= 450) { // Filtro de tiempo real: exige mínimo 450ms entre aspas
        clicks++;
        lastClickTime = millis();
        Serial.print("[DEBUG] Aspa ");
        Serial.print(clicks);
        Serial.println(" posicionada sobre el sensor IR");
      }
    }
    
    lastState = currentState;
  }
  
  // Freno suave inmediato apenas el aspa presiona/tapa el sensor (LOW)
  stepper.stop();
  while (stepper.distanceToGo() != 0) {
    stepper.run();
  }
  
  // Si hay pasos extra configurados (+ avanzar / - retroceder)
  if (clicks > 0 && motorExtraSteps != 0) {
    Serial.print("[DEBUG] Ejecutando ");
    Serial.print(motorExtraSteps);
    Serial.println(" pasos extra de ajuste posicional...");
    stepper.move(motorExtraSteps * motorDirection);
    while (stepper.distanceToGo() != 0) {
      stepper.run();
    }
  }
  
  stopMotorCoils(); // Apagar bobinas. Queda posicionado exactamente sobre el sensor IR.
  
  Serial.print("[DEBUG] Dispensación finalizada. Clics contados: ");
  Serial.println(clicks);
  
  if (clicks >= count || stepper.distanceToGo() == 0) {
    Serial.println("{\"event\":\"motor_done\"}");
  } else {
    Serial.println("{\"event\":\"motor_error\",\"reason\":\"switch_timeout\"}");
  }
}

bool motorWasMoving = false;
void checkMotorStatus() {
  if (stepper.distanceToGo() != 0) {
    if (!motorWasMoving) {
      motorWasMoving = true;
      stepper.enableOutputs();
    }
  } else {
    if (motorWasMoving) {
      motorWasMoving = false;
      stopMotorCoils();
      Serial.println("{\"event\":\"motor_done\"}");
    }
  }
}

// Control de debounce/estado de sensores
unsigned long lastTriggerTime[NUM_SENSORS] = {0};
bool lastSensorState[NUM_SENSORS] = {false};

// Control del estado del sensor de proximidad (HLK-LD2410)
// Configuración inteligente del Radar HLK-LD2410C por Puerto Serial2
int radarMinDistCm = 100;       // Distancia mínima para detectar jugador (1 metro)
int radarMaxDistCm = 250;       // Distancia máxima para detectar jugador (2.5 metros)
int radarMovingThreshold = 55;  // Energía mínima para movimiento (ignora pelotas)
int radarStaticThreshold = 35;  // Energía mínima estática (detecta persona quieta)
int radarTriggerMs = 1000;      // Tiempo continuo requerido para disparar alarma (ms)

// Variables para el parsing del HLK-LD2410 (Máquina de estados no bloqueante)
enum RadarState { RADAR_WAIT_HEADER, RADAR_READ_LEN, RADAR_READ_PAYLOAD };
RadarState radarState = RADAR_WAIT_HEADER;
uint16_t radarPayloadLen = 0;
uint16_t radarBytesRead = 0;
uint8_t radarHeaderCount = 0;
uint8_t radarRxBuf[64] = {0};

bool lastProximityState = false;
unsigned long radarDetectionStart = 0;
bool radarRawDetected = false;

// ==========================================
// INTERFAZ DASHBOARD ENRIQUECIDA (HTML / CSS / JS)
// ==========================================
const char index_html[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0, user-scalable=no">
    <title>Dashboard Dispensador y Diagnóstico</title>
    <style>
        body { font-family: -apple-system, BlinkMacSystemFont, "Segoe UI", Roboto, sans-serif; text-align: center; background-color: #121212; color: #e0e0e0; padding: 15px; margin: 0; touch-action: manipulation; }
        .container { max-width: 450px; margin: 10px auto; background: #1e1e1e; padding: 20px; border-radius: 12px; box-shadow: 0 4px 15px rgba(0,0,0,0.6); }
        h2 { color: #00adb5; margin-bottom: 20px; font-size: 1.5em; text-transform: uppercase; letter-spacing: 1px; }
        .section { background: #2a2a2a; padding: 15px; border-radius: 8px; margin-bottom: 15px; }
        .section-title { font-size: 0.85em; color: #888; text-transform: uppercase; letter-spacing: 1px; margin-bottom: 15px; text-align: left; border-bottom: 1px solid #444; padding-bottom: 5px; }
        
        .reference-table { width: 100%; border-collapse: collapse; margin-bottom: 15px; font-size: 0.9em; }
        .reference-table th, .reference-table td { border: 1px solid #444; padding: 8px; text-align: center; }
        .reference-table th { background-color: #333; color: #00adb5; }
        .reference-table td { color: #ccc; }

        input { width: 90%; padding: 12px; margin-bottom: 15px; border: 1px solid #333; background: #222; color: #fff; border-radius: 6px; text-align: center; font-size: 1.2em; box-sizing: border-box; }
        .btn-group { display: flex; justify-content: space-between; gap: 10px; }
        button { flex: 1; padding: 12px; border: none; border-radius: 6px; font-weight: bold; font-size: 0.95em; cursor: pointer; transition: background 0.2s; user-select: none; -webkit-user-select: none; }
        
        .btn-step { background-color: #00adb5; color: #fff; }
        .btn-step:active { background-color: #008288; }
        .btn-hold { background-color: #e6a817; color: #fff; }
        .btn-hold:active { background-color: #c58e0f; }

        .sensors-grid { display: grid; grid-template-columns: repeat(2, 1fr); gap: 10px; margin-top: 10px; }
        .sensor-badge { padding: 10px; border-radius: 6px; background: #202020; font-size: 0.85em; font-weight: bold; text-align: left; border-left: 5px solid #444; display: flex; justify-content: space-between; align-items: center; transition: all 0.2s; }
        .sensor-badge.active { background: #2d1e18; border-left-color: #ff5722; color: #ff5722; box-shadow: 0 0 8px rgba(255,87,34,0.3); }
        .sensor-badge.inactive { background: #1a231a; border-left-color: #4caf50; color: #4caf50; }
        .indicator { width: 8px; height: 8px; border-radius: 50%; background: #444; }
        .sensor-badge.active .indicator { background: #ff5722; box-shadow: 0 0 8px #ff5722; }
        .sensor-badge.inactive .indicator { background: #4caf50; }
    </style>
</head>
<body>
    <div class="container">
        <h2>Panel Bolirana I2C</h2>
        
        <div class="section">
            <div class="section-title">Diagnóstico de Sensores (MCP23017)</div>
            <div class="sensors-grid" id="sensorsGrid">
                <!-- Se poblará de forma dinámica -->
            </div>
        </div>

        <div class="section">
            <div class="section-title">Movimiento por Pasos</div>
            <table class="reference-table">
                <tr><th>Rotación</th><th>Pasos</th></tr>
                <tr><td>1 Vuelta (360&deg;)</td><td>4096</td></tr>
                <tr><td>1/2 Vuelta (180&deg;)</td><td>2048</td></tr>
                <tr><td>1/4 Vuelta (90&deg;)</td><td>1024</td></tr>
            </table>
            <input type="number" id="stepsInput" value="1024" min="1">
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
    </div>

    <script>
        const sensorNames = {
            "rana": "🐸 Rana (1000)",
            "sapo": "🦎 Sapo (500)",
            "fosa_1": "🎯 Fosa 1 (100)",
            "fosa_2": "🎯 Fosa 2 (50)",
            "fosa_3": "🎯 Fosa 3 (20)",
            "fosa_4": "🎯 Fosa 4 (10)",
            "fosa_5": "⚪ Fosa 5",
            "fosa_6": "⚪ Fosa 6",
            "fosa_7": "⚪ Fosa 7",
            "fosa_8": "⚪ Fosa 8",
            "fosa_9": "⚪ Fosa 9",
            "cero": "❌ Bola Cero",
            "proximity": "🚨 Radar Proximidad"
        };

        const grid = document.getElementById('sensorsGrid');
        grid.innerHTML = Object.keys(sensorNames).map(id => `
            <div class="sensor-badge" id="badge-${id}">
                <span>${sensorNames[id]}</span>
                <span class="indicator" id="ind-${id}"></span>
            </div>
        `).join('');

        function moveSteps(direction) {
            const steps = document.getElementById('stepsInput').value;
            fetch(`/step?dir=${direction}&steps=${steps}`).catch(err => console.error(err));
        }

        function setupHoldButton(buttonId, direction) {
            const btn = document.getElementById(buttonId);
            let isPressing = false;

            const startMove = (e) => {
                e.preventDefault();
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

            btn.addEventListener('touchstart', startMove);
            btn.addEventListener('touchend', stopMove);
            btn.addEventListener('touchcancel', stopMove);
            btn.addEventListener('mousedown', startMove);
            btn.addEventListener('mouseup', stopMove);
            btn.addEventListener('mouseleave', stopMove);
        }

        setupHoldButton('btnHoldFwd', 'forward');
        setupHoldButton('btnHoldRev', 'reverse');

        setInterval(() => {
            fetch('/status')
                .then(response => response.json())
                .then(data => {
                    for (const [id, active] of Object.entries(data)) {
                        const card = document.getElementById(`badge-${id}`);
                        if (card) {
                            if (active) {
                                card.className = "sensor-badge active";
                            } else {
                                card.className = "sensor-badge inactive";
                            }
                        }
                    }
                }).catch(err => console.error(err));
        }, 150);
    </script>
</body>
</html>
)rawliteral";

// ==========================================
// MANEJADORES DE RUTAS DEL SERVIDOR WEB
// ==========================================
void handleRoot() { 
  server.send_P(200, "text/html", index_html); 
}

void handleStep() {
  if (server.hasArg("steps") && server.hasArg("dir")) {
    long steps = server.arg("steps").toInt();
    if (server.arg("dir") == "reverse") stepper.move(-steps);
    else stepper.move(steps);
  }
  server.send(200, "text/plain", "OK");
}

void handleStartContinuous() {
  if (server.hasArg("dir")) {
    if (server.arg("dir") == "reverse") stepper.move(-1000000);
    else stepper.move(1000000);
  }
  server.send(200, "text/plain", "OK");
}

void handleStop() {
  stepper.stop(); 
  server.send(200, "text/plain", "OK");
}

void handleSensor() {
  // Retorna "1" si algún sensor está activo en este momento (retrocompatible)
  bool active = false;
  for (int i = 0; i < NUM_SENSORS; i++) {
    if (mcp.digitalRead(SENSOR_PINS[i]) == SENSOR_ACTIVE_STATE) {
      active = true;
      break;
    }
  }
  server.send(200, "text/plain", active ? "1" : "0");
}

void handleStatus() {
  // Retorna un objeto JSON con el estado en tiempo real de todos los sensores
  String json = "{";
  for (int i = 0; i < NUM_SENSORS; i++) {
    bool state = (mcp.digitalRead(SENSOR_PINS[i]) == SENSOR_ACTIVE_STATE);
    json += "\"" + String(SENSOR_IDS[i]) + "\":" + (state ? "true" : "false");
    json += ",";
  }
  // Agregar también el sensor de proximidad
  bool proxState = (digitalRead(PROXIMITY_PIN) == HIGH);
  json += "\"proximity\":" + String(proxState ? "true" : "false");
  json += "}";
  server.send(200, "application/json", json);
}

// ==========================================
// CONTROL DE EFECTOS LED NEOPIXEL
// ==========================================
void triggerScoringEffect(String zone) {
  currentLedState = LED_SCORING;
  ledStateStartTime = millis();
  currentScoringZone = zone;
  
  if (zone == "rana") {
    ledStateDuration = 1800; // 1.8 segundos para la Rana
  } else if (zone == "sapo") {
    ledStateDuration = 1200; // 1.2 segundos para el Sapo
  } else if (zone == "cero") {
    ledStateDuration = 1000; // 1 segundo para Bola Cero
  } else {
    ledStateDuration = 800;  // 800ms para fosas comunes
  }
}

void triggerEffect(String effectName, String zoneName) {
  if (effectName == "goal") {
    triggerScoringEffect(zoneName);
  } else if (effectName == "game_over") {
    currentLedState = LED_GAME_OVER;
    ledStateStartTime = millis();
  } else if (effectName == "idle") {
    currentLedState = LED_IDLE;
  }
}

void setProximityState(bool active) {
  if (active) {
    currentLedState = LED_PROXIMITY_ALERT;
  } else {
    currentLedState = LED_IDLE;
  }
}

// Generador de color de arcoíris (0 a 255)
uint32_t Wheel(byte WheelPos) {
  WheelPos = 255 - WheelPos;
  if(WheelPos < 85) {
    return strip.Color(255 - WheelPos * 3, 0, WheelPos * 3);
  }
  if(WheelPos < 170) {
    WheelPos -= 85;
    return strip.Color(0, WheelPos * 3, 255 - WheelPos * 3);
  }
  WheelPos -= 170;
  return strip.Color(WheelPos * 3, 255 - WheelPos * 3, 0);
}

void updateLeds() {
  static unsigned long lastLedUpdate = 0;
  unsigned long now = millis();
  
  // Limitar la tasa de refresco a un máximo de 30 FPS (cada 33ms) para no ahogar el buffer de interrupción Serial
  if (now - lastLedUpdate < 33) {
    return;
  }
  lastLedUpdate = now;
  
  switch (currentLedState) {
    case LED_IDLE: {
      // Respiración elegante en tono turquesa/cian (Aesthetica Premium)
      float brightness = (sin(now / 1200.0) + 1.0) / 2.0; // Oscila suavemente entre 0 y 1
      int r = 0;
      int g = 140 * (0.2 + 0.8 * brightness); // Mantiene una base mínima de brillo
      int b = 180 * (0.2 + 0.8 * brightness);
      
      for (int i = 0; i < NUM_LEDS; i++) {
        strip.setPixelColor(i, strip.Color(r, g, b));
      }
      break;
    }
    
    case LED_SCORING: {
      unsigned long elapsed = now - ledStateStartTime;
      if (elapsed > ledStateDuration) {
        currentLedState = LED_IDLE;
        return;
      }
      
      if (currentScoringZone == "rana") {
        // Destello Messi dorado brillante con destellos aleatorios (chispas/sparkles)
        for (int i = 0; i < NUM_LEDS; i++) {
          if (random(100) > 85) {
            strip.setPixelColor(i, strip.Color(255, 240, 150)); // Chispas blancas/doradas
          } else {
            // Decaimiento del brillo dorado
            float factor = 1.0 - ((float)elapsed / ledStateDuration);
            strip.setPixelColor(i, strip.Color(230 * factor, 170 * factor, 0));
          }
        }
      } else if (currentScoringZone == "sapo") {
        // Onda expansiva verde esmeralda
        float factor = 1.0 - ((float)elapsed / ledStateDuration);
        int center = (elapsed * NUM_LEDS) / ledStateDuration; // Propagación a lo largo de la tira
        
        for (int i = 0; i < NUM_LEDS; i++) {
          int distance = abs(i - center);
          if (distance < 5) {
            strip.setPixelColor(i, strip.Color(0, 255 * factor, 30 * factor));
          } else {
            strip.setPixelColor(i, strip.Color(0, 80 * factor, 10 * factor));
          }
        }
      } else if (currentScoringZone == "cero") {
        // Parpadeo intermitente rojo de advertencia/error
        bool on = (elapsed / 120) % 2 == 0;
        for (int i = 0; i < NUM_LEDS; i++) {
          strip.setPixelColor(i, on ? strip.Color(220, 0, 0) : strip.Color(30, 0, 0));
        }
      } else {
        // Fosas normales: destello azul eléctrico
        float factor = 1.0 - ((float)elapsed / ledStateDuration);
        for (int i = 0; i < NUM_LEDS; i++) {
          strip.setPixelColor(i, strip.Color(0, 100 * factor, 255 * factor));
        }
      }
      break;
    }
    
    case LED_PROXIMITY_ALERT: {
      // Parpadeo naranja/rojo alternado muy agresivo (Anti-Cheat)
      bool alt = (now / 150) % 2 == 0;
      for (int i = 0; i < NUM_LEDS; i++) {
        if ((i % 2 == 0) == alt) {
          strip.setPixelColor(i, strip.Color(255, 0, 0));     // Rojo
        } else {
          strip.setPixelColor(i, strip.Color(255, 90, 0));    // Naranja brillante
        }
      }
      break;
    }
    
    case LED_GAME_OVER: {
      // Ciclo de arcoíris festivo lento para celebrar el fin de la partida
      for (int i = 0; i < NUM_LEDS; i++) {
        byte wheelPos = ((i * 256 / NUM_LEDS) + (now / 15)) & 255;
        strip.setPixelColor(i, Wheel(wheelPos));
      }
      break;
    }
  }
  
  strip.show();
}

// ==========================================
// RECEPCIÓN Y PARSEO DE COMANDOS SERIALES JSON
// ==========================================
void readIncomingSerial() {
  if (Serial.available() > 0) {
    String input = Serial.readStringUntil('\n');
    input.trim();
    
    if (input.length() > 0 && input.startsWith("{")) {
      // Soporte compatible de memoria para ArduinoJson v6 y v7
      #if ARDUINOJSON_VERSION_MAJOR >= 7
        JsonDocument doc;
      #else
        StaticJsonDocument<256> doc;
      #endif
      
      DeserializationError error = deserializeJson(doc, input);
      if (!error) {
        const char* type = doc["type"];
        if (type) {
          if (strcmp(type, "effect") == 0) {
            const char* name = doc["name"];
            const char* zone = doc["zone"];
            triggerEffect(name ? name : "", zone ? zone : "");
          } else if (strcmp(type, "proximity") == 0) {
            bool active = doc["active"];
            setProximityState(active);
          } else if (strcmp(type, "config_proximity") == 0) {
            if (doc.containsKey("min_dist")) radarMinDistCm = doc["min_dist"];
            if (doc.containsKey("max_dist")) radarMaxDistCm = doc["max_dist"];
            if (doc.containsKey("moving_th")) radarMovingThreshold = doc["moving_th"];
            if (doc.containsKey("static_th")) radarStaticThreshold = doc["static_th"];
            if (doc.containsKey("trigger_ms")) radarTriggerMs = doc["trigger_ms"];
            Serial.println("[SYSTEM] Configuración de radar actualizada desde el Backend.");
          } else if (strcmp(type, "config_motor") == 0) {
            if (doc.containsKey("speed")) motorSpeed = doc["speed"];
            if (doc.containsKey("accel")) motorAccel = doc["accel"];
            if (doc.containsKey("dir")) motorDirection = doc["dir"];
            if (doc.containsKey("extra_steps")) motorExtraSteps = doc["extra_steps"];
            Serial.println("[SYSTEM] Configuración de motor actualizada desde el Backend.");
          } else if (strcmp(type, "coin") == 0) {
            // Si el backend envía confirmación de ficha
            triggerEffect("goal", "fosa_2"); // Flash de rebote
          } else if (strcmp(type, "release_balls") == 0) {
            int count = doc["count"];
            if (count > 0) {
              Serial.print("[COMMAND] Recibida orden de dispensar ");
              Serial.print(count);
              Serial.println(" bola(s)");
              releaseBalls(count);
            }
          } else if (strcmp(type, "step") == 0) {
            int steps = doc["steps"];
            if (steps != 0) {
              Serial.print("[COMMAND] Recibida orden de movimiento manual: ");
              Serial.print(steps);
              Serial.println(" pasos");
              stepper.setMaxSpeed(motorSpeed);
              stepper.setAcceleration(motorAccel);
              stepper.enableOutputs();
              stepper.move(steps * motorDirection);
            }
          } else if (strcmp(type, "home") == 0 || strcmp(type, "home_stepper") == 0) {
            Serial.println("[COMMAND] Recibida orden de calibracion / homing");
            homeStepper();
          } else if (strcmp(type, "test_coil") == 0) {
            int coil = doc["coil"];
            stopMotorCoils();
            stepper.disableOutputs();
            
            if (coil == 1) digitalWrite(MOTOR_IN1, HIGH);
            else if (coil == 2) digitalWrite(MOTOR_IN2, HIGH);
            else if (coil == 3) digitalWrite(MOTOR_IN3, HIGH);
            else if (coil == 4) digitalWrite(MOTOR_IN4, HIGH);
            
            Serial.print("[TEST] Probando bobina IN");
            Serial.print(coil);
            Serial.println(" encendida por 5 segundos...");
            
            delay(5000);
            stopMotorCoils();
            Serial.println("[TEST] Bobina apagada.");
          }
        }
      }
    }
  }
}

// ==========================================
// LECTURA DE SENSORES CON DEBOUNCE INDEPENDIENTE (LECTURA POR BLOQUE OPTIMIZADA)
// ==========================================
void readSensors(uint16_t gpioState) {
  unsigned long now = millis();
  
  for (int i = 0; i < NUM_SENSORS; i++) {
    // Obtener la lectura del pin desde el estado completo leído (1 << pin)
    // El MCP23017 activa en HIGH o LOW según configuración física
    bool pinReading = (gpioState & (1 << SENSOR_PINS[i])) != 0;
    
    // Convertir a booleano de activación según polaridad
    bool activeState = (pinReading == SENSOR_ACTIVE_STATE);
    
    // Detección de flanco de subida (transición de inactivo a activo)
    if (activeState && !lastSensorState[i]) {
      if (now - lastTriggerTime[i] > SENSOR_COOLDOWN_MS) {
        lastTriggerTime[i] = now;
        
        // 1. Reportar de inmediato al backend con formato JSON normalizado
        Serial.print("{\"event\":\"sensor\",\"target\":\"");
        Serial.print(SENSOR_IDS[i]);
        Serial.println("\"}");
        
        // 2. Disparar efecto visual local inmediato (sin latencia de red)
        triggerScoringEffect(SENSOR_IDS[i]);
      }
    }
    
    lastSensorState[i] = activeState;
  }
}

// ==========================================
// SETUP
// ==========================================
void setup() {
  Serial.begin(115200);
  Serial.println("\n=======================================================");
  Serial.println("BOLIRANA ARCADE - FIRMWARE v1.4.0 (PROXIMITY DIAGS)");
  Serial.println("=======================================================");
  
  // Inicialización del bus I2C manual con pines custom
  Wire.begin(I2C_SDA, I2C_SCL);
  Wire.setClock(400000); // Forzar reloj I2C a 400kHz para máxima rapidez en sensores

  
  // Configuración del monedero (PULLUP interno)
  pinMode(COIN_PIN, INPUT_PULLUP);
  
  // Iniciar UART2 para el radar HLK-LD2410 (Baud 256000, RX=GPIO 16, TX=GPIO 17)
  Serial2.begin(256000, SERIAL_8N1, 16, 17);
  Serial.println("[SYSTEM] UART2 para radar HLK-LD2410 configurado en 256000 baud.");
  
  // Inicialización de LEDs NeoPixel
  strip.begin();
  strip.setBrightness(180); // Brillo alto pero seguro (0-255)
  strip.show();             // Apagar todos inicialmente
  
  // Inicialización del Expansor I2C MCP23017
  Serial.println("Inicializando expansor MCP23017...");
  if (!mcp.begin_I2C(MCP_ADDRESS)) {
    Serial.println("ERROR: No se encontró el chip MCP23017 en la dirección especificada.");
    
    // Parpadear en rojo alternado para alertar sobre error físico de I2C
    for (int j = 0; j < 10; j++) {
      for (int i = 0; i < NUM_LEDS; i++) {
        strip.setPixelColor(i, (j % 2 == 0) ? strip.Color(255, 0, 0) : strip.Color(0, 0, 0));
      }
      strip.show();
      delay(300);
    }
    // Continuar en bucle de error sin congelar completamente (para poder usar stepper o wifi si estuvieran ok)
  } else {
    Serial.println("MCP23017 Inicializado con éxito.");
    mcpInitialized = true;
    
    // Configurar los pines de sensores en el expansor
    for (int i = 0; i < NUM_SENSORS; i++) {
      mcp.pinMode(SENSOR_PINS[i], SENSOR_PIN_MODE);
    }
    
    // Configurar los botones físicos en el expansor
    mcp.pinMode(BTN_START_PIN, INPUT_PULLUP);
    mcp.pinMode(BTN_PAUSE_PIN, INPUT_PULLUP);
  }

  // Apagar bobinas del motor 28BYJ-48 por defecto para evitar calentamiento
  stopMotorCoils();

  // Configurar pin del microswitch de tope de la hélice con PULL-UP interno
  pinMode(MOTOR_LIMIT_SWITCH_PIN, INPUT_PULLUP);

  // Configuración de velocidad y aceleración para el motor 28BYJ-48 (ULN2003 en HALF4WIRE)
  stepper.setMaxSpeed(800.0);
  stepper.setAcceleration(400.0);

  // Conexión WiFi
  WiFi.begin(ssid, password);
  Serial.print("Conectando al WiFi");
  int wifiTimeout = 0;
  while (WiFi.status() != WL_CONNECTED && wifiTimeout < 20) {
    delay(500);
    Serial.print(".");
    wifiTimeout++;
  }
  
  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\n¡WiFi Conectado!");
    Serial.print("IP del Dashboard: ");
    Serial.println(WiFi.localIP());
  } else {
    Serial.println("\nNo se pudo conectar al WiFi. Iniciando en modo local serial únicamente.");
  }

  // Rutas del Servidor Web Local
  server.on("/", HTTP_GET, handleRoot);
  server.on("/step", HTTP_GET, handleStep);
  server.on("/start", HTTP_GET, handleStartContinuous);
  server.on("/stop", HTTP_GET, handleStop);
  server.on("/sensor", HTTP_GET, handleSensor);
  server.on("/status", HTTP_GET, handleStatus);

  server.begin();

  // Auto-alinear (home) el dispensador al arrancar la máquina
  homeStepper();
}

// ==========================================
// LECTURA DEL SENSOR DE PROXIMIDAD (RADAR HLK-LD2410)
// ==========================================
void processRadarData(int movingDist, int movingEnergy, int staticDist, int staticEnergy) {
  bool currentRawState = false;
  
  // 1. Filtrado inteligente de presencia:
  // - Si es un objetivo estático (persona quieta en rango): evaluamos staticEnergy
  // - Si es un objetivo en movimiento: evaluamos movingEnergy para ignorar pelotas (baja energía)
  if (staticDist >= radarMinDistCm && staticDist <= radarMaxDistCm && staticEnergy >= radarStaticThreshold) {
    currentRawState = true;
  }
  else if (movingDist >= radarMinDistCm && movingDist <= radarMaxDistCm && movingEnergy >= radarMovingThreshold) {
    currentRawState = true;
  }
  
  unsigned long now = millis();
  
  // 2. Control de tiempo de detección continua (debe persistir por radarTriggerMs)
  if (currentRawState) {
    if (!radarRawDetected) {
      radarRawDetected = true;
      radarDetectionStart = now;
    }
  } else {
    radarRawDetected = false;
    radarDetectionStart = 0;
  }
  
  bool playerDetected = false;
  if (radarRawDetected && (now - radarDetectionStart >= (unsigned long)radarTriggerMs)) {
    playerDetected = true;
  }
  
  if (playerDetected != lastProximityState) {
    lastProximityState = playerDetected;
    
    // 1. Enviar el evento JSON al backend por Serial
    Serial.print("{\"event\":\"proximity\",\"active\":");
    Serial.print(playerDetected ? "true" : "false");
    Serial.println("}");
    
    // 2. Imprimir mensaje de depuración en el monitor serial
    if (playerDetected) {
      Serial.print("[DEBUG] Radar Proximidad: ¡PRESENCIA DETECTADA! (D_mov=");
      Serial.print(movingDist);
      Serial.print("cm, E_mov=");
      Serial.print(movingEnergy);
      Serial.print(" | D_est=");
      Serial.print(staticDist);
      Serial.print("cm, E_est=");
      Serial.print(staticEnergy);
      Serial.println(")");
    } else {
      Serial.println("[DEBUG] Radar Proximidad: Área despejada / Sin presencia.");
    }
  }
}

void readProximity() {
  while (Serial2.available() > 0) {
    uint8_t c = Serial2.read();
    
    switch (radarState) {
      case RADAR_WAIT_HEADER: {
        // Registro de desplazamiento para buscar cabecera F4 F3 F2 F1
        if (radarHeaderCount == 0 && c == 0xF4) radarHeaderCount = 1;
        else if (radarHeaderCount == 1 && c == 0xF3) radarHeaderCount = 2;
        else if (radarHeaderCount == 2 && c == 0xF2) radarHeaderCount = 3;
        else if (radarHeaderCount == 3 && c == 0xF1) {
          radarState = RADAR_READ_LEN;
          radarBytesRead = 0;
          radarPayloadLen = 0;
          radarHeaderCount = 0;
        } else {
          radarHeaderCount = 0;
        }
        break;
      }
      
      case RADAR_READ_LEN: {
        if (radarBytesRead == 0) {
          radarPayloadLen = c;
          radarBytesRead = 1;
        } else {
          radarPayloadLen |= (c << 8);
          radarState = RADAR_READ_PAYLOAD;
          radarBytesRead = 0;
          
          // Seguridad por tamaño de buffer
          if (radarPayloadLen + 4 > sizeof(radarRxBuf)) {
            radarState = RADAR_WAIT_HEADER;
          }
        }
        break;
      }
      
      case RADAR_READ_PAYLOAD: {
        radarRxBuf[radarBytesRead++] = c;
        if (radarBytesRead >= radarPayloadLen + 4) {
          // Frame completo recibido. Verificar cola (F8 F7 F6 F5)
          if (radarRxBuf[radarPayloadLen] == 0xF8 && 
              radarRxBuf[radarPayloadLen+1] == 0xF7 && 
              radarRxBuf[radarPayloadLen+2] == 0xF6 && 
              radarRxBuf[radarPayloadLen+3] == 0xF5) {
            
            // Parsear datos de presencia (Tipo de dato 0x02)
            if (radarRxBuf[0] == 0x02) {
              int targetState = radarRxBuf[2];
              int movingDist = radarRxBuf[3] | (radarRxBuf[4] << 8);
              int movingEnergy = radarRxBuf[5];
              int staticDist = radarRxBuf[6] | (radarRxBuf[7] << 8);
              int staticEnergy = radarRxBuf[8];
              
              processRadarData(movingDist, movingEnergy, staticDist, staticEnergy);
            }
          }
          radarState = RADAR_WAIT_HEADER;
        }
        break;
      }
    }
  }
}

// ==========================================
// LECTURA DE BOTONES FISICOS LOCALES (Start / Pause)
// ==========================================
unsigned long lastStartBtnTime = 0;
unsigned long lastPauseBtnTime = 0;
bool lastStartBtnState = HIGH; // HIGH = Inactivo (Pull-up)
bool lastPauseBtnState = HIGH;
const unsigned long BTN_DEBOUNCE_MS = 250;

void readButtons(uint16_t gpioState) {
  unsigned long now = millis();
  
  // Leer botón Start desde el estado de pines (LOW = Presionado / Pull-up activo)
  bool startVal = (gpioState & (1 << BTN_START_PIN)) != 0;
  if (startVal == LOW && lastStartBtnState == HIGH) { // Flanco de bajada (Presión)
    if (now - lastStartBtnTime > BTN_DEBOUNCE_MS) {
      lastStartBtnTime = now;
      Serial.println("{\"event\":\"button\",\"name\":\"start\"}");
    }
  }
  lastStartBtnState = startVal;
  
  // Leer botón Pause/Select (LOW = Presionado / Pull-up activo)
  bool pauseVal = (gpioState & (1 << BTN_PAUSE_PIN)) != 0;
  if (pauseVal == LOW && lastPauseBtnState == HIGH) { // Flanco de bajada (Presión)
    if (now - lastPauseBtnTime > BTN_DEBOUNCE_MS) {
      lastPauseBtnTime = now;
      Serial.println("{\"event\":\"button\",\"name\":\"pause\"}");
    }
  }
  lastPauseBtnState = pauseVal;
}

// ==========================================
// MANEJO DE MONEDERO POR SONDEO (POLLING) DE ESTADO (GPIO 18)
// ==========================================
int coinPulses = 0;
unsigned long lastCoinPulseTime = 0;
const unsigned long COIN_FLUSH_DELAY_MS = 300; // Esperar 300ms de inactividad de pulsos antes de consolidar la moneda

bool lastCoinPinVal = HIGH;        // Inactivo en HIGH (Pull-up)
unsigned long coinLowStartTime = 0; // Guardar el momento en que baja el pin
bool coinRegistered = false;       // Bandera para evitar lecturas repetidas del mismo pulso

void checkCoin() {
  bool currentReading = digitalRead(COIN_PIN);
  unsigned long now = millis();
  
  if (currentReading == LOW) {
    if (lastCoinPinVal == HIGH) {
      // El pin acaba de bajar a LOW, registramos el tiempo de inicio
      coinLowStartTime = now;
      coinRegistered = false;
    } else if (!coinRegistered) {
      // El pin se mantiene en LOW. Si dura al menos 20ms de forma continua, es un pulso real
      if (now - coinLowStartTime >= 20) {
        coinPulses++;
        lastCoinPulseTime = now;
        coinRegistered = true; // Evitar registrar más de una vez para el mismo pulso
      }
    }
  }
  lastCoinPinVal = currentReading;

  // Consolidar e informar los pulsos acumulados al backend
  if (coinPulses > 0) {
    if (now - lastCoinPulseTime > COIN_FLUSH_DELAY_MS) {
      int pulses = coinPulses;
      coinPulses = 0;
      
      // Enviar los pulsos al backend
      Serial.print("{\"event\":\"coin\",\"pulses\":");
      Serial.print(pulses);
      Serial.println("}");
    }
  }
}

bool lastLimitSwitchReportState = false;

void readLimitSwitch() {
  bool currentLimitSwitchState = (digitalRead(MOTOR_LIMIT_SWITCH_PIN) == LOW); // LOW = Presionado (Pull-up)
  if (currentLimitSwitchState != lastLimitSwitchReportState) {
    lastLimitSwitchReportState = currentLimitSwitchState;
    if (currentLimitSwitchState) {
      Serial.println("[HARDWARE] Final de carrera ACTIVADO (GPIO 27 LOW)");
    } else {
      Serial.println("[HARDWARE] Final de carrera SOLTADO (GPIO 27 HIGH)");
    }
    Serial.print("{\"event\":\"limit_switch\",\"active\":");
    Serial.print(currentLimitSwitchState ? "true" : "false");
    Serial.println("}");
  }
}

// ==========================================
// LOOP PRINCIPAL (Optimizado y no-bloqueante)
// ==========================================
void loop() {
  // 1. Manejo de WebServer, Motor paso a paso y LEDs
  server.handleClient();
  stepper.run();
  checkMotorStatus();
  updateLeds();

  // 2. Lectura y procesamiento de comandos seriales JSON entrantes desde el backend
  readIncomingSerial();

  // 3. Lectura periódica de sensores y botones a través de lectura rápida por bloque I2C
  if (mcpInitialized) {
    uint16_t gpioState = mcp.readGPIOAB();
    readSensors(gpioState);
    readButtons(gpioState);
  }

  // 4. Lectura periódica del radar de proximidad anti-trampa y final de carrera
  readProximity();
  readLimitSwitch();
  
  // 5. Lectura del monedero (consolidación de pulsos)
  checkCoin();
}