#include <WiFi.h>
#include <WebServer.h>

// =========================================================================
// CONFIGURACIÓN DE PINES
// =========================================================================
// Driver IBT_2 (BTS7960 H-Bridge)
// Nota: R_EN y L_EN deben estar puenteados y conectados a 3.3V (o a un pin HIGH)
#define GOALIE_RPWM_PIN 18 // Giro derecha
#define GOALIE_LPWM_PIN 19 // Giro izquierda

// Finales de Carrera (Limit Switches)
// NOTA: Usamos GPIO 14 y 27 que son pines seguros (evitan conflictos de arranque de la memoria flash)
#define GOALIE_LIMIT_L_PIN 14 // Final de carrera izquierdo (GND al hacer clic)
#define GOALIE_LIMIT_R_PIN 27 // Final de carrera derecho (GND al hacer clic)

// =========================================================================
// PARÁMETROS WIFI
// =========================================================================
const char* ssid     = "Fibrasky";
const char* password = "corsa000";

// =========================================================================
// ESTADO Y VARIABLES DE CALIBRACIÓN
// =========================================================================
int motorSpeed = 70;            // Velocidad PWM para control manual/desplazamiento (0-255)
int calibrationSpeed = 70;      // Velocidad PWM para la autocalibración (lenta para no pasarse del sensor)
unsigned long travelTimeRightMs = 0; // Tiempo de Izquierda a Derecha (ms)
unsigned long travelTimeLeftMs = 0;  // Tiempo de Derecha a Izquierda (ms)
float currentPositionPercent = 0.0; // Posición estimada (0% a 100%)
bool isCalibrated = false;

// Variables para Bucle Automático No-Bloqueante
bool loopActive = false;
float loopMinPercent = 20.0;
float loopMaxPercent = 80.0;

enum LoopState {
  LOOP_TRAVELING,
  LOOP_PAUSING_MID,
  LOOP_GOING_MID
};
LoopState gameLoopState = LOOP_TRAVELING;
int loopPhase = 1;
int loopTravelCount = 0;
int loopTargetTravels = 6;
bool currentLoopDirection = true;
unsigned long gameLoopTimer = 0;

// Variables de Secuencia de Prueba Automatizada
enum SeqState {
  SEQ_IDLE,
  SEQ_P1_INIT,
  SEQ_P1_CAL_L1,
  SEQ_P1_CAL_R,
  SEQ_P1_CAL_L2,
  SEQ_P1_LOOP_START,
  SEQ_P1_LOOP_MOVE,
  SEQ_P1_LOOP_WAIT_MID,
  SEQ_P1_LOOP_RESUME,
  SEQ_P2_INIT,
  SEQ_P2_CAL_L1,
  SEQ_P2_CAL_R,
  SEQ_P2_CAL_L2,
  SEQ_P2_LOOP_START,
  SEQ_P2_LOOP_MOVE,
  SEQ_P2_STOP_40,
  SEQ_P2_WAIT_40,
  SEQ_P2_STOP_60,
  SEQ_P2_WAIT_60,
  SEQ_P2_FINAL_START,
  SEQ_P2_FINAL_MOVE,
  SEQ_DONE
};
SeqState seqState = SEQ_IDLE;
int sequenceLoopCount = 0;
unsigned long sequenceTimer = 0;

enum GoalieState {
  GOALIE_IDLE,
  GOALIE_MOVING_LEFT,
  GOALIE_MOVING_RIGHT
};
GoalieState motorState = GOALIE_IDLE;
float targetPositionPercent = 0.0;
unsigned long movementStartTime = 0;
unsigned long movementDuration = 0;

WebServer server(80);

// =========================================================================
// FUNCIONES DE CONTROL DEL MOTOR
// =========================================================================

void stopGoalie() {
  Serial.println("[MOTOR] stopGoalie() - Frenando motor (RPWM=0, LPWM=0)");
  analogWrite(GOALIE_RPWM_PIN, 0);
  analogWrite(GOALIE_LPWM_PIN, 0);
}

void moveLeft(int speed) {
  Serial.print("[MOTOR] moveLeft() - Intentando girar a la Izquierda. Velocidad: ");
  Serial.println(speed);
  // Solo se mueve si el final de carrera izquierdo NO está presionado
  if (digitalRead(GOALIE_LIMIT_L_PIN) == HIGH) {
    Serial.println("[MOTOR] -> Límite Izquierdo Libre. Activando LPWM.");
    analogWrite(GOALIE_RPWM_PIN, 0);
    analogWrite(GOALIE_LPWM_PIN, speed);
  } else {
    Serial.println("[MOTOR] -> Límite Izquierdo PRESIONADO. Abortando movimiento.");
    stopGoalie();
  }
}

void moveRight(int speed) {
  Serial.print("[MOTOR] moveRight() - Intentando girar a la Derecha. Velocidad: ");
  Serial.println(speed);
  // Solo se mueve si el final de carrera derecho NO está presionado
  if (digitalRead(GOALIE_LIMIT_R_PIN) == HIGH) {
    Serial.println("[MOTOR] -> Límite Derecho Libre. Activando RPWM.");
    analogWrite(GOALIE_RPWM_PIN, speed);
    analogWrite(GOALIE_LPWM_PIN, 0);
  } else {
    Serial.println("[MOTOR] -> Límite Derecho PRESIONADO. Abortando movimiento.");
    stopGoalie();
  }
}

