/*
=============================================================================
SKETCH DE DIAGNÓSTICO SIMPLIFICADO: PRUEBA DE SENSOR IR EN GPIO 27
=============================================================================
Este código lee en tiempo real el estado del fotodiodo/fototransistor negro 
conectado al GPIO 27 y muestra el resultado en el Monitor Serie (115200 baudios).

Conexiones necesarias:
----------------------
1. LED Transparente (Emisor IR):
   - Pata Larga (+)  -> Resistencia 220 Ω -> 5V (o 3.3V)
   - Pata Corta (-)  -> GND

2. Diodo Negro (Receptor IR):
   - Pata Larga (+)  -> GND
   - Pata Corta (-)  -> GPIO 27 Y Resistencia 10 kΩ a 3.3V
=============================================================================
*/

#define IR_SENSOR_PIN 27   // Pin asignado para el receptor infrarrojo
#define LED_BUILTIN_PIN 2  // LED integrado en la placa ESP32 (opcional)

bool lastState = false;
int triggerCount = 0;
unsigned long lastPrintTime = 0;

void setup() {
  // Inicializar comunicación serie a 115200 baudios
  Serial.begin(115200);
  delay(1000);
  
  Serial.println("\n========================================================");
  Serial.println("   FUTSAPO / BOLIRANA - DIAGNÓSTICO DE SENSOR IR (GPIO 27)");
  Serial.println("========================================================");
  
  // Configurar el GPIO 27 como entrada digital
  pinMode(IR_SENSOR_PIN, INPUT);
  
  // Configurar LED integrado para indicación visual en la placa
  pinMode(LED_BUILTIN_PIN, OUTPUT);
  
  // Leer estado inicial
  lastState = digitalRead(IR_SENSOR_PIN);
  
  Serial.print("[INICIO] Estado actual del sensor en GPIO 27: ");
  if (lastState == HIGH) {
    Serial.println("1 (HIGH / BLOQUEADO - Haz de luz interrumpido)");
    digitalWrite(LED_BUILTIN_PIN, HIGH);
  } else {
    Serial.println("0 (LOW / LIBRE - Haz de luz recibiéndose normalmente)");
    digitalWrite(LED_BUILTIN_PIN, LOW);
  }
  Serial.println("--> Pasa un objeto o la mano entre los LEDs para probar...\n");
}

void loop() {
  // Lectura del estado actual en el GPIO 27
  bool currentState = digitalRead(IR_SENSOR_PIN);
  
  // Detectar cambios de estado
  if (currentState != lastState) {
    delay(20); // Filtro básico antirebotes de 20ms
    currentState = digitalRead(IR_SENSOR_PIN);
    
    if (currentState != lastState) {
      lastState = currentState;
      
      if (currentState == HIGH) {
        triggerCount++;
        digitalWrite(LED_BUILTIN_PIN, HIGH);
        Serial.print("  [X] ¡BLOQUEADO! Haz de luz interrumpido. Eventos contados: ");
        Serial.println(triggerCount);
      } else {
        digitalWrite(LED_BUILTIN_PIN, LOW);
        Serial.println("  [O] LIBRE - Luz IR recibida correctamente.");
      }
    }
  }
  
  // Imprimir resumen del estado cada 3 segundos
  if (millis() - lastPrintTime >= 3000) {
    lastPrintTime = millis();
    Serial.print("[ESTADO VIVO] GPIO 27 = ");
    Serial.print(currentState ? "1 (BLOQUEADO)" : "0 (LIBRE)");
    Serial.print(" | Total interrupciones detectadas: ");
    Serial.println(triggerCount);
  }
}
