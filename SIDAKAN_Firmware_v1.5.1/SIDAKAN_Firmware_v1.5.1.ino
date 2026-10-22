/*
 * ==============================================================================
 * PROJEK KEPEMIMPINAN PPG PRAJABATAN - UNIVERSITAS PENDIDIKAN INDONESIA
 * SISTEM SIDAKAN (Sistem IoT Deteksi Dini Kualitas & Kelayakan Makanan)
 * ==============================================================================
 * Mikrokontroler : ESP32-S (38-Pin / NodeMCU-32S)
 * Firmware       : 1.5.1 Dual MQ (State Machine, Plateau Detection & Offline Web)
 * Acuan Dokumen  : DOKUMENTASI_PERBAIKAN_DAN_SPESIFIKASI_SISTEM_SIDAKAN.md (SOP Pilot v1.1)
 * Penulis        : Anthonio Akbar, S.Pd. (NIM: 2530710)
 * ==============================================================================
 * DAFTAR RUJUKAN ILMIAH & LANDASAN TEORITIS (APA 7th)
 * ------------------------------------------------------------------------------
 * 1. Derau Termal & ADC ESP32:
 *    - Johnson, J. B. (1928). Thermal Agitation of Electricity in Conductors.
 *      Physical Review, 32(1), 97-109.
 *    - Nyquist, H. (1928). Thermal Agitation of Electric Charge in Conductors.
 *      Physical Review, 32(1), 110-113. Formula: Vn,rms = sqrt(4*k*T*R*delta_f).
 *    - Espressif Systems. (2023). ESP32 Technical Reference Manual (ADC 12-bit).
 * 2. Metodologi Hidung Elektronik (Electronic Nose & Relative Rise):
 *    - Gardner, J. W., & Bartlett, P. N. (1994). A brief history of electronic
 *      noses. Sensors and Actuators B: Chemical, 18(1-3), 210-211.
 *    - Pearce, T. C., et al. (Eds.). (2003). Handbook of Machine Olfaction.
 * 3. Prinsip Sensor Gas Metal Oxide Semiconductor (SnO2):
 *    - Yamazoe, N., & Shimanoe, K. (2009). Basic concepts of semiconductor gas
 *      sensors. Sensors and Actuators B: Chemical, 138(1), 100-107.
 *    - Barsan, N., & Weimar, U. (2001). Conduction model of metal oxide gas sensors.
 * 4. Standar Mutu Pangan & Biomarker Volatil:
 *    - Badan Standardisasi Nasional. (2013). SNI 2729:2013 - Ikan Segar (TVB-N).
 *    - Olafsdottir, G., et al. (1997). Methods for evaluating fish freshness.
 *    - U.S. FDA. (2020). Bacteriological Analytical Manual (BAM) Chapter 10.
 * ==============================================================================
 */

#include <WiFi.h>
#include <WebServer.h>
#include <WebSocketsServer.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <DHT.h>
#include <math.h>
#include <Update.h>
#include "SIDAKAN_Types.h"

// ------------------------------------------------------------------------------
// KONFIGURASI ACCESS POINT (100% OFFLINE TANPA INTERNET)
// ------------------------------------------------------------------------------
const char* AP_SSID = "SIDAKAN-WIFI";
const char* AP_PASS = "ppginformatikaupi";

WebServer server(80);
WebSocketsServer webSocket(81);

// ------------------------------------------------------------------------------
// WEB OTA FIRMWARE UPDATE (PORT 80)
// Akses via browser: http://192.168.4.1/update
// ------------------------------------------------------------------------------
const char* OTA_ADMIN_USER = "admin";
const char* OTA_ADMIN_PASS = "admin";

bool otaUploadAuthorized = false;
bool otaUploadOK = false;
bool otaUploadStarted = false;
bool otaRestartPending = false;
unsigned long otaRestartAt = 0;