// Rutina de calibración para medir el recorrido total de lado a lado en AMBOS sentidos
void calibrateGoalie() {
  Serial.println("\n[CALIBRACIÓN] Iniciando calibracion bidireccional del arquero...");
  stopGoalie();
  delay(200);

  // 1. Mover hacia la izquierda hasta presionar LIMIT_L
  Serial.println("[CALIBRACIÓN] 1/3: Buscando limite izquierdo...");
  unsigned long startWait = millis();
  moveLeft(calibrationSpeed); // Encender motor una sola vez
  
  while (digitalRead(GOALIE_LIMIT_L_PIN) == HIGH) {
    if (millis() - startWait > 12000) { // Timeout de 12 segundos
      stopGoalie();
      Serial.println("[CALIBRACIÓN] ERROR: Timeout buscando limite izquierdo.");
      return;
    }
    delay(10);
  }
  stopGoalie();
  delay(600); // Pausa para detener inercia mecánica por completo
  Serial.println("[CALIBRACIÓN] Limite izquierdo alcanzado.");

  // 2. Mover hacia la derecha hasta presionar LIMIT_R y medir tiempo
  Serial.println("[CALIBRACIÓN] 2/3: Buscando limite derecho y midiendo tiempo...");
  unsigned long startTime = millis();
  startWait = millis();
  moveRight(calibrationSpeed); // Encender motor una sola vez
  
  while (digitalRead(GOALIE_LIMIT_R_PIN) == HIGH) {
    if (millis() - startWait > 12000) {
      stopGoalie();
      Serial.println("[CALIBRACIÓN] ERROR: Timeout buscando limite derecho.");
      return;
    }
    delay(10);
  }
  unsigned long endTime = millis();
  stopGoalie();
  travelTimeRightMs = endTime - startTime;
  delay(600); // Pausa para inercia
  Serial.print("[CALIBRACIÓN] Tiempo Izquierda -> Derecha: ");
  Serial.print(travelTimeRightMs);
  Serial.println(" ms.");

  // 3. Mover de regreso a la izquierda hasta presionar LIMIT_L y medir tiempo
  Serial.println("[CALIBRACIÓN] 3/3: Buscando limite izquierdo y midiendo tiempo de retorno...");
  startTime = millis();
  startWait = millis();
  moveLeft(calibrationSpeed); // Encender motor
  
  while (digitalRead(GOALIE_LIMIT_L_PIN) == HIGH) {
    if (millis() - startWait > 12000) {
      stopGoalie();
      Serial.println("[CALIBRACIÓN] ERROR: Timeout buscando limite izquierdo de retorno.");
      return;
    }
    delay(10);
  }
  endTime = millis();
  stopGoalie();
  travelTimeLeftMs = endTime - startTime;
  delay(200);

  isCalibrated = true;
  currentPositionPercent = 0.0; // Quedamos posicionados en el extremo izquierdo
  
  Serial.print("[CALIBRACIÓN] Tiempo Derecha -> Izquierda: ");
  Serial.print(travelTimeLeftMs);
  Serial.println(" ms.");
  Serial.println("[CALIBRACIÓN] Calibración bidireccional completada con éxito.");
}

// Mover el arquero a una posición de forma NO-BLOQUEANTE (State Machine)
void startMoveToPosition(float targetPercent) {
  if (!isCalibrated) {
    Serial.println("[ERROR] No se puede posicionar. El sistema no esta calibrado.");
    return;
  }
  
  if (targetPercent < 0) targetPercent = 0;
  if (targetPercent > 100) targetPercent = 100;
  
  float diff = targetPercent - currentPositionPercent;
  if (abs(diff) < 2.0) {
    return;
  }
  
  targetPositionPercent = targetPercent;
  // Escalar el tiempo de viaje según la relación de velocidad: a mayor velocidad, menor duración de encendido
  float speedRatio = (float)calibrationSpeed / motorSpeed;
  
  if (diff < 0) {
    movementDuration = (unsigned long)((abs(diff) / 100.0) * travelTimeLeftMs * speedRatio);
    motorState = GOALIE_MOVING_LEFT;
    moveLeft(motorSpeed);
  } else {
    movementDuration = (unsigned long)((abs(diff) / 100.0) * travelTimeRightMs * speedRatio);
    motorState = GOALIE_MOVING_RIGHT;
    moveRight(motorSpeed);
  }
  movementStartTime = millis();
  Serial.print("[MOVIMIENTO] Desplazando no-bloqueante a ");
  Serial.print(targetPercent);
  Serial.print("% (duracion estimada: ");
  Serial.print(movementDuration);
  Serial.println(" ms)");
}

// Actualizador no-bloqueante del arquero en loop()
void updateGoalieStateMachine() {
  if (motorState == GOALIE_MOVING_LEFT) {
    // Caso 1: Toca el fin de carrera izquierdo
    if (digitalRead(GOALIE_LIMIT_L_PIN) == LOW) {
      stopGoalie();
      currentPositionPercent = 0.0;
      motorState = GOALIE_IDLE;
      Serial.println("[MOTOR] Límite Izquierdo alcanzado físicamente.");
    }
    // Caso 2: Se completa el tiempo calculado
    else if (millis() - movementStartTime >= movementDuration) {
      stopGoalie();
      currentPositionPercent = targetPositionPercent;
      motorState = GOALIE_IDLE;
      Serial.println("[MOTOR] Movimiento Izquierda completado por tiempo.");
    }
  }
  else if (motorState == GOALIE_MOVING_RIGHT) {
    // Caso 1: Toca el fin de carrera derecho
    if (digitalRead(GOALIE_LIMIT_R_PIN) == LOW) {
      stopGoalie();
      currentPositionPercent = 100.0;
      motorState = GOALIE_IDLE;
      Serial.println("[MOTOR] Límite Derecho alcanzado físicamente.");
    }
    // Caso 2: Se completa el tiempo calculado
    else if (millis() - movementStartTime >= movementDuration) {
      stopGoalie();
      currentPositionPercent = targetPositionPercent;
      motorState = GOALIE_IDLE;
      Serial.println("[MOTOR] Movimiento Derecha completado por tiempo.");
    }
  }
}

