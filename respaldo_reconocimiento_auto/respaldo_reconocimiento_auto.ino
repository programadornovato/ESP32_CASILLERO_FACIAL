/*
  =====================================================================
   CASILLERO FACIAL CON ESP32-CAM  —  Programador Novato
  =====================================================================
   - Reconoce la cara de cada persona registrada (todo dentro de la ESP32).
   - Cada persona tiene asignado un compartimento con su propio servo.
   - Si reconoce tu cara 2 veces seguidas, abre TU compartimento unos
     segundos y luego lo vuelve a cerrar.
   - Las caras se guardan en la flash (partición "fr"): no se pierden
     al apagar.

   Detector/reconocedor: esp-face (MTMN + FRMN), el mismo del ejemplo
   oficial CameraWebServer del core 1.0.6.

   ⚠ No es un sistema de seguridad real: una foto de la cara puede
     engañarlo. Es un proyecto educativo.

   REQUISITOS:
     - Core "esp32 by Espressif Systems" versión 1.0.6 (NO 2.x ni 3.x:
       desde 2.x el reconocimiento facial no funciona en el ESP32 clásico)
     - Placa: "AI Thinker ESP32-CAM"
     - El archivo partitions.csv de esta carpeta (se usa automáticamente)

   CONEXIONES (servos con fuente de 5 V APARTE, GND común con la ESP32):
     Compartimento 1 -> GPIO 14
     Compartimento 2 -> GPIO 15
     Compartimento 3 -> GPIO 13
     LED flash integrado -> GPIO 4 (opcional, para poca luz)

   Wi-Fi: SSID ESP32CAM-CASILLERO / clave 12345678 / http://192.168.4.1
   PIN de administración por defecto: 1234 (cámbialo en Ajustes)
  =====================================================================
*/

#include <Arduino.h>
#if defined(ESP_ARDUINO_VERSION_MAJOR) && ESP_ARDUINO_VERSION_MAJOR >= 2
#error "Este proyecto necesita el core esp32 1.0.6 (Herramientas > Placa > Gestor de tarjetas)"
#endif

#include <WiFi.h>
#include <WebServer.h>
#include <Preferences.h>
#include <ESPmDNS.h>       // nombre fácil en la red: http://casillero.local
#include "esp_camera.h"
#include "img_converters.h"
#include "fd_forward.h"     // face_detect()  (MTMN)
#include "fr_forward.h"     // align_face(), recognize_face()  (FRMN)
#include "fr_flash.h"       // guardar/leer caras en la partición "fr"
#include "pagina_app.h"     // HTML/CSS/JS de la aplicación
#include "pagina_wifi.h"    // HTML/CSS/JS de la configuración Wi-Fi

#ifndef BOARD_HAS_PSRAM
#error "Selecciona la placa 'AI Thinker ESP32-CAM'"
#endif

// =====================================================================
// 1) CONFIGURACIÓN
// =====================================================================
const char* AP_SSID = "ESP32CAM-CASILLERO";
const char* AP_PASS = "12345678";
const char* NOMBRE_MDNS = "casillero";   // -> http://casillero.local (cámbialo si quieres)

#define IMG_W 320
#define IMG_H 240
#define JPG_MAX (48 * 1024)

#define MAX_CARAS            7      // personas que se pueden registrar
#define MUESTRAS_REGISTRO    5      // fotos de la cara por registro
#define MS_ENTRE_MUESTRAS    400    // separación entre muestras al registrar
#define CONFIRMACIONES       2      // reconocimientos seguidos para abrir
#define VENTANA_CONFIRM_MS   2500   // ...dentro de este tiempo
#define MOSTRAR_ACCESO_MS    3000   // tiempo que se muestra "¡Hola, X!"

#define NUM_COMP 3
const uint8_t PIN_SERVO[NUM_COMP]   = { 14, 15, 13 };
const uint8_t CANAL_SERVO[NUM_COMP] = { 2, 3, 4 };   // canal LEDC 0 lo usa la cámara
#define PIN_LED_FLASH 4