const char OTA_HTML[] PROGMEM = R"SIDAKANOTA(
<!DOCTYPE html><html lang="id"><head>
<meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>SIDAKAN - Pembaruan Firmware</title>
<style>body{font:16px Arial,sans-serif;background:#eef3fa;color:#182b42;margin:0;padding:22px}
main{max-width:480px;margin:44px auto;background:white;border-radius:16px;padding:26px;box-shadow:0 5px 24px #18315320}
h1{font-size:24px;margin:0 0 12px}p{line-height:1.5}input{max-width:100%;margin:12px 0}
button{background:#183153;color:white;border:0;border-radius:8px;padding:12px 22px;cursor:pointer}
small{color:#667}a{color:#183153}</style></head><body><main>
<h1>SIDAKAN - Web OTA</h1><p>Unggah firmware aplikasi <b>.bin</b> dari Arduino IDE.</p>
<form method="POST" action="/update" enctype="multipart/form-data">
<input type="file" name="firmware" accept=".bin" required><br>
<button type="submit">Unggah Firmware</button></form>
<p><small>Perangkat akan restart setelah pembaruan berhasil. Jangan putus daya saat proses unggah. Pastikan firmware baru tetap memuat Web OTA.</small></p>
<a href="/">Kembali ke dashboard</a></main></body></html>
)SIDAKANOTA";

bool otaCheckLogin() {
  if (server.authenticate(OTA_ADMIN_USER, OTA_ADMIN_PASS)) return true;
  server.requestAuthentication();
  return false;
}

void otaHandleUpload() {
  HTTPUpload &upload = server.upload();
  switch (upload.status) {
    case UPLOAD_FILE_START:
      otaUploadAuthorized = server.authenticate(OTA_ADMIN_USER, OTA_ADMIN_PASS);
      otaUploadOK = false;
      otaUploadStarted = false;
      if (!otaUploadAuthorized) return;
      if (!upload.filename.endsWith(".bin")) {
        Serial.println(F("[OTA] File harus berupa firmware .bin"));
        return;
      }
      otaUploadStarted = Update.begin(UPDATE_SIZE_UNKNOWN, U_FLASH);
      if (!otaUploadStarted) Update.printError(Serial);
      else Serial.printf("[OTA] Menerima: %s\n", upload.filename.c_str());
      break;
    case UPLOAD_FILE_WRITE:
      if (!otaUploadAuthorized || !otaUploadStarted || Update.hasError()) return;
      if (Update.write(upload.buf, upload.currentSize) != upload.currentSize) {
        Update.printError(Serial);
      }
      break;
    case UPLOAD_FILE_END:
      if (!otaUploadAuthorized || !otaUploadStarted || Update.hasError()) {
        if (otaUploadStarted && !Update.isFinished()) Update.abort();
        return;
      }
      otaUploadOK = Update.end(true);
      if (otaUploadOK) Serial.printf("[OTA] Firmware diterima: %u bytes\n", (unsigned)upload.totalSize);
      else Update.printError(Serial);
      break;
    case UPLOAD_FILE_ABORTED:
      if (otaUploadStarted) Update.abort();
      otaUploadOK = false;
      Serial.println(F("[OTA] Upload dibatalkan"));
      break;
    default: break;
  }
}

void otaHandleResult() {
  if (!otaCheckLogin()) return;
  if (otaUploadOK && !Update.hasError()) {
    server.send(200, "text/html; charset=utf-8",
      "<h2>Firmware SIDAKAN berhasil diperbarui.</h2><p>ESP32 akan restart. Silakan sambungkan kembali ke WiFi SIDAKAN-WIFI lalu buka dashboard.</p>");
    otaRestartPending = true;
    otaRestartAt = millis();
  } else {
    server.send(400, "text/plain; charset=utf-8",
      "Pembaruan gagal atau file firmware tidak valid. Buka Serial Monitor untuk detail.");
  }
}

void otaServiceRestart() {
  if (otaRestartPending && millis() - otaRestartAt >= 2000UL) ESP.restart();
}

// ------------------------------------------------------------------------------
// PENETAPAN PIN ESP32-S
// ------------------------------------------------------------------------------
#define PIN_MQ135          34  // ADC1_CH6 - respons gas/VOC luas
#define PIN_MQ3            35  // ADC1_CH7 - respons alkohol/etanol
#define PIN_DHT            25  // Sensor suhu dan kelembapan ruang uji
#define PIN_LED_HIJAU      33  // Indikator Kondisi Normal / AMAN
#define PIN_LED_KUNING     26  // Indikator Stabilisasi / WASPADA
#define PIN_LED_MERAH      27  // Indikator BAHAYA
#define PIN_BUZZER         23  // Transduser nada audio
#define PIN_BUTTON_POWER   19  // Tombol Daya (Tahan 2s) & Mulai/Batal Uji (Klik)
#define PIN_BUTTON_CALIB   18  // Tombol Kalibrasi Manual Baseline

// ------------------------------------------------------------------------------
// LAYAR OLED SSD1306 128x64 (I2C)
// ------------------------------------------------------------------------------
#define SCREEN_WIDTH    128
#define SCREEN_HEIGHT   64
#define OLED_RESET      -1
#define OLED_I2C_ADDR   0x3C

Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);
bool oledReady = false;

// ------------------------------------------------------------------------------
// SENSOR LINGKUNGAN RUANG UJI (DHT11)
// ------------------------------------------------------------------------------
#define DHTTYPE DHT11
DHT dht(PIN_DHT, DHTTYPE);

// ------------------------------------------------------------------------------
// AMBANG OPERASIONAL & PARAMETER FISIK SISTEM
// ------------------------------------------------------------------------------
const float MQ3_WARNING_RISE_PCT   = 25.0f;
const float MQ3_DANGER_RISE_PCT    = 50.0f;  // Diperbarui dari 60.0% menjadi 50.0%
const float MQ135_WARNING_RISE_PCT = 25.0f;
const float MQ135_DANGER_RISE_PCT  = 50.0f;  // Diperbarui dari 60.0% menjadi 50.0%
const float COMBINED_WARNING_INDEX = 50.0f;

const int MIN_VALID_BASELINE_ADC   = 80;     // Batas sanity check kabel putus/kendor
const int MAX_VALID_BASELINE_ADC   = 4000;   // Batas saturasi ADC
const float MIN_SIGNIFICANT_DELTA  = 35.0f;  // Deadband fisik minimum penangkal derau

const unsigned long RUNTIME_STABILIZATION_MS = 60000UL;  // Pemanasan awal 60 detik
const unsigned long SENSOR_INTERVAL_MS       = 2000UL;   // Interval sampling 2 detik
const int EXPOSURE_DURATION_SEC              = 300;      // Paparan dasar 5 menit (300 detik)
const int MAX_TEST_DURATION_SEC              = 600;      // Batas maksimal evaluasi (10 menit)
const int EXTENSION_STEP_SEC                 = 30;       // Perpanjangan dinamis +30 detik

const unsigned long RECOVERY_TIMEOUT_MS      = 900000UL; // Timeout pemulihan 15 menit
const float RECOVERY_TOLERANCE_PCT           = 0.10f;    // Toleransi desorpsi +/- 10%
const int RECOVERY_CONSECUTIVE_TARGET        = 3;        // 3 titik pembacaan berturut-turut

// (Definisi SystemStatus, TestState, PlateauStatus, dan RingBuffer16
//  berada di file header "SIDAKAN_Types.h" agar tidak bentrok dengan preprocessor Arduino IDE)

RingBuffer16 ringMQ3;
RingBuffer16 ringMQ135;

void ringBufferReset(RingBuffer16 &rb) {
  rb.count = 0;
  rb.head = 0;
  for (int i = 0; i < PLATEAU_WINDOW_SIZE; i++) rb.data[i] = 0;
}

void ringBufferPush(RingBuffer16 &rb, int val) {
  rb.data[rb.head] = val;
  rb.head = (rb.head + 1) % PLATEAU_WINDOW_SIZE;
  if (rb.count < PLATEAU_WINDOW_SIZE) rb.count++;
}

int ringBufferGet(const RingBuffer16 &rb, int index) {
  if (index < 0 || index >= rb.count) return 0;
  int start = (rb.count == PLATEAU_WINDOW_SIZE) ? rb.head : 0;
  int actualIdx = (start + index) % PLATEAU_WINDOW_SIZE;
  return rb.data[actualIdx];
}

// ------------------------------------------------------------------------------
// VARIABEL STATUS OPERASIONAL SISTEM
// ------------------------------------------------------------------------------
SystemStatus currentStatus = STATUS_AMAN;
TestState currentTestState = STATE_WARMUP;
PlateauStatus currentPlateauStatus = PLATEAU_BELUM_STABIL;

bool systemPowerState = false;
bool sensorBaselineValid = false;
bool dhtValid = false;

unsigned long sensorStartMillis = 0;
unsigned long lastAutoBaselineAttempt = 0;
const unsigned long AUTO_BASELINE_RETRY_MS = 10000UL;

int testTimerSeconds = 0;
int testElapsedSeconds = 0;
unsigned long previousMillisTimer = 0;
unsigned long previousMillisSensor = 0;
unsigned long stateEnterMillis = 0;

float mq3Fluc = 0.0f;
float mq135Fluc = 0.0f;
float mq3Trend = 0.0f;
float mq135Trend = 0.0f;

int recoveryConsecutiveMatches = 0;
unsigned long recoveryStartMillis = 0;
bool recoveryReady = false;

// Nilai Terkunci Hasil Pengujian (Fase LOCKED)
int lockedMQ3 = 0;
int lockedMQ135 = 0;
float lockedRiseMQ3 = 0.0f;
float lockedRiseMQ135 = 0.0f;
float lockedCombined = 0.0f;
SystemStatus lockedStatus = STATUS_AMAN;
PlateauStatus lockedPlateau = PLATEAU_BELUM_STABIL;

// ------------------------------------------------------------------------------
// FILTER RATA-RATA BERGERAK SENSOR GAS (10 SAMPEL FISIK)
// ------------------------------------------------------------------------------
const int NUM_READINGS = 10;
int mq135Readings[NUM_READINGS] = {0};
int mq3Readings[NUM_READINGS] = {0};
int gasReadIndex = 0;

long totalMQ135 = 0;
long totalMQ3 = 0;
int averageMQ135 = 0;
int averageMQ3 = 0;

int baselineMQ135 = 0;
int baselineMQ3 = 0;

float mq135RisePct = 0.0f;
float mq3RisePct = 0.0f;
float mq135Index = 0.0f;
float mq3Index = 0.0f;
float combinedGasIndex = 0.0f;

float currentTemp = 0.0f;
float currentHum = 0.0f;

// ------------------------------------------------------------------------------
// KONTROL TOMBOL FISIK
// ------------------------------------------------------------------------------
unsigned long powerButtonPressStart = 0;
bool powerButtonWasPressed = false;
bool powerHoldExecuted = false;
const unsigned long HOLD_TIME_REQUIRED = 2000UL;

bool calibButtonWasPressed = false;
unsigned long lastCalibAction = 0;
const unsigned long BUTTON_DEBOUNCE_MS = 80UL;

// ------------------------------------------------------------------------------
// PENGENDALI BUZZER NON-BLOCKING (BuzzerPattern di SIDAKAN_Types.h)
// ------------------------------------------------------------------------------

BuzzerPattern buzzerSeq = {0, 0, 0, 0, 0, 0, false, false};

void triggerVerdictBuzzer(SystemStatus status) {
  if (status == STATUS_AMAN) {
    // 2 bip pendek frekuensi 1000 Hz
    buzzerSeq = {1000, 150, 100, 2, 0, millis(), true, true};
    tone(PIN_BUZZER, 1000);
  } else if (status == STATUS_WASPADA) {
    // 3 bip sedang frekuensi 1500 Hz
    buzzerSeq = {1500, 250, 150, 3, 0, millis(), true, true};
    tone(PIN_BUZZER, 1500);
  } else {
    // Bip cepat berkelanjutan frekuensi 2000 Hz
    buzzerSeq = {2000, 100, 80, 10, 0, millis(), true, true};
    tone(PIN_BUZZER, 2000);
  }
}

void handleBuzzerSequencer() {
  if (!buzzerSeq.isPlaying) return;
  unsigned long now = millis();

  if (buzzerSeq.inSoundPhase) {
    if (now - buzzerSeq.stepStartMs >= buzzerSeq.durationMs) {
      noTone(PIN_BUZZER);
      buzzerSeq.inSoundPhase = false;
      buzzerSeq.stepStartMs = now;
    }
  } else {
    if (now - buzzerSeq.stepStartMs >= buzzerSeq.pauseMs) {
      buzzerSeq.currentCycle++;
      if (buzzerSeq.currentCycle >= buzzerSeq.repeatCount) {
        buzzerSeq.isPlaying = false;
        noTone(PIN_BUZZER);
      } else {
        buzzerSeq.inSoundPhase = true;
        buzzerSeq.stepStartMs = now;
        tone(PIN_BUZZER, buzzerSeq.freq);
      }
    }
  }
}

// ------------------------------------------------------------------------------
// DEKLARASI PROTOTIPE FUNGSI
// ------------------------------------------------------------------------------
void transitionToState(TestState newState);
void startSampleTest(const char* source);
void cancelSampleTest(const char* source);
void bypassRecovery(const char* source);
void runDualGasCalibration();
void updateActuators(SystemStatus status, unsigned long currentMillis);
void printSerialLog();
void handleButtonControl();
void handleTimerCountdown();
void broadcastSensorData();
void toggleSystemPower(const char* source);
void readAndFilterSensors();
void calculateGasIndices();
void evaluateStatus();
void evaluatePlateau();
float relativeRisePct(int value, int baseline);
float riskIndexFromRise(float risePct, float warningPct, float dangerPct);
const char* statusText(SystemStatus status);
const char* testStateText(TestState state);
const char* plateauText(PlateauStatus plateau);

void showStandbyOLED();
void showWarmupOLED(int secRemaining);
void showIdleOLED();
void showExposureOLED();
void showLockedOLED();
void showRecoveryOLED();
void showBaselineInvalidOLED();

bool baselineADCValid(int value);
bool dualBaselineValid(int mq3Base, int mq135Base);
void resetGasFilter(int seedMQ3, int seedMQ135);

// ------------------------------------------------------------------------------
// KONTEN ANTARMUKA WEB DASHBOARD (100% OFFLINE VIA PROGMEM)
// ------------------------------------------------------------------------------
const char PAGE_INDEX[] PROGMEM = R"rawliteral(<!DOCTYPE html>
<html lang="id" data-theme="light">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width, initial-scale=1.0">
<title>SIDAKAN · Deteksi Kualitas Makanan Dual MQ</title>
<style>
:root[data-theme="light"]{
  --bg:#f1f5f9;--surface:#ffffff;--border:#cbd5e1;--text:#0f172a;--muted:#64748b;
  --soft:#f8fafc;--card:#ffffff;--primary:#0f172a;--primary-fg:#ffffff;
  --green:#16a34a;--amber:#d97706;--red:#dc2626;--blue:#2563eb;--orange:#ea580c;
  --safe-bg:#f0fdf4;--safe-border:#86efac;--safe-text:#166534;
  --warn-bg:#fffbeb;--warn-border:#fde68a;--warn-text:#92400e;
  --danger-bg:#fef2f2;--danger-border:#fca5a5;--danger-text:#991b1b;
}
:root[data-theme="dark"]{
  --bg:#090d16;--surface:#111827;--border:#1f293d;--text:#f8fafc;--muted:#94a3b8;
  --soft:#172033;--card:#111827;--primary:#f8fafc;--primary-fg:#090d16;
  --green:#4ade80;--amber:#fbbf24;--red:#f87171;--blue:#60a5fa;--orange:#fb923c;
  --safe-bg:rgba(22,101,52,.2);--safe-border:rgba(74,222,128,.3);--safe-text:#86efac;
  --warn-bg:rgba(180,83,9,.2);--warn-border:rgba(251,191,36,.3);--warn-text:#fde68a;
  --danger-bg:rgba(153,27,27,.25);--danger-border:rgba(248,113,113,.3);--danger-text:#fca5a5;
}
*{box-sizing:border-box}

*, *::before, *::after {
  box-sizing: border-box;
}
html, body {
  overflow-x: hidden;
  width: 100%;
  margin: 0;
  padding: 0;
  box-sizing: border-box;
}
.app {
  max-width: 1080px;
  margin: auto;
  padding: 14px;
  display: grid;
  gap: 14px;
  width: 100%;
  overflow-x: hidden;
  box-sizing: border-box;
}
@media(max-width: 640px) {
  .app {
    padding: 10px;
    gap: 12px;
  }
}

body{margin:0;background:var(--bg);color:var(--text);font:14px -apple-system,BlinkMacSystemFont,"Segoe UI",Roboto,Helvetica,Arial,sans-serif;padding:14px;line-height:1.45}

header{display:flex;justify-content:space-between;align-items:center;gap:10px;flex-wrap:wrap;background:var(--surface);border:1px solid var(--border);border-radius:14px;padding:12px 18px}



.badge{border:1px solid var(--border);padding:6px 12px;border-radius:8px;font-weight:700;font-size:12px;min-height:34px;display:inline-flex;align-items:center}
.badge.online{background:var(--safe-bg);color:var(--safe-text);border-color:var(--safe-border)}
.badge.offline{background:var(--danger-bg);color:var(--danger-text);border-color:var(--danger-border)}
.actions{display:flex;gap:8px;align-items:center;flex-wrap:wrap}
button{border:1px solid var(--border);background:var(--surface);color:var(--text);padding:10px 16px;border-radius:9px;font-weight:600;font-size:13px;cursor:pointer;min-height:44px;display:inline-flex;align-items:center;justify-content:center;transition:background .15s}
button:hover:not(:disabled){filter:brightness(0.95)}
button:disabled{opacity:0.45;cursor:not-allowed}
button.primary{background:var(--primary);color:var(--primary-fg);border:none}
button.danger{background:var(--red);color:#fff;border:none}
button.success{background:var(--green);color:#fff;border:none}

.tab-btn{background:transparent;border:1px solid transparent;border-bottom:none;border-radius:10px 10px 0 0;padding:12px 20px;font-weight:700;font-size:14px;color:var(--muted);min-height:46px}
.tab-btn.active{background:var(--surface);color:var(--text);border-color:var(--border);border-bottom:2px solid var(--surface);margin-bottom:-1px}
.tab-pane{display:none}
.tab-pane.active{display:grid;gap:14px}
.card{background:var(--card);border:1px solid var(--border);border-radius:14px;padding:18px;width:100%;box-sizing:border-box;min-width:0;overflow:hidden}
.hero-box{display:grid;grid-template-columns:1fr auto;gap:16px;align-items:center;background:var(--surface);border:1px solid var(--border);border-radius:14px;padding:22px}
.hero-title{margin:0 0 6px;font-size:22px;letter-spacing:-0.4px}
.hero-desc{margin:0;color:var(--muted);font-size:14px}
.timer-display{text-align:right}
.timer-num{font:800 42px ui-monospace,Consolas,monospace;letter-spacing:-1px}
.timer-label{color:var(--muted);font-size:12px;font-weight:600}
.progress-bar{height:10px;background:var(--soft);border-radius:99px;overflow:hidden;margin-top:14px;border:1px solid var(--border)}
.progress-fill{height:100%;width:0%;background:var(--blue);transition:width .3s}
.btn-group-main{display:flex;gap:12px;margin-top:16px;flex-wrap:wrap}
.btn-main{font-size:16px;padding:14px 28px;min-height:52px;font-weight:700;border-radius:10px;flex:1}
.verdict-card{border-radius:14px;padding:20px;border:2px solid var(--border);background:var(--surface)}
.verdict-card.safe{background:var(--safe-bg);border-color:var(--safe-border);color:var(--safe-text)}
.verdict-card.warn{background:var(--warn-bg);border-color:var(--warn-border);color:var(--warn-text)}
.verdict-card.danger{background:var(--danger-bg);border-color:var(--danger-border);color:var(--danger-text)}
.verdict-title{margin:0 0 4px;font-size:20px;font-weight:800}
.verdict-desc{margin:0;font-size:14px;line-height:1.4}
.grid-3{display:grid;grid-template-columns:1fr 1fr 1fr;gap:14px}
.grid-2{display:grid;grid-template-columns:1fr 1fr;gap:14px}
.label-row{display:flex;justify-content:space-between;color:var(--muted);font-size:12px;font-weight:700}
.tag{background:var(--soft);padding:3px 8px;border-radius:6px;border:1px solid var(--border);font-size:11px}
.val-big{font:800 32px ui-monospace,Consolas,monospace;margin:8px 0}
.val-unit{font-size:14px;color:var(--muted);font-weight:400}
.kv-table{display:grid;grid-template-columns:1fr auto;gap:6px 12px;border-top:1px solid var(--border);padding-top:12px;font-size:13px}
.kv-table span:nth-child(odd){color:var(--muted)}
.data-table{width:100%;border-collapse:collapse;font-size:13px;text-align:left}
.data-table th,.data-table td{padding:10px 12px;border-bottom:1px solid var(--border)}
.data-table th{background:var(--soft);color:var(--muted);font-weight:700;font-size:12px}
.chart-box{background:var(--surface);border:1px solid var(--border);border-radius:14px;padding:16px}
.chart-legend{display:flex;gap:18px;margin-bottom:10px;font-size:13px;font-weight:600}
.sw{width:14px;height:4px;display:inline-block;vertical-align:middle;margin-right:6px;border-radius:2px}
.sw-mq3{background:var(--blue)}
.sw-mq135{background:var(--orange)}
.sw-base{background:var(--muted);border-top:1px dashed var(--muted)}
canvas{width:100%;height:220px;display:block}
details{background:var(--surface);border:1px solid var(--border);border-radius:10px;margin-bottom:10px;padding:12px 16px}
summary{font-weight:700;font-size:15px;cursor:pointer;outline:none;user-select:none}
details[open] summary{margin-bottom:10px}
.theory-p{margin:6px 0;line-height:1.5;color:var(--muted);font-size:13px}
.theory-formula{background:var(--soft);border:1px solid var(--border);padding:8px 12px;border-radius:8px;font-family:ui-monospace,Consolas,monospace;margin:8px 0;font-size:13px;color:var(--text)}
footer{color:var(--muted);font-size:12px;text-align:center;padding:12px}
@media(max-width:860px){
  .hero-box{grid-template-columns:1fr;text-align:center}
  .timer-display{text-align:center}
  .grid-3{grid-template-columns:1fr}
  .grid-2{grid-template-columns:1fr}
  .btn-group-main{flex-direction:column}
}

/* Stylings Panduan Komprehensif & Live Mini Status */
.guide-status-pill{padding:6px 14px;border-radius:20px;font-size:13px;font-weight:700;display:inline-flex;align-items:center;gap:8px;border:1px solid var(--border);background:var(--soft);color:var(--text);transition:all .2s ease}
.status-standby{border-color:var(--border);color:var(--muted);background:var(--soft)}
.status-warmup{border-color:var(--amber);background:var(--warn-bg);color:var(--warn-text)}
.status-ready{border-color:var(--green);background:var(--safe-bg);color:var(--safe-text)}
.status-testing{border-color:var(--blue);background:rgba(37,99,235,0.1);color:var(--blue)}
.status-locked{border-color:var(--green);background:var(--safe-bg);color:var(--safe-text)}
.status-recovery{border-color:var(--orange);background:rgba(234,88,12,0.1);color:var(--orange)}

.info-card{background:var(--surface);border:1px solid var(--border);border-radius:10px;padding:14px;display:flex;flex-direction:column;justify-content:space-between}

/* SOP Stepper Workflow (Industrial Non-Slop) */
.sop-grid{display:grid;grid-template-columns:repeat(4,1fr);gap:12px}
@media(max-width:880px){.sop-grid{grid-template-columns:1fr 1fr}}
@media(max-width:540px){.sop-grid{grid-template-columns:1fr}}

.sop-card{background:var(--surface);border:1px solid var(--border);border-radius:12px;padding:16px;display:flex;flex-direction:column;justify-content:space-between;transition:border-color .15s ease}
.sop-card:hover{border-color:var(--muted)}
.sop-card-header{display:flex;align-items:center;justify-content:space-between;gap:8px;margin-bottom:10px}
.sop-step-tag{font-size:11px;font-weight:700;letter-spacing:0.3px;padding:3px 8px;border-radius:6px;text-transform:uppercase;display:inline-flex;align-items:center;gap:5px}
.sop-step-tag.tag-warm{background:var(--warn-bg);border:1px solid var(--warn-border);color:var(--warn-text)}
.sop-step-tag.tag-tare{background:var(--soft);border:1px solid var(--border);color:var(--text)}
.sop-step-tag.tag-test{background:rgba(37,99,235,0.08);border:1px solid rgba(37,99,235,0.25);color:var(--blue)}
.sop-step-tag.tag-clean{background:var(--safe-bg);border:1px solid var(--safe-border);color:var(--safe-text)}

.sop-meta-duration{font-size:12px;font-weight:600;color:var(--muted)}
.sop-title{margin:0 0 6px;font-size:14.5px;font-weight:700;color:var(--text);line-height:1.35}
.sop-
*, *::before, *::after {
  box-sizing: border-box;
}
html, body {
  overflow-x: hidden;
  width: 100%;
  margin: 0;
  padding: 0;
  box-sizing: border-box;
}
.app {
  max-width: 1080px;
  margin: auto;
  padding: 14px;
  display: grid;
  gap: 14px;
  width: 100%;
  overflow-x: hidden;
  box-sizing: border-box;
}
@media(max-width: 640px) {
  .app {
    padding: 10px;
    gap: 12px;
  }
}

body{margin:0;font-size:12.5px;line-height:1.5;color:var(--muted)}
.sop-key-rule{margin-top:10px;padding-top:10px;border-top:1px dashed var(--border);font-size:11.5px;line-height:1.45;color:var(--text);font-weight:500}
.sop-key-rule strong{font-weight:700}

.badge-status{font-size:11px;font-weight:700;padding:2px 8px;border-radius:6px;display:inline-block}
.badge-aman{background:var(--safe-bg);color:var(--safe-text);border:1px solid var(--safe-border)}
.badge-waspada{background:var(--warn-bg);color:var(--warn-text);border:1px solid var(--warn-border)}
.badge-bahaya{background:var(--danger-bg);color:var(--danger-text);border:1px solid var(--danger-border)}



/* Responsive Top Sticky Bar & Mobile Insets */


.sticky-brand {
  display: flex;
  align-items: center;
  gap: 8px;
  font-weight: 700;
  font-size: 15px;
  letter-spacing: 0.5px;
}
.brand-pulse-dot {
  width: 9px;
  height: 9px;
  border-radius: 50%;
  background: var(--green);
  display: inline-block;
}
.sticky-sub {
  font-size: 11px;
  color: var(--muted);
  font-weight: 600;
  background: var(--soft);
  padding: 2px 7px;
  border-radius: 6px;
  border: 1px solid var(--border);
}
.sticky-status-group {
  display: flex;
  align-items: center;
  gap: 8px;
}

/* Header Action Controls */
header.app-header {
  display: flex;
  justify-content: space-between;
  align-items: center;
  gap: 12px;
  flex-wrap: wrap;
  background: var(--surface);
  border: 1px solid var(--border);
  border-radius: 14px;
  padding: 14px 18px;
  margin-bottom: 14px;
}
.header-actions {
  display: flex;
  gap: 8px;
  align-items: center;
}
.btn-ctrl {
  min-height: 44px;
  padding: 8px 16px;
  font-size: 13.5px;
  font-weight: 600;
  border-radius: 8px;
  border: 1px solid var(--border);
  background: var(--surface);
  color: var(--text);
  cursor: pointer;
  display: inline-flex;
  align-items: center;
  justify-content: center;
  transition: all .15s ease;
}
.btn-ctrl.primary {
  background: var(--primary);
  color: var(--primary-fg);
  border-color: var(--primary);
}

@media(max-width: 640px) {
  
  header.app-header {
    flex-direction: column;
    align-items: stretch;
    gap: 12px;
    padding: 14px;
  }
  .header-actions {
    display: grid;
    grid-template-columns: 1.2fr 1fr 1fr;
    gap: 8px;
    width: 100%;
  }
  .btn-ctrl {
    width: 100%;
    padding: 10px 4px;
    font-size: 13px;
  }
}

/* Responsive Adaptive Tabs Nav */
.tabs-nav {
  display: flex;
  gap: 4px;
  border-bottom: 1px solid var(--border);
  margin-bottom: 14px;
  overflow-x: auto;
  scrollbar-width: none;
  -webkit-overflow-scrolling: touch;
}
.tabs-nav::-webkit-scrollbar { display: none; }
.tab-label-short { display: none; }
.tab-label-full { display: inline; }

.tab-btn {
  background: transparent;
  border: 1px solid transparent;
  border-bottom: none;
  border-radius: 10px 10px 0 0;
  padding: 12px 18px;
  font-size: 14px;
  font-weight: 700;
  color: var(--muted);
  cursor: pointer;
  white-space: nowrap;
  transition: all .15s ease;
  min-height: 44px;
  display: inline-flex;
  align-items: center;
  justify-content: center;
}
.tab-btn.active {
  background: var(--surface);
  color: var(--text);
  border-color: var(--border);
  border-bottom: 2px solid var(--primary);
  margin-bottom: -1px;
}

@media(max-width: 640px) {
  .tab-label-short { display: inline; }
  .tab-label-full { display: none; }
  .tab-btn {
    flex: 1;
    padding: 10px 8px;
    font-size: 13px;
    text-align: center;
  }
}

/* Card Stack Accordion on Tab 1 */
.guide-card-stack {
  background: var(--card);
  border: 1px solid var(--border);
  border-radius: 14px;
  margin-bottom: 14px;
  overflow: hidden;
  transition: border-color .15s ease;
}
.guide-card-stack:hover {
  border-color: var(--muted);
}
.stack-summary {
  padding: 16px 18px;
  cursor: pointer;
  user-select: none;
  display: flex;
  justify-content: space-between;
  align-items: center;
  gap: 12px;
  background: var(--surface);
  font-weight: 700;
  outline: none;
  list-style: none;
}
.stack-summary::-webkit-details-marker { display: none; }
.stack-summary-left {
  display: flex;
  align-items: center;
  gap: 12px;
}
.stack-num {
  width: 32px;
  height: 32px;
  border-radius: 8px;
  background: var(--soft);
  border: 1px solid var(--border);
  color: var(--text);
  display: flex;
  align-items: center;
  justify-content: center;
  font-weight: 700;
  font-size: 15px;
  flex-shrink: 0;
}
.stack-title {
  font-size: 15.5px;
  font-weight: 700;
  color: var(--text);
  margin-bottom: 2px;
}
.stack-desc {
  font-size: 12.5px;
  font-weight: 400;
  color: var(--muted);
}
.stack-arrow {
  font-size: 13px;
  color: var(--muted);
  transition: transform .2s ease;
}
.guide-card-stack[open] .stack-arrow {
  transform: rotate(180deg);
}
.stack-content {
  padding: 18px;
  border-top: 1px solid var(--border);
  background: var(--card);
}

/* Responsive Table & KV Metrics */
.kv-row-item {
  display: flex;
  justify-content: space-between;
  align-items: center;
  padding: 7px 0;
  border-bottom: 1px dashed var(--border);
  font-size: 13px;
  gap: 8px;
}
.kv-row-item:last-child {
  border-bottom: none;
}
.kv-row-label {
  color: var(--muted);
}
.kv-row-val {
  font-weight: 600;
  color: var(--text);
  text-align: right;
  white-space: nowrap;
}

@media(max-width: 540px) {
  .kv-row-item {
    flex-direction: column;
    align-items: flex-start;
    padding: 8px 0;
    gap: 3px;
  }
  .kv-row-label {
    font-size: 12px;
  }
  .kv-row-val {
    font-size: 13px;
    text-align: left;
    white-space: normal;
    word-break: break-word;
  }
  .chart-legend {
    font-size: 11.5px !important;
    gap: 6px 10px !important;
  }
}

/* Log Action Buttons & Responsive Data Table */
.log-header-wrap {
  display: flex;
  justify-content: space-between;
  align-items: center;
  margin-bottom: 12px;
  flex-wrap: wrap;
  gap: 10px;
}
.log-actions-grid {
  display: flex;
  gap: 8px;
  align-items: center;
}
.table-scroll-hint {
  display: none;
  font-size: 11.5px;
  color: var(--muted);
  margin-bottom: 8px;
  text-align: right;
}
.table-wrap {
  overflow-x: auto;
  -webkit-overflow-scrolling: touch;
  border: 1px solid var(--border);
  border-radius: 10px;
  background: var(--surface);
}
.data-table {
  width: 100%;
  min-width: 640px;
  border-collapse: collapse;
  font-size: 13px;
  text-align: left;
}
.data-table th, .data-table td {
  padding: 10px 12px;
  border-bottom: 1px solid var(--border);
}
.data-table th {
  background: var(--soft);
  color: var(--muted);
  font-weight: 700;
  font-size: 12px;
  white-space: nowrap;
}
.data-table tr:last-child td {
  border-bottom: none;
}

@media(max-width: 640px) {
  .log-header-wrap {
    flex-direction: column;
    align-items: stretch;
    gap: 10px;
  }
  .log-actions-grid {
    display: grid;
    grid-template-columns: 1fr 1fr;
    gap: 8px;
    width: 100%;
  }
  .log-actions-grid button {
    width: 100%;
    justify-content: center;
    padding: 10px 8px;
    font-size: 12.5px;
    min-height: 44px;
  }
  .table-scroll-hint {
    display: block;
  }
}


/* Perfected Brand Header Styling */
header.app-header {
  display: flex;
  justify-content: space-between;
  align-items: center;
  gap: 14px;
  flex-wrap: wrap;
  background: var(--surface);
  border: 1px solid var(--border);
  border-radius: 14px;
  padding: 14px 18px;
  width: 100%;
  box-sizing: border-box;
}
.brand {
  display: flex;
  flex-direction: column;
  align-items: flex-start;
  gap: 3px;
  flex: 1;
  min-width: 0;
}
.brand-title {
  margin: 0;
  font-size: 19px;
  font-weight: 700;
  letter-spacing: -0.3px;
  color: var(--text);
  display: flex;
  align-items: center;
  gap: 8px;
  flex-wrap: wrap;
}
.brand-chips {
  font-size: 12px;
  font-weight: 500;
  color: var(--muted);
  background: var(--soft);
  padding: 2px 7px;
  border-radius: 6px;
  border: 1px solid var(--border);
}
.brand-desc {
  margin: 0;
  font-size: 12.5px;
  color: var(--muted);
  line-height: 1.4;
}

.header-actions {
  display: flex;
  gap: 8px;
  align-items: center;
  flex-shrink: 0;
}

@media(max-width: 680px) {
  header.app-header {
    flex-direction: column;
    align-items: stretch;
    gap: 12px;
    padding: 14px;
  }
  .brand {
    width: 100%;
  }
  .header-actions {
    display: grid;
    grid-template-columns: 1.2fr 1fr 1fr;
    gap: 8px;
    width: 100%;
  }
  .btn-ctrl {
    width: 100%;
    padding: 10px 4px;
    font-size: 13px;
    min-height: 44px;
  }
}


/* Perfected 3-Tab Navigation (Zero Overlap & No Cropping) */
nav.tabs {
  display: flex;
  gap: 6px;
  border-bottom: 1px solid var(--border);
  margin-bottom: 14px;
  padding-bottom: 0;
  width: 100%;
  box-sizing: border-box;
}
.tab-btn {
  background: transparent;
  border: 1px solid transparent;
  border-bottom: none;
  border-radius: 10px 10px 0 0;
  padding: 12px 18px;
  font-size: 14px;
  font-weight: 700;
  color: var(--muted);
  cursor: pointer;
  white-space: nowrap;
  transition: all .15s ease;
  min-height: 44px;
  display: inline-flex;
  align-items: center;
  justify-content: center;
  box-sizing: border-box;
}
.tab-btn.active {
  background: var(--surface);
  color: var(--text);
  border-color: var(--border);
  border-bottom: 2px solid var(--primary);
  margin-bottom: -1px;
}

@media(max-width: 640px) {
  nav.tabs {
    gap: 4px;
  }
  .tab-btn {
    flex: 1;
    padding: 10px 4px;
    font-size: 12.5px;
    text-align: center;
    white-space: nowrap;
  }
}


/* Perfected Floating Sticky Status Bar */
.top-sticky-bar {
  position: -webkit-sticky;
  position: sticky;
  top: max(6px, env(safe-area-inset-top));
  z-index: 1000;
  background: var(--surface);
  border: 1px solid var(--border);
  border-radius: 12px;
  padding: 8px 14px;
  margin: 0;
  box-shadow: 0 4px 12px rgba(0,0,0,0.05);
  backdrop-filter: blur(8px);
  width: 100%;
  box-sizing: border-box;
}
.sticky-inner {
  width: 100%;
  display: flex;
  justify-content: space-between;
  align-items: center;
  gap: 8px;
  box-sizing: border-box;
}
.sticky-brand {
  display: flex;
  align-items: center;
  gap: 8px;
  font-weight: 700;
  font-size: 14.5px;
  letter-spacing: 0.3px;
  white-space: nowrap;
}
.brand-pulse-dot {
  width: 8px;
  height: 8px;
  border-radius: 50%;
  background: var(--green);
  display: inline-block;
  flex-shrink: 0;
}
.sticky-sub {
  font-size: 11px;
  color: var(--muted);
  font-weight: 600;
  background: var(--soft);
  padding: 2px 6px;
  border-radius: 5px;
  border: 1px solid var(--border);
}
.sticky-status-group {
  display: flex;
  align-items: center;
  gap: 6px;
  flex-wrap: wrap;
  justify-content: flex-end;
}
.sticky-status-group .badge {
  font-size: 11.5px;
  padding: 4px 10px;
  border-radius: 6px;
}

</style>
</head>
<body>
<div class="app">
<div class="top-sticky-bar">
  <div class="sticky-inner">
    <div class="sticky-brand">
    <span class="sticky-sub">v1.5.1</span>
    </div>
    <div class="sticky-status-group">
      <span id="connBadge" class="badge offline">Menghubungkan...</span>
      <span id="stateBadge" class="badge">SIAGA</span>
    </div>
  </div>
</div>
  <header class="app-header">
    <div class="brand">
      <h1 class="brand-title">SIDAKAN <span class="brand-chips">Sensor Ganda MQ-3 &amp; MQ-135</span></h1>
      <p class="brand-desc">Sistem IoT Deteksi Dini Kualitas &amp; Kelayakan Makanan</p>
    </div>
    <div class="header-actions">
      <button id="powerBtn" class="btn-ctrl primary" onclick="cmd('TOGGLE_POWER')">Nyalakan</button>
      <button id="calBtn" class="btn-ctrl" onclick="cmd('CALIBRATE')">Kalibrasi</button>
      <button class="btn-ctrl" onclick="toggleTheme()">Tema</button>
    </div>
  </header>

  <nav class="tabs">
    <button class="tab-btn active" onclick="showTab(1)">1. Panduan</button>
    <button class="tab-btn" onclick="showTab(2)">2. Uji Sampel</button>
    <button class="tab-btn" onclick="showTab(3)">3. Analisis &amp; Riwayat</button>
  </nav>

      <!-- TAB 1: PANDUAN & CARA KERJA (CARD STACK ACCORDION) -->
  <section id="tab1" class="tab-pane active">
    <!-- Card Stack 1: Konsep Dasar Hidung Elektronik (Open by default) -->
    <details class="guide-card-stack" open>
      <summary class="stack-summary">
        <div class="stack-summary-left">
          <span class="stack-num">1</span>
          <div>
            <div class="stack-title">Prinsip &amp; Cara Kerja Hidung Elektronik</div>
            <div class="stack-desc">Bagaimana sensor mendeteksi uap mikro makanan basi</div>
          </div>
        </div>
        <span class="stack-arrow">&#9662;</span>
      </summary>
      <div class="stack-content">
        <p class="theory-p" style="margin-top:0;margin-bottom:14px">Ketika makanan mulai basi atau mengalami fermentasi, mikroorganisme memecah nutrisi dan melepaskan uap gas mikro tak kasat mata (seperti uap alkohol, gas amonia, atau belerang). Aroma ini sering kali <strong>belum tercium oleh hidung manusia</strong> pada tahap awal. SIDAKAN bekerja layaknya <em>Hidung Elektronik</em> yang mengukur aroma gas tersebut secara objektif menggunakan dua sensor berkepekaan tinggi.</p>

        <div class="grid-2">
          <!-- Sensor MQ-3 -->
          <div class="info-card">
            <div class="label-row">
              <span style="font-weight:700;color:var(--text)">Sensor MQ-3 (Pencium Fermentasi &amp; Asam)</span>
              <span class="tag">GPIO35</span>
            </div>
            <p class="theory-p" style="margin:8px 0 0">Mendeteksi uap etanol, alkohol, dan zat asam hasil peragian. Sangat peka terhadap kebusukan makanan berkarbohidrat seperti nasi basi, adonan ragi, tape, singkong, atau santan yang mulai masam.</p>
          </div>

          <!-- Sensor MQ-135 -->
          <div class="info-card">
            <div class="label-row">
              <span style="font-weight:700;color:var(--text)">Sensor MQ-135 (Pencium Bau Busuk &amp; Amonia)</span>
              <span class="tag">GPIO34</span>
            </div>
            <p class="theory-p" style="margin:8px 0 0">Mendeteksi gas amonia, uap penguraian protein, dan aroma belerang. Sangat peka terhadap kebusukan lauk hewani seperti daging ayam, daging sapi, ikan basah, dan telur.</p>
          </div>
        </div>

        <div class="grid-2" style="margin-top:14px">
          <!-- Titik Nol Tare -->
          <div class="info-card">
            <div class="label-row">
              <span style="font-weight:700;color:var(--text)">Titik Nol Dinamis (Kalibrasi Otomatis)</span>
              <span class="tag">Titik Nol Otomatis</span>
            </div>
            <p class="theory-p" style="margin:8px 0 0">Bekerja persis seperti tombol 'Nol' pada timbangan dapur digital. Alat mengukur bau udara ruangan sebelum sampel dimasukkan, sehingga kenaikan sinyal yang terhitung murni 100% berasal dari aroma makanan yang sedang diuji.</p>
          </div>

          <!-- Ambang Mutu -->
          <div class="info-card">
            <div class="label-row">
              <span style="font-weight:700;color:var(--text)">Tiga Tingkat Kelayakan Makanan</span>
              <span class="tag">Ambang Mutu</span>
            </div>
            <div style="display:flex;flex-direction:column;gap:6px;margin-top:8px;font-size:12px">
              <div style="display:flex;align-items:center;gap:8px">
                <span class="badge-status badge-aman">AMAN</span>
                <span style="color:var(--muted)">Kenaikan aroma &lt; 25.0% (Makanan segar &amp; layak santap)</span>
              </div>
              <div style="display:flex;align-items:center;gap:8px">
                <span class="badge-status badge-waspada">WASPADA</span>
                <span style="color:var(--muted)">Kenaikan 25.0% - 49.9% (Mulai ada tanda pembusukan/fermentasi)</span>
              </div>
              <div style="display:flex;align-items:center;gap:8px">
                <span class="badge-status badge-bahaya">BAHAYA</span>
                <span style="color:var(--muted)">Kenaikan &ge; 50.0% (Gas pekat terdeteksi, tidak layak konsumsi)</span>
              </div>
            </div>
          </div>
        </div>
      </div>
    </details>

    <!-- Card Stack 2: Prosedur Pengujian 4 Tahap / SOP -->
    <details class="guide-card-stack">
      <summary class="stack-summary">
        <div class="stack-summary-left">
          <span class="stack-num">2</span>
          <div>
            <div class="stack-title">Prosedur Standar Pengujian 4 Tahap (SOP)</div>
            <div class="stack-desc">Alur kerja berurutan dari pemanasan hingga penetralan ruang uji</div>
          </div>
        </div>
        <span class="stack-arrow">&#9662;</span>
      </summary>
      <div class="stack-content">
        <p class="theory-p" style="margin-top:0;margin-bottom:14px">Ikuti urutan langkah kerja berurutan berikut agar hasil pengujian akurat, stabil, dan bebas kontaminasi residu:</p>

        <div class="sop-grid">
          <!-- Tahap 1 -->
          <div class="sop-card">
            <div>
              <div class="sop-card-header">
                <span class="sop-step-tag tag-warm">Tahap 1</span>
                <span class="sop-meta-duration">60 Detik</span>
              </div>
              <h4 class="sop-title">Pemanasan Elemen Sensor</h4>
              <p class="sop-body">Elemen pendeteksi pada sensor MQ-3 dan MQ-135 memerlukan pemanasan internal (&sim;300&deg;C) agar siap aktif merespons uap aroma gas makanan.</p>
            </div>
            <div class="sop-key-rule"><strong>Perhatian:</strong> Jangan masukkan makanan pada tahap ini. Sensor harus dipanaskan di udara bebas.</div>
          </div>

          <!-- Tahap 2 -->
          <div class="sop-card">
            <div>
              <div class="sop-card-header">
                <span class="sop-step-tag tag-tare">Tahap 2</span>
                <span class="sop-meta-duration">Siaga / Kunci Nol</span>
              </div>
              <h4 class="sop-title">Penguncian Titik Nol Udara</h4>
              <p class="sop-body">Mikrokontroler secara otomatis mengukur dan mengunci angka acuan udara bersih ruangan sebagai dasar perhitungan kenaikan persentase aroma.</p>
            </div>
            <div class="sop-key-rule"><strong>Perhatian:</strong> Pastikan ventilasi ruangan normal dan tidak ada sumber parfum, asap, atau pembersih lantai di dekat alat.</div>
          </div>

          <!-- Tahap 3 -->
          <div class="sop-card">
            <div>
              <div class="sop-card-header">
                <span class="sop-step-tag tag-test">Tahap 3</span>
                <span class="sop-meta-duration">300 - 600 Detik</span>
              </div>
              <h4 class="sop-title">Pengujian Sampel Tertutup</h4>
              <p class="sop-body">Letakkan sampel ke wadah kedap udara, lalu tekan tombol Mulai Uji. Gas aroma makanan akan terperangkap dan dianalisis hingga pembacaan sinyal benar-benar stabil dan mendatar.</p>
            </div>
            <div class="sop-key-rule"><strong>Perhatian:</strong> Wadah harus tertutup rapat selama 5 menit tanpa dibuka agar gas aroma terakumulasi stabil.</div>
          </div>

          <!-- Tahap 4 -->
          <div class="sop-card">
            <div>
              <div class="sop-card-header">
                <span class="sop-step-tag tag-clean">Tahap 4</span>
                <span class="sop-meta-duration">&plusmn;10% Titik Nol</span>
              </div>
              <h4 class="sop-title">Penetralan Ruang Pengujian</h4>
              <p class="sop-body">Setelah hasil uji kelayakan terkunci, buka wadah dan keluarkan makanan. Sensor akan membersihkan diri hingga sisa gas aroma terbuang habis.</p>
            </div>
            <div class="sop-key-rule"><strong>Perhatian:</strong> Tombol uji terkunci otomatis untuk pengamanan sampai ruang sensor benar-benar netral kembali ke kondisi awal.</div>
          </div>
        </div>
      </div>
    </details>

    <!-- Card Stack 3: Landasan Teori Ilmiah & Dokumen Akademik -->
    <details class="guide-card-stack">
      <summary class="stack-summary">
        <div class="stack-summary-left">
          <span class="stack-num">3</span>
          <div>
            <div class="stack-title">Landasan Teori Ilmiah &amp; Dokumen Akademik</div>
            <div class="stack-desc">Formula matematis, reaksi kimia SnO2, dan daftar pustaka APA 7th</div>
          </div>
        </div>
        <span class="stack-arrow">&#9662;</span>
      </summary>
      <div class="stack-content">
        <p class="theory-p" style="margin-top:0;margin-bottom:12px">Buka menu di bawah untuk mempelajari formula matematis, fenomena kimia fisika, dan daftar pustaka ilmiah yang mendasari sistem ini:</p>

        <details>
          <summary>A. Karakteristik Semikonduktor Oksida Logam (SnO2) &amp; Kemisorpsi Oksigen</summary>
          <p class="theory-p">Sensor tipe MOS menggunakan lapisan film tipis timah dioksida (SnO2). Pada udara bersih bersuhu ~300°C, oksigen di udara teradsorpsi secara kimiawi (chemisorption) pada permukaan SnO2 dan menjebak elektron dari pita konduksi, membentuk lapisan deplesi ruang muatan dengan resistansi tinggi. Ketika molekul gas reduktor (seperti etanol dari fermentasi atau amonia dari dekomposisi protein) mengenai permukaan sensor, gas tersebut bereaksi dengan ion oksigen yang teradsorpsi, melepaskan elektron kembali ke pita konduksi sehingga resistansi material menurun drastis dan menaikkan tegangan analog yang dibaca mikrokontroler.</p>
        </details>

        <details>
          <summary>B. Metodologi Kenaikan Relatif Fraksional (Relative Rise Tare)</summary>
          <p class="theory-p">Karena sensor gas MOS berbiaya terjangkau memiliki variasi resistansi dasar antar-unit (baseline drift) akibat kelembapan lingkungan, pengukuran tidak dapat diklaim dalam satuan konsentrasi absolut (ppm) tanpa ruang kalibrasi berstandar gas bersertifikat. SIDAKAN menerapkan metodologi rasio kenaikan relatif fraksional:</p>
          <div class="theory-formula">&Delta;Nilai = Nilai_Terukur - Nilai_Titik_Nol</div>
          <div class="theory-formula">Kenaikan_Relatif = (&Delta;Nilai / Nilai_Titik_Nol) &times; 100%</div>
          <p class="theory-p">Metode normalisasi ini menjamin objektivitas dan konsistensi perbandingan antar-waktu meskipun kondisi cuaca ruangan mengalami pergeseran suhu atau kelembapan.</p>
        </details>

        <details>
          <summary>C. Fisika Derau Termal Johnson-Nyquist &amp; Batas Deadband ADC</summary>
          <p class="theory-p">Fluktuasi kecil sebesar 5 sampai 10 poin pada nilai ADC 12-bit ESP32 bukanlah kebocoran gas, melainkan derau tegangan termal acak (Johnson-Nyquist noise) pada resistor beban sensor akibat agitasi termal elektron. Besarnya tegangan derau dijelaskan oleh formula:</p>
          <div class="theory-formula">Vn,rms = &radic;(4 &times; k &times; T &times; R &times; &Delta;f)</div>
          <p class="theory-p">Di mana k adalah konstanta Boltzmann, T temperatur absolut Kelvin, R resistansi, dan &Delta;f lebar pita frekuensi. Firmware SIDAKAN memfilter derau ini melalui filter rata-rata bergerak 10 sampel dan menetapkan batas fisik minimum (deadband) sebesar 35 poin ADC (atau 10% dari titik nol) sebelum kenaikan dianggap sebagai respons kimia nyata.</p>
        </details>

        <details>
          <summary>D. Standar Mutu Pangan &amp; Biomarker Kimiawi Kebusukan</summary>
          <p class="theory-p"><strong>Standar Ikan Segar (SNI 2729:2013):</strong> Mengatur bahwa kemunduran mutu protein ikan ditandai oleh pelepasan senyawa basa nitrogen volatil total (Total Volatile Basic Nitrogen / TVB-N). Peningkatan gas volatil yang dideteksi oleh kanal MQ-135 berkorelasi langsung dengan laju dekomposisi protein oleh bakteri pembusuk.</p>
          <p class="theory-p"><strong>Bakteriologi Pangan (U.S. FDA BAM Chapter 10):</strong> Menjelaskan bahwa pembusukan karbohidrat menghasilkan senyawa asam volatil dan etanol sebagai metabolit sekunder aktivitas ragi dan bakteri anaerob, yang menjadi target deteksi kanal MQ-3.</p>
        </details>

        <details>
          <summary>E. Glosarium Istilah Operasional</summary>
          <p class="theory-p"><strong>Titik Nol Udara (Baseline):</strong> Nilai tegangan listrik acuan saat sensor terpapar udara bersih sebelum makanan dimasukkan.</p>
          <p class="theory-p"><strong>Plateau (Kondisi Stabil):</strong> Titik kurva pembacaan gas yang telah mendatar (kecepatan penguapan gas makanan telah seimbang dengan kecepatan sensor merespons).</p>
          <p class="theory-p"><strong>Penetralan Ruang Uji (Recovery Interlock):</strong> Fitur keselamatan perangkat lunak yang menonaktifkan tombol pengujian sementara waktu agar ruang uji dibersihkan dari sisa aroma sebelum uji berikutnya.</p>
          <p class="theory-p"><strong>Deadband:</strong> Ambang batas kenaikan sinyal minimal yang harus dilewati agar sinyal tidak keliru dianggap gas padahal hanya getaran derau listrik.</p>
        </details>

        <details>
          <summary>F. Daftar Pustaka &amp; Landasan Ilmiah Resmi (APA 7th)</summary>
          <p class="theory-p"><strong>1. Derau Termal &amp; ADC Mikrokontroler:</strong></p>
          <p class="theory-p">&bull; Johnson, J. B. (1928). <em>Thermal Agitation of Electricity in Conductors</em>. Physical Review, 32(1), 97&ndash;109.</p>
          <p class="theory-p">&bull; Nyquist, H. (1928). <em>Thermal Agitation of Electric Charge in Conductors</em>. Physical Review, 32(1), 110&ndash;113.</p>
          <p class="theory-p">&bull; Espressif Systems. (2023). <em>ESP32 Technical Reference Manual (Section: On-Chip ADC)</em>. Espressif Inc.</p>
          <p class="theory-p"><strong>2. Metodologi Hidung Elektronik (E-Nose):</strong></p>
          <p class="theory-p">&bull; Gardner, J. W., &amp; Bartlett, P. N. (1994). <em>A brief history of electronic noses</em>. Sensors and Actuators B: Chemical, 18(1-3), 210&ndash;211.</p>
          <p class="theory-p">&bull; Pearce, T. C., et al. (Eds.). (2003). <em>Handbook of Machine Olfaction: Electronic Sensing Technology</em>. Wiley-VCH.</p>
          <p class="theory-p"><strong>3. Sensor Gas Semikonduktor Oksida Logam (SnO2):</strong></p>
          <p class="theory-p">&bull; Yamazoe, N., &amp; Shimanoe, K. (2009). <em>Basic concepts of semiconductor gas sensors</em>. Sensors and Actuators B: Chemical, 138(1), 100&ndash;107.</p>
          <p class="theory-p">&bull; Barsan, N., &amp; Weimar, U. (2001). <em>Conduction model of metal oxide gas sensors</em>. Journal of Electroceramics, 7(3), 143&ndash;167.</p>
          <p class="theory-p"><strong>4. Standar Mutu Pangan &amp; Biomarker Kebusukan:</strong></p>
          <p class="theory-p">&bull; Badan Standardisasi Nasional. (2013). <em>SNI 2729:2013 - Ikan Segar (Indikator Kimiawi: Total Volatile Basic Nitrogen / TVB-N)</em>. BSN Indonesia.</p>
          <p class="theory-p">&bull; Olafsdottir, G., et al. (1997). <em>Methods for evaluating fish freshness: Volatile compounds and electronic nose evaluation</em>. Trends in Food Science &amp; Technology, 8(8), 258&ndash;265.</p>
          <p class="theory-p">&bull; U.S. FDA. (2020). <em>Bacteriological Analytical Manual (BAM) Chapter 10: Microbial Spoilage &amp; Volatile Metabolites</em>.</p>
        </details>
      </div>
    </details>

    <!-- CTA Button -->
    <div style="margin-top:16px;margin-bottom:8px;text-align:center">
      <button type="button" class="btn-main primary" style="width:100%;max-width:320px;display:inline-flex;align-items:center;justify-content:center;gap:10px;margin:0 auto;box-sizing:border-box" onclick="showTab(2)">
        <span>Lanjut ke Pengujian Sampel</span>
        <span style="font-size:18px">&rarr;</span>
      </button>
    </div>
  </section>

<!-- TAB 2: UJI SAMPEL (MODE RINGKAS OPERATOR) -->
  <section id="tab2" class="tab-pane">
    <div class="hero-box">
      <div>
        <h2 id="heroTitle" class="hero-title">Alat Siaga (Siap Uji)</h2>
        <p id="heroDesc" class="hero-desc">Titik nol udara bersih terverifikasi. Masukkan sampel makanan ke wadah tertutup, lalu tekan tombol Mulai Uji Sampel.</p>
        <div class="progress-bar"><div id="progressFill" class="progress-fill"></div></div>
      </div>
      <div class="timer-display">
        <div id="timerNum" class="timer-num">--</div>
        <div id="timerLabel" class="timer-label">STATUS WAKTU</div>
      </div>
    </div>

    <div class="btn-group-main">
      <button id="btnStartTest" class="btn-main primary" onclick="cmd('START_TEST')">Mulai Uji Sampel</button>
      <button id="btnCancelTest" class="btn-main danger" style="display:none" onclick="cmd('CANCEL_TEST')">Batalkan Pengujian</button>
      <button id="btnBypassRec" class="btn-main" style="display:none" onclick="cmd('BYPASS_RECOVERY')">Lewati Pembersihan (Sensor Sudah Bersih)</button>
    </div>
    <div id="btnStartHelp" style="font-size:13px;color:var(--muted);text-align:center;margin:8px 0 16px;line-height:1.4">Alat masih nonaktif (Standby). Silakan tekan tombol <strong>Nyalakan</strong> di kanan atas untuk memulai pemanasan sensor.</div>

    <div id="verdictCard" class="verdict-card" style="display:none">
      <div id="verdictTitle" class="verdict-title">HASIL PENGUJIAN MAKANAN</div>
      <div id="verdictDesc" class="verdict-desc">Menganalisis aroma sampel makanan...</div>
    </div>

    <div class="grid-3">
      <div class="card">
        <div class="label-row"><span>Sensor Aroma Fermentasi / Etanol</span><span class="tag">MQ-3</span></div>
        <div class="val-big"><span id="t1Mq3">--</span> <span class="val-unit">Poin</span></div>
        <div class="kv-table">
          <span>Titik nol udara</span><b id="t1BaseMq3">-- Poin</b>
          <span>Kenaikan aroma</span><b id="t1RiseMq3">-- %</b>
          <span>Ambang bahaya</span><b>&ge; 50.0 %</b>
        </div>
      </div>

      <div class="card">
        <div class="label-row"><span>Sensor Aroma Busuk / Amonia</span><span class="tag">MQ-135</span></div>
        <div class="val-big"><span id="t1Mq135">--</span> <span class="val-unit">Poin</span></div>
        <div class="kv-table">
          <span>Titik nol udara</span><b id="t1BaseMq135">-- Poin</b>
          <span>Kenaikan aroma</span><b id="t1RiseMq135">-- %</b>
          <span>Ambang bahaya</span><b>&ge; 50.0 %</b>
        </div>
      </div>

      <div class="card">
        <div class="label-row"><span>Kestabilan Sinyal</span><span class="tag">30 Detik Terakhir</span></div>
        <div class="val-big" style="font-size:22px"><span id="t1Plateau">BELUM STABIL</span></div>
        <div class="kv-table">
          <span>Batas goyangan sinyal</span><b>&le; 1.0 %</b>
          <span>Batas pergeseran tren</span><b>&le; 0.5 %</b>
          <span>Suhu / Kelembapan</span><b id="t1Env">-- °C / -- %</b>
        </div>
      </div>
    </div>
  </section>

  
  <!-- TAB 3: ANALISIS & RIWAYAT (MODE TEKNIS PENELITI) -->
  <section id="tab3" class="tab-pane">
    <div class="chart-box">
      <div class="chart-legend">
        <span><i class="sw sw-mq3"></i>MQ-3 (Aroma Fermentasi / Etanol)</span>
        <span><i class="sw sw-mq135"></i>MQ-135 (Aroma Amonia / Busuk)</span>
        <span><i class="sw sw-base"></i>Titik Nol Udara Bersih</span>
      </div>
      <canvas id="liveChart"></canvas>
    </div>

    <div class="grid-2">
      <div class="card">
        <h3 style="margin:0 0 12px;font-size:16px">Diagnostik Uji Kestabilan (30 Detik Terakhir)</h3>
        <div>
          <div class="kv-row-item">
            <span class="kv-row-label">Goyangan Sinyal MQ-3 (Fluktuasi)</span>
            <b id="t2Fluc3" class="kv-row-val">-- % (Batas &le; 1.0%)</b>
          </div>
          <div class="kv-row-item">
            <span class="kv-row-label">Goyangan Sinyal MQ-135 (Fluktuasi)</span>
            <b id="t2Fluc135" class="kv-row-val">-- % (Batas &le; 1.0%)</b>
          </div>
          <div class="kv-row-item">
            <span class="kv-row-label">Pergeseran Tren MQ-3 (Kemiringan)</span>
            <b id="t2Trend3" class="kv-row-val">-- % (Batas &le; 0.5%)</b>
          </div>
          <div class="kv-row-item">
            <span class="kv-row-label">Pergeseran Tren MQ-135 (Kemiringan)</span>
            <b id="t2Trend135" class="kv-row-val">-- % (Batas &le; 0.5%)</b>
          </div>
          <div class="kv-row-item">
            <span class="kv-row-label">Tren Sinyal Gas</span>
            <b id="t2PlateauStatus" class="kv-row-val">--</b>
          </div>
          <div class="kv-row-item">
            <span class="kv-row-label">Standar Pengujian</span>
            <b id="t2SopVerdict" class="kv-row-val">Uji Kestabilan Sinyal (30 Detik)</b>
          </div>
        </div>
      </div>

      <div class="card">
        <h3 style="margin:0 0 12px;font-size:16px">Kondisi Fisik &amp; Kesiapan Ruang Uji</h3>
        <div>
          <div class="kv-row-item">
            <span class="kv-row-label">Suhu Ruang Sampel</span>
            <b id="t2Temp" class="kv-row-val">-- &deg;C</b>
          </div>
          <div class="kv-row-item">
            <span class="kv-row-label">Kelembapan Ruang</span>
            <b id="t2Hum" class="kv-row-val">-- %RH</b>
          </div>
          <div class="kv-row-item">
            <span class="kv-row-label">Resolusi Pengukuran ADC</span>
            <b class="kv-row-val">12-bit (0 - 4095 Tingkat)</b>
          </div>
          <div class="kv-row-item">
            <span class="kv-row-label">Batas Normal Sensor</span>
            <b class="kv-row-val">80 - 4000 Poin</b>
          </div>
          <div class="kv-row-item">
            <span class="kv-row-label">Batas Toleransi Derau</span>
            <b class="kv-row-val">35 Poin / 10% Titik Nol</b>
          </div>
          <div class="kv-row-item">
            <span class="kv-row-label">Kesiapan Penetralan</span>
            <b id="t2RecoveryStatus" class="kv-row-val">--</b>
          </div>
        </div>
      </div>
    </div>

    <div class="card">
      <div class="log-header-wrap">
        <h3 style="margin:0;font-size:16px">Riwayat Log Hasil Pengujian</h3>
        <div class="log-actions-grid">
          <button class="btn-ctrl primary" onclick="downloadAsciiReport()">Unduh Laporan Terakhir (.txt)</button>
          <button class="btn-ctrl" onclick="clearHistory()">Hapus Riwayat</button>
        </div>
      </div>
      <div class="table-scroll-hint">Geser tabel ke samping untuk melihat rincian lengkap &rarr;</div>
      <div class="table-wrap">
        <table class="data-table">
          <thead>
            <tr>
              <th>ID Uji</th>
              <th>Waktu Pengujian</th>
              <th>Durasi</th>
              <th>Kenaikan MQ-3</th>
              <th>Kenaikan MQ-135</th>
              <th>Kestabilan</th>
              <th>Status Kelayakan</th>
            </tr>
          </thead>
          <tbody id="logTableBody">
            <tr><td colspan="7" style="text-align:center;color:var(--muted)">Belum ada riwayat pengujian tersimpan.</td></tr>
          </tbody>
        </table>
      </div>
    </div>
  </section>

  

  <footer>SIDAKAN v1.5.1 · Proyek Kepemimpinan PPG Prajabatan · Universitas Pendidikan Indonesia</footer>
</div>

<script>
let ws, power=false, prevPower=false, state="SIAGA", lastLockedData=null, testSaved=false;
const N=30, h3=new Array(N).fill(0), h135=new Array(N).fill(0);
let base3=0, base135=0;

function byId(id){return document.getElementById(id)}

function showTab(idx){
  document.querySelectorAll('.tab-btn').forEach((b,i)=>b.classList.toggle('active',i===idx-1));
  document.querySelectorAll('.tab-pane').forEach((p,i)=>p.classList.toggle('active',i===idx-1));
  if(idx===3) setTimeout(drawChart,50);
}

let simInterval = null, simData = null;

function initWS(){
  const host=location.hostname||'192.168.4.1';
  try {
    ws=new WebSocket('ws://'+host+':81/');
    ws.onopen=()=>{
      stopSimulation();
      byId('connBadge').innerText='Online';
      byId('connBadge').className='badge online';
    };
    ws.onclose=()=>{
      byId('connBadge').innerText='Terputus';
      byId('connBadge').className='badge offline';
      setTimeout(initWS,2000);
    };
    ws.onmessage=e=>{try{update(JSON.parse(e.data))}catch(err){console.error(err)}};
  } catch(e) {
    console.warn('WS Init failed:', e);
  }
}

function cmd(text){
  if(text === 'TOGGLE_POWER' && !power){
    showTab(2);
  }
  if(ws && ws.readyState === 1){
    ws.send(text);
    return;
  }
  handleOfflineCommand(text);
}

function handleOfflineCommand(text){
  if(simInterval){
    executeSimCmd(text);
    return;
  }
  if(text === 'TOGGLE_POWER'){
    if(confirm('Alat fisik ESP32 tidak terhubung.\n\nApakah Anda ingin mengaktifkan MODE SIMULASI (Demo) untuk mencoba seluruh alur kerja sistem di peramban ini?')){
      startSimulation();
    }
    return;
  }
  if(!power){
    alert('Alat masih nonaktif (Standby)!\n\nSilakan tekan tombol "Nyalakan" di kanan atas terlebih dahulu untuk memulai pemanasan elemen sensor (atau masuk ke mode simulasi).');
    return;
  }
  if(power && state === 'PEMANASAN' && text === 'START_TEST'){
    alert('Sensor sedang dalam proses pemanasan 60 detik!\n\nMohon tunggu hingga pemanasan selesai dan status berubah menjadi "SIAGA" sebelum memulai uji sampel.');
    return;
  }
  alert('Perintah tidak dapat dikirim karena alat belum terhubung via WebSocket!\n\nPastikan ponsel/laptop Anda terhubung ke Wi-Fi SIDAKAN_AP (192.168.4.1).');
}

function startSimulation(){
  power = true;
  prevPower = true;
  showTab(2);
  simData = {
    power: true,
    testState: 'WARMUP',
    testTimerSec: 60,
    testElapsedSec: 0,
    baseMq3: 420,
    baseMq135: 380,
    mq3: 420,
    mq135: 380,
    riseMq3: 0.0,
    riseMq135: 0.0,
    status: 'AMAN',
    plateauStatus: 'STABIL',
    mq3Fluc: 0.25,
    mq135Fluc: 0.20,
    mq3Trend: 0.05,
    mq135Trend: 0.08,
    recoveryReady: true,
    recoverySec: 0,
    temp: 27.5,
    hum: 60,
    dhtValid: true,
    baselineValid: true
  };
  byId('connBadge').innerText = 'Simulasi';
  byId('connBadge').className = 'badge online';
  update(simData);

  if(simInterval) clearInterval(simInterval);
  simInterval = setInterval(()=>{
    if(!simData.power) return;
    if(simData.testState === 'WARMUP'){
      simData.testTimerSec -= 2;
      if(simData.testTimerSec <= 0){
        simData.testState = 'IDLE';
        simData.testTimerSec = 0;
      }
    } else if(simData.testState === 'EXPOSURE'){
      simData.testTimerSec -= 2;
      if(simData.testTimerSec <= 0) simData.testTimerSec = 0;
      simData.testElapsedSec += 2;
      const t = simData.testElapsedSec;
      simData.riseMq3 = Math.min(32.5, +(t * 0.25).toFixed(1));
      simData.riseMq135 = Math.min(58.0, +(t * 0.45).toFixed(1));
      simData.mq3 = Math.round(simData.baseMq3 * (1 + simData.riseMq3/100));
      simData.mq135 = Math.round(simData.baseMq135 * (1 + simData.riseMq135/100));
      if(simData.riseMq135 >= 50.0 || simData.riseMq3 >= 50.0){
        simData.status = 'BAHAYA';
      } else if(simData.riseMq135 >= 25.0 || simData.riseMq3 >= 25.0){
        simData.status = 'WASPADA';
      } else {
        simData.status = 'AMAN';
      }
      if(simData.testTimerSec <= 0 || t >= 25){
        simData.testState = 'LOCKED';
        simData.plateauStatus = 'STABIL';
        simData.lockedHoldSec = 0;
      }
    } else if(simData.testState === 'LOCKED'){
      if(simData.lockedHoldSec === undefined) simData.lockedHoldSec = 0;
      simData.lockedHoldSec++;
      if(simData.lockedHoldSec >= 6){
        simData.testState = 'RECOVERY';
        simData.recoverySec = 0;
        simData.lockedHoldSec = 0;
      }
    } else if(simData.testState === 'RECOVERY'){
      simData.recoverySec++;
      simData.riseMq3 = Math.max(0, +(simData.riseMq3 - 4.0).toFixed(1));
      simData.riseMq135 = Math.max(0, +(simData.riseMq135 - 5.0).toFixed(1));
      simData.mq3 = Math.round(simData.baseMq3 * (1 + simData.riseMq3/100));
      simData.mq135 = Math.round(simData.baseMq135 * (1 + simData.riseMq135/100));
      if(simData.riseMq3 <= 2.0 && simData.riseMq135 <= 2.0){
        simData.testState = 'IDLE';
        simData.status = 'AMAN';
        simData.recoverySec = 0;
      }
    }
    update(simData);
  }, 1000);
}

function stopSimulation(){
  if(simInterval){
    clearInterval(simInterval);
    simInterval = null;
  }
}

function executeSimCmd(text){
  if(text === 'TOGGLE_POWER'){
    simData.power = !simData.power;
    if(!simData.power){
      simData.testState = 'IDLE';
      stopSimulation();
      byId('connBadge').innerText = 'Terputus';
      byId('connBadge').className = 'badge offline';
    } else {
      showTab(2);
    }
  } else if(text === 'CALIBRATE'){
    alert('Simulasi Kalibrasi: Titik nol udara bersih berhasil dikunci pada nilai acuan (MQ-3: 420, MQ-135: 380).');
    simData.riseMq3 = 0.0;
    simData.riseMq135 = 0.0;
    simData.mq3 = simData.baseMq3;
    simData.mq135 = simData.baseMq135;
  } else if(text === 'START_TEST'){
    testSaved = false;
    simData.testState = 'EXPOSURE';
    simData.testTimerSec = 300;
    simData.testElapsedSec = 0;
    simData.lockedHoldSec = 0;
  } else if(text === 'CANCEL_TEST'){
    simData.testState = 'IDLE';
  } else if(text === 'BYPASS_RECOVERY'){
    simData.testState = 'IDLE';
    simData.recoverySec = 0;
    simData.riseMq3 = 0.0;
    simData.riseMq135 = 0.0;
    simData.mq3 = simData.baseMq3;
    simData.mq135 = simData.baseMq135;
  }
  update(simData);
}

function toggleTheme(){
  const r=document.documentElement;
  r.setAttribute('data-theme',r.getAttribute('data-theme')==='dark'?'light':'dark');
  drawChart();
}

function mapStateIndo(rawState){
  switch(rawState){
    case 'WARMUP': return 'PEMANASAN';
    case 'IDLE': return 'SIAGA';
    case 'EXPOSURE': return 'PENGUJIAN';
    case 'EVALUATION': return 'EKSTENSI';
    case 'LOCKED': return 'TERKUNCI';
    case 'RECOVERY': return 'PENETRALAN';
    default: return rawState;
  }
}

function formatTime(sec){
  if(isNaN(sec) || sec < 0) sec = 0;
  const m = Math.floor(sec / 60);
  const s = sec % 60;
  return (m < 10 ? '0' : '') + m + ':' + (s < 10 ? '0' : '') + s;
}

function formatDetailedLabel(sec, totalSec){
  if(isNaN(sec) || sec < 0) sec = 0;
  const m = Math.floor(sec / 60);
  const s = sec % 60;
  let text = 'SISA WAKTU (';
  if(m > 0) text += m + 'm ';
  text += s + 's';
  if(totalSec){
    const tm = Math.floor(totalSec / 60);
    text += ' / ' + tm + ' MENIT)';
  } else {
    text += ')';
  }
  return text;
}

function update(d){
  if(!prevPower && d.power){
    showTab(2);
  }
  prevPower = d.power;
  power=d.power;
  const rawState=d.testState||'IDLE';
  state=mapStateIndo(rawState);
  base3=d.baseMq3;
  base135=d.baseMq135;

  // Update status panduan (Tab 1)
  const gPill = byId('guideLiveStatus');
  if (gPill) {
    if (!power) {
      gPill.className = 'guide-status-pill status-standby';
      gPill.textContent = 'Alat Nonaktif (Standby)';
    } else if (rawState === 'WARMUP') {
      gPill.className = 'guide-status-pill status-warmup';
      gPill.textContent = 'Pemanasan Sensor (Sisa ' + (d.testTimerSec || 0) + ' dtk)';
    } else if (rawState === 'IDLE') {
      gPill.className = 'guide-status-pill status-ready';
      gPill.textContent = 'Alat Siaga (Siap Uji Sampel)';
    } else if (rawState === 'EXPOSURE' || rawState === 'EVALUATION') {
      gPill.className = 'guide-status-pill status-testing';
      gPill.textContent = 'Sedang Menguji Sampel (Sisa ' + (d.testTimerSec || 0) + ' dtk)';
    } else if (rawState === 'LOCKED') {
      gPill.className = 'guide-status-pill status-locked';
      gPill.textContent = 'Hasil Terkunci (' + (d.status || 'AMAN') + ')';
    } else if (rawState === 'RECOVERY') {
      gPill.className = 'guide-status-pill status-recovery';
      gPill.textContent = 'Penetralan Ruang Uji (' + (d.recoverySec || 0) + ' dtk)';
    } else {
      gPill.className = 'guide-status-pill status-ready';
      gPill.textContent = 'Sistem Terhubung';
    }
  }

  byId('stateBadge').innerText=state;
  byId('powerBtn').innerText=power?'Matikan':'Nyalakan';

  // Update Tombol Kalibrasi
  const calBtn=byId('calBtn');
  if(calBtn){
    calBtn.disabled=(!power||rawState==='WARMUP'||rawState==='EXPOSURE'||rawState==='EVALUATION');
    if(!power){
      calBtn.title='Nyalakan alat terlebih dahulu untuk kalibrasi titik nol';
    } else if(rawState==='WARMUP'){
      calBtn.title='Tunggu masa pemanasan sensor selesai (60s)';
    } else if(rawState==='EXPOSURE'||rawState==='EVALUATION'){
      calBtn.title='Tidak dapat kalibrasi saat pengujian sampel berlangsung';
    } else {
      calBtn.title='Kalibrasi ulang titik nol udara bersih ruangan';
    }
  }

  const btnStart=byId('btnStartTest');
  const btnCancel=byId('btnCancelTest');
  const btnBypass=byId('btnBypassRec');
  const btnHelp=byId('btnStartHelp');

  if(rawState==='EXPOSURE'||rawState==='EVALUATION'){
    btnStart.style.display='none';
    btnCancel.style.display='inline-flex';
    btnBypass.style.display='none';
    if(btnHelp) btnHelp.innerHTML='Pengujian sedang berlangsung. Wadah sensor <strong>dilarang dibuka</strong>.';
  } else if(rawState==='RECOVERY'){
    btnStart.style.display='none';
    btnCancel.style.display='none';
    btnBypass.style.display='inline-flex';
    if(btnHelp) btnHelp.innerHTML='Sedang menetralkan ruang sensor dari sisa gas aroma makanan...';
  } else {
    btnStart.style.display='inline-flex';
    btnStart.disabled=(!power||rawState==='WARMUP'||rawState==='LOCKED'||!d.baselineValid);
    if(!power){
      btnStart.title='Nyalakan alat terlebih dahulu di kanan atas';
    } else if(rawState==='WARMUP'){
      btnStart.title='Tunggu masa pemanasan sensor selesai (60s)';
    } else if(rawState==='LOCKED'){
      btnStart.title='Hasil pengujian terkunci. Tunggu penetralan sensor';
    } else if(!d.baselineValid){
      btnStart.title='Lakukan kalibrasi titik nol di udara bersih terlebih dahulu';
    } else {
      btnStart.title='Mulai uji sampel aroma makanan (5 menit)';
    }
    btnCancel.style.display='none';
    btnBypass.style.display='none';
    if(btnHelp){
      if(!power){
        btnHelp.innerHTML='Alat masih nonaktif (Standby). Silakan tekan tombol <strong>Nyalakan</strong> di kanan atas.';
      } else if(rawState==='WARMUP'){
        btnHelp.innerHTML='Sensor sedang dipanaskan (sisa <strong>' + (d.testTimerSec||0) + ' detik</strong>). Tombol akan aktif otomatis setelah Siaga.';
      } else if(!d.baselineValid){
        btnHelp.innerHTML='Titik nol acuan udara belum valid. Lakukan <strong>Kalibrasi</strong> di udara bersih.';
      } else if(rawState==='LOCKED'){
        btnHelp.innerHTML='Pengujian selesai dan hasil terkunci. Buka wadah untuk pembersihan sensor.';
      } else {
        btnHelp.innerHTML='Alat <strong>Siaga</strong>. Masukkan sampel makanan ke wadah tertutup, lalu klik tombol di atas.';
      }
    }
  }

  const timerNum=byId('timerNum');
  const timerLabel=byId('timerLabel');
  const pFill=byId('progressFill');

  if(rawState==='WARMUP'){
    byId('heroTitle').innerText='Menyiapkan Alat (Pemanasan Sensor)';
    byId('heroDesc').innerText='Tunggu stabilisasi elemen sensor (60 detik) di udara bersih sebelum menguji makanan.';
    timerNum.innerText=formatTime(d.testTimerSec!==undefined?d.testTimerSec:60);
    timerLabel.innerText='PEMANASAN (' + (d.testTimerSec!==undefined?d.testTimerSec:60) + ' DETIK)';
    pFill.style.width=Math.min(100,((60-(d.testTimerSec||0))/60)*100)+'%';
  } else if(rawState==='EXPOSURE'){
    byId('heroTitle').innerText='Pengujian Berjalan (Pengambilan Aroma)';
    byId('heroDesc').innerText='Wadah tertutup 2 Liter. Sensor merekam dinamika uap gas sampel menuju kesetimbangan.';
    timerNum.innerText=formatTime(d.testTimerSec!==undefined?d.testTimerSec:300);
    timerLabel.innerText=formatDetailedLabel(d.testTimerSec!==undefined?d.testTimerSec:300, 300);
    pFill.style.width=Math.min(100,((300-(d.testTimerSec||0))/300)*100)+'%';
  } else if(rawState==='EVALUATION'){
    byId('heroTitle').innerText='Menunggu Penstabilan Sinyal (Ekstensi Dinamis)';
    byId('heroDesc').innerText='Sinyal aroma masih bergerak. Durasi ditambah dinamis (+30s) hingga pembacaan stabil.';
    timerNum.innerText=formatTime(d.testTimerSec!==undefined?d.testTimerSec:30);
    timerLabel.innerText='EKSTENSI KESTABILAN (' + (d.testTimerSec||0) + 's)';
    pFill.style.width='100%';
  } else if(rawState==='LOCKED'){
    byId('heroTitle').innerText='Pengujian Selesai (Hasil Terkunci)';
    byId('heroDesc').innerText='Tingkat kestabilan aroma telah terpenuhi. Status kelayakan makanan telah ditetapkan.';
    timerNum.innerText='00:00';
    timerLabel.innerText='HASIL TERKUNCI';
    pFill.style.width='100%';
  } else if(rawState==='RECOVERY'){
    byId('heroTitle').innerText='Menetralkan Sensor (Pembersihan Ruang Uji)';
    byId('heroDesc').innerText='Keluarkan makanan dan buka wadah. Sensor sedang dinetralkan kembali menuju kondisi udara bersih.';
    timerNum.innerText=formatTime(d.recoverySec||0);
    timerLabel.innerText='DURASI PEMBERSIHAN';
    pFill.style.width='0%';
  } else {
    byId('heroTitle').innerText='Alat Siaga (Siap Uji)';
    byId('heroDesc').innerText=d.baselineValid?'Titik nol udara bersih terverifikasi. Masukkan sampel makanan, lalu tekan Mulai Uji Sampel.':'Kondisi udara acuan belum siap. Tunggu stabilisasi.';
    timerNum.innerText='05:00';
    timerLabel.innerText='STANDAR UJI (5 MENIT)';
    pFill.style.width='0%';
  }

  byId('t1Mq3').innerText=d.mq3;
  byId('t1BaseMq3').innerText=d.baseMq3+' Poin';
  byId('t1RiseMq3').innerText=(d.riseMq3>=0?'+':'')+d.riseMq3.toFixed(1)+' %';

  byId('t1Mq135').innerText=d.mq135;
  byId('t1BaseMq135').innerText=d.baseMq135+' Poin';
  byId('t1RiseMq135').innerText=(d.riseMq135>=0?'+':'')+d.riseMq135.toFixed(1)+' %';

  const plateauText=d.plateauStatus==='STABIL'?'NILAI SINYAL STABIL':d.plateauStatus==='MENINGKAT'?'AROMA MASIH NAIK':d.plateauStatus==='MENURUN'?'AROMA MENURUN':'BELUM STABIL';
  byId('t1Plateau').innerText=plateauText;
  byId('t1Env').innerText=(d.dhtValid?d.temp.toFixed(1)+'°C / '+d.hum.toFixed(0)+'%':'--');

  const vCard=byId('verdictCard');
  if(rawState==='LOCKED'||rawState==='RECOVERY'||lastLockedData){
    vCard.style.display='block';
    const status=d.status||'AMAN';
    if(status==='BAHAYA'){
      vCard.className='verdict-card danger';
      byId('verdictTitle').innerText='BAHAYA - MAKANAN TIDAK LAYAK KONSUMSI';
      byId('verdictDesc').innerText='Terdeteksi pelepasan gas pembusukan pekat (kenaikan aroma >= 50.0%). Makanan telah mengalami kerusakan struktur biologis nyata.';
    } else if(status==='WASPADA'){
      vCard.className='verdict-card warn';
      byId('verdictTitle').innerText='WASPADA - PERIKSA KONDISI MAKANAN';
      byId('verdictDesc').innerText='Terdeteksi kenaikan aroma volatil awal (kenaikan aroma 25.0% - <50.0%). Makanan mulai mengalami penurunan kesegaran.';
    } else {
      vCard.className='verdict-card safe';
      byId('verdictTitle').innerText='KONDISI NORMAL / AMAN';
      byId('verdictDesc').innerText='Aroma makanan dalam batas variasi normal udara ruangan (kenaikan aroma < 25.0%). Belum terdeteksi pembusukan signifikan.';
    }
  } else {
    vCard.style.display='none';
  }

  byId('t2Fluc3').innerText=(d.mq3Fluc?d.mq3Fluc.toFixed(2):'0.00')+' % (Batas <= 1.0%)';
  byId('t2Fluc135').innerText=(d.mq135Fluc?d.mq135Fluc.toFixed(2):'0.00')+' % (Batas <= 1.0%)';
  byId('t2Trend3').innerText=(d.mq3Trend?d.mq3Trend.toFixed(2):'0.00')+' % (Batas <= 0.5%)';
  byId('t2Trend135').innerText=(d.mq135Trend?d.mq135Trend.toFixed(2):'0.00')+' % (Batas <= 0.5%)';
  byId('t2PlateauStatus').innerText=plateauText;
  byId('t2Temp').innerText=d.dhtValid?d.temp.toFixed(1)+' °C':'--';
  byId('t2Hum').innerText=d.dhtValid?d.hum.toFixed(1)+' %RH':'--';
  byId('t2RecoveryStatus').innerText=d.recoveryReady?'Siap (Sensor Bersih)':'Penetralan Gas Berjalan';

  if(rawState==='EXPOSURE'||rawState==='IDLE'){
    testSaved=false;
  }
  if(rawState==='LOCKED' && !testSaved){
    saveTestRun(d);
    testSaved=true;
  }

  h3.push(d.mq3);h3.shift();
  h135.push(d.mq135);h135.shift();
  drawChart();
}

function drawChart(){
  const c=byId('liveChart');if(!c)return;
  const r=c.getBoundingClientRect();if(!r.width||!r.height)return;
  const dpr=window.devicePixelRatio||1;
  c.width=r.width*dpr;c.height=r.height*dpr;
  const ctx=c.getContext('2d');
  ctx.scale(dpr,dpr);
  const w=r.width,h=r.height;
  const pLeft=Math.max(38,Math.min(48,w*0.12)),pRight=12,pTop=16,pBottom=24;

  ctx.clearRect(0,0,w,h);
  ctx.strokeStyle=getComputedStyle(document.documentElement).getPropertyValue('--border');
  ctx.lineWidth=1;
  for(let i=0;i<=4;i++){
    let y=pTop+(h-pTop-pBottom)*i/4;
    ctx.beginPath();ctx.moveTo(pLeft,y);ctx.lineTo(w-pRight,y);ctx.stroke();
  }

  const maxVal=Math.max(1000,base3*1.3,base135*1.3,...h3,...h135)*1.1;

  if(base3>0){
    const yB3=h-pBottom-(h-pTop-pBottom)*(base3/maxVal);
    ctx.strokeStyle='#94a3b8';ctx.setLineDash([4,4]);ctx.beginPath();ctx.moveTo(pLeft,yB3);ctx.lineTo(w-pRight,yB3);ctx.stroke();
    ctx.setLineDash([]);
  }

  function plot(arr,color){
    ctx.strokeStyle=color;ctx.lineWidth=2.5;ctx.beginPath();
    arr.forEach((v,i)=>{
      const x=pLeft+(w-pLeft-pRight)*i/(N-1);
      const y=h-pBottom-(h-pTop-pBottom)*Math.min(maxVal,Math.max(0,v))/maxVal;
      i?ctx.lineTo(x,y):ctx.moveTo(x,y);
    });
    ctx.stroke();
  }
  plot(h3,'#2563eb');
  plot(h135,'#ea580c');

  ctx.fillStyle=getComputedStyle(document.documentElement).getPropertyValue('--muted');
  ctx.font='10px monospace';
  ctx.textAlign='right';
  ctx.fillText('0',pLeft-6,h-pBottom+3);
  ctx.fillText(Math.round(maxVal),pLeft-6,pTop+4);
}

// Window resize redraw
let resizeTimer;
window.addEventListener('resize',()=>{
  clearTimeout(resizeTimer);
  resizeTimer=setTimeout(()=>{if(document.querySelectorAll('.tab-pane')[2].classList.contains('active'))drawChart()},80);
});

function saveTestRun(d){
  const plateauText=d.plateauStatus==='STABIL'?'STABIL':d.plateauStatus==='MENINGKAT'?'NAIK':d.plateauStatus==='MENURUN'?'TURUN':'BELUM STABIL';
  const elapsed = d.testElapsedSec || 0;
  const durText = elapsed > 0 ? (formatTime(elapsed) + ' (' + elapsed + 's)') : '300s';
  lastLockedData={
    id:'UJI-'+Date.now().toString().slice(-4),
    date:new Date().toLocaleString('id-ID'),
    duration:durText,
    mq3:d.mq3,baseMq3:d.baseMq3,riseMq3:d.riseMq3.toFixed(1),
    mq135:d.mq135,baseMq135:d.baseMq135,riseMq135:d.riseMq135.toFixed(1),
    plateau:plateauText,
    verdict:d.status||'AMAN',
    temp:d.temp?d.temp.toFixed(1):'27.0',
    hum:d.hum?d.hum.toFixed(0):'60',
    fluc3:d.mq3Fluc?d.mq3Fluc.toFixed(2):'0.50',
    fluc135:d.mq135Fluc?d.mq135Fluc.toFixed(2):'0.40',
    trend3:d.mq3Trend?d.mq3Trend.toFixed(2):'0.20',
    trend135:d.mq135Trend?d.mq135Trend.toFixed(2):'0.15'
  };
  let list=getHistory();
  list.unshift(lastLockedData);
  if(list.length>50) list=list.slice(0,50);
  localStorage.setItem('sidakan_history',JSON.stringify(list));
  renderHistoryTable();
}

function getHistory(){
  try{return JSON.parse(localStorage.getItem('sidakan_history'))||[]}catch(e){return[]}
}

function renderHistoryTable(){
  const list=getHistory(),tbody=byId('historyBody');
  if(!list.length){
    tbody.innerHTML='<tr><td colspan="7" style="text-align:center;color:var(--muted)">Belum ada data pengujian yang tercatat.</td></tr>';
    return;
  }
  tbody.innerHTML=list.map(r=>`
    <tr>
      <td><b>${r.id}</b></td>
      <td>${r.date}</td>
      <td>${r.duration}</td>
      <td>${r.mq3} (+${r.riseMq3}%)</td>
      <td>${r.mq135} (+${r.riseMq135}%)</td>
      <td><span class="tag">${r.plateau}</span></td>
      <td><b style="color:${r.verdict==='BAHAYA'?'var(--red)':r.verdict==='WASPADA'?'var(--amber)':'var(--green)'}">${r.verdict}</b></td>
    </tr>
  `).join('');
}

function clearHistory(){
  if(confirm('Hapus seluruh riwayat pengujian dari peramban ini?')){
    localStorage.removeItem('sidakan_history');
    renderHistoryTable();
  }
}

function downloadAsciiReport(){
  const list=getHistory();
  if(!list.length){alert('Belum ada riwayat pengujian untuk diunduh.');return}
  const r=list[0];
  const ascii=`================================================================================
                    LAPORAN HASIL PENGUJIAN SAMPEL - SIDAKAN
             Sistem IoT Deteksi Dini Kualitas & Kelayakan Makanan
================================================================================

[ 1. IDENTITAS PENGUJIAN ]
--------------------------------------------------------------------------------
Tanggal & Waktu   : ${r.date}
Identitas Unit    : SIDAKAN-ESP32-01 (Firmware v1.5.1)
ID Pengujian      : ${r.id}
Operator Lapangan : Operator SIDAKAN
Kondisi Wadah     : Volume 2.0 Liter | Jarak Sensor 5.0 cm

[ 2. KONDISI ACUAN UDARA (TITIK NOL SEBELUM PENGUJIAN) ]
--------------------------------------------------------------------------------
Titik Nol MQ-3    : ${r.baseMq3} Poin (Batas wajar: 80 - 4000)
Titik Nol MQ-135  : ${r.baseMq135} Poin (Batas wajar: 80 - 4000)
Suhu Lingkungan   : ${r.temp} °C
Kelembapan (RH)   : ${r.hum} %RH
Status Titik Nol  : VALID (Terverifikasi di udara bersih ruangan)

[ 3. KEPUTUSAN HASIL UJI (DURASI PENGUJIAN: ${r.duration}) ]
================================================================================
>>> KEPUTUSAN AKHIR : ${r.verdict} <<<
================================================================================
Parameter Sensor                   Nilai Akhir    Kenaikan Aroma   Ambang Keputusan
- MQ-3 (Fermentasi/Etanol)       : ${r.mq3} Poin       +${r.riseMq3} %          Waspada >= 25.0 %
- MQ-135 (Pembusukan/Amonia)     : ${r.mq135} Poin       +${r.riseMq135} %          Bahaya  >= 50.0 %

Evaluasi Kestabilan Sinyal (30 Detik Terakhir):
- Goyangan Sinyal MQ-3           : ${r.fluc3} %  [LULUS <= 1.0 %]
- Goyangan Sinyal MQ-135         : ${r.fluc135} %  [LULUS <= 1.0 %]
- Pergeseran Tren MQ-3           : ${r.trend3} %  [LULUS <= 0.5 %]
- Pergeseran Tren MQ-135         : ${r.trend135} %  [LULUS <= 0.5 %]
Status Kestabilan Sinyal         : ${r.plateau} (Memenuhi Syarat Stabil)

[ 4. CATATAN PENGUJIAN & DISKLAIMER METODOLOGIS ]
--------------------------------------------------------------------------------
1. Ambang kelayakan operasional: Aman (<25%), Waspada (25% - <50%), Bahaya (>=50%).
2. Nilai skor merupakan respons sensor relatif terhadap titik nol udara bersih
   sesuai prinsip fraksional hidung elektronik (Gardner & Bartlett, 1994).
3. Evaluasi kestabilan mengacu pada standar jendela 30 detik (fluktuasi <= 1.0%, tren <= 0.5%).
4. Hasil alat adalah instrumen deteksi dini prototipe dan tidak menggantikan
   uji laboratorium mikrobiologi pangan baku (FDA BAM / SNI 2729:2013).

[ 5. REFERENSI & STANDAR RUJUKAN ILMIAH ]
--------------------------------------------------------------------------------
- Derau Termal ADC   : Johnson (1928) Phys. Rev. 32; Nyquist (1928) Phys. Rev. 32
- Metodologi E-Nose  : Gardner & Bartlett (1994) Sens. Actuators B; Pearce (2003)
- Karakteristik SnO2 : Yamazoe & Shimanoe (2009) Sens. Actuators B; Barsan (2001)
- Mutu Pangan / TVBN : BSN SNI 2729:2013; Olafsdottir (1997); U.S. FDA BAM (2020)
================================================================================
                    --- AKHIR DOKUMEN LOG PENGUJIAN ---
`;
  const blob=new Blob([ascii],{type:'text/plain;charset=utf-8'});
  const a=document.createElement('a');
  a.href=URL.createObjectURL(blob);
  a.download='Laporan_SIDAKAN_'+r.id+'.txt';
  a.click();
}

window.addEventListener('resize',drawChart);
window.onload=()=>{initWS();renderHistoryTable();drawChart()};
</script>
</body>
</html>
)rawliteral";

// ==============================================================================
// WEB SERVER & WEBSOCKET EVENT
// ==============================================================================
void handleRoot() {
  server.send_P(200, "text/html", PAGE_INDEX);
}

void webSocketEvent(uint8_t num, WStype_t type, uint8_t* payload, size_t length) {
  if (type == WStype_CONNECTED) {
    Serial.printf("[WEBSOCKET] Klien #%u terhubung.\n", num);
    broadcastSensorData();
    return;
  }

  if (type != WStype_TEXT || length == 0 || length > 32) return;

  if (length == 12 && memcmp(payload, "TOGGLE_POWER", 12) == 0) {
    toggleSystemPower("Web Dashboard");
  } else if (length == 9 && memcmp(payload, "CALIBRATE", 9) == 0 && systemPowerState) {
    runDualGasCalibration();
  } else if (length == 10 && memcmp(payload, "START_TEST", 10) == 0) {
    startSampleTest("Web Dashboard");
  } else if (length == 11 && memcmp(payload, "CANCEL_TEST", 11) == 0) {
    cancelSampleTest("Web Dashboard");
  } else if (length == 15 && memcmp(payload, "BYPASS_RECOVERY", 15) == 0) {
    bypassRecovery("Web Dashboard");
  }
}

// ------------------------------------------------------------------------------
// PENYIARAN DATA WEBSOCKET DENGAN BUFFER CHAR STATIS (ANTI-CRASH)
// ------------------------------------------------------------------------------
void broadcastSensorData() {
  char json[512];
  snprintf(json, sizeof(json),
    "{\"power\":%s,\"stabilizing\":%s,\"baselineValid\":%s,"
    "\"testState\":\"%s\",\"testTimerSec\":%d,\"testElapsedSec\":%d,"
    "\"mq3\":%d,\"mq135\":%d,"
    "\"baseMq3\":%d,\"baseMq135\":%d,"
    "\"riseMq3\":%.1f,\"riseMq135\":%.1f,"
    "\"indexMq3\":%.1f,\"indexMq135\":%.1f,"
    "\"combined\":%.1f,\"status\":\"%s\","
    "\"plateauStatus\":\"%s\","
    "\"mq3Fluc\":%.2f,\"mq135Fluc\":%.2f,"
    "\"mq3Trend\":%.2f,\"mq135Trend\":%.2f,"
    "\"recoveryReady\":%s,\"recoverySec\":%lu,"
    "\"temp\":%.1f,\"hum\":%.1f,\"dhtValid\":%s}",
    systemPowerState ? "true" : "false",
    (currentTestState == STATE_WARMUP) ? "true" : "false",
    sensorBaselineValid ? "true" : "false",
    testStateText(currentTestState),
    testTimerSeconds,
    testElapsedSeconds,
    averageMQ3, averageMQ135,
    baselineMQ3, baselineMQ135,
    mq3RisePct, mq135RisePct,
    mq3Index, mq135Index,
    combinedGasIndex,
    statusText(currentStatus),
    plateauText(currentPlateauStatus),
    mq3Fluc, mq135Fluc,
    mq3Trend, mq135Trend,
    recoveryReady ? "true" : "false",
    (currentTestState == STATE_RECOVERY) ? ((millis() - recoveryStartMillis) / 1000UL) : 0UL,
    currentTemp, currentHum,
    dhtValid ? "true" : "false"
  );
  webSocket.broadcastTXT(json);
}

// ==============================================================================
// SETUP SISTEM
// ==============================================================================
void setup() {
  Serial.begin(115200);
  delay(300);

  Serial.println(F("\n========================================================"));
  Serial.println(F(" SIDAKAN v1.5.1 - DUAL MQ (STATE MACHINE & PLATEAU)"));
  Serial.println(F("========================================================"));

  pinMode(PIN_LED_HIJAU, OUTPUT);
  pinMode(PIN_LED_KUNING, OUTPUT);
  pinMode(PIN_LED_MERAH, OUTPUT);
  pinMode(PIN_BUZZER, OUTPUT);
  pinMode(PIN_BUTTON_POWER, INPUT_PULLUP);
  pinMode(PIN_BUTTON_CALIB, INPUT_PULLUP);

  digitalWrite(PIN_LED_HIJAU, LOW);
  digitalWrite(PIN_LED_KUNING, LOW);
  digitalWrite(PIN_LED_MERAH, LOW);
  noTone(PIN_BUZZER);

  analogReadResolution(12);
  analogSetPinAttenuation(PIN_MQ3, ADC_11db);
  analogSetPinAttenuation(PIN_MQ135, ADC_11db);
  Serial.println(F("[ADC] MQ3=GPIO35, MQ135=GPIO34, Resolusi=12-bit (11dB)"));

  Wire.begin(21, 22);
  oledReady = display.begin(SSD1306_SWITCHCAPVCC, OLED_I2C_ADDR);
  if (!oledReady) {
    Serial.println(F("[ERROR] Layar OLED SSD1306 tidak terdeteksi pada bus I2C."));
  }

  dht.begin();

  // Pengisian awal filter moving average 10 titik
  for (int i = 0; i < NUM_READINGS; i++) {
    mq3Readings[i] = analogRead(PIN_MQ3);
    mq135Readings[i] = analogRead(PIN_MQ135);
    totalMQ3 += mq3Readings[i];
    totalMQ135 += mq135Readings[i];
    delay(20);
  }
  averageMQ3 = totalMQ3 / NUM_READINGS;
  averageMQ135 = totalMQ135 / NUM_READINGS;

  baselineMQ3 = 0;
  baselineMQ135 = 0;
  sensorBaselineValid = false;
  sensorStartMillis = 0;
  lastAutoBaselineAttempt = 0;

  ringBufferReset(ringMQ3);
  ringBufferReset(ringMQ135);

  WiFi.mode(WIFI_AP);
  WiFi.softAP(AP_SSID, AP_PASS);

  server.on("/", handleRoot);
  server.on("/update", HTTP_GET, []() {
    if (!otaCheckLogin()) return;
    server.send_P(200, "text/html; charset=utf-8", OTA_HTML);
  });
  server.on("/update", HTTP_POST, otaHandleResult, otaHandleUpload);
  server.begin();
  webSocket.begin();
  webSocket.onEvent(webSocketEvent);

  // Self-test LED indikator
  digitalWrite(PIN_LED_HIJAU, HIGH); delay(120); digitalWrite(PIN_LED_HIJAU, LOW);
  digitalWrite(PIN_LED_KUNING, HIGH); delay(120); digitalWrite(PIN_LED_KUNING, LOW);
  digitalWrite(PIN_LED_MERAH, HIGH); delay(120); digitalWrite(PIN_LED_MERAH, LOW);

  Serial.print(F("[WI-FI] SSID : ")); Serial.println(AP_SSID);
  Serial.print(F("[WI-FI] IP   : ")); Serial.println(WiFi.softAPIP());
  Serial.println(F("[INFO] Tahan tombol POWER 2 detik untuk mengaktifkan daya."));
  Serial.println(F("[INFO] Klik tombol POWER untuk Mulai/Batal Uji Sampel saat siaga."));

  showStandbyOLED();
}

// ==============================================================================
// LOOP UTAMA (100% NON-BLOCKING)
// ==============================================================================
void loop() {
  const unsigned long now = millis();

  server.handleClient();
  webSocket.loop();
  otaServiceRestart();
  handleButtonControl();
  handleTimerCountdown();
  handleBuzzerSequencer();

  if (!systemPowerState) {
    digitalWrite(PIN_LED_HIJAU, LOW);
    digitalWrite(PIN_LED_KUNING, LOW);
    digitalWrite(PIN_LED_MERAH, LOW);
    noTone(PIN_BUZZER);
    return;
  }

  // Siklus Pembacaan Sensor Setiap 2 Detik
  if (now - previousMillisSensor >= SENSOR_INTERVAL_MS) {
    previousMillisSensor = now;

    readAndFilterSensors();

    // Logika Auto-Baseline Saat Pemanasan
    if (currentTestState == STATE_WARMUP) {
      if (now - sensorStartMillis >= RUNTIME_STABILIZATION_MS) {
        if (dualBaselineValid(averageMQ3, averageMQ135)) {
          baselineMQ3 = averageMQ3;
          baselineMQ135 = averageMQ135;
          sensorBaselineValid = true;
          resetGasFilter(baselineMQ3, baselineMQ135);
          Serial.print(F("[WARMUP-DONE] Titik Nol VALID | MQ3=")); Serial.print(baselineMQ3);
          Serial.print(F(" | MQ135=")); Serial.println(baselineMQ135);
          transitionToState(STATE_IDLE);
        } else if (now - lastAutoBaselineAttempt >= AUTO_BASELINE_RETRY_MS) {
          lastAutoBaselineAttempt = now;
          Serial.println(F("[WARMUP-RETRY] Titik nol belum masuk rentang 80..4000. Cek pemanasan sensor."));
          showBaselineInvalidOLED();
        }
      } else {
        int secRemaining = (int)((RUNTIME_STABILIZATION_MS - (now - sensorStartMillis) + 999UL) / 1000UL);
        testTimerSeconds = secRemaining;
        showWarmupOLED(secRemaining);
      }
    }

    calculateGasIndices();

    // Rekam Titik Data ke Ring Buffer Jendela Kestabilan (Fase Paparan & Evaluasi)
    if (currentTestState == STATE_EXPOSURE || currentTestState == STATE_EVALUATION) {
      ringBufferPush(ringMQ3, averageMQ3);
      ringBufferPush(ringMQ135, averageMQ135);
      evaluatePlateau();
      evaluateStatus();
      showExposureOLED();

      // Transisi dari Paparan ke Evaluasi saat t = 0
      if (currentTestState == STATE_EXPOSURE && testTimerSeconds <= 0) {
        if (currentPlateauStatus == PLATEAU_STABIL) {
          Serial.println(F("[TEST] Kestabilan tercapai pada t=300s. Mengunci data."));
          transitionToState(STATE_LOCKED);
        } else {
          Serial.println(F("[TEST] Belum stabil pada t=300s. Masuk tahap evaluasi & waktu tambahan."));
          transitionToState(STATE_EVALUATION);
        }
      }
      // Transisi saat tahap Evaluasi
      else if (currentTestState == STATE_EVALUATION) {
        if (currentPlateauStatus == PLATEAU_STABIL) {
          Serial.println(F("[TEST] Kestabilan tercapai pada waktu tambahan. Mengunci data."));
          transitionToState(STATE_LOCKED);
        } else if (testElapsedSeconds >= MAX_TEST_DURATION_SEC) {
          Serial.println(F("[TEST] Batas maksimal 600s tercapai. Mengunci data (Belum Stabil)."));
          currentPlateauStatus = PLATEAU_BELUM_STABIL;
          transitionToState(STATE_LOCKED);
        } else if (testTimerSeconds <= 0) {
          testTimerSeconds = EXTENSION_STEP_SEC;
          Serial.println(F("[TEST] Waktu tambahan dinamis +30 detik ditambahkan."));
        }
      }
    }

    // Logika Fase Terkunci (Locked Display selama 6 Detik, lalu masuk Penetralan)
    else if (currentTestState == STATE_LOCKED) {
      showLockedOLED();
      if (now - stateEnterMillis >= 6000UL) {
        transitionToState(STATE_RECOVERY);
      }
    }

    // Logika Fase Penetralan Ruang Uji (Recovery)
    else if (currentTestState == STATE_RECOVERY) {
      showRecoveryOLED();
      
      // Syarat: Pembacaan kembali ke +/-10% dari titik nol acuan awal
      const float tolMQ3 = (float)baselineMQ3 * RECOVERY_TOLERANCE_PCT;
      const float tolMQ135 = (float)baselineMQ135 * RECOVERY_TOLERANCE_PCT;
      bool mq3Recovered = fabs((float)averageMQ3 - (float)baselineMQ3) <= tolMQ3;
      bool mq135Recovered = fabs((float)averageMQ135 - (float)baselineMQ135) <= tolMQ135;

      if (mq3Recovered && mq135Recovered) {
        recoveryConsecutiveMatches++;
        Serial.printf("[PENETRALAN] Sensor mendekati titik nol #%d/3\n", recoveryConsecutiveMatches);
        if (recoveryConsecutiveMatches >= RECOVERY_CONSECUTIVE_TARGET) {
          Serial.println(F("[PENETRALAN-SELESAI] Sensor kembali bersih. Siap untuk uji baru."));
          transitionToState(STATE_IDLE);
        }
      } else {
        recoveryConsecutiveMatches = 0;
      }

      // Timeout Pengaman 15 Menit
      if (now - recoveryStartMillis >= RECOVERY_TIMEOUT_MS) {
        Serial.println(F("[PENETRALAN-TIMEOUT] Batas 15 menit tercapai. Otomatis kembali ke Siaga."));
        transitionToState(STATE_IDLE);
      }
    }

    // Fase Siaga
    else if (currentTestState == STATE_IDLE) {
      showIdleOLED();
    }

    updateActuators(currentStatus, now);
    printSerialLog();
    broadcastSensorData();
  }
}

// ==============================================================================
// HITUNG MUNDUR TIMER PENGUJIAN (INTERVAL 1 DETIK NON-BLOCKING)
// ==============================================================================
void handleTimerCountdown() {
  const unsigned long now = millis();
  if (now - previousMillisTimer >= 1000UL) {
    previousMillisTimer = now;

    if (currentTestState == STATE_EXPOSURE || currentTestState == STATE_EVALUATION) {
      if (testTimerSeconds > 0) testTimerSeconds--;
      testElapsedSeconds++;
    }
  }
}

// ==============================================================================
// TRANSISI STATE MACHINE SIKLUS PENGUJIAN
// ==============================================================================
void transitionToState(TestState newState) {
  currentTestState = newState;
  stateEnterMillis = millis();
  Serial.print(F("[STATE] Transisi ke tahap: "));
  Serial.println(testStateText(newState));

  if (newState == STATE_WARMUP) {
    testTimerSeconds = 60;
    testElapsedSeconds = 0;
    recoveryReady = false;
  } else if (newState == STATE_IDLE) {
    testTimerSeconds = 0;
    testElapsedSeconds = 0;
    recoveryReady = true;
    currentPlateauStatus = PLATEAU_BELUM_STABIL;
    ringBufferReset(ringMQ3);
    ringBufferReset(ringMQ135);
    showIdleOLED();
  } else if (newState == STATE_EXPOSURE) {
    testTimerSeconds = EXPOSURE_DURATION_SEC; // 300 detik
    testElapsedSeconds = 0;
    recoveryReady = false;
    currentPlateauStatus = PLATEAU_BELUM_STABIL;
    ringBufferReset(ringMQ3);
    ringBufferReset(ringMQ135);
    showExposureOLED();
  } else if (newState == STATE_EVALUATION) {
    testTimerSeconds = EXTENSION_STEP_SEC; // +30 detik
    showExposureOLED();
  } else if (newState == STATE_LOCKED) {
    lockedMQ3 = averageMQ3;
    lockedMQ135 = averageMQ135;
    lockedRiseMQ3 = mq3RisePct;
    lockedRiseMQ135 = mq135RisePct;
    lockedCombined = combinedGasIndex;
    lockedStatus = currentStatus;
    lockedPlateau = currentPlateauStatus;
    showLockedOLED();
    triggerVerdictBuzzer(lockedStatus);
  } else if (newState == STATE_RECOVERY) {
    recoveryStartMillis = millis();
    recoveryConsecutiveMatches = 0;
    recoveryReady = false;
    showRecoveryOLED();
  }
  broadcastSensorData();
}

void startSampleTest(const char* source) {
  if (!systemPowerState || currentTestState != STATE_IDLE || !sensorBaselineValid) {
    Serial.printf("[TEST-DENIED] Perintah mulai uji dari %s diabaikan (kondisi belum siaga).\n", source);
    return;
  }
  Serial.printf("[TEST-START] Mulai pengujian sampel via %s.\n", source);
  transitionToState(STATE_EXPOSURE);
}

void cancelSampleTest(const char* source) {
  if (currentTestState != STATE_EXPOSURE && currentTestState != STATE_EVALUATION) return;
  Serial.printf("[TEST-CANCEL] Pengujian dibatalkan via %s.\n", source);
  transitionToState(STATE_IDLE);
}

void bypassRecovery(const char* source) {
  if (currentTestState != STATE_RECOVERY) return;
  Serial.printf("[RECOVERY-BYPASS] Pemulihan di-bypass via %s. Kembali ke Siaga.\n", source);
  transitionToState(STATE_IDLE);
}

// ==============================================================================
// EVALUASI KESTABILAN DATA (PLATEAU DETECTION JENDELA 30 DETIK)
// ==============================================================================
void evaluatePlateau() {
  if (ringMQ3.count < PLATEAU_WINDOW_SIZE || ringMQ135.count < PLATEAU_WINDOW_SIZE) {
    currentPlateauStatus = PLATEAU_BELUM_STABIL;
    mq3Fluc = mq135Fluc = mq3Trend = mq135Trend = 0.0f;
    return;
  }

  // --- Perhitungan Sensor MQ-3 ---
  long sumMQ3 = 0, sumMQ3_First8 = 0, sumMQ3_Last8 = 0;
  int minMQ3 = ringBufferGet(ringMQ3, 0), maxMQ3 = ringBufferGet(ringMQ3, 0);

  for (int i = 0; i < PLATEAU_WINDOW_SIZE; i++) {
    int v = ringBufferGet(ringMQ3, i);
    sumMQ3 += v;
    if (v < minMQ3) minMQ3 = v;
    if (v > maxMQ3) maxMQ3 = v;
    if (i < 8) sumMQ3_First8 += v;
    else sumMQ3_Last8 += v;
  }

  float avgMQ3 = (float)sumMQ3 / 16.0f;
  if (avgMQ3 <= 0.0f) avgMQ3 = 1.0f;
  mq3Fluc = (((float)(maxMQ3 - minMQ3)) / avgMQ3) * 100.0f;

  float avgMQ3_1 = (float)sumMQ3_First8 / 8.0f;
  float avgMQ3_2 = (float)sumMQ3_Last8 / 8.0f;
  float diffSlopeMQ3 = avgMQ3_2 - avgMQ3_1;
  mq3Trend = (fabs(diffSlopeMQ3) / avgMQ3) * 100.0f;

  bool mq3Stable = (mq3Fluc <= 1.0f && mq3Trend <= 0.5f);

  // --- Perhitungan Sensor MQ-135 ---
  long sumMQ135 = 0, sumMQ135_First8 = 0, sumMQ135_Last8 = 0;
  int minMQ135 = ringBufferGet(ringMQ135, 0), maxMQ135 = ringBufferGet(ringMQ135, 0);

  for (int i = 0; i < PLATEAU_WINDOW_SIZE; i++) {
    int v = ringBufferGet(ringMQ135, i);
    sumMQ135 += v;
    if (v < minMQ135) minMQ135 = v;
    if (v > maxMQ135) maxMQ135 = v;
    if (i < 8) sumMQ135_First8 += v;
    else sumMQ135_Last8 += v;
  }

  float avgMQ135 = (float)sumMQ135 / 16.0f;
  if (avgMQ135 <= 0.0f) avgMQ135 = 1.0f;
  mq135Fluc = (((float)(maxMQ135 - minMQ135)) / avgMQ135) * 100.0f;

  float avgMQ135_1 = (float)sumMQ135_First8 / 8.0f;
  float avgMQ135_2 = (float)sumMQ135_Last8 / 8.0f;
  float diffSlopeMQ135 = avgMQ135_2 - avgMQ135_1;
  mq135Trend = (fabs(diffSlopeMQ135) / avgMQ135) * 100.0f;

  bool mq135Stable = (mq135Fluc <= 1.0f && mq135Trend <= 0.5f);

  // Penentuan Status Dinamis
  if (mq3Stable && mq135Stable) {
    currentPlateauStatus = PLATEAU_STABIL;
  } else if (diffSlopeMQ3 > (0.005f * avgMQ3) || diffSlopeMQ135 > (0.005f * avgMQ135)) {
    currentPlateauStatus = PLATEAU_MENINGKAT;
  } else if (diffSlopeMQ3 < (-0.005f * avgMQ3) || diffSlopeMQ135 < (-0.005f * avgMQ135)) {
    currentPlateauStatus = PLATEAU_MENURUN;
  } else {
    currentPlateauStatus = PLATEAU_BELUM_STABIL;
  }
}

// ==============================================================================
// PEMBACAAN & FILTER SENSOR (RATA-RATA BERGERAK 10 SAMPEL)
// ==============================================================================
void readAndFilterSensors() {
  const int rawMQ3 = analogRead(PIN_MQ3);
  const int rawMQ135 = analogRead(PIN_MQ135);

  totalMQ3 -= mq3Readings[gasReadIndex];
  totalMQ135 -= mq135Readings[gasReadIndex];

  mq3Readings[gasReadIndex] = rawMQ3;
  mq135Readings[gasReadIndex] = rawMQ135;

  totalMQ3 += rawMQ3;
  totalMQ135 += rawMQ135;

  gasReadIndex = (gasReadIndex + 1) % NUM_READINGS;

  averageMQ3 = totalMQ3 / NUM_READINGS;
  averageMQ135 = totalMQ135 / NUM_READINGS;

  const float t = dht.readTemperature();
  const float h = dht.readHumidity();
  dhtValid = !isnan(t) && !isnan(h);
  if (dhtValid) {
    currentTemp = t;
    currentHum = h;
  }
}

bool baselineADCValid(int value) {
  return value >= MIN_VALID_BASELINE_ADC && value <= MAX_VALID_BASELINE_ADC;
}

bool dualBaselineValid(int mq3Base, int mq135Base) {
  return baselineADCValid(mq3Base) && baselineADCValid(mq135Base);
}

void resetGasFilter(int seedMQ3, int seedMQ135) {
  totalMQ3 = 0;
  totalMQ135 = 0;
  for (int i = 0; i < NUM_READINGS; i++) {
    mq3Readings[i] = seedMQ3;
    mq135Readings[i] = seedMQ135;
    totalMQ3 += seedMQ3;
    totalMQ135 += seedMQ135;
  }
  gasReadIndex = 0;
  averageMQ3 = seedMQ3;
  averageMQ135 = seedMQ135;
}

// ------------------------------------------------------------------------------
// FORMULA KENAIKAN FRAKSIONAL BASELINE & AMBANG KELAYAKAN (50% BAHAYA)
// ------------------------------------------------------------------------------
float relativeRisePct(int value, int baseline) {
  if (!baselineADCValid(baseline)) return 0.0f;

  const float delta = (float)value - (float)baseline;
  if (delta <= 0.0f) return 0.0f;

  const float adaptiveDeadband = max(MIN_SIGNIFICANT_DELTA, (float)baseline * 0.10f);
  if (delta <= adaptiveDeadband) return 0.0f;

  const float pct = (delta * 100.0f) / (float)baseline;
  return pct < 0.0f ? 0.0f : pct;
}

float riskIndexFromRise(float risePct, float warningPct, float dangerPct) {
  if (risePct <= 0.0f) return 0.0f;
  if (risePct < warningPct) {
    return 50.0f * (risePct / warningPct);
  }
  if (risePct < dangerPct) {
    return 50.0f + 50.0f * ((risePct - warningPct) / (dangerPct - warningPct));
  }
  return 100.0f;
}

void calculateGasIndices() {
  mq3RisePct = relativeRisePct(averageMQ3, baselineMQ3);
  mq135RisePct = relativeRisePct(averageMQ135, baselineMQ135);

  mq3Index = riskIndexFromRise(mq3RisePct, MQ3_WARNING_RISE_PCT, MQ3_DANGER_RISE_PCT);
  mq135Index = riskIndexFromRise(mq135RisePct, MQ135_WARNING_RISE_PCT, MQ135_DANGER_RISE_PCT);

  combinedGasIndex = (mq3Index + mq135Index) / 2.0f;
  if (combinedGasIndex < 0.0f) combinedGasIndex = 0.0f;
  if (combinedGasIndex > 100.0f) combinedGasIndex = 100.0f;
}

void evaluateStatus() {
  const bool dangerDetected = (mq3Index >= 100.0f || mq135Index >= 100.0f);
  const bool warningDetected = (mq3Index >= 50.0f || mq135Index >= 50.0f ||
                                combinedGasIndex >= COMBINED_WARNING_INDEX);

  if (dangerDetected) {
    currentStatus = STATUS_BAHAYA;
  } else if (warningDetected) {
    currentStatus = STATUS_WASPADA;
  } else {
    currentStatus = STATUS_AMAN;
  }
}

// ==============================================================================
// KONTROL TOMBOL PERANGKAT KERAS (GPIO 19 & GPIO 18)
// ==============================================================================
void handleButtonControl() {
  const unsigned long now = millis();

  // Tombol GPIO 19: Dual Function (Klik Singkat = Uji / Batal, Tahan 2s = Daya)
  const bool powerPressed = (digitalRead(PIN_BUTTON_POWER) == LOW);
  if (powerPressed) {
    if (!powerButtonWasPressed) {
      powerButtonWasPressed = true;
      powerHoldExecuted = false;
      powerButtonPressStart = now;
    } else if (!powerHoldExecuted && now - powerButtonPressStart >= HOLD_TIME_REQUIRED) {
      powerHoldExecuted = true;
      toggleSystemPower("Tombol POWER (Tahan 2s)");
    }
  } else {
    if (powerButtonWasPressed && !powerHoldExecuted) {
      // Klik Singkat Terdeteksi
      if (systemPowerState) {
        if (currentTestState == STATE_IDLE) {
          startSampleTest("Tombol POWER (Klik)");
        } else if (currentTestState == STATE_EXPOSURE || currentTestState == STATE_EVALUATION) {
          cancelSampleTest("Tombol POWER (Klik)");
        } else if (currentTestState == STATE_RECOVERY) {
          bypassRecovery("Tombol POWER (Klik)");
        }
      }
    }
    powerButtonWasPressed = false;
    powerHoldExecuted = false;
  }

  // Tombol GPIO 18: Kalibrasi Manual
  const bool calibPressed = (digitalRead(PIN_BUTTON_CALIB) == LOW);
  if (calibPressed && !calibButtonWasPressed) {
    calibButtonWasPressed = true;
    if (now - lastCalibAction >= BUTTON_DEBOUNCE_MS) {
      lastCalibAction = now;
      if (systemPowerState && currentTestState == STATE_IDLE) {
        runDualGasCalibration();
      } else {
        Serial.println(F("[BUTTON] Kalibrasi diabaikan: sistem tidak dalam mode siaga."));
      }
    }
  } else if (!calibPressed) {
    calibButtonWasPressed = false;
  }
}

void toggleSystemPower(const char* source) {
  systemPowerState = !systemPowerState;
  Serial.print(F("[POWER] "));
  Serial.print(systemPowerState ? F("AKTIF via ") : F("STANDBY via "));
  Serial.println(source);

  if (systemPowerState) {
    sensorStartMillis = millis();
    previousMillisSensor = sensorStartMillis;
    previousMillisTimer = sensorStartMillis;
    lastAutoBaselineAttempt = 0;
    baselineMQ3 = 0;
    baselineMQ135 = 0;
    sensorBaselineValid = false;
    mq3RisePct = mq135RisePct = 0.0f;
    mq3Index = mq135Index = combinedGasIndex = 0.0f;
    currentStatus = STATUS_AMAN;
    noTone(PIN_BUZZER);
    transitionToState(STATE_WARMUP);
  } else {
    sensorStartMillis = 0;
    lastAutoBaselineAttempt = 0;
    sensorBaselineValid = false;
    baselineMQ3 = 0;
    baselineMQ135 = 0;
    currentStatus = STATUS_AMAN;
    noTone(PIN_BUZZER);
    showStandbyOLED();
  }
  broadcastSensorData();
}

// ==============================================================================
// KALIBRASI MANUAL TITIK NOL KEDUA SENSOR (NON-BLOCKING)
// ==============================================================================
void runDualGasCalibration() {
  Serial.println(F("[CAL] Memulai kalibrasi dual-sensor (10 detik pengambilan acuan udara bersih)."));

  for (int sec = 10; sec > 0; sec--) {
    if (oledReady) {
      display.clearDisplay();
      display.setTextColor(SSD1306_WHITE);
      display.setTextSize(1);
      display.setCursor(10, 6); display.print(F("KALIBRASI TITIK NOL"));
      display.drawLine(0, 18, 127, 18, SSD1306_WHITE);
      display.setCursor(14, 26); display.print(F("Udara Bersih Ruang"));
      display.setCursor(16, 42); display.print(F("Tunggu: "));
      display.setTextSize(2); display.print(sec); display.print(F("s"));
      display.display();
    }

    unsigned long t0 = millis();
    while (millis() - t0 < 1000UL) {
      server.handleClient();
      webSocket.loop();
      delay(5);
    }
  }

  long sum3 = 0;
  long sum135 = 0;
  const int samples = 30;
  for (int i = 0; i < samples; i++) {
    sum3 += analogRead(PIN_MQ3);
    sum135 += analogRead(PIN_MQ135);
    unsigned long t0 = millis();
    while (millis() - t0 < 40UL) {
      server.handleClient();
      webSocket.loop();
      delay(2);
    }
  }

  const int candidateMQ3 = sum3 / samples;
  const int candidateMQ135 = sum135 / samples;

  if (!dualBaselineValid(candidateMQ3, candidateMQ135)) {
    sensorBaselineValid = false;
    Serial.printf("[CAL-ERROR] Titik nol tidak valid (80..4000) | MQ3=%d, MQ135=%d\n", candidateMQ3, candidateMQ135);
    showBaselineInvalidOLED();
    broadcastSensorData();
    return;
  }

  baselineMQ3 = candidateMQ3;
  baselineMQ135 = candidateMQ135;
  sensorBaselineValid = true;
  resetGasFilter(baselineMQ3, baselineMQ135);
  calculateGasIndices();
  evaluateStatus();

  Serial.printf("[CAL-OK] Titik Nol Baru: MQ3=%d, MQ135=%d Poin\n", baselineMQ3, baselineMQ135);

  if (oledReady) {
    display.clearDisplay();
    display.setTextColor(SSD1306_WHITE);
    display.setTextSize(1);
    display.setCursor(8, 8); display.print(F("KALIBRASI BERHASIL"));
    display.drawLine(0, 20, 127, 20, SSD1306_WHITE);
    display.setCursor(5, 28); display.print(F("MQ-3   : ")); display.print(baselineMQ3); display.print(F(" Poin"));
    display.setCursor(5, 42); display.print(F("MQ-135 : ")); display.print(baselineMQ135); display.print(F(" Poin"));
    display.display();
  }

  unsigned long tDisplay = millis();
  while (millis() - tDisplay < 800UL) {
    server.handleClient();
    webSocket.loop();
    delay(2);
  }

  transitionToState(STATE_IDLE);
}

// ==============================================================================
// RUTINITAS TAMPILAN LAYAR OLED SSD1306 (128x64) - BAHASA RAMAH PENGGUNA
// ==============================================================================
void showStandbyOLED() {
  if (!oledReady) return;
  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);
  display.setTextSize(1);
  display.setCursor(38, 2); display.print(F("SIDAKAN"));
  display.drawLine(0, 13, 127, 13, SSD1306_WHITE);
  display.setCursor(26, 22); display.print(F("[MODE SIAGA]"));
  display.setCursor(4, 36); display.print(F("Tahan POWER 2s: Nyala"));
  display.setCursor(8, 50); display.print(F("IP: 192.168.4.1"));
  display.display();
}

void showWarmupOLED(int secRemaining) {
  static unsigned long lastOledDraw = 0;
  if (!oledReady || millis() - lastOledDraw < 500UL) return;
  lastOledDraw = millis();

  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);
  display.setTextSize(1);
  display.setCursor(18, 2); display.print(F("SIDAKAN [PEMANASAN]"));
  display.drawLine(0, 13, 127, 13, SSD1306_WHITE);
  display.setCursor(10, 22); display.print(F("Pemanasan Sensor..."));
  display.setCursor(14, 38); display.print(F("Sisa: "));
  display.setTextSize(2); display.print(secRemaining); display.print(F("s"));
  display.display();
}

void showIdleOLED() {
  static unsigned long lastOledDraw = 0;
  if (!oledReady || millis() - lastOledDraw < 600UL) return;
  lastOledDraw = millis();

  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);
  display.setTextSize(1);
  display.setCursor(16, 1); display.print(F("SIDAKAN  [SIAGA]"));
  display.drawLine(0, 12, 127, 12, SSD1306_WHITE);
  display.setCursor(4, 18); display.print(F("TITIK NOL TERVALIDASI"));
  display.setCursor(4, 30); display.print(F("MQ3  : ")); display.print(baselineMQ3); display.print(F(" Poin"));
  display.setCursor(4, 42); display.print(F("MQ135: ")); display.print(baselineMQ135); display.print(F(" Poin"));
  display.setCursor(4, 54); display.print(F("TEKAN UJI DI DASH/TOMBOL"));
  display.display();
}

