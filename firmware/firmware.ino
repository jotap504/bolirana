#include <Arduino.h>
#include <Adafruit_NeoPixel.h> // Biblioteca estándar de Arduino para tiras direccionables WS2812B

// ══════════════════════════════════════════════════════════════════
// CONFIGURACIÓN DE PINES
// ══════════════════════════════════════════════════════════════════

// Sensores de Puntos (Entradas digitales con PULLUP interno)
const int PIN_RANA   = 12;
const int PIN_SAPO   = 14;
const int PIN_FOSA_1 = 27;
const int PIN_FOSA_2 = 26;
const int PIN_FOSA_3 = 25;
const int PIN_FOSA_4 = 33;
const int PIN_CERO   = 5;  // Sensor Cero Puntos / Bola no acertada (salida final)

// Entrada de Fichas (Monedero)
const int PIN_MONEDERO = 18;

// Botones de Panel Físicos
const int PIN_BTN_START = 19;
const int PIN_BTN_PAUSE = 21;

// Actuadores de Salida
const int PIN_BELL       = 13; // Solenoide/Campana física
const int PIN_LED_PUNTOS = 2;  // LED indicador de aciertos (LED AZUL INTEGRADO EN ESP32 - GPIO 2)
const int PIN_PROXIMIDAD = 16; // Pin de salida digital (OUT) del radar de proximidad HLK-LD2410C (Antitrampa)

// 🌈 CONFIGURACIÓN DE ILUMINACIÓN NEOPINDEX (WS2812B)
const int PIN_NEOPIXEL = 4;   // Pin de datos para tira direccionable
const int CANTIDAD_LEDS = 60;  // Número de leds en la tira direccionable
Adafruit_NeoPixel strip(CANTIDAD_LEDS, PIN_NEOPIXEL, NEO_GRB + NEO_KHZ800);

// 🚥 CONFIGURACIÓN DE LEDS RGB ESTÁNDAR (Opcional, para iluminación general)
const int PIN_RGB_RED   = 32; // Pines PWM para LED RGB analógico
const int PIN_RGB_GREEN = 22;
const int PIN_RGB_BLUE  = 23;

// ⚽ MOTOR DEL ARQUERO MÓVIL (Driver Puente H - L298N o L9110S)
const int PIN_MOTOR_IN1 = 17; // Dirección A (GPIO 17)
const int PIN_MOTOR_IN2 = 15; // Dirección B (GND/VCC alternado)
const int PIN_LIMIT_IZQ = 34; // Sensor de límite/Fin de carrera Izquierdo
const int PIN_LIMIT_DER = 35; // Sensor de límite/Fin de carrera Derecho

// ══════════════════════════════════════════════════════════════════
// VARIABLES DE ESTADO Y CONTROL
// ══════════════════════════════════════════════════════════════════
bool juegoEnEjecucion = false; // El motor del arquero solo funciona en true
int direccionArquero  = 1;     // 1 = Derecha, -1 = Izquierda

unsigned long ultimoDisparoRana  = 0;
unsigned long ultimoDisparoSapo  = 0;
unsigned long ultimoDisparoF1    = 0;
unsigned long ultimoDisparoF2    = 0;
unsigned long ultimoDisparoF3    = 0;
unsigned long ultimoDisparoF4    = 0;
unsigned long ultimoDisparoCero  = 0;
unsigned long ultimoDisparoBtnS  = 0;
unsigned long ultimoDisparoBtnP  = 0;
unsigned long ultimoLimiteIzq    = 0;
unsigned long ultimoLimiteDer    = 0;

const unsigned long TIEMPO_DEBOUNCE = 300; // Debounce de sensores

// Monedero
volatile int pulsosMonedero = 0;
unsigned long ultimoPulsoMonedero = 0;
const unsigned long ESPERA_MONEDERO = 500;

// Variables de Efectos de Luces
unsigned long tiempoUltimoEfecto = 0;
String efectoActivo = "rainbow"; // rainbow, gold_breath, green_flash, off

// Temporizadores y estados no bloqueantes para LED y Campana
unsigned long ledOffTime = 0;
unsigned long bellOffTime = 0;
bool ledEncendido = false;
bool bellEncendido = false;

// Variables de estado para sensor de proximidad Radar HLK-LD2410C
int ultimoEstadoProximidad = LOW;
unsigned long ultimoCambioProximidad = 0;

// ══════════════════════════════════════════════════════════════════
// INTERRUPCIÓN MONEDERO (ISR)
// ══════════════════════════════════════════════════════════════════
void IRAM_ATTR ISR_Monedero() {
  pulsosMonedero++;
  ultimoPulsoMonedero = millis();
}

