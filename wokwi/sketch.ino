// Prueba de servos para Wokwi (sin cámara: Wokwi no simula la ESP32-CAM).
// Mismos pines, ángulos y tiempo que el casillero real.
// Escribe 1, 2 o 3 en el Monitor Serie para abrir ese casillero 5 s.
// Escribe L para encender/apagar el LED flash (GPIO 4).
#include <ESP32Servo.h>

const int PIN_SERVO[3] = { 14, 15, 13 };
const int ANG_CERRADO = 0;
const int ANG_ABIERTO = 90;
const unsigned long TIEMPO_ABIERTO = 5000;
const int PIN_LED_FLASH = 4;

Servo servo[3];
bool abierto[3] = { false, false, false };
unsigned long tCierre[3] = { 0, 0, 0 };

void abrir(int c) {
  servo[c].write(ANG_ABIERTO);
  abierto[c] = true;
  tCierre[c] = millis() + TIEMPO_ABIERTO;
  Serial.printf("Casillero %d ABIERTO (GPIO %d)\n", c + 1, PIN_SERVO[c]);
}

void setup() {
  Serial.begin(115200);
  pinMode(PIN_LED_FLASH, OUTPUT);
  for (int c = 0; c < 3; c++) {
    servo[c].setPeriodHertz(50);
    servo[c].attach(PIN_SERVO[c], 500, 2500);   // 0.5 .. 2.5 ms, igual que el firmware real
    servo[c].write(ANG_CERRADO);
  }
  Serial.println("Escribe 1, 2 o 3 para abrir un casillero. L = LED flash.");
}

void loop() {
  if (Serial.available()) {
    char ch = Serial.read();
    if (ch >= '1' && ch <= '3') abrir(ch - '1');
    if (ch == 'L' || ch == 'l') {
      digitalWrite(PIN_LED_FLASH, !digitalRead(PIN_LED_FLASH));
      Serial.println(digitalRead(PIN_LED_FLASH) ? "LED flash ON" : "LED flash OFF");
    }
  }
  for (int c = 0; c < 3; c++) {
    if (abierto[c] && millis() >= tCierre[c]) {
      servo[c].write(ANG_CERRADO);
      abierto[c] = false;
      Serial.printf("Casillero %d cerrado\n", c + 1);
    }
  }
  delay(10);
}