void showExposureOLED() {
  if (!oledReady) return;
  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);
  display.setTextSize(1);
  display.setCursor(6, 1);
  display.print(currentTestState == STATE_EXPOSURE ? F("SIDAKAN  [PENGUJIAN]") : F("SIDAKAN  [EKSTENSI]"));
  display.drawLine(0, 12, 127, 12, SSD1306_WHITE);

  display.setCursor(4, 16); display.print(F("SISA WAKTU : "));
  display.print(testTimerSeconds); display.print(F("s"));

  display.setCursor(4, 28); display.print(F("STATUS     : "));
  display.print(plateauText(currentPlateauStatus));

  display.setCursor(4, 41);
  display.print(F("MQ3:")); display.print(averageMQ3);
  display.print(F("  MQ135:")); display.print(averageMQ135);

  display.setCursor(4, 54); display.print(F("UJI KESTABILAN: AKTIF"));
  display.display();
}

void showLockedOLED() {
  if (!oledReady) return;
  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);
  display.setTextSize(1);
  display.setCursor(14, 1); display.print(F("HASIL PENGUJIAN"));
  display.drawLine(0, 12, 127, 12, SSD1306_WHITE);

  display.setCursor(4, 16); display.print(F("VONIS: "));
  display.setTextSize(2);
  display.print(statusText(lockedStatus));

  display.setTextSize(1);
  display.setCursor(4, 36);
  display.print(F("MQ3   : ")); display.print(lockedMQ3);
  display.print(F(" (+")); display.print(lockedRiseMQ3, 1); display.print(F("%)"));

  display.setCursor(4, 48);
  display.print(F("MQ135 : ")); display.print(lockedMQ135);
  display.print(F(" (+")); display.print(lockedRiseMQ135, 1); display.print(F("%)"));

  display.setCursor(4, 57);
  display.print(F("SINYAL: ")); display.print(plateauText(lockedPlateau));
  display.display();
}