// Ajustes guardados (namespace "cfg")
int    cfgTiempo = 5;          // segundos que el compartimento queda abierto
int    cfgAngCerrado = 0;
int    cfgAngAbierto = 90;
bool   cfgLuz = false;
bool   cfgVflip = false;     // voltear la imagen verticalmente (sensor al revés)
bool   cfgEspejo = false;    // espejo horizontal
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
// 3) ESTADO COMPARTIDO
// =====================================================================
enum EstadoVision { ESPERANDO = 0, CARA_DETECTADA, RECONOCIENDO, ACCESO, DESCONOCIDO, REGISTRANDO, ERROR_VISION };
const char* NOMBRE_ESTADO[] = { "ESPERANDO", "CARA_DETECTADA", "RECONOCIENDO", "ACCESO", "DESCONOCIDO", "REGISTRANDO", "ERROR" };

struct Vision {
  uint32_t frame = 0;
  bool     camOk = false;
  EstadoVision estado = ESPERANDO;
  bool     cara = false;
  int      x1 = 0, y1 = 0, x2 = 0, y2 = 0;
  int      id = -2;                 // -2 sin dato, -1 desconocido, >=0 persona
  char     nombre[ENROLL_NAME_LEN + 1] = "";
  int      comp = -1;
  int      progreso = 0;            // muestras de registro tomadas
  uint32_t msCaptura = 0, msDeteccion = 0, msReconocer = 0, msTotal = 0;
  float    fps = 0;
  uint32_t erroresCam = 0;
  char     error[64] = "";
};

Vision vision;
SemaphoreHandle_t mtx;

// Personas (índice = posición de la cara en la lista de esp-face)
String   nombrePersona[MAX_CARAS];
int8_t   compPersona[MAX_CARAS];

// Registro de eventos (los últimos 12)
#define MAX_EVENTOS 12
struct Evento { uint32_t t; char txt[56]; };
Evento   eventos[MAX_EVENTOS];
int      nEventos = 0, posEvento = 0;

// Peticiones de la web a la tarea de visión (solo ella toca la lista de caras)
volatile bool pedidoRegistro = false;
volatile bool pedidoBorrarTodo = false;
String   regNombre;
int      regComp = 0;

// Peticiones de la visión al loop (solo el loop mueve servos)
volatile bool pedidoAbrir[NUM_COMP] = { false, false, false };

uint8_t* jpgTrabajo = nullptr;    size_t jpgTrabajoLen = 0;
uint8_t* jpgCompartido = nullptr; size_t jpgCompartidoLen = 0;
uint8_t* jpgEnvio = nullptr;

mtmn_config_t mtmn_config = { 0 };
face_id_list  id_list = { 0 };

WebServer server(80);
Preferences prefs;

void agregarEvento(const char* fmt, ...) {
  char buf[56];
  va_list ap; va_start(ap, fmt); vsnprintf(buf, sizeof(buf), fmt, ap); va_end(ap);
  xSemaphoreTake(mtx, portMAX_DELAY);
  eventos[posEvento].t = millis() / 1000;
  strncpy(eventos[posEvento].txt, buf, sizeof(eventos[posEvento].txt));
  posEvento = (posEvento + 1) % MAX_EVENTOS;
  if (nEventos < MAX_EVENTOS) nEventos++;
  xSemaphoreGive(mtx);
  Serial.printf("[EVENTO] %s\n", buf);
}

// =====================================================================
// 4) MEMORIA NO VOLÁTIL: ajustes y personas
// =====================================================================
void cargarAjustes() {
  prefs.begin("cfg", true);
  cfgTiempo     = prefs.getInt("tiempo", 5);
  cfgAngCerrado = prefs.getInt("angC", 0);
  cfgAngAbierto = prefs.getInt("angA", 90);
  cfgLuz        = prefs.getBool("luz", false);
  cfgVflip      = prefs.getBool("vflip", false);
  cfgEspejo     = prefs.getBool("espejo", false);
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
  prefs.putString("pin", cfgPin);
  prefs.end();
}
void cargarPersonas() {
  prefs.begin("caras", true);
  for (int i = 0; i < MAX_CARAS; i++) {
    char k[6];
    snprintf(k, sizeof(k), "n%d", i); nombrePersona[i] = prefs.getString(k, "");
    snprintf(k, sizeof(k), "c%d", i); compPersona[i] = prefs.getChar(k, -1);
  }
  prefs.end();
}
void guardarPersona(int i) {
  prefs.begin("caras", false);
  char k[6];
  snprintf(k, sizeof(k), "n%d", i); prefs.putString(k, nombrePersona[i]);
  snprintf(k, sizeof(k), "c%d", i); prefs.putChar(k, compPersona[i]);
  prefs.end();
}
void borrarPersonas() {
  prefs.begin("caras", false);
  prefs.clear();
  prefs.end();
  for (int i = 0; i < MAX_CARAS; i++) { nombrePersona[i] = ""; compPersona[i] = -1; }
}

