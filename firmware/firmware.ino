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

// ==========================================
// CONFIGURACIÓN DE RED WIFI
// ==========================================
const char* ssid     = "Fibrasky";
// Mantener la contraseña original del usuario
const char* password = "corsa000";

// ==========================================
// CONFIGURACIÓN DE HARDWARE Y PINES (ESP32)
// ==========================================
// Pines del Driver ULN2003 para el motor paso a paso
#define MOTOR_IN1 26
#define MOTOR_IN2 25
#define MOTOR_IN3 33
#define MOTOR_IN4 32

// Pin físico dedicado a la Tira NeoPixel (WS2812B/W) en el ESP32
#define NEOPIXEL_PIN 4
#define NUM_LEDS     60 // Cantidad de LEDs en la tira

// Pines del Bus I2C
#define I2C_SDA      21
#define I2C_SCL      22
#define MCP_ADDRESS  0x20 // Dirección I2C por defecto (A0, A1, A2 a GND)

// Botón de Pausa relocalizado (liberado del sensor fosa_1)
#define BTN_PAUSE_PIN 27

// ==========================================
// CONFIGURACIÓN DE SENSORES EN MCP23017
// ==========================================
// Tipo de Entrada: INPUT (si tienes pull-downs/pull-ups externos de 10k) o INPUT_PULLUP
#define SENSOR_PIN_MODE    INPUT
// Polaridad del Sensor: HIGH (activo al pasar bola, normalmente LOW) o LOW (activo en bajo)
#define SENSOR_ACTIVE_STATE HIGH
// Tiempo de enfriamiento / debounce por sensor (en milisegundos)
#define SENSOR_COOLDOWN_MS 1000

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
Adafruit_NeoPixel strip(NUM_LEDS, NEOPIXEL_PIN, NEO_GRB + NEO_KHZ800);
AccelStepper stepper(AccelStepper::HALF4WIRE, MOTOR_IN1, MOTOR_IN3, MOTOR_IN2, MOTOR_IN4);
WebServer server(80);

// Control de debounce/estado de sensores
unsigned long lastTriggerTime[NUM_SENSORS] = {0};
bool lastSensorState[NUM_SENSORS] = {false};

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
            "cero": "❌ Bola Cero"
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
  for (int i = 0; i < 11; i++) {
    if (mcp.digitalRead(i) == SENSOR_ACTIVE_STATE) {
      active = true;
      break;
    }
  }
  server.send(200, "text/plain", active ? "1" : "0");
}

void handleStatus() {
  // Retorna un objeto JSON con el estado en tiempo real de todos los sensores
  String json = "{";
  for (int i = 0; i < 11; i++) {
    bool state = (mcp.digitalRead(i) == SENSOR_ACTIVE_STATE);
    json += "\"" + String(SENSOR_IDS[i]) + "\":" + (state ? "true" : "false");
    if (i < 10) json += ",";
  }
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
  unsigned long now = millis();
  
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
          } else if (strcmp(type, "coin") == 0) {
            // Si el backend envía confirmación de ficha
            triggerEffect("goal", "fosa_2"); // Flash de rebote
          }
        }
      }
    }
  }
}

// ==========================================
// LECTURA DE SENSORES CON DEBOUNCE INDEPENDIENTE
// ==========================================
void readSensors() {
  unsigned long now = millis();
  
  for (int i = 0; i < NUM_SENSORS; i++) {
    // Lectura del pin correspondiente del MCP23017
    bool pinReading = mcp.digitalRead(SENSOR_PINS[i]);
    
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
        
        // 3. Activar el motor físico 500 pasos para girar
        // Se activa para todos los sensores de bola (incluyendo bola cero) para que siga el ciclo del gabinete
        stepper.move(500);
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
  
  // Inicialización del bus I2C manual con pines custom
  Wire.begin(I2C_SDA, I2C_SCL);
  
  // Configuración del botón de pausa relocalizado (Pullup interno)
  pinMode(BTN_PAUSE_PIN, INPUT_PULLUP);
  
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
    
    // Configurar los pines de sensores en el expansor
    for (int i = 0; i < NUM_SENSORS; i++) {
      mcp.pinMode(SENSOR_PINS[i], SENSOR_PIN_MODE);
    }
  }

  // Configuración de velocidad y aceleración del motor paso a paso
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
}

// ==========================================
// LOOP PRINCIPAL (Optimizado y no-bloqueante)
// ==========================================
void loop() {
  // 1. Manejo de WebServer, Motor paso a paso y LEDs
  server.handleClient();
  stepper.run();
  updateLeds();

  // 2. Lectura y procesamiento de comandos seriales JSON entrantes desde el backend
  readIncomingSerial();

  // 3. Lectura periódica de sensores IR a través del Expansor I2C
  readSensors();
}