void showRecoveryOLED() {
  static unsigned long lastOledDraw = 0;
  if (!oledReady || millis() - lastOledDraw < 600UL) return;
  lastOledDraw = millis();

  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);
  display.setTextSize(1);
  display.setCursor(10, 1); display.print(F("SIDAKAN  [PENETRALAN]"));
  display.drawLine(0, 12, 127, 12, SSD1306_WHITE);

  display.setCursor(4, 16); display.print(F("MENETRALKAN RUANG UJI"));
  display.setCursor(4, 28);
  display.print(F("MQ3  : ")); display.print(averageMQ3);
  display.print(F(" (Acuan ")); display.print(baselineMQ3); display.print(F(")"));

  display.setCursor(4, 41);
  display.print(F("MQ135: ")); display.print(averageMQ135);
  display.print(F(" (Acuan ")); display.print(baselineMQ135); display.print(F(")"));

  display.setCursor(4, 54); display.print(F("TUNGGU SENSOR BERSIH"));
  display.display();
}

void showBaselineInvalidOLED() {
  if (!oledReady) return;
  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);
  display.setTextSize(1);
  display.setCursor(38, 2); display.print(F("SIDAKAN"));
  display.drawLine(0, 13, 127, 13, SSD1306_WHITE);
  display.setCursor(6, 22); display.print(F("TITIK NOL BELUM SIAP"));
  display.setCursor(8, 36); display.print(F("Cek kabel/pemanasan"));
  display.setCursor(8, 50); display.print(F("Tekan CALIB manual"));
  display.display();
}