// =====================================================================
// 5) CÁMARA
// =====================================================================
// Orientación a nivel del sensor: el detector también recibe la imagen corregida
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
  c.frame_size = FRAMESIZE_QVGA;      // 320x240: lo que usa el ejemplo oficial con caras
  c.jpeg_quality = 12;
  c.fb_count = 2;                     // con PSRAM los buffers van a la PSRAM

  esp_err_t err = esp_camera_init(&c);
  if (err != ESP_OK) { Serial.printf("[CAM] Error 0x%x\n", err); return false; }
  aplicarOrientacion();
  return true;
}

// Configuración del detector: valores del ejemplo oficial CameraWebServer 1.0.6
void configurarDetector() {
  mtmn_config.type = FAST;
  mtmn_config.min_face = 80;          // cara de al menos 80 px: ponte a ~40-70 cm
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
  mtmn_config.o_threshold.candidate_number = 1;   // una sola cara: la más clara
}

// =====================================================================
// 6) SERVOS (compartimentos)
// =====================================================================
struct Compartimento { bool abierto = false; uint32_t tCierre = 0; };
Compartimento comp[NUM_COMP];

void servoAngulo(int c, int ang) {
  ang = constrain(ang, 0, 180);
  uint32_t us = 500 + (uint32_t)ang * 2000 / 180;   // 0.5 ms .. 2.5 ms
  ledcWrite(CANAL_SERVO[c], us * 65535UL / 20000UL); // periodo 20 ms (50 Hz), 16 bits
}

void iniciarServos() {
  for (int c = 0; c < NUM_COMP; c++) {
    ledcSetup(CANAL_SERVO[c], 50, 16);
    ledcAttachPin(PIN_SERVO[c], CANAL_SERVO[c]);
    servoAngulo(c, cfgAngCerrado);
  }
}

void abrirCompartimento(int c) {
  if (c < 0 || c >= NUM_COMP) return;
  servoAngulo(c, cfgAngAbierto);
  comp[c].abierto = true;
  comp[c].tCierre = millis() + (uint32_t)cfgTiempo * 1000;
}

// Se llama en cada loop(): atiende peticiones y cierra cuando toca
void atenderServos() {
  for (int c = 0; c < NUM_COMP; c++) {
    if (pedidoAbrir[c]) { pedidoAbrir[c] = false; abrirCompartimento(c); }
    if (comp[c].abierto && (int32_t)(millis() - comp[c].tCierre) >= 0) {
      servoAngulo(c, cfgAngCerrado);
      comp[c].abierto = false;
      agregarEvento("Compartimento %d cerrado", c + 1);
    }
  }
}

// =====================================================================
// 7) TAREA DE VISIÓN (núcleo 1). Única que usa la cámara y la lista de caras.
// =====================================================================
void ponerError(const char* msg) {
  xSemaphoreTake(mtx, portMAX_DELAY);
  vision.estado = ERROR_VISION;
  strncpy(vision.error, msg, sizeof(vision.error) - 1);
  xSemaphoreGive(mtx);
  Serial.printf("[ERROR] %s\n", msg);
}