// Máquina de estados no-bloqueante para la secuencia de pruebas especial
void updateTestSequence() {
  if (seqState == SEQ_IDLE) return;
  
  switch(seqState) {
    case SEQ_P1_INIT:
      motorSpeed = 50;
      calibrationSpeed = 50;
      isCalibrated = false;
      Serial.println("\n[SECUENCIA] === INICIANDO PARTE 1 (Velocidad 50) ===");
      seqState = SEQ_P1_CAL_L1;
      moveLeft(calibrationSpeed);
      sequenceTimer = millis();
      break;
      
    case SEQ_P1_CAL_L1:
      if (digitalRead(GOALIE_LIMIT_L_PIN) == LOW) {
        stopGoalie();
        delay(300);
        Serial.println("[SECUENCIA] P1 Cal: Límite izquierdo alcanzado.");
        seqState = SEQ_P1_CAL_R;
        moveRight(calibrationSpeed);
        sequenceTimer = millis();
      } else if (millis() - sequenceTimer > 15000) {
        stopGoalie();
        Serial.println("[SECUENCIA] ERROR: Timeout buscando límite izquierdo en P1.");
        seqState = SEQ_IDLE;
      }
      break;
      
    case SEQ_P1_CAL_R:
      if (digitalRead(GOALIE_LIMIT_R_PIN) == LOW) {
        travelTimeRightMs = millis() - sequenceTimer;
        stopGoalie();
        delay(300);
        Serial.print("[SECUENCIA] P1 Cal: Límite derecho alcanzado. Tiempo de ida: ");
        Serial.print(travelTimeRightMs);
        Serial.println(" ms.");
        seqState = SEQ_P1_CAL_L2;
        moveLeft(calibrationSpeed);
        sequenceTimer = millis();
      } else if (millis() - sequenceTimer > 15000) {
        stopGoalie();
        Serial.println("[SECUENCIA] ERROR: Timeout buscando límite derecho en P1.");
        seqState = SEQ_IDLE;
      }
      break;
      
    case SEQ_P1_CAL_L2:
      if (digitalRead(GOALIE_LIMIT_L_PIN) == LOW) {
        travelTimeLeftMs = millis() - sequenceTimer;
        stopGoalie();
        isCalibrated = true;
        currentPositionPercent = 0.0;
        Serial.print("[SECUENCIA] P1 Cal: Retorno izquierdo alcanzado. Tiempo de vuelta: ");
        Serial.print(travelTimeLeftMs);
        Serial.println(" ms.");
        seqState = SEQ_P1_LOOP_START;
      } else if (millis() - sequenceTimer > 15000) {
        stopGoalie();
        Serial.println("[SECUENCIA] ERROR: Timeout buscando retorno izquierdo en P1.");
        seqState = SEQ_IDLE;
      }
      break;
      
    case SEQ_P1_LOOP_START:
      Serial.println("[SECUENCIA] P1 Loop: Iniciando 10 viajes entre 30% y 70%.");
      sequenceLoopCount = 0;
      startMoveToPosition(30.0); // Posicionamiento inicial al 30%
      seqState = SEQ_P1_LOOP_MOVE;
      break;
      
    case SEQ_P1_LOOP_MOVE:
      if (motorState == GOALIE_IDLE) {
        if (sequenceLoopCount >= 10) {
          Serial.println("[SECUENCIA] P1 Loop: 10 viajes completados.");
          seqState = SEQ_P2_INIT;
          break;
        }
        
        float target = (sequenceLoopCount % 2 == 0) ? 70.0 : 30.0;
        
        // En los viajes 3 (index 2) y 7 (index 6), parar en el medio (50%) por 1 segundo
        if ((sequenceLoopCount == 2 || sequenceLoopCount == 6) && currentPositionPercent < 50.0) {
          Serial.print("[SECUENCIA] Viaje ");
          Serial.print(sequenceLoopCount + 1);
          Serial.println(": Parando en el medio (50%) por 1 segundo...");
          startMoveToPosition(50.0);
          seqState = SEQ_P1_LOOP_WAIT_MID;
        } else {
          Serial.print("[SECUENCIA] Viaje ");
          Serial.print(sequenceLoopCount + 1);
          Serial.print(" hacia ");
          Serial.print(target);
          Serial.println("%...");
          startMoveToPosition(target);
          sequenceLoopCount++;
        }
      }
      break;
      
    case SEQ_P1_LOOP_WAIT_MID:
      if (motorState == GOALIE_IDLE) {
        stopGoalie();
        sequenceTimer = millis();
        seqState = SEQ_P1_LOOP_RESUME;
      }
      break;
      
    case SEQ_P1_LOOP_RESUME:
      if (millis() - sequenceTimer >= 1000) {
        Serial.println("[SECUENCIA] Resumiendo viaje hacia 70%...");
        startMoveToPosition(70.0);
        sequenceLoopCount++;
        seqState = SEQ_P1_LOOP_MOVE;
      }
      break;
      
    case SEQ_P2_INIT:
      motorSpeed = 70;
      calibrationSpeed = 70;
      isCalibrated = false;
      Serial.println("\n[SECUENCIA] === INICIANDO PARTE 2 (Velocidad 70) ===");
      seqState = SEQ_P2_CAL_L1;
      moveLeft(calibrationSpeed);
      sequenceTimer = millis();
      break;
      
    case SEQ_P2_CAL_L1:
      if (digitalRead(GOALIE_LIMIT_L_PIN) == LOW) {
        stopGoalie();
        delay(300);
        Serial.println("[SECUENCIA] P2 Cal: Límite izquierdo alcanzado.");
        seqState = SEQ_P2_CAL_R;
        moveRight(calibrationSpeed);
        sequenceTimer = millis();
      } else if (millis() - sequenceTimer > 15000) {
        stopGoalie();
        Serial.println("[SECUENCIA] ERROR: Timeout buscando límite izquierdo en P2.");
        seqState = SEQ_IDLE;
      }
      break;
      
    case SEQ_P2_CAL_R:
      if (digitalRead(GOALIE_LIMIT_R_PIN) == LOW) {
        travelTimeRightMs = millis() - sequenceTimer;
        stopGoalie();
        delay(300);
        Serial.print("[SECUENCIA] P2 Cal: Límite derecho alcanzado. Tiempo de ida: ");
        Serial.print(travelTimeRightMs);
        Serial.println(" ms.");
        seqState = SEQ_P2_CAL_L2;
        moveLeft(calibrationSpeed);
        sequenceTimer = millis();
      } else if (millis() - sequenceTimer > 15000) {
        stopGoalie();
        Serial.println("[SECUENCIA] ERROR: Timeout buscando límite derecho en P2.");
        seqState = SEQ_IDLE;
      }
      break;
      
    case SEQ_P2_CAL_L2:
      if (digitalRead(GOALIE_LIMIT_L_PIN) == LOW) {
        travelTimeLeftMs = millis() - sequenceTimer;
        stopGoalie();
        isCalibrated = true;
        currentPositionPercent = 0.0;
        Serial.print("[SECUENCIA] P2 Cal: Retorno izquierdo alcanzado. Tiempo de vuelta: ");
        Serial.print(travelTimeLeftMs);
        Serial.println(" ms.");
        seqState = SEQ_P2_LOOP_START;
      } else if (millis() - sequenceTimer > 15000) {
        stopGoalie();
        Serial.println("[SECUENCIA] ERROR: Timeout buscando retorno izquierdo en P2.");
        seqState = SEQ_IDLE;
      }
      break;
      
    case SEQ_P2_LOOP_START:
      Serial.println("[SECUENCIA] P2 Loop: Iniciando 10 viajes entre 20% y 80%.");
      sequenceLoopCount = 0;
      startMoveToPosition(20.0); // Posicionamiento inicial al 20%
      seqState = SEQ_P2_LOOP_MOVE;
      break;
      
    case SEQ_P2_LOOP_MOVE:
      if (motorState == GOALIE_IDLE) {
        if (sequenceLoopCount >= 10) {
          Serial.println("[SECUENCIA] P2 Loop: 10 viajes completados.");
          seqState = SEQ_P2_STOP_40;
          break;
        }
        float target = (sequenceLoopCount % 2 == 0) ? 80.0 : 20.0;
        Serial.print("[SECUENCIA] Viaje ");
        Serial.print(sequenceLoopCount + 1);
        Serial.print(" hacia ");
        Serial.print(target);
        Serial.println("%...");
        startMoveToPosition(target);
        sequenceLoopCount++;
      }
      break;
      
    case SEQ_P2_STOP_40:
      if (motorState == GOALIE_IDLE) {
        Serial.println("[SECUENCIA] Desplazando al 40%...");
        startMoveToPosition(40.0);
        seqState = SEQ_P2_WAIT_40;
      }
      break;
      
    case SEQ_P2_WAIT_40:
      if (motorState == GOALIE_IDLE) {
        stopGoalie();
        Serial.println("[SECUENCIA] Detenido en 40% por 2 segundos...");
        sequenceTimer = millis();
        seqState = SEQ_P2_STOP_60;
      }
      break;
      
    case SEQ_P2_STOP_60:
      if (millis() - sequenceTimer >= 2000) {
        Serial.println("[SECUENCIA] Desplazando al 60%...");
        startMoveToPosition(60.0);
        seqState = SEQ_P2_WAIT_60;
      }
      break;
      
    case SEQ_P2_WAIT_60:
      if (motorState == GOALIE_IDLE) {
        stopGoalie();
        Serial.println("[SECUENCIA] Detenido en 60% por 2 segundos...");
        sequenceTimer = millis();
        seqState = SEQ_P2_FINAL_START;
      }
      break;
      
    case SEQ_P2_FINAL_START:
      if (millis() - sequenceTimer >= 2000) {
        Serial.println("[SECUENCIA] Iniciando 5 viajes finales entre 30% y 70%.");
        sequenceLoopCount = 0;
        startMoveToPosition(30.0);
        seqState = SEQ_P2_FINAL_MOVE;
      }
      break;
      
    case SEQ_P2_FINAL_MOVE:
      if (motorState == GOALIE_IDLE) {
        if (sequenceLoopCount >= 5) {
          Serial.println("[SECUENCIA] Secuencia finalizada.");
          seqState = SEQ_DONE;
          break;
        }
        float target = (sequenceLoopCount % 2 == 0) ? 70.0 : 30.0;
        Serial.print("[SECUENCIA] Viaje final ");
        Serial.print(sequenceLoopCount + 1);
        Serial.print(" hacia ");
        Serial.print(target);
        Serial.println("%...");
        startMoveToPosition(target);
        sequenceLoopCount++;
      }
      break;
      
    case SEQ_DONE:
      stopGoalie();
      seqState = SEQ_IDLE;
      Serial.println("[SECUENCIA] === SECUENCIA COMPLETA FINALIZADA CON ÉXITO ===");
      break;
  }
}

