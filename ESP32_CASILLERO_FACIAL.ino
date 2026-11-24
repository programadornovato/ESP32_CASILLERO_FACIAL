/*
  =====================================================================
   CASILLEROS CON RECONOCIMIENTO FACIAL — ESP32-CAM  (Programador Novato)
  =====================================================================
   Funcionamiento:
     · Casillero LIBRE  -> lo tocas en la web, la ESP32 toma 5 fotos de tu
       cara, guarda tu "huella facial" en ese casillero, lo abre 5 s y lo
       cierra. Ahora el casillero es tuyo (OCUPADO).
     · Casillero OCUPADO -> lo tocas, la ESP32 compara tu cara SOLO con la
       del dueño de ese casillero. Si coincide 2 veces seguidas, abre 5 s.
       Si no coincide, no abre.
     · Después de abrir tu casillero tienes 30 s para pulsar "Liberar"
       y dejarlo libre para otra persona.

   Cómo reconoce: esp-face (core 1.0.6) detecta la cara (MTMN), la alinea
   y obtiene una huella de 512 números (get_face_id). Dos huellas se
   comparan con la similitud coseno; si supera el umbral (0.55 por defecto,
   el mismo valor FACE_REC_THRESHOLD de esp-face) es la misma persona.
   Las huellas se guardan en la partición "casilleros" de la flash.

   ⚠ No es un sistema de seguridad real: una foto puede engañarlo.

   REQUISITOS:
     - Core "esp32 by Espressif Systems" versión 1.0.6
     - Placa: "AI Thinker ESP32-CAM"
     - El archivo partitions.csv de esta carpeta (se usa automáticamente)

   CONEXIONES (servos con fuente de 5 V APARTE, GND común con la ESP32):
     Casillero 1 -> GPIO 14 | Casillero 2 -> GPIO 15 | Casillero 3 -> GPIO 13
     LED de encendido -> GPIO 2 -> resistencia 220-330 Ω -> LED (+) ; LED (-) -> GND
     (el LED flash integrado del GPIO 4 queda siempre apagado)

   Wi-Fi: SSID ESP32CAM-CASILLERO / clave 12345678 / http://192.168.4.1
   En tu red: http://casillero.local
   PIN de administración por defecto: 1234
  =====================================================================
*/

#include <Arduino.h>
#if defined(ESP_ARDUINO_VERSION_MAJOR) && ESP_ARDUINO_VERSION_MAJOR >= 2
#error "Este proyecto necesita el core esp32 1.0.6 (Herramientas > Placa > Gestor de tarjetas)"
#endif

#include <WiFi.h>
#include <WebServer.h>
#include <Preferences.h>
#include <ESPmDNS.h>
#include "esp_camera.h"
#include "esp_partition.h"
#include "esp_system.h"       // esp_reset_reason()
#include "esp_heap_caps.h"    // heap_caps_check_integrity_all()
#include "img_converters.h"
#include "fd_forward.h"     // face_detect()  (MTMN)
#include "fr_forward.h"     // align_face(), get_face_id()  (FRMN)
#include "pagina_app.h"
#include "pagina_wifi.h"

#ifndef BOARD_HAS_PSRAM
#error "Selecciona la placa 'AI Thinker ESP32-CAM'"
#endif

// =====================================================================
// 1) CONFIGURACIÓN
// =====================================================================
const char* AP_SSID = "ESP32CAM-CASILLERO";
const char* AP_PASS = "12345678";
const char* NOMBRE_MDNS = "casillero";   // -> http://casillero.local

#define IMG_W 320
#define IMG_H 240
#define JPG_MAX (48 * 1024)

#define NUM_CAS              3
#define MUESTRAS_REGISTRO    5        // fotos para registrar una cara
#define MS_ENTRE_MUESTRAS    400
#define CONFIRMACIONES       2        // coincidencias seguidas para abrir
#define TIMEOUT_REGISTRO_MS  20000
#define TIMEOUT_VERIFICAR_MS 10000
#define LIBERAR_MS           30000    // tiempo para pulsar "Liberar" tras abrir
#define MOSTRAR_RESULT_MS    6000

const uint8_t PIN_SERVO[NUM_CAS]   = { 14, 15, 13 };
const uint8_t CANAL_SERVO[NUM_CAS] = { 2, 3, 4 };   // canal LEDC 0 = cámara
#define PIN_LED_FLASH 4       // flash de la cámara: SIEMPRE apagado
#define PIN_LED_ENCENDIDO 2   // LED externo: prendido = ESP32-CAM encendida

// Ajustes guardados (Preferences "cfg")
int    cfgTiempo = 5;
int    cfgAngCerrado = 0;
int    cfgAngAbierto = 90;
bool   cfgLuz = false;
bool   cfgVflip = false;
bool   cfgEspejo = false;
float  cfgUmbral = FACE_REC_THRESHOLD;   // 0.55
String cfgPin = "1234";

// =====================================================================
// 2) PINES CÁMARA AI THINKER
// =====================================================================
#define PWDN_GPIO_NUM     32
#define RESET_GPIO_NUM    -1
#define XCLK_GPIO_NUM      0
#define SIOD_GPIO_NUM     26
#define SIOC_GPIO_NUM     27
#define Y9_GPIO_NUM       35
#define Y8_GPIO_NUM       34
#define Y7_GPIO_NUM       39
#define Y6_GPIO_NUM       36
#define Y5_GPIO_NUM       21
#define Y4_GPIO_NUM       19
#define Y3_GPIO_NUM       18
#define Y2_GPIO_NUM        5
#define VSYNC_GPIO_NUM    25
#define HREF_GPIO_NUM     23
#define PCLK_GPIO_NUM     22

// =====================================================================
// 3) CASILLEROS: huellas en RAM y en flash
// =====================================================================
#define MAGIA 0x43415331UL   // "CAS1"
struct RegistroFlash { uint32_t magia; uint32_t version; float id[FACE_ID_SIZE]; };

bool  ocupado[NUM_CAS];
float huella[NUM_CAS][FACE_ID_SIZE];
const esp_partition_t* partCas = nullptr;

void cargarCasilleros() {
  partCas = esp_partition_find_first(ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_ANY, "casilleros");
  static RegistroFlash r;
  for (int c = 0; c < NUM_CAS; c++) {
    ocupado[c] = false;
    if (partCas && esp_partition_read(partCas, c * 4096, &r, sizeof(r)) == ESP_OK && r.magia == MAGIA) {
      memcpy(huella[c], r.id, sizeof(r.id));
      ocupado[c] = true;
    }
  }
}

bool guardarCasillero(int c) {
  if (!partCas) return false;
  static RegistroFlash r;
  r.magia = MAGIA; r.version = 1;
  memcpy(r.id, huella[c], sizeof(r.id));
  if (esp_partition_erase_range(partCas, c * 4096, 4096) != ESP_OK) return false;
  return esp_partition_write(partCas, c * 4096, &r, sizeof(r)) == ESP_OK;
}