void tareaVision(void*) {
  dl_matrix3du_t* imagen   = dl_matrix3du_alloc(1, IMG_W, IMG_H, 3);
  dl_matrix3du_t* alineada = dl_matrix3du_alloc(1, FACE_WIDTH, FACE_HEIGHT, 3);
  if (!imagen || !alineada) { ponerError("Sin memoria para el reconocimiento"); vTaskDelete(NULL); }

  bool registrando = false;
  int  slotRegistro = 0, progreso = 0;
  uint32_t ultimaMuestra = 0;
  int  ultId = -2, cuentaId = 0;
  uint32_t ultT = 0;
  int  cuentaDesconocido = 0;
  uint32_t ultAvisoDesconocido = 0;
  int  accesoId = -1;
  uint32_t accesoHasta = 0;
  uint32_t ultimoCiclo = millis();
  float fps = 0;

  for (;;) {
    // --- Peticiones de la web ---
    if (pedidoBorrarTodo) {
      pedidoBorrarTodo = false;
      int guard = 0;
      while (id_list.count > 0 && guard++ < MAX_CARAS + 2) delete_face_id_in_flash(&id_list);
      xSemaphoreTake(mtx, portMAX_DELAY);
      borrarPersonas();
      xSemaphoreGive(mtx);
      registrando = false;
      agregarEvento("Todas las caras borradas");
    }
    if (pedidoRegistro) {
      pedidoRegistro = false;
      if (id_list.count >= MAX_CARAS) {
        agregarEvento("Registro rechazado: memoria llena");
      } else {
        registrando = true;
        slotRegistro = id_list.tail;      // esp-face guarda la nueva cara en "tail"
        progreso = 0;
        ultimaMuestra = 0;
        agregarEvento("Registrando a %s (comp. %d)", regNombre.c_str(), regComp + 1);
      }
    }

    // --- Captura ---
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
    uint32_t t1 = millis();
    if (!ok) {
      xSemaphoreTake(mtx, portMAX_DELAY); vision.erroresCam++; xSemaphoreGive(mtx);
      vTaskDelay(pdMS_TO_TICKS(20));
      continue;
    }

    // --- Detección ---
    box_array_t* caras = face_detect(imagen, &mtmn_config);
    uint32_t t2 = millis();

    bool hayCara = false, alineadaOk = false;
    int  id = -2;
    int  bx1 = 0, by1 = 0, bx2 = 0, by2 = 0;
    uint32_t ahora = millis();

    if (caras) {
      hayCara = true;
      bx1 = constrain((int)caras->box[0].box_p[0], 0, IMG_W - 1);
      by1 = constrain((int)caras->box[0].box_p[1], 0, IMG_H - 1);
      bx2 = constrain((int)caras->box[0].box_p[2], 0, IMG_W - 1);
      by2 = constrain((int)caras->box[0].box_p[3], 0, IMG_H - 1);

      if (align_face(caras, imagen, alineada) == ESP_OK) {
        alineadaOk = true;
        if (registrando) {
          if (ahora - ultimaMuestra >= MS_ENTRE_MUESTRAS) {
            ultimaMuestra = ahora;
            int8_t faltan = enroll_face_id_to_flash(&id_list, alineada);
            if (faltan == -2) {
              registrando = false;
              ponerError("Falta la particion 'fr' (revisa partitions.csv)");
            } else if (faltan >= 0) {
              progreso = MUESTRAS_REGISTRO - faltan;
              if (faltan == 0) {
                registrando = false;
                xSemaphoreTake(mtx, portMAX_DELAY);
                nombrePersona[slotRegistro] = regNombre;
                compPersona[slotRegistro] = regComp;
                guardarPersona(slotRegistro);
                xSemaphoreGive(mtx);
                agregarEvento("Registrado: %s -> comp. %d", regNombre.c_str(), regComp + 1);
              }
            }
          }
        } else if (id_list.count > 0) {
          id = recognize_face(&id_list, alineada);   // >=0 persona, -1 desconocido
        } else {
          id = -1;
        }
      }
      // Liberar como en el ejemplo oficial
      free(caras->score);
      free(caras->box);
      free(caras->landmark);
      free(caras);
    }
    uint32_t t3 = millis();

    // --- Confirmación: la MISMA persona 2 veces seguidas ---
    if (!registrando && id >= 0) {
      cuentaDesconocido = 0;
      if (id == ultId && ahora - ultT < VENTANA_CONFIRM_MS) cuentaId++;
      else cuentaId = 1;
      ultId = id; ultT = ahora;
      if (cuentaId >= CONFIRMACIONES) {
        cuentaId = 0;
        xSemaphoreTake(mtx, portMAX_DELAY);
        int c = (id < MAX_CARAS) ? compPersona[id] : -1;
        String n = (id < MAX_CARAS) ? nombrePersona[id] : String("");
        xSemaphoreGive(mtx);
        if (n.length() == 0) n = "Persona " + String(id);
        bool yaAbierto = (c >= 0 && c < NUM_COMP && comp[c].abierto);
        if (c >= 0 && c < NUM_COMP) pedidoAbrir[c] = true;   // si ya está abierto, alarga el tiempo
        if (accesoId != id || !yaAbierto) {
          if (c >= 0 && c < NUM_COMP) agregarEvento("%s reconocido: abre comp. %d", n.c_str(), c + 1);
          else agregarEvento("%s reconocido (sin compartimento)", n.c_str());
        }
        accesoId = id;
        accesoHasta = ahora + MOSTRAR_ACCESO_MS;
      }
    } else if (!registrando && id == -1) {
      cuentaId = 0;
      if (++cuentaDesconocido >= 3 && ahora - ultAvisoDesconocido > 10000) {
        ultAvisoDesconocido = ahora;
        agregarEvento("Cara NO registrada");
      }
    }

    // --- Estado para la web ---
    EstadoVision est;
    int idMostrar = id;
    if (registrando)                         est = REGISTRANDO;
    else if ((int32_t)(accesoHasta - ahora) > 0) { est = ACCESO; idMostrar = accesoId; }
    else if (!hayCara)                       est = ESPERANDO;
    else if (!alineadaOk)                    est = CARA_DETECTADA;
    else if (id >= 0)                        est = RECONOCIENDO;
    else                                     est = DESCONOCIDO;

    uint32_t periodo = ahora - ultimoCiclo;
    ultimoCiclo = ahora;
    if (periodo > 0) { float inst = 1000.0f / periodo; fps = fps == 0 ? inst : fps * 0.8f + inst * 0.2f; }

    xSemaphoreTake(mtx, portMAX_DELAY);
    Vision& v = vision;
    v.frame++;
    v.camOk = true;
    if (v.estado != ERROR_VISION || est == REGISTRANDO) v.estado = est;
    v.cara = hayCara;
    if (hayCara) { v.x1 = bx1; v.y1 = by1; v.x2 = bx2; v.y2 = by2; }
    v.id = idMostrar;
    if (idMostrar >= 0 && idMostrar < MAX_CARAS) {
      String n = nombrePersona[idMostrar];
      if (n.length() == 0) n = "Persona " + String(idMostrar);
      strncpy(v.nombre, n.c_str(), ENROLL_NAME_LEN); v.nombre[ENROLL_NAME_LEN] = 0;
      v.comp = compPersona[idMostrar];
    } else { v.nombre[0] = 0; v.comp = -1; }
    v.progreso = registrando ? progreso : 0;
    v.msCaptura = t1 - t0; v.msDeteccion = t2 - t1; v.msReconocer = t3 - t2; v.msTotal = t3 - t0;
    v.fps = fps;
    uint8_t* tmp = jpgCompartido; jpgCompartido = jpgTrabajo; jpgTrabajo = tmp;
    jpgCompartidoLen = jpgTrabajoLen;
    xSemaphoreGive(mtx);

    vTaskDelay(1);
  }
}

