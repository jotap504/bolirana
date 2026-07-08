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
#include <AccelStepper.h>

// Credenciales WiFi
const char* ssid     = "Fibrasky";
const char* password = "corsa000";

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

void setup() {
  Serial.begin(115200);
  delay(1000);
  
  Serial.println("\n=======================================================");
  Serial.println("  DIAGNÓSTICO INTERACTIVO: MOTOR 28BYJ-48 + SENSOR IR  ");
  Serial.println("=======================================================");
  Serial.printf("Pines Motor: IN1=%d, IN2=%d, IN3=%d, IN4=%d\n", MOTOR_IN1, MOTOR_IN2, MOTOR_IN3, MOTOR_IN4);
  Serial.printf("Final de carrera: GPIO %d | Sensor IR: GPIO %d\n", MOTOR_LIMIT_SWITCH_PIN, IR_SENSOR_PIN);
  Serial.println("-------------------------------------------------------");
  Serial.println("Comandos configurables (Escribilos y dale Enter):");
  Serial.println("  '1' a '5'  -> Soltar esa cantidad exacta de bolas");
  Serial.println("  'T'        -> Monitorear sensor IR en vivo (sin mover el motor)");
  Serial.println("  'H'        -> Iniciar calibración (Homing)");
  Serial.println("  'S'        -> Parada de Emergencia");
  Serial.println("  'I'        -> Invertir sentido de giro en caliente");
  Serial.println("  'V<valor>' -> Cambiar Velocidad (ej: V800, rango 100-1200)");
  Serial.println("  'A<valor>' -> Cambiar Aceleración (ej: A400, rango 50-800)");
  Serial.println("  'P<valor>' -> Pasos extras tras detectar (ej: P150, rango 0-3000)");
  Serial.println("  'L<0 o 1>' -> Traba de motor al parar (L0 = Libre/Frío, L1 = Frenado)");
  Serial.println("=======================================================\n");

  pinMode(MOTOR_LIMIT_SWITCH_PIN, INPUT_PULLUP);
  pinMode(IR_SENSOR_PIN, INPUT_PULLUP);
  pinMode(LED_INDICATOR_PIN, OUTPUT);
  digitalWrite(LED_INDICATOR_PIN, LOW);

  // Conectar WiFi (para probar estabilidad de corriente del módulo de radio)
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

  // Calibración inicial automática
  homeStepper();
}