// Máquina de estados no-bloqueante para el Bucle de Juego Automático (Multifase fin-a-fin)
void updateGameLoopState() {
  if (!loopActive) return;
  
  switch(gameLoopState) {
    case LOOP_TRAVELING:
      if (motorState == GOALIE_IDLE) {
        if (loopTravelCount >= loopTargetTravels) {
          Serial.print("[BUCLE JUEGO] Fase ");
          Serial.print(loopPhase);
          Serial.println(" completada. Desplazando al centro (50%)...");
          startMoveToPosition(50.0);
          gameLoopState = LOOP_GOING_MID;
        } else {
          float target = currentLoopDirection ? 100.0 : 0.0;
          Serial.print("[BUCLE JUEGO] Viaje ");
          Serial.print(loopTravelCount + 1);
          Serial.print("/");
          Serial.print(loopTargetTravels);
          Serial.print(" en Fase ");
          Serial.print(loopPhase);
          Serial.print(" hacia ");
          Serial.print(target);
          Serial.println("%...");
          
          startMoveToPosition(target);
          currentLoopDirection = !currentLoopDirection;
          loopTravelCount++;
        }
      }
      break;
      
    case LOOP_GOING_MID:
      if (motorState == GOALIE_IDLE) {
        Serial.println("[BUCLE JUEGO] Llegó al centro. Deteniendo 1 segundo...");
        gameLoopTimer = millis();
        gameLoopState = LOOP_PAUSING_MID;
      }
      break;
      
    case LOOP_PAUSING_MID:
      if (millis() - gameLoopTimer >= 1000) {
        loopPhase++;
        if (loopPhase > 3) {
          loopPhase = 1;
        }
        
        loopTravelCount = 0;
        if (loopPhase == 1) {
          loopTargetTravels = 6; // 3 ida y vuelta
          Serial.println("\n[BUCLE JUEGO] === INICIANDO FASE 1 (3 viajes de ida y vuelta) ===");
        } else if (loopPhase == 2) {
          loopTargetTravels = 4; // 2 ida y vuelta
          Serial.println("\n[BUCLE JUEGO] === INICIANDO FASE 2 (2 viajes de ida y vuelta) ===");
        } else if (loopPhase == 3) {
          loopTargetTravels = 10; // 5 ida y vuelta
          Serial.println("\n[BUCLE JUEGO] === INICIANDO FASE 3 (5 viajes de ida y vuelta) ===");
        }
        
        gameLoopState = LOOP_TRAVELING;
      }
      break;
  }
}

