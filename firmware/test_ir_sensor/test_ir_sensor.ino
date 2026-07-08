/*
=============================================================================
SKETCH DE DIAGNÓSTICO: PRUEBA DE SENSOR IR (BARRERA / ACOPLADOR ÓPTICO)
=============================================================================
Este sketch permite probar un sensor infrarrojo (LED Emisor + LED Receptor)
conectado directamente a un pin de la ESP32.

Tipos de conexión comunes:
1. MÓDULO CON COMPARADOR (ej. LM393, TCRT5000 con 3 o 4 pines):
   - VCC -> 3.3V (o 5V si el módulo lo requiere)
   - GND -> GND de la ESP32
   - D0  -> GPIO_PIN de prueba (ej. GPIO 15)
   
2. LED EMISOR Y RECEPTOR SUELTOS (Fotodiodo/Fototransistor):
   - Emisor IR (transmisor): Ánodo a 3.3V (mediante resistencia de 220 Ohm), Cátodo a GND.
   - Receptor IR (receptor): Colector a 3.3V, Emisor al GPIO de prueba y a una
     resistencia de 10k Ohm que va a GND (configuración pull-down externa).
=============================================================================
*/

// Pin de la ESP32 donde conectarás la salida de señal del receptor IR (D0)
#define SENSOR_PIN 15  

// Led indicador de placa (Azul en la ESP32 DevKit, GPIO 2 por defecto)
#define LED_INDICATOR_PIN 2

// Configura la polaridad de tu sensor:
// - true: El pin se pone en HIGH cuando pasa la pelota (Bloqueo/Reflexión)
// - false: El pin se pone en LOW cuando pasa la pelota (Normal en barreras activas en bajo)
const bool ACTIVE_STATE = HIGH; 

// Variables de estado
int lastSensorState = -1;
unsigned long lastStateChangeTime = 0;
const unsigned long DEBOUNCE_MS = 50; // Filtro de rebotes rápidos

void setup() {
  // Inicialización del Monitor Serial a 115200 baudios
  Serial.begin(115200);
  delay(1000);
  
  Serial.println("\n==================================================");
  Serial.println("  INICIANDO DIAGNÓSTICO DE SENSOR INFRARROJO (IR) ");
  Serial.println("==================================================");
  Serial.printf("Monitoreando señal en GPIO: %d\n", SENSOR_PIN);
  Serial.printf("Estado activo definido: %s\n", ACTIVE_STATE ? "HIGH" : "LOW");
  Serial.println("Colocá un objeto delante del sensor para verificar lectura...");
  Serial.println("==================================================\n");

  // Configuración de pines
  // Nota: Usamos INPUT_PULLUP por si estás usando un receptor directo sin circuito externo.
  // Si tu módulo ya tiene pull-up (como las placas LM393), funcionará igual.
  pinMode(SENSOR_PIN, INPUT_PULLUP);
  
  pinMode(LED_INDICATOR_PIN, OUTPUT);
  digitalWrite(LED_INDICATOR_PIN, LOW);
}

void loop() {
  // 1. Leer el estado físico del pin
  int currentReading = digitalRead(SENSOR_PIN);
  unsigned long now = millis();

  // 2. Filtro de rebotes
  if (currentReading != lastSensorState) {
    if (now - lastStateChangeTime > DEBOUNCE_MS) {
      lastStateChangeTime = now;
      lastSensorState = currentReading;

      // Determinar si la barrera está interrumpida (Detección de bola)
      bool isDetected = (currentReading == ACTIVE_STATE);

      // 3. Encender el LED azul de la ESP32 cuando se detecta el objeto
      digitalWrite(LED_INDICATOR_PIN, isDetected ? HIGH : LOW);

      // 4. Imprimir el diagnóstico detallado en el monitor serial
      if (isDetected) {
        Serial.println(" [ DETECTADO ]  ██████████████████ (Haz de luz INTERRUMPIDO / Acierto)");
      } else {
        Serial.println(" [  LIBRE   ]  ------------------ (Haz de luz OK / Despejado)");
      }
      
      // Imprimir el valor digital leído para diagnóstico rápido
      Serial.printf("   -> Lectura cruda en pin %d: %s (Voltaje en pin: %s)\n\n", 
                    SENSOR_PIN, 
                    currentReading == HIGH ? "HIGH" : "LOW",
                    currentReading == HIGH ? "~3.3V" : "0V (GND)");
    }
  }
}