bool borrarCasillero(int c) {
  ocupado[c] = false;
  if (!partCas) return false;
  return esp_partition_erase_range(partCas, c * 4096, 4096) == ESP_OK;
}

// Similitud coseno: 1 = idénticas, 0 = nada que ver
float similitud(const float* a, const float* b) {
  double dot = 0, na = 0, nb = 0;
  for (int i = 0; i < FACE_ID_SIZE; i++) { dot += a[i] * b[i]; na += a[i] * a[i]; nb += b[i] * b[i]; }
  if (na <= 0 || nb <= 0) return 0;
  return (float)(dot / sqrt(na * nb));
}

// =====================================================================
// 4) ESTADO COMPARTIDO
// =====================================================================
enum TipoOp { OP_NINGUNA = 0, OP_REGISTRO, OP_VERIFICAR };
const char* NOMBRE_OP[] = { "ninguna", "registro", "verificar" };

struct Vision {
  uint32_t frame = 0;
  bool     camOk = false;
  bool     camActiva = false;         // false = cámara en reposo (no captura ni analiza)
  bool     cara = false;
  bool     alineada = false;
  int      x1 = 0, y1 = 0, x2 = 0, y2 = 0;
  TipoOp   op = OP_NINGUNA;
  int      opCas = -1;
  int      progreso = 0;
  int      segundosRestantes = 0;
  float    simActual = -1;            // similitud del último intento (verificar)
  float    simMax = -1;
  char     resultado[96] = "";
  int      resultadoTipo = 0;         // 1 = bien, 2 = mal
  uint32_t resultadoT = 0;
  uint32_t msCaptura = 0, msDeteccion = 0, msReconocer = 0;
  float    fps = 0;
  uint32_t erroresCam = 0;
};
Vision vision;
SemaphoreHandle_t mtx;

// Web -> visión
volatile TipoOp pedidoOp = OP_NINGUNA;
volatile int    pedidoOpCas = -1;
volatile bool   pedidoCancelar = false;
volatile int    pedidoLiberar = -1;      // casillero a liberar (dueño, tras abrir)
volatile int    pedidoForzarLibre = -1;  // casillero a liberar (administrador)
uint32_t        liberableHasta[NUM_CAS] = { 0, 0, 0 };

// Visión -> loop (solo el loop mueve servos)
volatile bool pedidoAbrir[NUM_CAS] = { false, false, false };

uint8_t* jpgTrabajo = nullptr;    size_t jpgTrabajoLen = 0;
uint8_t* jpgCompartido = nullptr; size_t jpgCompartidoLen = 0;
uint8_t* jpgEnvio = nullptr;

mtmn_config_t mtmn_config = { 0 };
WebServer server(80);
Preferences prefs;

// Registro de eventos
#define MAX_EVENTOS 12
struct Evento { uint32_t t; char txt[64]; };
Evento eventos[MAX_EVENTOS];
int    nEventos = 0, posEvento = 0;

void agregarEvento(const char* fmt, ...) {
  char buf[64];
  va_list ap; va_start(ap, fmt); vsnprintf(buf, sizeof(buf), fmt, ap); va_end(ap);
  xSemaphoreTake(mtx, portMAX_DELAY);
  eventos[posEvento].t = millis() / 1000;
  strncpy(eventos[posEvento].txt, buf, sizeof(eventos[posEvento].txt));
  eventos[posEvento].txt[sizeof(eventos[posEvento].txt) - 1] = 0;
  posEvento = (posEvento + 1) % MAX_EVENTOS;
  if (nEventos < MAX_EVENTOS) nEventos++;
  xSemaphoreGive(mtx);
  Serial.printf("[EVENTO] %s\n", buf);
}

// Diagnóstico de estabilidad
const char* motivoReinicio = "";
volatile uint32_t descSta = 0, razonSta = 0, descAp = 0;

const char* textoMotivo(esp_reset_reason_t r) {
  switch (r) {
    case ESP_RST_POWERON:  return "Encendido normal";
    case ESP_RST_SW:       return "Reinicio por programa";
    case ESP_RST_PANIC:    return "ERROR del programa (panic)";
    case ESP_RST_INT_WDT:  return "Watchdog de interrupciones";
    case ESP_RST_TASK_WDT: return "Watchdog de tareas (bloqueo)";
    case ESP_RST_WDT:      return "Watchdog";
    case ESP_RST_BROWNOUT: return "FALLA DE ALIMENTACION (brownout)";
    case ESP_RST_DEEPSLEEP:return "Salida de deep sleep";
    case ESP_RST_EXT:      return "Boton RESET";
    default:               return "Desconocido";
  }
}

// =====================================================================
// 5) AJUSTES (memoria no volátil)
// =====================================================================
void cargarAjustes() {
  prefs.begin("cfg", true);
  cfgTiempo     = prefs.getInt("tiempo", 5);
  cfgAngCerrado = prefs.getInt("angC", 0);
  cfgAngAbierto = prefs.getInt("angA", 90);
  cfgLuz        = prefs.getBool("luz", false);
  cfgVflip      = prefs.getBool("vflip", false);
  cfgEspejo     = prefs.getBool("espejo", false);
  cfgUmbral     = prefs.getFloat("umbral", FACE_REC_THRESHOLD);
  cfgPin        = prefs.getString("pin", "1234");
  prefs.end();
}
void guardarAjustes() {
  prefs.begin("cfg", false);
  prefs.putInt("tiempo", cfgTiempo);
  prefs.putInt("angC", cfgAngCerrado);
  prefs.putInt("angA", cfgAngAbierto);
  prefs.putBool("luz", cfgLuz);
  prefs.putBool("vflip", cfgVflip);
  prefs.putBool("espejo", cfgEspejo);
  prefs.putFloat("umbral", cfgUmbral);
  prefs.putString("pin", cfgPin);
  prefs.end();
}

// =====================================================================
// 6) CÁMARA Y DETECTOR
// =====================================================================
void aplicarOrientacion() {
  sensor_t* s = esp_camera_sensor_get();
  if (!s) return;
  s->set_vflip(s, cfgVflip ? 1 : 0);
  s->set_hmirror(s, cfgEspejo ? 1 : 0);
}