// =========================================================================
// INTERFAZ DASHBOARD EN VIVO (HTML/JS)
// =========================================================================
const char index_html[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0, user-scalable=no">
    <title>Prueba de Arquero - Bolirana</title>
    <style>
        :root {
            --bg: #0b0f19;
            --card-bg: rgba(20, 30, 55, 0.6);
            --primary: #00d2ff;
            --danger: #ff4d4d;
            --text: #e2e8f0;
            --muted: #94a3b8;
            --border: rgba(255, 255, 255, 0.08);
        }
        body {
            font-family: -apple-system, BlinkMacSystemFont, "Segoe UI", Roboto, sans-serif;
            background-color: var(--bg);
            background-image: radial-gradient(circle at 50% 0%, #172554 0%, #0b0f19 100%);
            color: var(--text);
            margin: 0;
            padding: 15px;
        }
        .container {
            max-width: 450px;
            margin: 10px auto;
            background: var(--card-bg);
            backdrop-filter: blur(12px);
            -webkit-backdrop-filter: blur(12px);
            padding: 20px;
            border-radius: 16px;
            border: 1px solid var(--border);
            box-shadow: 0 8px 32px rgba(0,0,0,0.5);
            text-align: center;
        }
        h2 {
            color: var(--primary);
            margin-top: 0;
            text-transform: uppercase;
            letter-spacing: 1px;
        }
        .section {
            background: rgba(15, 23, 42, 0.5);
            padding: 15px;
            border-radius: 12px;
            margin-bottom: 15px;
            border: 1px solid var(--border);
            text-align: left;
        }
        .section-title {
            font-size: 0.85em;
            color: var(--muted);
            text-transform: uppercase;
            letter-spacing: 1px;
            margin-bottom: 12px;
            border-bottom: 1px solid rgba(255,255,255,0.05);
            padding-bottom: 4px;
        }
        .limit-row {
            display: flex;
            justify-content: space-between;
            margin-bottom: 10px;
        }
        .badge {
            padding: 4px 10px;
            border-radius: 20px;
            font-size: 0.8em;
            font-weight: bold;
        }
        .badge-pressed { background: rgba(239, 68, 68, 0.2); color: #ef4444; }
        .badge-free { background: rgba(16, 185, 129, 0.2); color: #10b981; }
        
        .btn-group {
            display: flex;
            gap: 10px;
            margin-bottom: 10px;
        }
        button {
            flex: 1;
            padding: 14px;
            border: none;
            border-radius: 8px;
            font-weight: bold;
            font-size: 1em;
            cursor: pointer;
            transition: all 0.2s;
            background: #334155;
            color: white;
        }
        button:active { transform: scale(0.98); }
        .btn-primary { background: linear-gradient(135deg, #00d2ff 0%, #007bb8 100%); color: #0b0f19; }
        .btn-danger { background: linear-gradient(135deg, #ff5c5c 0%, #c62828 100%); }
        
        input[type="range"] {
            width: 100%;
            margin-top: 8px;
            background: #1e293b;
            border-radius: 8px;
            height: 6px;
            -webkit-appearance: none;
        }
        input[type="range"]::-webkit-slider-thumb {
            -webkit-appearance: none;
            height: 18px; width: 18px;
            border-radius: 50%;
            background: var(--primary);
            cursor: pointer;
        }
        .val-display {
            float: right;
            color: var(--primary);
            font-weight: bold;
            font-family: monospace;
        }
    </style>
</head>
<body>
    <div class="container">
        <h2>Control del Arquero (DC)</h2>
        
        <!-- Límites de carrera -->
        <div class="section">
            <div class="section-title">Sensores Fin de Carrera</div>
            <div class="limit-row">
                <span>Límite Izquierdo (GND 12):</span>
                <span id="limit-l" class="badge badge-free">LIBRE</span>
            </div>
            <div class="limit-row">
                <span>Límite Derecho (GND 13):</span>
                <span id="limit-r" class="badge badge-free">LIBRE</span>
            </div>
        </div>

        <!-- Calibración -->
        <div class="section">
            <div class="section-title">Calibración y Posición</div>
            <div style="margin-bottom:12px">
                <span>Estado Calibración:</span>
                <span id="cal-status" class="badge badge-pressed">SIN CALIBRAR</span>
            </div>
            <div style="margin-bottom:12px">
                <span>Tiempo Recorrido:</span>
                <span id="cal-time" class="val-display" style="float:none">0 ms</span>
            </div>
            <div style="margin-bottom:15px">
                <label>Velocidad Calibración (PWM)<span id="val-cal-speed" class="val-display">60</span></label>
                <input type="range" id="input-cal-speed" min="30" max="150" value="60" onchange="updateCalSpeed(this.value)">
            </div>
            <button class="btn-primary" style="width:100%" onclick="calibrate()">📐 INICIAR AUTO-CALIBRACIÓN</button>
        </div>

        <!-- Controles Manuales -->
        <div class="section">
            <div class="section-title">Control Manual y Ajuste</div>
            <div class="btn-group">
                <button onmousedown="startMove('left')" onmouseup="stopMove()" ontouchstart="startMove('left')" ontouchend="stopMove()">◀ MOVER IZQ</button>
                <button class="btn-danger" onclick="action('stop')">⏹ PARAR</button>
                <button onmousedown="startMove('right')" onmouseup="stopMove()" ontouchstart="startMove('right')" ontouchend="stopMove()">MOVER DER ▶</button>
            </div>
            
            <div style="margin-top:15px">
                <label>Velocidad Motor (PWM)<span id="val-speed" class="val-display">150</span></label>
                <input type="range" id="input-speed" min="50" max="255" value="150" onchange="updateSpeed(this.value)">
            </div>
        </div>

        <!-- Posicionamiento Dinámico -->
        <div class="section">
            <div class="section-title">Posición en la Portería</div>
            <div>
                <label>Desplazar a Posición (%)<span id="val-pos" class="val-display">0%</span></label>
                <input type="range" id="input-pos" min="0" max="100" value="0" disabled onchange="goToPos(this.value)">
            </div>
        </div>

        <!-- BUCLE AUTOMÁTICO -->
        <div class="section">
            <div class="section-title">Bucle de Juego (Secuencia Dinámica Fin-a-Fin)</div>
            <div style="margin-bottom:12px; display:flex; justify-content:space-between; align-items:center">
                <span>Activar Bucle Infinito:</span>
                <label class="switch">
                    <input type="checkbox" id="input-loop" disabled onchange="toggleLoop(this.checked)">
                    <span class="slider"></span>
                </label>
            </div>
            <div style="font-size:0.85em; color:var(--muted); line-height:1.4">
                <strong>Secuencia activa (Fin a Fin):</strong><br>
                1. 3 viajes de ida y vuelta → Detener en el medio 1s.<br>
                2. 2 viajes de ida y vuelta → Detener en el medio 1s.<br>
                3. 5 viajes de ida y vuelta → Detener en el medio 1s.<br>
                4. Repetir bucle de forma infinita.
            </div>
        </div>

        <!-- SECUENCIA DE PRUEBA -->
        <div class="section">
            <div class="section-title">Secuencia de Prueba Automatizada</div>
            <div style="margin-bottom:12px">
                <span>Estado Secuencia:</span>
                <span id="seq-status" class="badge badge-free">INACTIVO</span>
            </div>
            <button class="btn-primary" style="width:100%; background:linear-gradient(135deg, #f59e0b, #d97706)" onclick="startSequence()">🚀 EJECUTAR SECUENCIA DE PRUEBA</button>
        </div>
    </div>

    <script>
        function updateVal(id, val) {
            document.getElementById('val-' + id).textContent = val;
        }

        async function loadStatus() {
            try {
                const res = await fetch('/status');
                const data = await res.json();
                
                // Finales de carrera (LOW = Presionado)
                const limitL = document.getElementById('limit-l');
                if (data.limitL === 0) {
                    limitL.className = 'badge badge-pressed';
                    limitL.textContent = 'PRESIONADO';
                } else {
                    limitL.className = 'badge badge-free';
                    limitL.textContent = 'LIBRE';
                }

                const limitR = document.getElementById('limit-r');
                if (data.limitR === 0) {
                    limitR.className = 'badge badge-pressed';
                    limitR.textContent = 'PRESIONADO';
                } else {
                    limitR.className = 'badge badge-free';
                    limitR.textContent = 'LIBRE';
                }

                // Calibración
                const calStatus = document.getElementById('cal-status');
                const inputPos = document.getElementById('input-pos');
                const inputLoop = document.getElementById('input-loop');
                if (data.calibrated) {
                    calStatus.className = 'badge badge-free';
                    calStatus.textContent = 'CALIBRADO';
                    inputPos.removeAttribute('disabled');
                    inputLoop.removeAttribute('disabled');
                } else {
                    calStatus.className = 'badge badge-pressed';
                    calStatus.textContent = 'SIN CALIBRAR';
                    inputPos.setAttribute('disabled', 'true');
                    inputLoop.setAttribute('disabled', 'true');
                }

                document.getElementById('cal-time').textContent = data.travelTime + ' ms';
                
                // Actualizar velocidad de calibración desde el estado
                const inputCalSpeed = document.getElementById('input-cal-speed');
                if (document.activeElement !== inputCalSpeed) {
                    inputCalSpeed.value = data.calSpeed;
                    updateVal('cal-speed', data.calSpeed);
                }

                // Si no se está arrastrando el slider de posición, actualizar valor
                if (document.activeElement !== inputPos) {
                    inputPos.value = Math.round(data.currentPos);
                    updateVal('pos', Math.round(data.currentPos) + '%');
                }

                // Actualizar interruptor de bucle
                if (document.activeElement !== inputLoop) {
                    inputLoop.checked = data.loopActive === 1;
                }

                // Actualizar estado de secuencia de prueba
                const seqStatus = document.getElementById('seq-status');
                if (data.seqState > 0) {
                    seqStatus.className = 'badge badge-pressed';
                    seqStatus.textContent = 'EJECUTANDO (Paso ' + data.seqState + ')';
                } else {
                    seqStatus.className = 'badge badge-free';
                    seqStatus.textContent = 'INACTIVO';
                }

            } catch(e) {
                console.error("Error leyendo estado del arquero:", e);
            }
        }

        async function action(act) {
            await fetch(`/action?cmd=${act}`);
        }

        let isMoving = false;
        async function startMove(dir) {
            isMoving = true;
            await fetch(`/action?cmd=move&dir=${dir}`);
        }
        async function stopMove() {
            if (isMoving) {
                isMoving = false;
                await fetch(`/action?cmd=stop`);
            }
        }

        async function updateSpeed(val) {
            updateVal('speed', val);
            await fetch(`/action?cmd=speed&val=${val}`);
        }

        async function goToPos(val) {
            updateVal('pos', val + '%');
            await fetch(`/action?cmd=pos&val=${val}`);
        }

        async function calibrate() {
            await fetch('/action?cmd=calibrate');
        }

        async function updateCalSpeed(val) {
            updateVal('cal-speed', val);
            await fetch(`/action?cmd=calSpeed&val=${val}`);
        }

        async function toggleLoop(checked) {
            const active = checked ? 1 : 0;
            await fetch(`/action?cmd=loop&active=${active}`);
        }

        async function startSequence() {
            if (confirm("¿Deseas iniciar la secuencia de prueba completa? (Velocidad 50 y 70 con paradas dinámicas)")) {
                await fetch('/action?cmd=sequence');
            }
        }

        // Leer estado
        setInterval(loadStatus, 500);
    </script>
</body>
</html>
)rawliteral";

// ==========================================
// ENDPOINTS
// ==========================================

void handleRoot() {
  server.send_P(200, "text/html", index_html);
}

void handleStatus() {
  String json = "{";
  json += "\"limitL\":" + String(digitalRead(GOALIE_LIMIT_L_PIN));
  json += ",\"limitR\":" + String(digitalRead(GOALIE_LIMIT_R_PIN));
  json += ",\"calibrated\":" + String(isCalibrated ? 1 : 0);
  json += ",\"travelTime\":" + String((travelTimeLeftMs + travelTimeRightMs) / 2);
  json += ",\"currentPos\":" + String(currentPositionPercent);
  json += ",\"loopActive\":" + String(loopActive ? 1 : 0);
  json += ",\"loopMin\":" + String(loopMinPercent);
  json += ",\"loopMax\":" + String(loopMaxPercent);
  json += ",\"seqState\":" + String(seqState);
  json += ",\"calSpeed\":" + String(calibrationSpeed);
  json += "}";
  server.send(200, "application/json", json);
}

void handleAction() {
  if (server.hasArg("cmd")) {
    String cmd = server.arg("cmd");
    Serial.print("\n[WEB] HTTP /action recibido. cmd = \"");
    Serial.print(cmd);
    Serial.println("\"");
    
    if (cmd == "stop") {
      loopActive = false;
      seqState = SEQ_IDLE;
      stopGoalie();
      motorState = GOALIE_IDLE;
    }
    else if (cmd == "move" && server.hasArg("dir")) {
      loopActive = false;
      seqState = SEQ_IDLE;
      motorState = GOALIE_IDLE;
      String dir = server.arg("dir");
      Serial.print("[WEB] -> Dirección de movimiento: ");
      Serial.println(dir);
      if (dir == "left") moveLeft(motorSpeed);
      else if (dir == "right") moveRight(motorSpeed);
    }
    else if (cmd == "speed" && server.hasArg("val")) {
      motorSpeed = server.arg("val").toInt();
      Serial.print("[WEB] -> Cambiando velocidad PWM a: ");
      Serial.println(motorSpeed);
    }
    else if (cmd == "calSpeed" && server.hasArg("val")) {
      calibrationSpeed = server.arg("val").toInt();
      Serial.print("[WEB] -> Cambiando velocidad de calibración a: ");
      Serial.println(calibrationSpeed);
    }
    else if (cmd == "pos" && server.hasArg("val")) {
      loopActive = false;
      seqState = SEQ_IDLE;
      motorState = GOALIE_IDLE;
      float val = server.arg("val").toFloat();
      Serial.print("[WEB] -> Solicitando ir a posición: ");
      Serial.print(val);
      Serial.println("%");
      startMoveToPosition(val);
    }
    else if (cmd == "calibrate") {
      calibrateGoalie();
    }
    else if (cmd == "loop") {
      seqState = SEQ_IDLE;
      if (server.hasArg("active")) {
        loopActive = (server.arg("active").toInt() == 1);
      }
      
      Serial.print("[WEB] -> Configurando bucle de juego. Activo: ");
      Serial.println(loopActive);
      
      if (loopActive) {
        gameLoopState = LOOP_TRAVELING;
        loopPhase = 1;
        loopTravelCount = 0;
        loopTargetTravels = 6;       // Fase 1: 3 ida y vuelta = 6 viajes sencillos
        currentLoopDirection = true; // Empezar viajando a la derecha (100.0%)
        Serial.println("\n[BUCLE JUEGO] === INICIANDO FASE 1 (3 viajes de ida y vuelta) ===");
      } else {
        stopGoalie();
        motorState = GOALIE_IDLE;
      }
    }
    else if (cmd == "sequence") {
      loopActive = false;
      motorState = GOALIE_IDLE;
      seqState = SEQ_P1_INIT; // Iniciar secuencia
      Serial.println("[WEB] -> Solicitando inicio de secuencia de prueba automatizada.");
    }
  }
  server.send(200, "text/plain", "OK");
}

// ==========================================
// SETUP & LOOP
// ==========================================

void setup() {
  Serial.begin(115200);

  // Configurar pines del IBT_2 como salidas
  pinMode(GOALIE_RPWM_PIN, OUTPUT);
  pinMode(GOALIE_LPWM_PIN, OUTPUT);
  stopGoalie();

  // Configurar finales de carrera con PULL-UP interno
  pinMode(GOALIE_LIMIT_L_PIN, INPUT_PULLUP);
  pinMode(GOALIE_LIMIT_R_PIN, INPUT_PULLUP);

  // Conectar WiFi
  WiFi.begin(ssid, password);
  Serial.print("Conectando al WiFi...");
  int wifiTimeout = 0;
  while (WiFi.status() != WL_CONNECTED && wifiTimeout < 20) {
    delay(500);
    Serial.print(".");
    wifiTimeout++;
  }

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\n¡Conectado al WiFi con éxito!");
    Serial.print("IP del Control de Arquero: ");
    Serial.println(WiFi.localIP());
  } else {
    Serial.println("\nNo se pudo conectar al WiFi.");
  }

  // Rutas Web
  server.on("/", HTTP_GET, handleRoot);
  server.on("/status", HTTP_GET, handleStatus);
  server.on("/action", handleAction); // Acepta GET y POST, evitando bloqueos OPTIONS

  server.begin();
}

void loop() {
  server.handleClient();

  static bool lastL = HIGH;
  static bool lastR = HIGH;
  bool currentL = digitalRead(GOALIE_LIMIT_L_PIN);
  bool currentR = digitalRead(GOALIE_LIMIT_R_PIN);

  // Informar por consola serial si el estado físico de los sensores cambia
  if (currentL != lastL) {
    lastL = currentL;
    Serial.print("[SENSOR] Límite Izquierdo cambió a: ");
    Serial.println(currentL == LOW ? "DETECTADO (LOW)" : "LIBRE (HIGH)");
  }
  if (currentR != lastR) {
    lastR = currentR;
    Serial.print("[SENSOR] Límite Derecho cambió a: ");
    Serial.println(currentR == LOW ? "DETECTADO (LOW)" : "LIBRE (HIGH)");
  }

  // Actualizar la máquina de estados del movimiento del arquero (No-bloqueante)
  updateGoalieStateMachine();

  // Actualizar la secuencia de prueba automatizada (No-bloqueante)
  updateTestSequence();

  // Actualizar el bucle de juego infinito (No-bloqueante)
  updateGameLoopState();

  // Control de seguridad por hardware adicional en caso de fallo
  if (currentL == LOW && motorState == GOALIE_IDLE) {
    analogWrite(GOALIE_LPWM_PIN, 0);
  }
  if (currentR == LOW && motorState == GOALIE_IDLE) {
    analogWrite(GOALIE_RPWM_PIN, 0);
  }
  
  delay(1);
}