// ══════════════════════════════════════════════════════════════════
// CONTROLLER MOTOR ARQUERO
// ══════════════════════════════════════════════════════════════════
void arrancarMotor(int dir) {
  if (dir == 1) {
    digitalWrite(PIN_MOTOR_IN1, HIGH);
    digitalWrite(PIN_MOTOR_IN2, LOW);
  } else {
    digitalWrite(PIN_MOTOR_IN1, LOW);
    digitalWrite(PIN_MOTOR_IN2, HIGH);
  }
}

void detenerMotor() {
  digitalWrite(PIN_MOTOR_IN1, LOW);
  digitalWrite(PIN_MOTOR_IN2, LOW);
}

void activarLed(unsigned long duracion) {
  digitalWrite(PIN_LED_PUNTOS, HIGH);
  ledOffTime = millis() + duracion;
  ledEncendido = true;
}

void activarCampana(unsigned long duracion) {
  digitalWrite(PIN_BELL, HIGH);
  bellOffTime = millis() + duracion;
  bellEncendido = true;
}

void destelloLed() {
  activarLed(1000); // 1 segundo encendido de manera no bloqueante
}

// ══════════════════════════════════════════════════════════════════
// ANIMACIONES DE LUCES NEOPINDEX
// ══════════════════════════════════════════════════════════════════
void aplicarLuces(String tipo, unsigned long t) {
  if (tipo == "green_flash") {
    // Destello verde intenso por gol
    for(int i=0; i<CANTIDAD_LEDS; i++) {
      strip.setPixelColor(i, strip.Color(0, 255, 100));
    }
    strip.show();
    if (t - tiempoUltimoEfecto > 800) { efectoActivo = juegoEnEjecucion ? "playing" : "rainbow"; }
  }
  else if (tipo == "gold_flash") {
    // Destello dorado por moneda
    for(int i=0; i<CANTIDAD_LEDS; i++) {
      strip.setPixelColor(i, strip.Color(245, 192, 0));
    }
    strip.show();
    if (t - tiempoUltimoEfecto > 1000) { efectoActivo = juegoEnEjecucion ? "playing" : "rainbow"; }
  }
  else if (tipo == "playing") {
    // Iluminación estática azul/cian futurista durante el juego
    for(int i=0; i<CANTIDAD_LEDS; i++) {
      strip.setPixelColor(i, strip.Color(0, 100, 200));
    }
    strip.show();
  }
  else if (tipo == "rainbow") {
    // Modo Attract (Espera): Efecto arcoíris rotativo clásico de arcade
    uint32_t offset = (t / 20) % 256;
    for(int i=0; i<CANTIDAD_LEDS; i++) {
      byte wheelPos = ((i * 256 / CANTIDAD_LEDS) + offset) & 255;
      uint32_t color;
      if(wheelPos < 85) {
        color = strip.Color(wheelPos * 3, 255 - wheelPos * 3, 0);
      } else if(wheelPos < 170) {
        wheelPos -= 85;
        color = strip.Color(255 - wheelPos * 3, 0, wheelPos * 3);
      } else {
        wheelPos -= 170;
        color = strip.Color(0, wheelPos * 3, 255 - wheelPos * 3);
      }
      strip.setPixelColor(i, color);
    }
    strip.show();
  }
}

// ══════════════════════════════════════════════════════════════════
// CONFIGURACIÓN INICIAL (SETUP)
// ══════════════════════════════════════════════════════════════════
void setup() {
  Serial.begin(115200);
  while (!Serial) { ; }
  
  // Sensores de Fosas
  pinMode(PIN_RANA,   INPUT_PULLUP);
  pinMode(PIN_SAPO,   INPUT_PULLUP);
  pinMode(PIN_FOSA_1, INPUT_PULLUP);
  pinMode(PIN_FOSA_2, INPUT_PULLUP);
  pinMode(PIN_FOSA_3, INPUT_PULLUP);
  pinMode(PIN_FOSA_4, INPUT_PULLUP);
  pinMode(PIN_CERO,   INPUT_PULLUP);
  pinMode(PIN_PROXIMIDAD, INPUT_PULLDOWN); // Pulldown interno para asegurar 0V cuando no hay presencia en el radar HLK-LD2410C
  
  // Botones e Interruptores de Límite (Fines de carrera)
  pinMode(PIN_BTN_START, INPUT_PULLUP);
  pinMode(PIN_BTN_PAUSE, INPUT_PULLUP);
  pinMode(PIN_LIMIT_IZQ, INPUT_PULLUP);
  pinMode(PIN_LIMIT_DER, INPUT_PULLUP);
  
  // Monedero
  pinMode(PIN_MONEDERO, INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(PIN_MONEDERO), ISR_Monedero, FALLING);
  
  // Actuadores y Motores
  pinMode(PIN_BELL, OUTPUT);
  digitalWrite(PIN_BELL, LOW);
  pinMode(PIN_LED_PUNTOS, OUTPUT);
  digitalWrite(PIN_LED_PUNTOS, LOW);
  
  pinMode(PIN_MOTOR_IN1, OUTPUT);
  pinMode(PIN_MOTOR_IN2, OUTPUT);
  detenerMotor();
  
  // Inicializar pines RGB estándar
  pinMode(PIN_RGB_RED,   OUTPUT);
  pinMode(PIN_RGB_GREEN, OUTPUT);
  pinMode(PIN_RGB_BLUE,  OUTPUT);
  analogWrite(PIN_RGB_RED, 0);
  analogWrite(PIN_RGB_GREEN, 0);
  analogWrite(PIN_RGB_BLUE, 0);

  // Inicializar Tira NeoPixel
  strip.begin();
  strip.show();
  
  Serial.println("{\"event\":\"system_ready\",\"message\":\"ESP32 Arquero y Luces Direccionales firmware cargado\"}");
}