// =====================================================================
// 8) RED WI-FI: punto de acceso propio + conexión opcional a la red de casa
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
// 9) SERVIDOR WEB + API JSON
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
    int i = (posEvento - 1 - k + MAX_EVENTOS) % MAX_EVENTOS;   // más reciente primero
    if (k) ev += ',';
    ev += "{\"t\":" + String(eventos[i].t) + ",\"txt\":\"" + jsonEsc(eventos[i].txt) + "\"}";
  }
  ev += "]";
  String duenos[NUM_COMP];
  for (int i = 0; i < MAX_CARAS; i++) {
    bool ocupado = id_list.id_list && id_list.id_list[i] != NULL;
    if (ocupado && compPersona[i] >= 0 && compPersona[i] < NUM_COMP) {
      String& d = duenos[compPersona[i]];
      if (d.length()) d += ", ";
      d += nombrePersona[i].length() ? nombrePersona[i] : ("Persona " + String(i));
    }
  }
  int registradas = id_list.count;
  xSemaphoreGive(mtx);

  String comps = "[";
  for (int c = 0; c < NUM_COMP; c++) {
    if (c) comps += ',';
    int rest = comp[c].abierto ? max(0, (int)((int32_t)(comp[c].tCierre - millis()) / 1000) + 1) : 0;
    comps += "{\"abierto\":" + String(comp[c].abierto ? "true" : "false") + ",\"restante\":" + String(rest) +
             ",\"gpio\":" + String(PIN_SERVO[c]) + ",\"duenos\":\"" + jsonEsc(duenos[c]) + "\"}";
  }
  comps += "]";

  String red = WiFi.isConnected() ? (WiFi.localIP().toString() + " · " + NOMBRE_MDNS + ".local") : String("AP 192.168.4.1");
  String j = "{\"camara\":" + String(v.camOk ? "true" : "false") +
    ",\"frame\":" + String(v.frame) +
    ",\"estado\":\"" + NOMBRE_ESTADO[v.estado] + "\"" +
    ",\"cara\":" + String(v.cara ? "true" : "false") +
    ",\"caja\":[" + String(v.x1) + "," + String(v.y1) + "," + String(v.x2) + "," + String(v.y2) + "]" +
    ",\"id\":" + String(v.id) +
    ",\"nombre\":\"" + jsonEsc(v.nombre) + "\",\"comp\":" + String(v.comp) +
    ",\"progreso\":" + String(v.progreso) + ",\"muestras\":" + String(MUESTRAS_REGISTRO) +
    ",\"registradas\":" + String(registradas) + ",\"max\":" + String(MAX_CARAS) +
    ",\"msCaptura\":" + String(v.msCaptura) + ",\"msDeteccion\":" + String(v.msDeteccion) +
    ",\"msReconocer\":" + String(v.msReconocer) + ",\"msTotal\":" + String(v.msTotal) +
    ",\"fps\":" + String(v.fps, 2) + ",\"erroresCam\":" + String(v.erroresCam) +
    ",\"error\":\"" + jsonEsc(v.error) + "\"" +
    ",\"comps\":" + comps + ",\"eventos\":" + ev +
    ",\"tiempo\":" + String(cfgTiempo) + ",\"angC\":" + String(cfgAngCerrado) + ",\"angA\":" + String(cfgAngAbierto) +
    ",\"luz\":" + String(cfgLuz ? "true" : "false") +
    ",\"vflip\":" + String(cfgVflip ? "true" : "false") + ",\"espejo\":" + String(cfgEspejo ? "true" : "false") +
    ",\"red\":\"" + red + "\",\"heap\":" + String(ESP.getFreeHeap()) + ",\"psram\":" + String(ESP.getFreePsram()) +
    ",\"uptime\":" + String(millis() / 1000) + "}";
  enviarJson(200, j);
}