bool iniciarCamara() {
  camera_config_t c;
  c.ledc_channel = LEDC_CHANNEL_0;
  c.ledc_timer   = LEDC_TIMER_0;
  c.pin_d0 = Y2_GPIO_NUM;  c.pin_d1 = Y3_GPIO_NUM;
  c.pin_d2 = Y4_GPIO_NUM;  c.pin_d3 = Y5_GPIO_NUM;
  c.pin_d4 = Y6_GPIO_NUM;  c.pin_d5 = Y7_GPIO_NUM;
  c.pin_d6 = Y8_GPIO_NUM;  c.pin_d7 = Y9_GPIO_NUM;
  c.pin_xclk = XCLK_GPIO_NUM;
  c.pin_pclk = PCLK_GPIO_NUM;
  c.pin_vsync = VSYNC_GPIO_NUM;
  c.pin_href = HREF_GPIO_NUM;
  c.pin_sscb_sda = SIOD_GPIO_NUM;     // en el core 1.0.6 se llama "sscb"
  c.pin_sscb_scl = SIOC_GPIO_NUM;
  c.pin_pwdn = PWDN_GPIO_NUM;
  c.pin_reset = RESET_GPIO_NUM;
  c.xclk_freq_hz = 20000000;
  c.pixel_format = PIXFORMAT_JPEG;
  c.frame_size = FRAMESIZE_QVGA;
  c.jpeg_quality = 12;
  c.fb_count = 1;                     // 1 buffer: solo captura cuando se pide una foto
                                      // (con 2 capturaría sin parar en segundo plano)
  esp_err_t err = esp_camera_init(&c);
  if (err != ESP_OK) { Serial.printf("[CAM] Error 0x%x\n", err); return false; }
  aplicarOrientacion();
  return true;
}

// Valores del ejemplo oficial CameraWebServer 1.0.6
void configurarDetector() {
  mtmn_config.type = FAST;
  mtmn_config.min_face = 80;
  mtmn_config.pyramid = 0.707;
  mtmn_config.pyramid_times = 4;
  mtmn_config.p_threshold.score = 0.6;
  mtmn_config.p_threshold.nms = 0.7;
  mtmn_config.p_threshold.candidate_number = 20;
  mtmn_config.r_threshold.score = 0.7;
  mtmn_config.r_threshold.nms = 0.7;
  mtmn_config.r_threshold.candidate_number = 10;
  mtmn_config.o_threshold.score = 0.7;
  mtmn_config.o_threshold.nms = 0.7;
  mtmn_config.o_threshold.candidate_number = 1;
}

// =====================================================================
// 7) SERVOS
// =====================================================================
struct EstadoServo { bool abierto = false; uint32_t tCierre = 0; };
EstadoServo servo[NUM_CAS];

void servoAngulo(int c, int ang) {
  ang = constrain(ang, 0, 180);
  uint32_t us = 500 + (uint32_t)ang * 2000 / 180;    // 0.5 .. 2.5 ms
  ledcWrite(CANAL_SERVO[c], us * 65535UL / 20000UL);  // 50 Hz, 16 bits
}
void iniciarLeds() {
  pinMode(PIN_LED_FLASH, OUTPUT);
  digitalWrite(PIN_LED_FLASH, LOW);        // flash de la cámara apagado
  pinMode(PIN_LED_ENCENDIDO, OUTPUT);
  digitalWrite(PIN_LED_ENCENDIDO, HIGH);   // indicador de encendido
}
void iniciarServos() {
  for (int c = 0; c < NUM_CAS; c++) {
    ledcSetup(CANAL_SERVO[c], 50, 16);
    ledcAttachPin(PIN_SERVO[c], CANAL_SERVO[c]);
    servoAngulo(c, cfgAngCerrado);
  }
}
void atenderServos() {
  for (int c = 0; c < NUM_CAS; c++) {
    if (pedidoAbrir[c]) {
      pedidoAbrir[c] = false;
      servoAngulo(c, cfgAngAbierto);
      servo[c].abierto = true;
      servo[c].tCierre = millis() + (uint32_t)cfgTiempo * 1000;
    }
    if (servo[c].abierto && (int32_t)(millis() - servo[c].tCierre) >= 0) {
      servoAngulo(c, cfgAngCerrado);
      servo[c].abierto = false;
      agregarEvento("Casillero %d cerrado", c + 1);
    }
  }
}

// =====================================================================
// 8) TAREA DE VISIÓN (núcleo 1). Única que usa cámara y huellas.
// =====================================================================
// ---- Depuración de memoria: revisa el heap tras cada paso del reconocimiento ----
// Si algo daña la memoria, el Monitor Serie dice en qué paso pasó.
// Pon 0 cuando todo funcione bien (ahorra unos milisegundos por foto).
#define DEPURAR_HEAP 0
#define TAM_PILA_VISION 20480
TaskHandle_t hVision = nullptr;
volatile bool heapDanado = false;
char pasoDanado[40] = "";

void revisarHeap(const char* paso) {
#if DEPURAR_HEAP
  if (heapDanado) return;
  if (!heap_caps_check_integrity_all(true)) {
    heapDanado = true;
    strncpy(pasoDanado, paso, sizeof(pasoDanado) - 1);
    Serial.printf("\n[HEAP] *** MEMORIA DANADA despues de: %s ***\n", paso);
    Serial.printf("[HEAP] Pila libre de la tarea vision: %u bytes\n", (unsigned)uxTaskGetStackHighWaterMark(NULL));
  }
#endif
}

void ponerResultado(const char* txt, int tipo) {
  xSemaphoreTake(mtx, portMAX_DELAY);
  strncpy(vision.resultado, txt, sizeof(vision.resultado) - 1);
  vision.resultado[sizeof(vision.resultado) - 1] = 0;
  vision.resultadoTipo = tipo;
  vision.resultadoT = millis();
  xSemaphoreGive(mtx);
}