// ══════════════════════════════════════════════════════════════════
// BUCLE PRINCIPAL (LOOP)
// ══════════════════════════════════════════════════════════════════
void loop() {
  unsigned long tiempoActual = millis();

  // ── 0. Control No Bloqueante Asíncrono de Actuadores (LED y Campana) ──
  if (ledEncendido && tiempoActual >= ledOffTime) {
    digitalWrite(PIN_LED_PUNTOS, LOW);
    ledEncendido = false;
  }
  if (bellEncendido && tiempoActual >= bellOffTime) {
    digitalWrite(PIN_BELL, LOW);
    bellEncendido = false;
  }
  
  // ── 1. Movimiento del Arquero Físico (Puente H + Fines de Carrera) ──
  if (juegoEnEjecucion) {
    // Si golpea el fin de carrera Izquierdo: cambiar dirección a Derecha
    if (digitalRead(PIN_LIMIT_IZQ) == LOW && (tiempoActual - ultimoLimiteIzq > TIEMPO_DEBOUNCE)) {
      direccionArquero = 1;
      ultimoLimiteIzq = tiempoActual;
      Serial.println("{\"event\":\"motor_limit\",\"side\":\"left\"}");
    }
    // Si golpea el fin de carrera Derecho: cambiar dirección a Izquierda
    if (digitalRead(PIN_LIMIT_DER) == LOW && (tiempoActual - ultimoLimiteDer > TIEMPO_DEBOUNCE)) {
      direccionArquero = -1;
      ultimoLimiteDer = tiempoActual;
      Serial.println("{\"event\":\"motor_limit\",\"side\":\"right\"}");
    }
    
    // Mantener motor activo en la dirección definida
    arrancarMotor(direccionArquero);
  } else {
    detenerMotor();
  }

  // ── 2. Lectura de Sensores Ópticos de Puntos ──────────────────────
  if (digitalRead(PIN_RANA) == LOW && (tiempoActual - ultimoDisparoRana > TIEMPO_DEBOUNCE)) {
    Serial.println("{\"event\":\"sensor\",\"target\":\"rana\"}");
    ultimoDisparoRana = tiempoActual;
    
    // Campana física y destello LED sin bloquear el procesador!
    activarCampana(100); // 100ms para la campana de manera segura
    activarLed(1000);    // 1 segundo completo para el LED indicador
    
    // Disparar flash verde
    efectoActivo = "green_flash";
    tiempoUltimoEfecto = tiempoActual;
  }
  
  if (digitalRead(PIN_SAPO) == LOW && (tiempoActual - ultimoDisparoSapo > TIEMPO_DEBOUNCE)) {
    Serial.println("{\"event\":\"sensor\",\"target\":\"sapo\"}");
    ultimoDisparoSapo = tiempoActual;
    destelloLed();
    efmeta: efectoActivo = "green_flash";
    tiempoUltimoEfecto = tiempoActual;
  }
  
  if (digitalRead(PIN_FOSA_1) == LOW && (tiempoActual - ultimoDisparoF1 > TIEMPO_DEBOUNCE)) {
    Serial.println("{\"event\":\"sensor\",\"target\":\"fosa_1\"}");
    ultimoDisparoF1 = tiempoActual;
    destelloLed();
  }
  
  if (digitalRead(PIN_FOSA_2) == LOW && (tiempoActual - ultimoDisparoF2 > TIEMPO_DEBOUNCE)) {
    Serial.println("{\"event\":\"sensor\",\"target\":\"fosa_2\"}");
    ultimoDisparoF2 = tiempoActual;
    destelloLed();
  }
  
  if (digitalRead(PIN_FOSA_3) == LOW && (tiempoActual - ultimoDisparoF3 > TIEMPO_DEBOUNCE)) {
    Serial.println("{\"event\":\"sensor\",\"target\":\"fosa_3\"}");
    ultimoDisparoF3 = tiempoActual;
    destelloLed();
  }
  
  if (digitalRead(PIN_FOSA_4) == LOW && (tiempoActual - ultimoDisparoF4 > TIEMPO_DEBOUNCE)) {
    Serial.println("{\"event\":\"sensor\",\"target\":\"fosa_4\"}");
    ultimoDisparoF4 = tiempoActual;
    destelloLed();
  }
  
  if (digitalRead(PIN_CERO) == LOW && (tiempoActual - ultimoDisparoCero > TIEMPO_DEBOUNCE)) {
    Serial.println("{\"event\":\"sensor\",\"target\":\"cero\"}");
    ultimoDisparoCero = tiempoActual;
  }

  // ── 3. Lectura de Botones Físicos ────────────────────────────────
  if (digitalRead(PIN_BTN_START) == LOW && (tiempoActual - ultimoDisparoBtnS > TIEMPO_DEBOUNCE)) {
    Serial.println("{\"event\":\"button\",\"name\":\"start\"}");
    ultimoDisparoBtnS = tiempoActual;
  }
  
  if (digitalRead(PIN_BTN_PAUSE) == LOW && (tiempoActual - ultimoDisparoBtnP > TIEMPO_DEBOUNCE)) {
    Serial.println("{\"event\":\"button\",\"name\":\"pause\"}");
    ultimoDisparoBtnP = tiempoActual;
  }

  // ── 3.5. Lectura de Sensor de Proximidad Radar HLK-LD2410C (Antitrampa) ──
  int estadoProximidad = digitalRead(PIN_PROXIMIDAD);
  if (estadoProximidad != ultimoEstadoProximidad && (tiempoActual - ultimoCambioProximidad > 500)) {
    ultimoEstadoProximidad = estadoProximidad;
    ultimoCambioProximidad = tiempoActual;
    if (estadoProximidad == HIGH) {
      Serial.println("{\"event\":\"proximity\",\"active\":true}");
    } else {
      Serial.println("{\"event\":\"proximity\",\"active\":false}");
    }
  }

  // ── 4. Lectura del Monedero (Monedas) ───────────────────────────
  if (pulsosMonedero > 0 && (tiempoActual - ultimoPulsoMonedero > ESPERA_MONEDERO)) {
    Serial.print("{\"event\":\"coin\",\"pulses\":");
    Serial.print(pulsosMonedero);
    Serial.println("}");
    
    // Destello dorado de luces por la ficha ingresada
    efectoActivo = "gold_flash";
    tiempoUltimoEfecto = tiempoActual;
    
    pulsosMonedero = 0;
  }

  // ── 5. Recepción de Comandos desde la Netbook (Serial) ──────────
  if (Serial.available() > 0) {
    String comandoRaw = Serial.readStringUntil('\n');
    comandoRaw.trim();
    
    // Comando para cambiar estado del juego (Activa/Desactiva el arquero)
    if (comandoRaw.indexOf("\"cmd\":\"state\"") != -1) {
      if (comandoRaw.indexOf("\"state\":\"playing\"") != -1) {
        juegoEnEjecucion = true;
        efectoActivo = "playing";
        Serial.println("{\"status\":\"ack\",\"message\":\"Arquero iniciado - Modo Juego\"}");
      } else {
        juegoEnEjecucion = false;
        efectoActivo = "rainbow";
        Serial.println("{\"status\":\"ack\",\"message\":\"Arquero detenido - Modo Attract\"}");
      }
    }
    // Campana
    else if (comandoRaw.indexOf("\"cmd\":\"actuate\"") != -1) {
      if (comandoRaw.indexOf("\"target\":\"bell\"") != -1) {
        activarCampana(120); // Activación no bloqueante de campana
        Serial.println("{\"status\":\"ack\",\"message\":\"Campana activada\"}");
      }
      else if (comandoRaw.indexOf("\"target\":\"led\"") != -1) {
        activarLed(2000); // Enciende el LED por 2 segundos de forma no bloqueante
        Serial.println("{\"status\":\"ack\",\"message\":\"LED de aciertos activado\"}");
      }
    }
    // Forzar efectos de luz
    else if (comandoRaw.indexOf("\"cmd\":\"fx\"") != -1) {
      if (comandoRaw.indexOf("\"type\":\"goal\"") != -1) {
        efectoActivo = "green_flash";
        tiempoUltimoEfecto = tiempoActual;
      }
      Serial.println("{\"status\":\"ack\",\"message\":\"Efecto de luces forzado\"}");
    }
  }

  // ── 6. Actualizar Animación de Luces ─────────────────────────────
  aplicarLuces(efectoActivo, tiempoActual);
  
  // Pequeña pausa para no saturar al procesador
  delay(10);
}