// GET /personas
void handlePersonas() {
  String j = "[";
  bool primero = true;
  xSemaphoreTake(mtx, portMAX_DELAY);
  for (int i = 0; i < MAX_CARAS; i++) {
    bool ocupado = id_list.id_list && id_list.id_list[i] != NULL;
    if (!ocupado) continue;
    if (!primero) j += ',';
    primero = false;
    j += "{\"id\":" + String(i) + ",\"nombre\":\"" + jsonEsc(nombrePersona[i]) + "\",\"comp\":" + String(compPersona[i]) + "}";
  }
  xSemaphoreGive(mtx);
  j += "]";
  enviarJson(200, j);
}

// GET /foto -> último JPEG analizado; la cara en la cabecera X-Cara
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
  if (v.cara) snprintf(cab, sizeof(cab), "%d,%d,%d,%d,%d", v.x1, v.y1, v.x2, v.y2, v.id);
  else strcpy(cab, "none");
  server.sendHeader("X-Cara", cab);
  server.sendHeader("Cache-Control", "no-store");
  server.setContentLength(len);
  server.send(200, "image/jpeg", "");
  server.client().write(jpgEnvio, len);
}

// POST /verificar (pin)
void handleVerificar() { if (pinOk()) enviarJson(200, "{\"ok\":true}"); }