void tareaVision(void*) {
  dl_matrix3du_t* imagen   = dl_matrix3du_alloc(1, IMG_W, IMG_H, 3);
  dl_matrix3du_t* alineada = dl_matrix3du_alloc(1, FACE_WIDTH, FACE_HEIGHT, 3);
  static float suma[FACE_ID_SIZE];
  if (!imagen || !alineada) { ponerResultado("Sin memoria para el reconocimiento", 2); vTaskDelete(NULL); }

  TipoOp op = OP_NINGUNA;
  int opCas = -1, progreso = 0, aciertos = 0;
  uint32_t opInicio = 0, ultMuestra = 0;
  float simActual = -1, simMax = -1;
  uint32_t ultimoCiclo = millis();
  float fps = 0;
  char txt[96];

  auto terminar = [&](const char* msg, int tipo) {
    op = OP_NINGUNA; opCas = -1; progreso = 0; aciertos = 0;
    ponerResultado(msg, tipo);
  };

  for (;;) {
    // --- Peticiones de la web ---
    if (pedidoCancelar) { pedidoCancelar = false; if (op != OP_NINGUNA) { terminar("Operación cancelada", 2); } }
    if (pedidoLiberar >= 0) {
      int c = pedidoLiberar; pedidoLiberar = -1;
      borrarCasillero(c);
      liberableHasta[c] = 0;
      agregarEvento("Casillero %d liberado por su dueño", c + 1);
      snprintf(txt, sizeof(txt), "Casillero %d liberado", c + 1); ponerResultado(txt, 1);
    }
    if (pedidoForzarLibre >= 0) {
      int c = pedidoForzarLibre; pedidoForzarLibre = -1;
      if (op != OP_NINGUNA && opCas == c) terminar("Operación cancelada", 2);
      borrarCasillero(c);
      liberableHasta[c] = 0;
      agregarEvento("Casillero %d liberado por el administrador", c + 1);
    }
    if (pedidoOp != OP_NINGUNA) {
      op = pedidoOp; opCas = pedidoOpCas; pedidoOp = OP_NINGUNA;
      progreso = 0; aciertos = 0; opInicio = millis(); ultMuestra = 0;
      simActual = -1; simMax = -1;
      memset(suma, 0, sizeof(suma));
      xSemaphoreTake(mtx, portMAX_DELAY); vision.resultado[0] = 0; vision.resultadoTipo = 0; xSemaphoreGive(mtx);
      agregarEvento(op == OP_REGISTRO ? "Registrando cara para casillero %d" : "Verificando cara del casillero %d", opCas + 1);
    }

    // --- Cámara en reposo: sin operación no se captura, no se detecta, no se envía nada ---
    if (op == OP_NINGUNA) {
      xSemaphoreTake(mtx, portMAX_DELAY);
      vision.camActiva = false;
      vision.cara = false; vision.alineada = false;
      vision.op = OP_NINGUNA; vision.opCas = -1;
      vision.progreso = 0; vision.segundosRestantes = 0; vision.simActual = -1;
      vision.fps = 0;
      jpgCompartidoLen = 0;            // no servir una imagen vieja
      xSemaphoreGive(mtx);
      fps = 0;
      ultimoCiclo = millis();
      vTaskDelay(pdMS_TO_TICKS(50));
      continue;
    }

    // --- Captura ---
    revisarHeap("inicio del ciclo");
    uint32_t t0 = millis();
    camera_fb_t* fb = esp_camera_fb_get();
    if (!fb) {
      xSemaphoreTake(mtx, portMAX_DELAY); vision.erroresCam++; vision.camOk = false; xSemaphoreGive(mtx);
      vTaskDelay(pdMS_TO_TICKS(50));
      continue;
    }
    if (fb->len <= JPG_MAX) { memcpy(jpgTrabajo, fb->buf, fb->len); jpgTrabajoLen = fb->len; }
    else jpgTrabajoLen = 0;
    bool ok = fb->width == IMG_W && fb->height == IMG_H && fmt2rgb888(fb->buf, fb->len, fb->format, imagen->item);
    esp_camera_fb_return(fb);
    revisarHeap("captura + fmt2rgb888");
    uint32_t t1 = millis();
    if (!ok) {
      xSemaphoreTake(mtx, portMAX_DELAY); vision.erroresCam++; xSemaphoreGive(mtx);
      vTaskDelay(pdMS_TO_TICKS(20));
      continue;
    }

    // --- Detección ---
    box_array_t* caras = face_detect(imagen, &mtmn_config);
    revisarHeap("face_detect");
    uint32_t t2 = millis();
    bool hayCara = false, alineadaOk = false;
    int bx1 = 0, by1 = 0, bx2 = 0, by2 = 0;
    uint32_t ahora = millis();

    if (caras) {
      hayCara = true;
      bx1 = constrain((int)caras->box[0].box_p[0], 0, IMG_W - 1);
      by1 = constrain((int)caras->box[0].box_p[1], 0, IMG_H - 1);
      bx2 = constrain((int)caras->box[0].box_p[2], 0, IMG_W - 1);
      by2 = constrain((int)caras->box[0].box_p[3], 0, IMG_H - 1);

      // Solo se calcula la huella si hay una operación en curso (ahorra tiempo)
      bool alineo = (op != OP_NINGUNA) && align_face(caras, imagen, alineada) == ESP_OK;
      if (op != OP_NINGUNA) revisarHeap("align_face");
      if (alineo) {
        alineadaOk = true;
        bool tomar = (op == OP_VERIFICAR) || (ahora - ultMuestra >= MS_ENTRE_MUESTRAS);
        dl_matrix3d_t* id = tomar ? get_face_id(alineada) : nullptr;
        if (tomar) revisarHeap("get_face_id");
        if (id) {
          const float* v = id->item;
          if (op == OP_REGISTRO) {
            ultMuestra = ahora;
            // ¿Esta cara ya es dueña de otro casillero?
            int yaTiene = -1;
            if (progreso == 0) {
              for (int k = 0; k < NUM_CAS; k++) if (ocupado[k] && similitud(v, huella[k]) >= cfgUmbral) { yaTiene = k; break; }
            }
            if (yaTiene >= 0) {
              snprintf(txt, sizeof(txt), "Tu cara ya tiene el casillero %d", yaTiene + 1);
              agregarEvento("Registro rechazado: ya tiene el casillero %d", yaTiene + 1);
              terminar(txt, 2);
            } else {
              for (int i = 0; i < FACE_ID_SIZE; i++) suma[i] += v[i];
              progreso++;
              if (progreso >= MUESTRAS_REGISTRO) {
                int c = opCas;
                memcpy(huella[c], suma, sizeof(suma));
                ocupado[c] = true;
                if (guardarCasillero(c)) {
                  pedidoAbrir[c] = true;
                  agregarEvento("Casillero %d asignado y abierto", c + 1);
                  snprintf(txt, sizeof(txt), "¡Casillero %d es tuyo! Abriendo…", c + 1);
                  terminar(txt, 1);
                } else {
                  ocupado[c] = false;
                  terminar("Error al guardar en la flash (revisa partitions.csv)", 2);
                }
              }
            }
          } else if (op == OP_VERIFICAR) {
            float s = similitud(v, huella[opCas]);
            simActual = s;
            if (s > simMax) simMax = s;
            if (s >= cfgUmbral) aciertos++; else aciertos = 0;
            if (aciertos >= CONFIRMACIONES) {
              int c = opCas;
              // Dueño reconocido: se abre y el casillero queda LIBRE de inmediato
              pedidoAbrir[c] = true;
              borrarCasillero(c);
              liberableHasta[c] = 0;
              agregarEvento("Casillero %d abierto y liberado por su dueño (%.0f%%)", c + 1, s * 100);
              snprintf(txt, sizeof(txt), "¡Eres tú! Casillero %d abierto y liberado", c + 1);
              terminar(txt, 1);
            }
          }
          dl_matrix3d_free(id);
          revisarHeap("dl_matrix3d_free");
        }
      }
      revisarHeap("antes de liberar las cajas");
      // IMPORTANTE: face_detect reserva estas cajas con dl_lib_calloc(), que devuelve
      // un puntero desplazado. Se DEBEN liberar con dl_lib_free(), nunca con free()
      // (el ejemplo oficial CameraWebServer 1.0.6 usa free() y por eso se reinicia).
      dl_lib_free(caras->score);
      dl_lib_free(caras->box);
      dl_lib_free(caras->landmark);
      dl_lib_free(caras);
    }
    uint32_t t3 = millis();

    // --- Tiempos límite ---
    if (op == OP_REGISTRO && ahora - opInicio > TIMEOUT_REGISTRO_MS) {
      agregarEvento("Registro del casillero %d sin terminar", opCas + 1);
      terminar("No se pudo tomar las 5 fotos: acércate, de frente y con luz", 2);
    } else if (op == OP_VERIFICAR && ahora - opInicio > TIMEOUT_VERIFICAR_MS) {
      int c = opCas;
      if (simMax < 0) {
        snprintf(txt, sizeof(txt), "No se vio ninguna cara. El casillero %d sigue cerrado", c + 1);
      } else {
        snprintf(txt, sizeof(txt), "No eres el dueño (parecido máx. %.0f%%). No se abre", simMax * 100);
      }
      agregarEvento("Acceso NEGADO al casillero %d", c + 1);
      terminar(txt, 2);
    }

    // --- Publicar ---
    uint32_t periodo = ahora - ultimoCiclo;
    ultimoCiclo = ahora;
    if (periodo > 0) { float inst = 1000.0f / periodo; fps = fps == 0 ? inst : fps * 0.8f + inst * 0.2f; }
    uint32_t limite = op == OP_REGISTRO ? TIMEOUT_REGISTRO_MS : TIMEOUT_VERIFICAR_MS;

    xSemaphoreTake(mtx, portMAX_DELAY);
    Vision& vs = vision;
    vs.frame++;
    vs.camOk = true;
    vs.camActiva = (op != OP_NINGUNA);
    vs.cara = hayCara;
    vs.alineada = alineadaOk;
    if (hayCara) { vs.x1 = bx1; vs.y1 = by1; vs.x2 = bx2; vs.y2 = by2; }
    vs.op = op;
    vs.opCas = opCas;
    vs.progreso = progreso;
    vs.segundosRestantes = op != OP_NINGUNA ? max(0, (int)((int32_t)(opInicio + limite - ahora) / 1000) + 1) : 0;
    vs.simActual = op == OP_VERIFICAR ? simActual : -1;
    vs.simMax = simMax;
    vs.msCaptura = t1 - t0; vs.msDeteccion = t2 - t1; vs.msReconocer = t3 - t2;
    vs.fps = fps;
    uint8_t* tmp = jpgCompartido; jpgCompartido = jpgTrabajo; jpgTrabajo = tmp;
    jpgCompartidoLen = jpgTrabajoLen;
    xSemaphoreGive(mtx);

    vTaskDelay(1);
  }
}

