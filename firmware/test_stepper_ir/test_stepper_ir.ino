/*
=============================================================================
SKETCH DE DIAGNÓSTICO: PRUEBA INTEGRADA DISPENSADOR (STEPPER) + SENSOR IR
=============================================================================
Este sketch permite probar el funcionamiento conjunto del motor paso a paso 
NEMA 17 (dispensador de bolas con driver A4988) y el sensor de barrera IR (GPIO 15).

Comportamiento:
1. Al arrancar, calibra el dispensador (homing) girando en reversa hasta presionar
   el microswitch (GPIO 32) para alinear la hélice.
2. Espera comandos interactivos ingresados en el Monitor Serial (115200 baudios).
3. Escribir del '1' al '5' y dar Enter para soltar esa cantidad exacta de bolas.
4. El motor se detendrá de inmediato al detectar la última bola.
=============================================================================
*/

#include <AccelStepper.h>

// Pines del Driver A4988 para el motor NEMA 17 (Dispensador)
#define MOTOR_STEP_PIN 26
#define MOTOR_DIR_PIN  25
#define MOTOR_EN_PIN   33 // Activo en LOW
#define MOTOR_LIMIT_SWITCH_PIN 32 // Microswitch de tope de hélice (INPUT_PULLUP)

// Pin de señal del receptor IR
#define IR_SENSOR_PIN 15  

// Led indicador integrado
#define LED_INDICATOR_PIN 2

// Configuración del sensor IR (Ajustá a HIGH o LOW según tu sensor)
const bool IR_ACTIVE_STATE = HIGH; 

// Inicializar AccelStepper en modo DRIVER (2 hilos: STEP y DIR)
AccelStepper stepper(AccelStepper::DRIVER, MOTOR_STEP_PIN, MOTOR_DIR_PIN);

// Variables dinámicas configurables por Monitor Serial
float releaseSpeed = 300.0;
float releaseAccel = 150.0;
int directionMultiplier = 1; // 1 = Giro normal, -1 = Giro invertido

// Estados de la prueba
bool releasingActive = false;
int targetBallCount = 0;
int detectedBallCount = 0;
bool lastIrState = !IR_ACTIVE_STATE;
unsigned long lastBallDetectTime = 0;
const unsigned long BALL_COOLDOWN_MS = 300; // Cooldown de 300ms para evitar falsas re-lecturas de la misma bola

void setup() {
  Serial.begin(115200);
  delay(1000);
  
  Serial.println("\n=======================================================");
  Serial.println("   DIAGNÓSTICO INTEGRADO INTERACTIVO: DISPENSADOR + IR ");
  Serial.println("=======================================================");
  Serial.printf("Pines: STEP=%d, DIR=%d, EN=%d, LIMIT_SWITCH=%d, IR=%d\n", 
                MOTOR_STEP_PIN, MOTOR_DIR_PIN, MOTOR_EN_PIN, MOTOR_LIMIT_SWITCH_PIN, IR_SENSOR_PIN);
  Serial.println("-------------------------------------------------------");
  Serial.println("Comandos configurables (Escribilos y dale Enter):");
  Serial.println("  '1' a '5' -> Soltar esa cantidad exacta de bolas");
  Serial.println("  'H'       -> Iniciar calibración (Homing)");
  Serial.println("  'S'       -> Parada de Emergencia");
  Serial.println("  'I'       -> Invertir sentido de giro en caliente");
  Serial.println("  'V<valor>' -> Cambiar Velocidad (ej: V500, rango 50-2000)");
  Serial.println("  'A<valor>' -> Cambiar Aceleración (ej: A300, rango 10-1000)");
  Serial.println("=======================================================\n");

  // Configurar pines
  pinMode(MOTOR_EN_PIN, OUTPUT);
  digitalWrite(MOTOR_EN_PIN, HIGH); // Apagar bobinas inicialmente
  
  pinMode(MOTOR_LIMIT_SWITCH_PIN, INPUT_PULLUP);
  pinMode(IR_SENSOR_PIN, INPUT_PULLUP);
  pinMode(LED_INDICATOR_PIN, OUTPUT);
  digitalWrite(LED_INDICATOR_PIN, LOW);

  // Calibración inicial automática
  homeStepper();
}