// POST /registrar (pin, nombre, comp)
void handleRegistrar() {
  if (!pinOk()) return;
  String n = server.arg("nombre"); n.trim();
  int c = server.arg("comp").toInt() - 1;
  if (n.length() < 1 || n.length() > ENROLL_NAME_LEN) { enviarJson(400, "{\"ok\":false,\"error\":\"El nombre debe tener de 1 a 16 caracteres\"}"); return; }
  if (c < 0 || c >= NUM_COMP) { enviarJson(400, "{\"ok\":false,\"error\":\"Compartimento no válido\"}"); return; }
  if (id_list.count >= MAX_CARAS) { enviarJson(400, "{\"ok\":false,\"error\":\"Memoria llena: borra las caras para registrar más\"}"); return; }
  regNombre = n; regComp = c;
  xSemaphoreTake(mtx, portMAX_DELAY); vision.error[0] = 0; if (vision.estado == ERROR_VISION) vision.estado = ESPERANDO; xSemaphoreGive(mtx);
  pedidoRegistro = true;
  enviarJson(200, "{\"ok\":true}");
}

// POST /cancelar (pin) -> reinicia: descarta las muestras a medio tomar
void handleCancelar() {
  if (!pinOk()) return;
  enviarJson(200, "{\"ok\":true}");
  delay(300);
  ESP.restart();
}

// POST /borrarTodo (pin)
void handleBorrarTodo() { if (!pinOk()) return; pedidoBorrarTodo = true; enviarJson(200, "{\"ok\":true}"); }

// POST /probar (pin, comp)
void handleProbar() {
  if (!pinOk()) return;
  int c = server.arg("comp").toInt() - 1;
  if (c < 0 || c >= NUM_COMP) { enviarJson(400, "{\"ok\":false,\"error\":\"Compartimento no válido\"}"); return; }
  pedidoAbrir[c] = true;
  agregarEvento("Prueba manual: comp. %d", c + 1);
  enviarJson(200, "{\"ok\":true}");
}