// =====================================================================
// 9) RED WI-FI: punto de acceso propio + conexión opcional a la red de casa
// =====================================================================
enum ModoRed { MODO_SIN_ELEGIR = 0, MODO_AP = 1, MODO_LOCAL = 2 };
enum EstadoSta { STA_INACTIVO = 0, STA_CONECTANDO, STA_CONECTADO, STA_ERROR };
const char* NOMBRE_MODO[] = { "ninguno", "ap", "local" };
const char* NOMBRE_STA[]  = { "inactivo", "conectando", "conectado", "error" };
#define STA_TIMEOUT_MS 15000

ModoRed   modoRed = MODO_SIN_ELEGIR;
EstadoSta estadoSta = STA_INACTIVO;
String    staSsid, staPass, pendSsid, pendPass, staError;
bool      pendGuardar = false;
uint32_t  tInicioSta = 0;

void cargarRed() {
  prefs.begin("red", true);
  modoRed = (ModoRed)prefs.getUChar("modo", MODO_SIN_ELEGIR);
  staSsid = prefs.getString("ssid", "");
  staPass = prefs.getString("pass", "");
  prefs.end();
  if (modoRed > MODO_LOCAL) modoRed = MODO_SIN_ELEGIR;
}
void guardarRed() {
  prefs.begin("red", false);
  prefs.putUChar("modo", modoRed);
  prefs.putString("ssid", staSsid);
  prefs.putString("pass", staPass);
  prefs.end();
}
void iniciarConexionLocal(const String& ssid, const String& pass, bool guardar) {
  WiFi.disconnect(false, false);
  pendSsid = ssid; pendPass = pass; pendGuardar = guardar;
  staError = "";
  estadoSta = STA_CONECTANDO;
  tInicioSta = millis();
  WiFi.begin(ssid.c_str(), pass.length() ? pass.c_str() : nullptr);
  Serial.printf("[RED] Conectando a \"%s\"...\n", ssid.c_str());
}
void atenderRed() {
  if (estadoSta != STA_CONECTANDO) return;
  wl_status_t st = WiFi.status();
  if (st == WL_CONNECTED) {
    estadoSta = STA_CONECTADO;
    if (pendGuardar) { staSsid = pendSsid; staPass = pendPass; modoRed = MODO_LOCAL; guardarRed(); }
    Serial.println("-----------------------------------------");
    Serial.printf(" Conectado a  : %s\n", pendSsid.c_str());
    Serial.printf(" Abre         : http://%s\n", WiFi.localIP().toString().c_str());
    Serial.printf(" o tambien    : http://%s.local\n", NOMBRE_MDNS);
    Serial.printf(" (respaldo)   : http://192.168.4.1 en la red %s\n", AP_SSID);
    Serial.println("-----------------------------------------");
  } else if (millis() - tInicioSta > STA_TIMEOUT_MS) {
    if (st == WL_NO_SSID_AVAIL)       staError = "No se encontró la red (¿está lejos o es de 5 GHz?)";
    else if (st == WL_CONNECT_FAILED) staError = "No se pudo conectar (¿contraseña incorrecta?)";
    else                              staError = "Tiempo agotado: revisa la contraseña y la señal";
    estadoSta = STA_ERROR;
    WiFi.disconnect(false, false);
    Serial.printf("[RED] Error con \"%s\": %s\n", pendSsid.c_str(), staError.c_str());
  }
}

// =====================================================================
// 10) SERVIDOR WEB + API JSON
// =====================================================================
String jsonEsc(const String& s) {
  String o; o.reserve(s.length() + 8);
  for (unsigned int i = 0; i < s.length(); i++) {
    unsigned char c = s[i];
    if (c == '"' || c == '\\') { o += '\\'; o += (char)c; }
    else if (c < 0x20) { char b[8]; snprintf(b, sizeof(b), "\\u%04x", c); o += b; }
    else o += (char)c;
  }
  return o;
}
void enviarJson(int code, const String& j) {
  server.sendHeader("Cache-Control", "no-store");
  server.send(code, "application/json", j);
}
bool pinOk() {
  if (server.arg("pin") == cfgPin) return true;
  enviarJson(403, "{\"ok\":false,\"error\":\"PIN incorrecto\"}");
  return false;
}
int argCasillero() {
  int c = server.arg("cas").toInt() - 1;
  if (c < 0 || c >= NUM_CAS) { enviarJson(400, "{\"ok\":false,\"error\":\"Casillero no válido\"}"); return -1; }
  return c;
}
bool hayOperacion() {
  xSemaphoreTake(mtx, portMAX_DELAY);
  bool ocupada = vision.op != OP_NINGUNA || pedidoOp != OP_NINGUNA;
  xSemaphoreGive(mtx);
  return ocupada;
}