void loop() {
  // 1. Leer y procesar comandos seriales
  readSerialCommand();

  // 2. Si la prueba está activa, correr el motor y monitorear el sensor IR
  if (releasingActive) {
    if (stepper.distanceToGo() != 0) {
      stepper.run();
      
      bool currentIrState = (digitalRead(IR_SENSOR_PIN) == IR_ACTIVE_STATE);
      unsigned long now = millis();
      
      // Detectar flanco de subida (de inactivo a activo) con filtro de cooldown
      if (currentIrState && lastIrState != IR_ACTIVE_STATE) {
        if (now - lastBallDetectTime > BALL_COOLDOWN_MS) {
          lastBallDetectTime = now;
          detectedBallCount++;
          
          Serial.printf("\n[ DETECCIÓN ] ¡Bola %d detectada! (%d de %d)\n", 
                        detectedBallCount, detectedBallCount, targetBallCount);
          
          digitalWrite(LED_INDICATOR_PIN, HIGH);
          
          if (detectedBallCount >= targetBallCount) {
            // ¡Se alcanzó la cantidad de bolas deseadas! Frenar motor de inmediato
            stepper.stop();
            while(stepper.distanceToGo() != 0) {
              stepper.run();
            }
            
            digitalWrite(MOTOR_EN_PIN, HIGH); // Apagar bobinas para enfriar
            releasingActive = false;
            
            long stepsTaken = stepper.currentPosition();
            Serial.println("\n=======================================================");
            Serial.printf("[ FIN ] Se soltaron las %d bolas programadas.\n", targetBallCount);
            Serial.printf("   -> Pasos avanzados totales: %ld\n", stepsTaken);
            Serial.println("=======================================================\n");
            
            delay(1000);
            digitalWrite(LED_INDICATOR_PIN, LOW);
          } else {
            // Aún faltan más bolas por soltar, apagamos el led de detección rápido
            delay(80);
            digitalWrite(LED_INDICATOR_PIN, LOW);
          }
        }
      }
      lastIrState = currentIrState ? IR_ACTIVE_STATE : !IR_ACTIVE_STATE;
    } else {
      // Llegó al límite de pasos de seguridad sin detectar todas las bolas
      digitalWrite(MOTOR_EN_PIN, HIGH);
      releasingActive = false;
      Serial.printf("\n[ ERROR ] Límite de recorrido alcanzado. Solo se detectaron %d de %d bolas.\n", 
                    detectedBallCount, targetBallCount);
      Serial.println("Verificá la alineación del haz de luz o aumentá la velocidad.\n");
    }
  }
}