// POST /ajustes (pin, tiempo, angC, angA, luz, nuevoPin)
void handleAjustes() {
  if (!pinOk()) return;
  int t = server.hasArg("tiempo") ? server.arg("tiempo").toInt() : cfgTiempo;
  int ac = server.hasArg("angC") ? server.arg("angC").toInt() : cfgAngCerrado;
  int aa = server.hasArg("angA") ? server.arg("angA").toInt() : cfgAngAbierto;
  String np = server.arg("nuevoPin");
  if (t < 1 || t > 60) { enviarJson(400, "{\"ok\":false,\"error\":\"Tiempo abierto: de 1 a 60 s\"}"); return; }
  if (ac < 0 || ac > 180 || aa < 0 || aa > 180) { enviarJson(400, "{\"ok\":false,\"error\":\"Ángulos: de 0 a 180\"}"); return; }
  if (np.length() && (np.length() < 4 || np.length() > 8)) { enviarJson(400, "{\"ok\":false,\"error\":\"El PIN debe tener de 4 a 8 caracteres\"}"); return; }
  cfgTiempo = t; cfgAngCerrado = ac; cfgAngAbierto = aa;
  if (server.hasArg("luz")) cfgLuz = server.arg("luz") == "1";
  if (server.hasArg("vflip")) cfgVflip = server.arg("vflip") == "1";
  if (server.hasArg("espejo")) cfgEspejo = server.arg("espejo") == "1";
  aplicarOrientacion();
  if (np.length()) cfgPin = np;
  guardarAjustes();
  digitalWrite(PIN_LED_FLASH, cfgLuz ? HIGH : LOW);
  for (int c = 0; c < NUM_COMP; c++) if (!comp[c].abierto) servoAngulo(c, cfgAngCerrado);
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
  server.on("/personas", HTTP_GET, handlePersonas);
  server.on("/foto", HTTP_GET, handleFoto);
  server.on("/verificar", HTTP_POST, handleVerificar);
  server.on("/registrar", HTTP_POST, handleRegistrar);
  server.on("/cancelar", HTTP_POST, handleCancelar);
  server.on("/borrarTodo", HTTP_POST, handleBorrarTodo);
  server.on("/probar", HTTP_POST, handleProbar);
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
// 10) SETUP / LOOP
// =====================================================================
void detenerConError(const char* msg) { Serial.println(msg); while (true) delay(1000); }

void setup() {
  Serial.begin(115200);
  delay(300);
  Serial.println();
  Serial.println("=========================================");
  Serial.println("  CASILLERO FACIAL ESP32-CAM");
  Serial.println("=========================================");

  if (!psramFound()) detenerConError("[ERROR] No hay PSRAM. Placa: AI Thinker ESP32-CAM.");
  mtx = xSemaphoreCreateMutex();

  cargarAjustes();
  cargarPersonas();

  pinMode(PIN_LED_FLASH, OUTPUT);
  digitalWrite(PIN_LED_FLASH, cfgLuz ? HIGH : LOW);
  iniciarServos();

  jpgTrabajo    = (uint8_t*)ps_malloc(JPG_MAX);
  jpgCompartido = (uint8_t*)ps_malloc(JPG_MAX);
  jpgEnvio      = (uint8_t*)ps_malloc(JPG_MAX);
  if (!jpgTrabajo || !jpgCompartido || !jpgEnvio) detenerConError("[ERROR] Sin memoria PSRAM.");

  if (!iniciarCamara()) detenerConError("[ERROR] Camara no iniciada. Revisa el cable plano y la alimentacion.");
  Serial.println("[CAM] OV2640 lista (320x240)");

  configurarDetector();
  face_id_init(&id_list, MAX_CARAS, MUESTRAS_REGISTRO);
  int8_t leidas = read_face_id_from_flash(&id_list);
  Serial.printf("[CARAS] Caras guardadas en flash: %d\n", leidas);
  for (int i = 0; i < MAX_CARAS; i++)
    if (id_list.id_list[i]) Serial.printf("  #%d %s -> compartimento %d\n", i, nombrePersona[i].c_str(), compPersona[i] + 1);

  cargarRed();
  WiFi.persistent(false);
  WiFi.mode(WIFI_AP_STA);
  WiFi.softAPConfig(IPAddress(192, 168, 4, 1), IPAddress(192, 168, 4, 1), IPAddress(255, 255, 255, 0));
  if (!WiFi.softAP(AP_SSID, AP_PASS)) detenerConError("[ERROR] No se pudo crear el punto de acceso.");
  WiFi.setSleep(false);
  WiFi.setAutoReconnect(true);

  // mDNS: la librería escucha los eventos Wi-Fi, así que también funciona
  // cuando más tarde se conecta a la red de casa
  if (MDNS.begin(NOMBRE_MDNS)) {
    MDNS.addService("http", "tcp", 80);
    Serial.printf("[mDNS] Nombre: http://%s.local\n", NOMBRE_MDNS);
  } else {
    Serial.println("[mDNS] No se pudo iniciar");
  }

  Serial.println("-----------------------------------------");
  Serial.printf(" Red Wi-Fi : %s\n", AP_SSID);
  Serial.printf(" Clave     : %s\n", AP_PASS);
  Serial.printf(" Abre      : http://%s\n", WiFi.softAPIP().toString().c_str());
  Serial.println("-----------------------------------------");
  if (modoRed == MODO_LOCAL && staSsid.length()) iniciarConexionLocal(staSsid, staPass, false);
  Serial.printf("Heap libre: %u | PSRAM libre: %u\n", ESP.getFreeHeap(), ESP.getFreePsram());

  iniciarServidor();
  xTaskCreatePinnedToCore(tareaVision, "vision", 16384, nullptr, 1, nullptr, 1);
  agregarEvento("Sistema iniciado (%d caras)", (int)id_list.count);
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
    Serial.printf("[VIS] %s id=%d %s | %.2f fps | det %u ms, rec %u ms | heap %u\n",
                  NOMBRE_ESTADO[v.estado], v.id, v.nombre, v.fps, v.msDeteccion, v.msReconocer, ESP.getFreeHeap());
  }
  delay(2);
}