void handleRaiz() {
  bool directo = (modoRed == MODO_AP) || WiFi.isConnected();
  server.sendHeader("Cache-Control", "no-store");
  server.send_P(200, "text/html; charset=utf-8", directo ? PAGINA_APP : PAGINA_WIFI);
}
void handleApp()  { server.sendHeader("Cache-Control", "no-store"); server.send_P(200, "text/html; charset=utf-8", PAGINA_APP); }
void handleWifi() { server.sendHeader("Cache-Control", "no-store"); server.send_P(200, "text/html; charset=utf-8", PAGINA_WIFI); }

// GET /estado
void handleEstado() {
  xSemaphoreTake(mtx, portMAX_DELAY);
  Vision v = vision;
  String ev = "[";
  for (int k = 0; k < nEventos; k++) {
    int i = (posEvento - 1 - k + MAX_EVENTOS) % MAX_EVENTOS;
    if (k) ev += ',';
    ev += "{\"t\":" + String(eventos[i].t) + ",\"txt\":\"" + jsonEsc(eventos[i].txt) + "\"}";
  }
  ev += "]";
  xSemaphoreGive(mtx);

  uint32_t ahora = millis();
  String cas = "[";
  for (int c = 0; c < NUM_CAS; c++) {
    if (c) cas += ',';
    int rest = servo[c].abierto ? max(0, (int)((int32_t)(servo[c].tCierre - ahora) / 1000) + 1) : 0;
    int lib = (ocupado[c] && (int32_t)(liberableHasta[c] - ahora) > 0) ? (int)((liberableHasta[c] - ahora) / 1000) + 1 : 0;
    cas += "{\"ocupado\":" + String(ocupado[c] ? "true" : "false") +
           ",\"abierto\":" + String(servo[c].abierto ? "true" : "false") +
           ",\"restante\":" + String(rest) + ",\"liberable\":" + String(lib) +
           ",\"gpio\":" + String(PIN_SERVO[c]) + "}";
  }
  cas += "]";
  bool resVigente = v.resultado[0] && (ahora - v.resultadoT < MOSTRAR_RESULT_MS);
  String red = WiFi.isConnected() ? (WiFi.localIP().toString() + " · " + NOMBRE_MDNS + ".local") : String("AP 192.168.4.1");

  String j = "{\"camara\":" + String(v.camOk ? "true" : "false") +
    ",\"camActiva\":" + String(v.camActiva ? "true" : "false") +
    ",\"frame\":" + String(v.frame) +
    ",\"cara\":" + String(v.cara ? "true" : "false") + ",\"alineada\":" + String(v.alineada ? "true" : "false") +
    ",\"caja\":[" + String(v.x1) + "," + String(v.y1) + "," + String(v.x2) + "," + String(v.y2) + "]" +
    ",\"op\":\"" + NOMBRE_OP[v.op] + "\",\"opCas\":" + String(v.opCas) +
    ",\"progreso\":" + String(v.progreso) + ",\"muestras\":" + String(MUESTRAS_REGISTRO) +
    ",\"segundos\":" + String(v.segundosRestantes) +
    ",\"sim\":" + String(v.simActual, 3) + ",\"simMax\":" + String(v.simMax, 3) + ",\"umbral\":" + String(cfgUmbral, 2) +
    ",\"resultado\":\"" + (resVigente ? jsonEsc(v.resultado) : String("")) + "\",\"resultadoTipo\":" + String(resVigente ? v.resultadoTipo : 0) +
    ",\"casilleros\":" + cas + ",\"eventos\":" + ev +
    ",\"msCaptura\":" + String(v.msCaptura) + ",\"msDeteccion\":" + String(v.msDeteccion) + ",\"msReconocer\":" + String(v.msReconocer) +
    ",\"fps\":" + String(v.fps, 2) + ",\"erroresCam\":" + String(v.erroresCam) +
    ",\"particion\":" + String(partCas ? "true" : "false") +
    ",\"tiempo\":" + String(cfgTiempo) + ",\"angC\":" + String(cfgAngCerrado) + ",\"angA\":" + String(cfgAngAbierto) +
    ",\"luz\":" + String(cfgLuz ? "true" : "false") + ",\"vflip\":" + String(cfgVflip ? "true" : "false") +
    ",\"espejo\":" + String(cfgEspejo ? "true" : "false") +
    ",\"red\":\"" + red + "\",\"heap\":" + String(ESP.getFreeHeap()) + ",\"psram\":" + String(ESP.getFreePsram()) +
    ",\"uptime\":" + String(millis() / 1000) +
    ",\"reinicio\":\"" + motivoReinicio + "\"" +
    ",\"minHeap\":" + String(ESP.getMinFreeHeap()) + ",\"maxBloque\":" + String(ESP.getMaxAllocHeap()) +
    ",\"pilaLibre\":" + String(hVision ? (unsigned)uxTaskGetStackHighWaterMark(hVision) : 0) +
    ",\"heapDanado\":\"" + String(heapDanado ? pasoDanado : "") + "\"" +
    ",\"descSta\":" + String(descSta) + ",\"razonSta\":" + String(razonSta) + ",\"descAp\":" + String(descAp) + "}";
  enviarJson(200, j);
}

// GET /foto
void handleFoto() {
  size_t len;
  Vision v;
  xSemaphoreTake(mtx, portMAX_DELAY);
  len = jpgCompartidoLen;
  if (len) memcpy(jpgEnvio, jpgCompartido, len);
  v = vision;
  xSemaphoreGive(mtx);
  if (!len) { server.send(503, "text/plain", "Sin imagen"); return; }
  char cab[48];
  if (v.cara) snprintf(cab, sizeof(cab), "%d,%d,%d,%d,%d", v.x1, v.y1, v.x2, v.y2, v.alineada ? 1 : 0);
  else strcpy(cab, "none");
  server.sendHeader("X-Cara", cab);
  server.sendHeader("Cache-Control", "no-store");
  server.setContentLength(len);
  server.send(200, "image/jpeg", "");
  server.client().write(jpgEnvio, len);
}

// POST /casillero (cas) -> libre: registrar | ocupado: verificar
void handleCasillero() {
  int c = argCasillero(); if (c < 0) return;
  if (!partCas) { enviarJson(500, "{\"ok\":false,\"error\":\"Falta la partición 'casilleros' (revisa partitions.csv)\"}"); return; }
  if (hayOperacion()) { enviarJson(409, "{\"ok\":false,\"error\":\"Espera: hay otra persona usando la cámara\"}"); return; }
  if (servo[c].abierto) { enviarJson(409, "{\"ok\":false,\"error\":\"Ese casillero ya está abierto\"}"); return; }
  pedidoOpCas = c;
  pedidoOp = ocupado[c] ? OP_VERIFICAR : OP_REGISTRO;
  enviarJson(200, String("{\"ok\":true,\"op\":\"") + (ocupado[c] ? "verificar" : "registro") + "\"}");
}

