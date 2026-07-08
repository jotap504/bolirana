/*
=============================================================================
SKETCH DE DIAGNÓSTICO: PRUEBA INTEGRADA DISPENSADOR (STEPPER) + SENSOR IR
=============================================================================
Este sketch permite probar el funcionamiento conjunto del motor paso a paso 
NEMA 17 (dispensador de bolas con driver A4988) y el sensor de barrera IR (GPIO 15).

Comportamiento:
1. Al arrancar, calibra el dispensador (homing) girando en reversa hasta presionar
   el microswitch (GPIO 32) para alinear la hélice.
2. Espera que ingreses la letra 'R' (Release) por el Monitor Serial (115200 baudios).
3. Al recibir 'R', el motor comenzará a girar de forma continua para empujar bolas.
4. Tan pronto como el sensor IR (GPIO 15) detecte que una bola pasa por la barrera,
   el motor se detendrá inmediatamente de forma automática.
5. Reportará por el Monitor Serial cuántos pasos y cuántos "clicks" de aspas se registraron.
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

// Estados de la prueba
bool releasingActive = false;
unsigned long releaseStartTime = 0;

void setup() {
  Serial.begin(115200);
  delay(1000);
  
  Serial.println("\n=======================================================");
  Serial.println("   DIAGNÓSTICO INTEGRADO: DISPENSADOR + SENSOR IR      ");
  Serial.println("=======================================================");
  Serial.printf("Pines del Motor: STEP=%d, DIR=%d, EN=%d\n", MOTOR_STEP_PIN, MOTOR_DIR_PIN, MOTOR_EN_PIN);
  Serial.printf("Final de carrera del Dispensador: GPIO %d\n", MOTOR_LIMIT_SWITCH_PIN);
  Serial.printf("Sensor de caida de bola (IR): GPIO %d\n", IR_SENSOR_PIN);
  Serial.println("-------------------------------------------------------");
  Serial.println("Comandos disponibles en el Monitor Serial:");
  Serial.println("  'H' -> Realizar Homing (Alineación/Calibración)");
  Serial.println("  'R' -> Iniciar avance continuo (Detiene al detectar bola)");
  Serial.println("  'S' -> Detener de emergencia");
  Serial.println("=======================================================\n");

  // Configurar pines
  pinMode(MOTOR_EN_PIN, OUTPUT);
  digitalWrite(MOTOR_EN_PIN, HIGH); // Apagar bobinas inicialmente
  
  pinMode(MOTOR_LIMIT_SWITCH_PIN, INPUT_PULLUP);
  pinMode(IR_SENSOR_PIN, INPUT_PULLUP); // Usar pullup interno para seguridad
  pinMode(LED_INDICATOR_PIN, OUTPUT);
  digitalWrite(LED_INDICATOR_PIN, LOW);

  // Configurar velocidades del motor por defecto
  stepper.setMaxSpeed(300.0);
  stepper.setAcceleration(150.0);

  // Calibración inicial automática
  homeStepper();
}

void loop() {
  // 1. Leer comandos seriales del usuario
  if (Serial.available() > 0) {
    char cmd = toupper(Serial.read());
    
    if (cmd == 'H') {
      homeStepper();
    } 
    else if (cmd == 'R') {
      if (!releasingActive) {
        startBallRelease();
      } else {
        Serial.println("[!] El motor ya está girando.");
      }
    } 
    else if (cmd == 'S') {
      stopMotorEmergency();
    }
  }

  // 2. Si la prueba está activa, mover el motor y verificar sensores
  if (releasingActive) {
    // Si todavía tiene trayectoria asignada, correr el motor
    if (stepper.distanceToGo() != 0) {
      stepper.run();
      
      // Verificar si el sensor IR detecta paso de bola
      bool ballDetected = (digitalRead(IR_SENSOR_PIN) == IR_ACTIVE_STATE);
      if (ballDetected) {
        // ¡BOLA DETECTADA! Frenar motor de inmediato
        stepper.stop();
        while(stepper.distanceToGo() != 0) {
          stepper.run();
        }
        
        digitalWrite(MOTOR_EN_PIN, HIGH); // Apagar bobinas para enfriar
        releasingActive = false;
        digitalWrite(LED_INDICATOR_PIN, HIGH); // Encender led indicador
        
        long stepsTaken = stepper.currentPosition();
        Serial.println("\n[ OK ] ¡BOLA DETECTADA POR EL SENSOR IR!");
        Serial.printf("   -> Pasos avanzados desde la calibración: %ld\n", stepsTaken);
        Serial.println("=======================================================\n");
        
        // Dejar el led encendido 2 segundos y apagarlo
        delay(2000);
        digitalWrite(LED_INDICATOR_PIN, LOW);
      }
    } else {
      // El motor llegó al límite asignado sin detectar la bola
      digitalWrite(MOTOR_EN_PIN, HIGH);
      releasingActive = false;
      Serial.println("\n[ ERROR ] Límite de pasos alcanzado sin detectar bola.");
      Serial.println("Verifica si el sensor IR está bien alineado o alimentado.\n");
    }
  }
}

// ==========================================
// RUTINA DE ALINEACIÓN (HOMING)
// ==========================================
void homeStepper() {
  Serial.println("[HOMING] Iniciando calibración del dispensador...");
  
  // Si ya está pisado el switch, salir
  if (digitalRead(MOTOR_LIMIT_SWITCH_PIN) == LOW) {
    Serial.println("[HOMING] Aspa ya alineada (microswitch en LOW). Homing listo.");
    stepper.setCurrentPosition(0);
    digitalWrite(MOTOR_EN_PIN, HIGH);
    return;
  }

  digitalWrite(MOTOR_EN_PIN, LOW); // Encender bobinas
  delay(100);
  
  stepper.setMaxSpeed(150.0);
  stepper.setAcceleration(80.0);
  stepper.move(-3000); // Mover en reversa buscando el switch
  
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
    Serial.println("[ ALERTA ] Homing falló por timeout (¿microswitch desconectado?)");
  } else {
    Serial.println("[HOMING] ¡Calibración exitosa! Hélice en posición CERO.");
  }
  Serial.println("Escribí 'R' para soltar una bola.\n");
}

// ==========================================
// INICIO DE PRUEBA DE GIRO
// ==========================================
void startBallRelease() {
  Serial.println("[TEST] Iniciando giro de dispensador...");
  Serial.println("[TEST] El motor girará hasta que el sensor IR (GPIO 15) detecte el paso de la bola.");
  
  digitalWrite(MOTOR_EN_PIN, LOW); // Encender bobinas
  delay(50);
  
  stepper.setMaxSpeed(300.0);
  stepper.setAcceleration(150.0);
  stepper.setCurrentPosition(0);
  
  // Mover una distancia larga (equivalente a unas 3 o 4 vueltas de hélice)
  // para dar tiempo a que caiga la bola. Si no cae antes, frenará por límite.
  stepper.move(8000); 
  
  releasingActive = true;
  releaseStartTime = millis();
}

// ==========================================
// DETENCIÓN DE EMERGENCIA
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