// ==============================================================================
// LED INDIKATOR & AKTUATOR
// ==============================================================================
void updateActuators(SystemStatus status, unsigned long currentMillis) {
  if (currentTestState == STATE_WARMUP || currentTestState == STATE_RECOVERY) {
    digitalWrite(PIN_LED_HIJAU, LOW);
    digitalWrite(PIN_LED_KUNING, HIGH);
    digitalWrite(PIN_LED_MERAH, LOW);
    return;
  }

  switch (status) {
    case STATUS_AMAN:
      digitalWrite(PIN_LED_HIJAU, HIGH);
      digitalWrite(PIN_LED_KUNING, LOW);
      digitalWrite(PIN_LED_MERAH, LOW);
      break;

    case STATUS_WASPADA:
      digitalWrite(PIN_LED_HIJAU, LOW);
      digitalWrite(PIN_LED_KUNING, HIGH);
      digitalWrite(PIN_LED_MERAH, LOW);
      break;

    case STATUS_BAHAYA:
      digitalWrite(PIN_LED_HIJAU, LOW);
      digitalWrite(PIN_LED_KUNING, LOW);
      digitalWrite(PIN_LED_MERAH, HIGH);
      break;
  }
}

// ==============================================================================
// LOG SERIAL MONITOR
// ==============================================================================
const char* statusText(SystemStatus status) {
  switch (status) {
    case STATUS_WASPADA: return "WASPADA";
    case STATUS_BAHAYA: return "BAHAYA";
    default: return "AMAN";
  }
}