// POST /cancelar
void handleCancelar() { pedidoCancelar = true; enviarJson(200, "{\"ok\":true}"); }

// POST /liberar (cas) -> solo justo después de que el dueño abrió
void handleLiberar() {
  int c = argCasillero(); if (c < 0) return;
  if (!ocupado[c] || (int32_t)(liberableHasta[c] - millis()) <= 0) {
    enviarJson(403, "{\"ok\":false,\"error\":\"Primero abre tu casillero con tu cara\"}");
    return;
  }
  pedidoLiberar = c;
  enviarJson(200, "{\"ok\":true}");
}

// ---- Administración (PIN) ----
void handleVerificarPin() { if (pinOk()) enviarJson(200, "{\"ok\":true}"); }
void handleForzarLibre() {
  if (!pinOk()) return;
  int c = argCasillero(); if (c < 0) return;
  pedidoForzarLibre = c;
  enviarJson(200, "{\"ok\":true}");
}
void handleProbar() {
  if (!pinOk()) return;
  int c = argCasillero(); if (c < 0) return;
  pedidoAbrir[c] = true;
  agregarEvento("Prueba manual: casillero %d", c + 1);
  enviarJson(200, "{\"ok\":true}");
}
// POST /servo (pin, cas, accion=abrir|cerrar) -> prueba manual del servo.
// "abrir" lo deja abierto hasta que se pulse "cerrar" (máx. 60 s por seguridad).
void handleServo() {
  if (!pinOk()) return;
  int c = argCasillero(); if (c < 0) return;
  String acc = server.arg("accion");
  if (acc == "abrir") {
    servoAngulo(c, cfgAngAbierto);
    servo[c].abierto = true;
    servo[c].tCierre = millis() + 60000UL;
    agregarEvento("Prueba servo: casillero %d ABIERTO", c + 1);
  } else if (acc == "cerrar") {
    servoAngulo(c, cfgAngCerrado);
    servo[c].abierto = false;
    agregarEvento("Prueba servo: casillero %d CERRADO", c + 1);
  } else {
    enviarJson(400, "{\"ok\":false,\"error\":\"Acción inválida\"}");
    return;
  }
  enviarJson(200, "{\"ok\":true}");
}
void handleAjustes() {
  if (!pinOk()) return;
  int t  = server.hasArg("tiempo") ? server.arg("tiempo").toInt() : cfgTiempo;
  int ac = server.hasArg("angC") ? server.arg("angC").toInt() : cfgAngCerrado;
  int aa = server.hasArg("angA") ? server.arg("angA").toInt() : cfgAngAbierto;
  float um = server.hasArg("umbral") ? server.arg("umbral").toFloat() : cfgUmbral;
  String np = server.arg("nuevoPin");
  if (t < 1 || t > 60) { enviarJson(400, "{\"ok\":false,\"error\":\"Tiempo abierto: de 1 a 60 s\"}"); return; }
  if (ac < 0 || ac > 180 || aa < 0 || aa > 180) { enviarJson(400, "{\"ok\":false,\"error\":\"Ángulos: de 0 a 180\"}"); return; }
  if (um < 0.40f || um > 0.90f) { enviarJson(400, "{\"ok\":false,\"error\":\"Umbral: de 0.40 a 0.90\"}"); return; }
  if (np.length() && (np.length() < 4 || np.length() > 8)) { enviarJson(400, "{\"ok\":false,\"error\":\"El PIN debe tener de 4 a 8 caracteres\"}"); return; }
  cfgTiempo = t; cfgAngCerrado = ac; cfgAngAbierto = aa; cfgUmbral = um;
  if (server.hasArg("luz")) cfgLuz = server.arg("luz") == "1";
  if (server.hasArg("vflip")) cfgVflip = server.arg("vflip") == "1";
  if (server.hasArg("espejo")) cfgEspejo = server.arg("espejo") == "1";
  if (np.length()) cfgPin = np;
  guardarAjustes();
  aplicarOrientacion();
  for (int c = 0; c < NUM_CAS; c++) if (!servo[c].abierto) servoAngulo(c, cfgAngCerrado);
  enviarJson(200, "{\"ok\":true}");
}

// ---- Wi-Fi ----
void handleRedes() {
  int n = WiFi.scanComplete();
  if (n == WIFI_SCAN_RUNNING) { enviarJson(200, "{\"escaneando\":true}"); return; }
  if (n == WIFI_SCAN_FAILED || server.hasArg("nuevo")) {
    WiFi.scanDelete();
    WiFi.scanNetworks(true);
    enviarJson(200, "{\"escaneando\":true}");
    return;
  }
  int idx[40]; int m = 0;
  for (int i = 0; i < n && m < 40; i++) {
    if (WiFi.SSID(i).length() == 0) continue;
    bool dup = false;
    for (int k = 0; k < m; k++) if (WiFi.SSID(idx[k]) == WiFi.SSID(i)) { dup = true; if (WiFi.RSSI(i) > WiFi.RSSI(idx[k])) idx[k] = i; break; }
    if (!dup) idx[m++] = i;
  }
  for (int a = 1; a < m; a++) { int v = idx[a], b = a - 1; while (b >= 0 && WiFi.RSSI(idx[b]) < WiFi.RSSI(v)) { idx[b + 1] = idx[b]; b--; } idx[b + 1] = v; }
  String j = "{\"escaneando\":false,\"redes\":[";
  for (int k = 0; k < m; k++) {
    int i = idx[k];
    if (k) j += ',';
    j += "{\"ssid\":\"" + jsonEsc(WiFi.SSID(i)) + "\",\"rssi\":" + String(WiFi.RSSI(i)) +
         ",\"abierta\":" + String(WiFi.encryptionType(i) == WIFI_AUTH_OPEN ? "true" : "false") + "}";
  }
  j += "]}";
  enviarJson(200, j);
}
void handleConectar() {
  String ssid = server.arg("ssid"), pass = server.arg("pass");
  if (ssid.length() < 1 || ssid.length() > 32) { enviarJson(400, "{\"ok\":false,\"error\":\"Nombre de red no válido\"}"); return; }
  if (pass.length() > 0 && (pass.length() < 8 || pass.length() > 63)) { enviarJson(400, "{\"ok\":false,\"error\":\"La contraseña debe tener entre 8 y 63 caracteres\"}"); return; }
  iniciarConexionLocal(ssid, pass, true);
  enviarJson(200, "{\"ok\":true}");
}
void handleEstadoRed() {
  String ssid = (estadoSta == STA_INACTIVO) ? staSsid : pendSsid;
  String j = "{\"modo\":\"" + String(NOMBRE_MODO[modoRed]) + "\",\"sta\":\"" + String(NOMBRE_STA[estadoSta]) +
             "\",\"conectado\":" + String(WiFi.isConnected() ? "true" : "false") +
             ",\"ssid\":\"" + jsonEsc(ssid) + "\",\"guardada\":\"" + jsonEsc(staSsid) +
             "\",\"ip\":\"" + (WiFi.isConnected() ? WiFi.localIP().toString() : String("")) +
             "\",\"error\":\"" + jsonEsc(staError) + "\",\"apSsid\":\"" + AP_SSID +
             "\",\"mdns\":\"" + String(NOMBRE_MDNS) + ".local" +
             "\",\"segundos\":" + String(estadoSta == STA_CONECTANDO ? (millis() - tInicioSta) / 1000 : 0) + "}";
  enviarJson(200, j);
}
void handleModoAP() {
  modoRed = MODO_AP; guardarRed();
  estadoSta = STA_INACTIVO; WiFi.disconnect(false, false);
  enviarJson(200, "{\"ok\":true}");
}
void handleOlvidar() {
  modoRed = MODO_SIN_ELEGIR; staSsid = ""; staPass = ""; guardarRed();
  estadoSta = STA_INACTIVO; WiFi.disconnect(false, false);
  enviarJson(200, "{\"ok\":true}");
}