void loop() {
  // 1. Leer y procesar comandos seriales
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
          Serial.println("\n=======================================================");
          Serial.printf("[ FIN ] Giro completo con %d pasos extras.\n", extraStepsAfterDetect);
          Serial.printf("   -> Pasos avanzados totales: %ld\n", stepsTaken);
          Serial.println("=======================================================\n");
          
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
          Serial.println(" [ SENSANDO ] Bola cruzando la barrera...");
        }
      }
      
      // B. La bola sale y libera el sensor
      if (!currentIrState && ballInSensor) {
        ballInSensor = false;
        lastBallDetectTime = now;
        detectedBallCount++;
        
        Serial.printf("\n[ DETECCIÓN ] ¡Bola %d liberada! (%d de %d)\n", 
                      detectedBallCount, detectedBallCount, targetBallCount);
        
        if (detectedBallCount >= targetBallCount) {
          if (extraStepsAfterDetect > 0) {
            long targetPos = stepper.currentPosition() + (extraStepsAfterDetect * directionMultiplier);
            stepper.moveTo(targetPos);
            finishingRelease = true;
            Serial.printf("[INFO] Avanzando %d pasos extras antes de frenar...\n", extraStepsAfterDetect);
          } else {
            // Parar inmediatamente
            stepper.moveTo(stepper.currentPosition());
            stopMotorCoils();
            releasingActive = false;
            
            long stepsTaken = stepper.currentPosition();
            Serial.println("\n=======================================================");
            Serial.printf("[ FIN ] Se soltaron las %d bolas programadas.\n", targetBallCount);
            Serial.printf("   -> Pasos avanzados totales: %ld\n", stepsTaken);
            Serial.println("=======================================================\n");
            
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
      Serial.printf("\n[ ERROR ] Límite alcanzado. Detecciones: %d/%d.\n", detectedBallCount, targetBallCount);
      Serial.println("Revisa alineación o aumenta velocidad.\n");
    }
  }

  // 3. Monitor del sensor IR en vivo (si está inactivo el motor)
  if (sensorMonitorActive && !releasingActive) {
    static unsigned long lastMonitorPrint = 0;
    static int lastMonitorVal = -1;
    int currentVal = digitalRead(IR_SENSOR_PIN);
    
    if (currentVal != lastMonitorVal || millis() - lastMonitorPrint > 200) {
      lastMonitorVal = currentVal;
      lastMonitorPrint = millis();
      
      bool isDetected = (currentVal == IR_ACTIVE_STATE);
      digitalWrite(LED_INDICATOR_PIN, isDetected ? HIGH : LOW);
      
      Serial.printf("[MONITOR IR] Pin 15: %s (%s) | Haz: %s\n",
                    currentVal == HIGH ? "HIGH (~3.3V)" : "LOW (0V / GND)",
                    isDetected ? "OBSTRUIDO" : "LIBRE",
                    isDetected ? "████████████" : "------------");
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
// COMANDOS DEL MONITOR SERIAL
// ==========================================
void readSerialCommand() {
  if (Serial.available() > 0) {
    String input = Serial.readStringUntil('\n');
    input.trim();
    if (input.length() == 0) return;
    
    char cmd = toupper(input[0]);
    
    if (cmd >= '1' && cmd <= '5') {
      if (!releasingActive) {
        int count = cmd - '0';
        startBallRelease(count);
      } else {
        Serial.println("[!] El motor ya está girando.");
      }
    }
    else if (cmd == 'T') {
      sensorMonitorActive = !sensorMonitorActive;
      if (sensorMonitorActive) {
        Serial.println("\n[MONITOR IR] Modo monitor INICIADO. Pasá un objeto por el haz para probar. (Mandá 'T' para apagar)\n");
      } else {
        Serial.println("\n[MONITOR IR] Modo monitor APAGADO.\n");
        digitalWrite(LED_INDICATOR_PIN, LOW);
      }
    }
    else if (cmd == 'H') {
      homeStepper();
    } 
    else if (cmd == 'S') {
      stopMotorEmergency();
    }
    else if (cmd == 'I') {
      directionMultiplier = -directionMultiplier;
      Serial.printf("[CONFIG] Dirección invertida. Multiplicador: %d\n\n", directionMultiplier);
    }
    else if (cmd == 'V') {
      float val = input.substring(1).toFloat();
      if (val >= 100.0 && val <= 1200.0) {
        releaseSpeed = val;
        Serial.printf("[CONFIG] Velocidad máxima: %.1f pasos/seg\n\n", releaseSpeed);
      } else {
        Serial.println("[ERROR] Rango de velocidad permitido: 100 a 1200.\n");
      }
    }
    else if (cmd == 'A') {
      float val = input.substring(1).toFloat();
      if (val >= 50.0 && val <= 800.0) {
        releaseAccel = val;
        Serial.printf("[CONFIG] Aceleración: %.1f pasos/seg^2\n\n", releaseAccel);
      } else {
        Serial.println("[ERROR] Rango de aceleración permitido: 50 a 800.\n");
      }
    }
    else if (cmd == 'P') {
      int val = input.substring(1).toInt();
      if (val >= 0 && val <= 3000) {
        extraStepsAfterDetect = val;
        Serial.printf("[CONFIG] Pasos extras: %d pasos\n\n", extraStepsAfterDetect);
      } else {
        Serial.println("[ERROR] Rango permitido: 0 a 3000.\n");
      }
    }
    else if (cmd == 'L') {
      int val = input.substring(1).toInt();
      keepLockedOnHalt = (val == 1);
      if (!releasingActive) {
        stopMotorCoils();
      }
      Serial.printf("[CONFIG] Traba de motor: %s\n\n", 
                    keepLockedOnHalt ? "ACTIVADA (Con consumo)" : "DESACTIVADA (Libre, frío y silencioso)");
    }
    else {
      Serial.println("[?] Comando no reconocido. Usá: 1-5, T, H, S, I, V<num>, A<num>, P<num>, L<0 o 1>.");
    }
  }
}

// ==========================================
// CALIBRACIÓN (HOMING)
// ==========================================
void homeStepper() {
  Serial.println("[HOMING] Iniciando calibración del dispensador...");
  
  if (digitalRead(MOTOR_LIMIT_SWITCH_PIN) == LOW) {
    Serial.println("[HOMING] Aspa ya alineada. Posición puesta a CERO.");
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
    Serial.println("[ ALERTA ] Homing falló por límite de seguridad.");
  } else {
    Serial.println("[HOMING] ¡Calibración exitosa! Aspa alineada en posición CERO.");
  }
  Serial.println("Escribí del '1' al '5' para iniciar la prueba.\n");
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
  
  Serial.printf("[TEST] Soltando %d bola(s)... (V=%.1f, A=%.1f, P=%d)\n", 
                targetBallCount, releaseSpeed, releaseAccel, extraStepsAfterDetect);
  
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
    Serial.println("\n[!] DETENCIÓN DE EMERGENCIA EJECUTADA por el usuario.\n");
  }
}