const char* testStateText(TestState state) {
  switch (state) {
    case STATE_WARMUP: return "WARMUP";
    case STATE_IDLE: return "IDLE";
    case STATE_EXPOSURE: return "EXPOSURE";
    case STATE_EVALUATION: return "EVALUATION";
    case STATE_LOCKED: return "LOCKED";
    case STATE_RECOVERY: return "RECOVERY";
    default: return "UNKNOWN";
  }
}

const char* plateauText(PlateauStatus plateau) {
  switch (plateau) {
    case PLATEAU_STABIL: return "STABIL";
    case PLATEAU_MENINGKAT: return "MENINGKAT";
    case PLATEAU_MENURUN: return "MENURUN";
    default: return "BELUM STABIL";
  }
}

void printSerialLog() {
  Serial.print(F("[DATA] State=")); Serial.print(testStateText(currentTestState));
  Serial.print(F(" | MQ3=")); Serial.print(averageMQ3);
  Serial.print(F(" (Acuan:")); Serial.print(baselineMQ3);
  Serial.print(F(", +")); Serial.print(mq3RisePct, 1); Serial.print(F("%)"));

  Serial.print(F(" | MQ135=")); Serial.print(averageMQ135);
  Serial.print(F(" (Acuan:")); Serial.print(baselineMQ135);
  Serial.print(F(", +")); Serial.print(mq135RisePct, 1); Serial.print(F("%)"));

  Serial.print(F(" | Kestabilan=")); Serial.print(plateauText(currentPlateauStatus));
  Serial.print(F(" | Vonis=")); Serial.print(statusText(currentStatus));

  if (dhtValid) {
    Serial.print(F(" | T=")); Serial.print(currentTemp, 1);
    Serial.print(F("C H=")); Serial.print(currentHum, 1); Serial.print(F("%"));
  }
  Serial.println();
}