// ==========================================
// LECTURA Y PROCESADO DE COMANDOS DEL MONITOR
// ==========================================
void readSerialCommand() {
  if (Serial.available() > 0) {
    String input = Serial.readStringUntil('\n');
    input.trim();
    if (input.length() == 0) return;
    
    char cmd = toupper(input[0]);
    
    // Si es un número del 1 al 5
    if (cmd >= '1' && cmd <= '5') {
      if (!releasingActive) {
        int count = cmd - '0';
        startBallRelease(count);
      } else {
        Serial.println("[!] El motor ya está girando.");
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
      Serial.printf("[CONFIG] Dirección de giro invertida. Multiplicador actual: %d (%s)\n\n", 
                    directionMultiplier, directionMultiplier == 1 ? "Normal" : "Invertida");
    }
    else if (cmd == 'V') {
      float val = input.substring(1).toFloat();
      if (val >= 50.0 && val <= 2000.0) {
        releaseSpeed = val;
        Serial.printf("[CONFIG] Velocidad máxima seteada a: %.1f pasos/seg\n\n", releaseSpeed);
      } else {
        Serial.println("[ERROR] Velocidad fuera de rango. Rango permitido: 50 a 2000.\n");
      }
    }
    else if (cmd == 'A') {
      float val = input.substring(1).toFloat();
      if (val >= 10.0 && val <= 1000.0) {
        releaseAccel = val;
        Serial.printf("[CONFIG] Aceleración seteada a: %.1f pasos/seg^2\n\n", releaseAccel);
      } else {
        Serial.println("[ERROR] Aceleración fuera de rango. Rango permitido: 10 a 1000.\n");
      }
    }
    else {
      Serial.println("[?] Comando no reconocido. Usá: 1-5, H, S, I, V<num>, A<num>.");
    }
  }
}

// ==========================================
// CALIBRACIÓN (HOMING)
// ==========================================
void homeStepper() {
  Serial.println("[HOMING] Iniciando calibración del dispensador...");
  
  if (digitalRead(MOTOR_LIMIT_SWITCH_PIN) == LOW) {
    Serial.println("[HOMING] Aspa ya alineada en switch de origen. Homing listo.");
    stepper.setCurrentPosition(0);
    digitalWrite(MOTOR_EN_PIN, HIGH);
    return;
  }

  digitalWrite(MOTOR_EN_PIN, LOW); // Activar bobinas
  delay(100);
  
  // Homing a la mitad de los valores máximos seteados por seguridad
  stepper.setMaxSpeed(releaseSpeed * 0.5);
  stepper.setAcceleration(releaseAccel * 0.5);
  
  // Buscar en sentido contrario al sentido de avance
  stepper.move(-3000 * directionMultiplier); 
  
  int stepsTaken = 0;
  int safetyLimit = 3000;
  
  while (digitalRead(MOTOR_LIMIT_SWITCH_PIN) == HIGH && stepsTaken < safetyLimit && stepper.distanceToGo() != 0) {
    stepper.run();
    stepsTaken++;
  }
  
  stepper.stop();
  while (stepper.distanceToGo() != 0) {
    stepper.run();
  }
  
  stepper.setCurrentPosition(0);
  digitalWrite(MOTOR_EN_PIN, HIGH); // Apagar bobinas
  
  if (stepsTaken >= safetyLimit) {
    Serial.println("[ ALERTA ] Homing falló por límite de seguridad. ¿Está bien cableado?");
  } else {
    Serial.println("[HOMING] ¡Calibración exitosa! Hélice en posición CERO.");
  }
  Serial.println("Escribí del '1' al '5' para iniciar la prueba.\n");
}

// ==========================================
// INICIO DE AVANCE CONTINUO PARA N BOLAS
// ==========================================
void startBallRelease(int count) {
  targetBallCount = count;
  detectedBallCount = 0;
  lastIrState = (digitalRead(IR_SENSOR_PIN) == IR_ACTIVE_STATE);
  lastBallDetectTime = 0;
  
  Serial.printf("[TEST] Soltando %d bola(s)... (Velocidad: %.1f | Aceleración: %.1f)\n", 
                targetBallCount, releaseSpeed, releaseAccel);
  
  digitalWrite(MOTOR_EN_PIN, LOW);
  delay(50);
  
  stepper.setMaxSpeed(releaseSpeed);
  stepper.setAcceleration(releaseAccel);
  stepper.setCurrentPosition(0);
  
  // Le damos un rango amplio al motor para que pueda dar suficientes vueltas por bola
  stepper.move(8000 * targetBallCount * directionMultiplier); 
  
  releasingActive = true;
}

// ==========================================
// PARADA DE EMERGENCIA
// ==========================================
void stopMotorEmergency() {
  if (releasingActive) {
    stepper.stop();
    while(stepper.distanceToGo() != 0) {
      stepper.run();
    }
    digitalWrite(MOTOR_EN_PIN, HIGH);
    releasingActive = false;
    Serial.println("\n[!] DETENCIÓN DE EMERGENCIA EJECUTADA por el usuario.\n");
  }
}