void iniciarServidor() {
  server.on("/", HTTP_GET, handleRaiz);
  server.on("/app", HTTP_GET, handleApp);
  server.on("/wifi", HTTP_GET, handleWifi);
  server.on("/estado", HTTP_GET, handleEstado);
  server.on("/foto", HTTP_GET, handleFoto);
  server.on("/casillero", HTTP_POST, handleCasillero);
  server.on("/cancelar", HTTP_POST, handleCancelar);
  server.on("/liberar", HTTP_POST, handleLiberar);
  server.on("/verificar", HTTP_POST, handleVerificarPin);
  server.on("/forzarLibre", HTTP_POST, handleForzarLibre);
  server.on("/probar", HTTP_POST, handleProbar);
  server.on("/servo", HTTP_POST, handleServo);
  server.on("/ajustes", HTTP_POST, handleAjustes);
  server.on("/redes", HTTP_GET, handleRedes);
  server.on("/conectar", HTTP_POST, handleConectar);
  server.on("/estadoRed", HTTP_GET, handleEstadoRed);
  server.on("/modoAP", HTTP_POST, handleModoAP);
  server.on("/olvidar", HTTP_POST, handleOlvidar);
  server.onNotFound([]() { server.sendHeader("Location", "/"); server.send(302, "text/plain", ""); });
  server.begin();
}

// =====================================================================
// 11) SETUP / LOOP
// =====================================================================
void detenerConError(const char* msg) { Serial.println(msg); while (true) delay(1000); }

void setup() {
  iniciarLeds();  // flash apagado + indicador de encendido (lo primero)
  Serial.begin(115200);
  delay(300);
  Serial.println();
  Serial.println("=========================================");
  Serial.println("  CASILLEROS CON RECONOCIMIENTO FACIAL");
  Serial.println("=========================================");

  motivoReinicio = textoMotivo(esp_reset_reason());
  Serial.printf("[INICIO] Motivo del ultimo reinicio: %s\n", motivoReinicio);

  if (!psramFound()) detenerConError("[ERROR] No hay PSRAM. Placa: AI Thinker ESP32-CAM.");
  mtx = xSemaphoreCreateMutex();

  cargarAjustes();
  cargarCasilleros();
  if (!partCas) Serial.println("[ERROR] Falta la particion 'casilleros': revisa partitions.csv");
  for (int c = 0; c < NUM_CAS; c++) Serial.printf("  Casillero %d: %s\n", c + 1, ocupado[c] ? "OCUPADO" : "libre");

  iniciarServos();

  jpgTrabajo    = (uint8_t*)ps_malloc(JPG_MAX);
  jpgCompartido = (uint8_t*)ps_malloc(JPG_MAX);
  jpgEnvio      = (uint8_t*)ps_malloc(JPG_MAX);
  if (!jpgTrabajo || !jpgCompartido || !jpgEnvio) detenerConError("[ERROR] Sin memoria PSRAM.");

  if (!iniciarCamara()) detenerConError("[ERROR] Camara no iniciada. Revisa el cable plano y la alimentacion.");
  Serial.println("[CAM] OV2640 lista (320x240)");
  configurarDetector();

  cargarRed();
  WiFi.persistent(false);
  WiFi.mode(WIFI_AP_STA);
  WiFi.softAPConfig(IPAddress(192, 168, 4, 1), IPAddress(192, 168, 4, 1), IPAddress(255, 255, 255, 0));
  if (!WiFi.softAP(AP_SSID, AP_PASS)) detenerConError("[ERROR] No se pudo crear el punto de acceso.");
  WiFi.setSleep(false);
  WiFi.setAutoReconnect(true);
  WiFi.onEvent(WiFiEventFuncCb([](system_event_id_t e, system_event_info_t info) {
    descSta++; razonSta = info.disconnected.reason;
  }), SYSTEM_EVENT_STA_DISCONNECTED);
  WiFi.onEvent(WiFiEventFuncCb([](system_event_id_t e, system_event_info_t info) { descAp++; }),
               SYSTEM_EVENT_AP_STADISCONNECTED);
  if (MDNS.begin(NOMBRE_MDNS)) {
    MDNS.addService("http", "tcp", 80);
    Serial.printf("[mDNS] Nombre: http://%s.local\n", NOMBRE_MDNS);
  }

  Serial.println("-----------------------------------------");
  Serial.printf(" Red Wi-Fi : %s\n", AP_SSID);
  Serial.printf(" Clave     : %s\n", AP_PASS);
  Serial.printf(" Abre      : http://%s\n", WiFi.softAPIP().toString().c_str());
  Serial.println("-----------------------------------------");
  if (modoRed == MODO_LOCAL && staSsid.length()) iniciarConexionLocal(staSsid, staPass, false);
  Serial.printf("Heap libre: %u | PSRAM libre: %u\n", ESP.getFreeHeap(), ESP.getFreePsram());

  iniciarServidor();
  xTaskCreatePinnedToCore(tareaVision, "vision", TAM_PILA_VISION, nullptr, 1, &hVision, 1);
  agregarEvento("Sistema iniciado");
}

void loop() {
  server.handleClient();
  atenderRed();
  atenderServos();

  static uint32_t ultimoLog = 0;
  if (millis() - ultimoLog > 3000) {
    ultimoLog = millis();
    xSemaphoreTake(mtx, portMAX_DELAY);
    Vision v = vision;
    xSemaphoreGive(mtx);
    Serial.printf("[VIS] op=%s cas=%d cara=%d sim=%.2f | %.2f fps | det %u ms, rec %u ms | heap %u (min %u) | pila libre %u | desc red %u / AP %u\n",
                  NOMBRE_OP[v.op], v.opCas + 1, v.cara, v.simActual, v.fps, v.msDeteccion, v.msReconocer,
                  ESP.getFreeHeap(), ESP.getMinFreeHeap(), hVision ? (unsigned)uxTaskGetStackHighWaterMark(hVision) : 0, descSta, descAp);
  }
  delay(2);
}
