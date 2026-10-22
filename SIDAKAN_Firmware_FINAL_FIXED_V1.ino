/*
 * ==============================================================================
 * PROJEK KEPEMIMPINAN PPG PRAJABATAN - UNIVERSITAS PENDIDIKAN INDONESIA
 * SISTEM SIDAKAN (Sistem IoT Deteksi Dini Kelayakan Makanan)
 * ==============================================================================
 * Mikrokontroler : ESP32-S (38-Pin / NodeMCU-32S)
 * Firmware       : 1.4.1 Dual MQ (Logic & Memory Stability Fix)
 * Fitur          : MQ-3 + MQ-135, OLED ringkas, LED status, tombol fisik,
 *                  Web Dashboard real-time dan kontrol 2-arah via WebSocket.
 * Penulis awal   : Anthonio Akbar, S.Pd. (NIM: 2530710)
 * ==============================================================================
 * CATATAN ILMIAH PENTING
 * ------------------------------------------------------------------------------
 * 1. MQ-3 dipakai sebagai sensor respons dominan terhadap alkohol/etanol.
 * 2. MQ-135 dipakai sebagai sensor respons luas terhadap NH3, sulfida,
 *    benzena-series vapour dan gas/VOC lain. MQ-135 BUKAN sensor NH3 selektif.
 * 3. Tanpa kalibrasi laboratorium menggunakan konsentrasi gas standar, ADC tidak
 *    boleh diklaim sebagai mg/L etanol atau ppm NH3. Firmware ini karena itu
 *    menampilkan ADC, baseline, kenaikan relatif dan INDEKS sensor 0-100%.
 * 4. Referensi penelitian MBG yang dipakai sebagai target validasi berikutnya:
 *       - Etanol: <= 0,10 mg/L
 *       - Perubahan amonia: <= 5 ppm
 *    Angka tersebut TIDAK dikonversi langsung dari ADC pada firmware ini.
 * 5. Ambang kenaikan relatif 25% (peringatan) dan 60% (bahaya) adalah ambang
 *    operasional PROVISIONAL untuk prototipe. Wajib disetel ulang setelah uji
 *    makanan segar -> menurun kualitas -> rusak dan/atau kalibrasi lab.
 * 6. Firmware v1.3.1 menolak baseline ADC yang terlalu rendah/terlalu dekat
 *    saturasi dan menerapkan deadband ADC untuk mencegah noise kecil (mis.
 *    0 -> 5 ADC) dianggap sebagai kenaikan gas berbahaya.
 * ==============================================================================
 */

#include <Update.h>
#include <WiFi.h>
#include <WebServer.h>
#include <WebSocketsServer.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <DHT.h>

// ------------------------------------------------------------------------------
// WI-FI ACCESS POINT (MANDIRI / TANPA INTERNET)
// ------------------------------------------------------------------------------
const char* AP_SSID = "SIDAKAN-WIFI";
const char* AP_PASS = "ppginformatikaupi";

WebServer server(80);
WebSocketsServer webSocket(81);

// Web OTA menggunakan server dashboard yang SAMA (port 80).
// Ganti kredensial ini sebelum mengunggah firmware.
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
      // Penulisan firmware dimulai hanya setelah autentikasi berhasil.
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
// PIN ESP32-S
// ------------------------------------------------------------------------------
#define PIN_MQ135          34  // ADC1_CH6 - respons gas/VOC luas
#define PIN_MQ3            35  // ADC1_CH7 - respons alkohol/etanol
#define PIN_DHT            25
#define PIN_LED_HIJAU      33
#define PIN_LED_KUNING     26
#define PIN_LED_MERAH      27
#define PIN_BUZZER         23
#define PIN_BUTTON_POWER   19
#define PIN_BUTTON_CALIB   18

// ------------------------------------------------------------------------------
// OLED SSD1306 128x64
// ------------------------------------------------------------------------------
#define SCREEN_WIDTH    128
#define SCREEN_HEIGHT   64
#define OLED_RESET      -1
#define OLED_I2C_ADDR   0x3C

Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);
bool oledReady = false;

// ------------------------------------------------------------------------------
// DHT11
// ------------------------------------------------------------------------------
#define DHTTYPE DHT11
DHT dht(PIN_DHT, DHTTYPE);

// ------------------------------------------------------------------------------
// REFERENSI RISET (TIDAK DIKONVERSI LANGSUNG DARI ADC)
// ------------------------------------------------------------------------------
const float RESEARCH_ETHANOL_LIMIT_MG_L = 0.10f;
const float RESEARCH_AMMONIA_CHANGE_LIMIT_PPM = 5.0f;

// ------------------------------------------------------------------------------
// AMBANG OPERASIONAL PROVISIONAL BERBASIS KENAIKAN DARI BASELINE
// Setelah pengujian empiris, ubah empat nilai berikut.
// ------------------------------------------------------------------------------
const float MQ3_WARNING_RISE_PCT   = 25.0f;
const float MQ3_DANGER_RISE_PCT    = 60.0f;
const float MQ135_WARNING_RISE_PCT = 25.0f;
const float MQ135_DANGER_RISE_PCT  = 60.0f;

// Indeks gabungan: 0-<50 Aman, >=50 Waspada, danger jika salah satu sensor 100%.
const float COMBINED_WARNING_INDEX = 50.0f;

// ------------------------------------------------------------------------------
// >>> [PERBAIKAN 1/5: PAGAR MINIMAL BASELINE VALID] <<<
// Nilai ADC di bawah 80 menandakan kabel kendor, drop tegangan, atau pemanas mati.
// Firmware menolak angka < 80 sebagai baseline agar tidak memproses data palsu.
// (Nilai asli lama: 3 ADC -> Diperbaiki menjadi: 80 ADC).
// ------------------------------------------------------------------------------
const int MIN_VALID_BASELINE_ADC = 80;
const int MAX_VALID_BASELINE_ADC = 4000;

// Perubahan kecil ADC dianggap noise/riak pembacaan dan tidak dihitung sebagai
// kenaikan gas. Nilai ini dapat disetel setelah melihat noise nyata alat.
const int ADC_NOISE_DEADBAND = 2;

// Waktu stabilisasi pendek setiap boot. Ini BUKAN pengganti burn-in awal sensor.
const unsigned long RUNTIME_STABILIZATION_MS = 60000UL;

// Timer stabilisasi dimulai saat SIDAKAN diaktifkan, bukan sejak ESP32 boot.
unsigned long sensorStartMillis = 0;
unsigned long lastAutoBaselineAttempt = 0;
const unsigned long AUTO_BASELINE_RETRY_MS = 10000UL;

// Konfirmasi berturut-turut untuk mencegah lonjakan ADC sesaat menjadi alarm.
const int REQUIRED_WARNING_CONFIRMATIONS = 3;
const int REQUIRED_DANGER_CONFIRMATIONS = 3;
const int REQUIRED_SAFE_CONFIRMATIONS = 2;
int warningConfirmations = 0;
int dangerConfirmations = 0;
int safeConfirmations = 0;

// ------------------------------------------------------------------------------
// STATUS SISTEM
// ------------------------------------------------------------------------------
enum SystemStatus {
  STATUS_AMAN,
  STATUS_WASPADA,
  STATUS_BAHAYA
};

SystemStatus currentStatus = STATUS_AMAN;
bool systemPowerState = false;
bool sensorBaselineValid = false;
bool dhtValid = false;

// ------------------------------------------------------------------------------
// FITUR PENGUJIAN SAMPEL MAKANAN & DETEKSI STABILITAS (STEADY-STATE)
// ------------------------------------------------------------------------------
bool sampleTestingActive = false;
unsigned long sampleStartMillis = 0;
bool sampleTestDone = false;
char sampleTriggerReason[40] = "";
int sampleDurationSec = 0;
unsigned long sampleTestId = 0;
int samplePhase = 0;  // 0=Idle, 1=Difusi uap 1 menit (alarm senyap), 2=Deteksi stabil

// Kalibrasi acuan udara bersih (durasi 60 detik)
bool isCalibrating = false;
int calSecRemaining = 0;

// Jendela kestabilan: toleransi <= 3 ADC selama 5 siklus berurutan (10 detik)
const int STABILITY_WINDOW = 5;
int stabilityMQ3[STABILITY_WINDOW] = {0};
int stabilityMQ135[STABILITY_WINDOW] = {0};
int stabilityIndex = 0;
int stabilitySamplesCount = 0;

// Snapshot data saat hasil uji sampel dikunci (locked)
int sampleLockedMQ3 = 0;
int sampleLockedMQ135 = 0;
float sampleLockedRiseMQ3 = 0.0f;
float sampleLockedRiseMQ135 = 0.0f;
float sampleLockedCombined = 0.0f;
SystemStatus sampleLockedStatus = STATUS_AMAN;
float sampleLockedTemp = 0.0f;
float sampleLockedHum = 0.0f;

// Durasi & toleransi uji sampel
const unsigned long SAMPLE_MIN_DELAY_MS   = 60000UL;   // 1 menit (60s) difusi uap makanan (tanpa alarm)
const unsigned long SAMPLE_MAX_TIMEOUT_MS = 300000UL;  // 5 menit (300s) batas waktu maksimal
const int ADC_STABILITY_TOLERANCE         = 3;         // Toleransi fluktuasi <= 3 ADC

// ------------------------------------------------------------------------------
// TOMBOL
// ------------------------------------------------------------------------------
unsigned long powerButtonPressStart = 0;
bool powerButtonWasPressed = false;
bool powerHoldExecuted = false;
const unsigned long HOLD_TIME_REQUIRED = 2000UL;

bool calibButtonWasPressed = false;
unsigned long lastCalibAction = 0;
const unsigned long BUTTON_DEBOUNCE_MS = 80UL;

// ------------------------------------------------------------------------------
// TIMING
// ------------------------------------------------------------------------------
unsigned long previousMillisSensor = 0;
const unsigned long SENSOR_INTERVAL_MS = 2000UL; // DHT11 lebih stabil pada 2 detik

unsigned long previousMillisBuzzer = 0;
bool buzzerState = false;

// ------------------------------------------------------------------------------
// FILTER SENSOR GAS
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
// PROTOTIPE FUNGSI
// ------------------------------------------------------------------------------
void runDualGasCalibration();
void startSampleTest();
void stopSampleTest(const char* reason);
void checkSampleStability(unsigned long currentMillis);
void updateOLEDSampleTesting(int sec);
void updateOLEDSampleResult(SystemStatus status, int sec);
void updateOLEDSummary(SystemStatus status, float combinedIndex);
void updateActuators(SystemStatus status, unsigned long currentMillis);
void printSerialLog();
void handleButtonControl();
void broadcastSensorData();
void toggleSystemPower(const char* source);
void readAndFilterSensors();
void calculateGasIndices();
void evaluateStatus();
float relativeRisePct(int value, int baseline);
float riskIndexFromRise(float risePct, float warningPct, float dangerPct);
const char* statusText(SystemStatus status);
void showStandbyOLED();
void showStabilizationOLED(unsigned long remainingMs);
void showBaselineInvalidOLED();
bool baselineADCValid(int value);
bool dualBaselineValid(int mq3Base, int mq135Base);
bool sensorIsStabilizing(unsigned long now);
void resetGasFilter(int seedMQ3, int seedMQ135);
void resetStatusConfirmations();

// ------------------------------------------------------------------------------
// WEB DASHBOARD - 100% OFFLINE
// ------------------------------------------------------------------------------
const char PAGE_INDEX[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html lang="id" data-theme="light">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width, initial-scale=1.0">
<title>SIDAKAN MBG · Deteksi Kelayakan MBG</title>
<style>
:root[data-theme="light"]{
  --bg:#f8fafc;--surface:#ffffff;--border:#e2e8f0;--text:#0f172a;--muted:#64748b;
  --soft:#f1f5f9;--green:#15803d;--amber:#b45309;--red:#b91c1c;--blue:#2563eb;
  --safe-bg:#f0fdf4;--safe-border:#bbf7d0;--safe-text:#166534;
  --warn-bg:#fffbeb;--warn-border:#fef08a;--warn-text:#854d0e;
  --danger-bg:#fef2f2;--danger-border:#fecaca;--danger-text:#991b1b;
  --card-shadow:0 1px 3px rgba(0,0,0,0.06);
}
:root[data-theme="dark"]{
  --bg:#0b0f17;--surface:#131926;--border:#273449;--text:#f8fafc;--muted:#94a3b8;
  --soft:#1e293b;--green:#86efac;--amber:#fde68a;--red:#fca5a5;--blue:#38bdf8;
  --safe-bg:rgba(22,101,52,.22);--safe-border:rgba(34,197,94,.4);--safe-text:#86efac;
  --warn-bg:rgba(133,77,14,.22);--warn-border:rgba(234,179,8,.4);--warn-text:#fde68a;
  --danger-bg:rgba(153,27,27,.25);--danger-border:rgba(239,68,68,.4);--danger-text:#fca5a5;
  --card-shadow:0 1px 4px rgba(0,0,0,0.3);
}
*{box-sizing:border-box}
body{margin:0;background:var(--bg);color:var(--text);font:14px system-ui,-apple-system,BlinkMacSystemFont,Segoe UI,Roboto,sans-serif;padding:16px}
.app{max-width:1080px;margin:auto;display:grid;gap:14px}
.top{display:flex;justify-content:space-between;align-items:center;gap:12px;flex-wrap:wrap;border-bottom:1px solid var(--border);padding-bottom:12px}
.brand b{font-size:18px;font-weight:800;letter-spacing:-0.3px}
.brand span{display:block;color:var(--muted);font-size:12px;margin-top:2px}
.header-right{display:flex;gap:8px;align-items:center;flex-wrap:wrap}

/* Mode Switcher Tabs */
.view-tabs{display:flex;background:var(--soft);border:1px solid var(--border);border-radius:10px;padding:3px;gap:2px}
.view-tab{border:none;background:transparent;color:var(--muted);padding:6px 14px;border-radius:8px;font-weight:700;font-size:12px;cursor:pointer;transition:all .15s}
.view-tab.active{background:var(--surface);color:var(--text);box-shadow:var(--card-shadow)}

.badge{border:1px solid var(--border);padding:6px 10px;border-radius:8px;font-weight:700;font-size:11px}
.badge.online{background:var(--safe-bg);color:var(--green);border-color:var(--safe-border)}
.badge.standby{background:var(--soft);color:var(--muted)}
.badge.active-power{background:var(--safe-bg);color:var(--green);border-color:var(--safe-border)}

button.btn{border:1px solid var(--border);background:var(--surface);color:var(--text);padding:8px 12px;border-radius:8px;font-weight:700;cursor:pointer;font-size:12px;transition:all .15s}
button.btn:hover{background:var(--soft)}
button.btn-primary{background:var(--text);color:var(--surface)}
button.btn-test{background:#15803d;color:#fff;border-color:#15803d;padding:12px 20px;font-size:14px;border-radius:10px;display:inline-flex;align-items:center;gap:8px;box-shadow:0 2px 6px rgba(21,128,61,0.3)}
button.btn-test:hover{background:#166534}
button.btn-test:disabled{background:var(--soft);color:var(--muted);border-color:var(--border);cursor:not-allowed;box-shadow:none}
button.btn-cancel{background:#dc2626;color:#fff;border-color:#dc2626;padding:8px 14px}
button.btn-cancel:hover{background:#b91c1c}

/* MODE SIMPLE (AWAM) STYLING */
.hero-card{border-radius:16px;border:2px solid var(--border);padding:22px;background:var(--surface);box-shadow:var(--card-shadow);transition:all .3s}
.hero-card.safe{background:var(--safe-bg);border-color:var(--safe-border)}
.hero-card.warn{background:var(--warn-bg);border-color:var(--warn-border)}
.hero-card.danger{background:var(--danger-bg);border-color:var(--danger-border)}
.hero-card.testing{background:rgba(37,99,235,.07);border-color:rgba(37,99,235,.35)}

.hero-header{display:flex;justify-content:space-between;align-items:center;gap:12px;flex-wrap:wrap}
.hero-badge{display:inline-block;padding:4px 10px;border-radius:99px;font-size:11px;font-weight:800;text-transform:uppercase;letter-spacing:.5px}
.hero-card.safe .hero-badge{background:var(--safe-border);color:var(--safe-text)}
.hero-card.warn .hero-badge{background:var(--warn-border);color:var(--warn-text)}
.hero-card.danger .hero-badge{background:var(--danger-border);color:var(--danger-text)}
.hero-card.testing .hero-badge{background:rgba(37,99,235,.2);color:var(--blue)}

.hero-title{margin:12px 0 6px;font-size:24px;font-weight:900}
.hero-desc{margin:0;font-size:14px;line-height:1.6;color:var(--muted)}
.hero-recom{margin-top:14px;padding:12px;border-radius:10px;background:rgba(255,255,255,0.6);border:1px solid rgba(0,0,0,0.06);font-size:13px;line-height:1.5;font-weight:600}
:root[data-theme="dark"] .hero-recom{background:rgba(0,0,0,0.3);border-color:rgba(255,255,255,0.08)}

.test-section{background:var(--surface);border:1px solid var(--border);border-radius:14px;padding:18px;text-align:center;box-shadow:var(--card-shadow)}
.timer-display{font:800 32px ui-monospace,Consolas,monospace;margin:6px 0;letter-spacing:1px;color:var(--blue)}
.test-guide{font-size:12px;color:var(--muted);margin-top:8px;line-height:1.5}
.progress-pulse{height:6px;background:var(--soft);border-radius:99px;overflow:hidden;margin:12px auto;max-width:320px}
.progress-pulse-bar{height:100%;width:40%;background:var(--blue);border-radius:99px;animation:pulseAnim 1.6s ease-in-out infinite}
@keyframes pulseAnim{0%{margin-left:0%;width:20%}50%{margin-left:50%;width:40%}100%{margin-left:80%;width:20%}}

/* Grid & Cards (Detail Mode) */
.grid{display:grid;grid-template-columns:1fr 1fr 1fr;gap:14px}
.card{background:var(--surface);border:1px solid var(--border);border-radius:12px;padding:16px;box-shadow:var(--card-shadow)}
.label{display:flex;justify-content:space-between;color:var(--muted);font-size:12px;font-weight:700}
.tag{background:var(--soft);padding:2px 7px;border-radius:999px}
.big{font:800 32px ui-monospace,Consolas,monospace;margin:8px 0 10px}.unit{font-size:14px;color:var(--muted)}
.kv{display:grid;grid-template-columns:1fr auto;gap:5px 12px;border-top:1px solid var(--border);padding-top:10px;font-size:12px}.kv span:nth-child(odd){color:var(--muted)}
.bar{height:9px;background:var(--soft);border-radius:99px;overflow:hidden;margin:8px 0}.fill{height:100%;width:0;background:var(--blue);transition:width .25s}
.fill.safe{background:#16a34a}.fill.warn{background:#d97706}.fill.danger{background:#dc2626}
.metrics{display:grid;grid-template-columns:1fr 1fr;gap:14px}
.metric{background:var(--surface);border:1px solid var(--border);border-radius:12px;padding:14px;display:flex;justify-content:space-between;align-items:center;box-shadow:var(--card-shadow)}
.metric .num{font:800 24px ui-monospace,Consolas,monospace}.metric small{color:var(--muted)}
.chart{background:var(--surface);border:1px solid var(--border);border-radius:12px;padding:14px;box-shadow:var(--card-shadow)}
.legend{display:flex;gap:16px;color:var(--muted);font-size:12px;margin-bottom:8px}.sw{width:18px;height:3px;display:inline-block;margin-right:5px;vertical-align:middle}.mq3c{background:#2563eb}.mq135c{background:#ea580c}
canvas{width:100%;height:180px;display:block}
.note{font-size:12px;color:var(--muted);line-height:1.5;background:var(--surface);border:1px solid var(--border);border-radius:12px;padding:12px}

/* Riwayat Log */
.log-box{background:var(--surface);border:1px solid var(--border);border-radius:14px;padding:18px;box-shadow:var(--card-shadow)}
.log-header{display:flex;justify-content:space-between;align-items:center;flex-wrap:wrap;gap:10px;margin-bottom:12px}
.log-title{font-size:14px;font-weight:800;margin:0}
.log-actions{display:flex;gap:6px}
.log-table-wrapper{overflow-x:auto}
table.log-table{width:100%;border-collapse:collapse;font-size:12px;text-align:left}
table.log-table th{background:var(--soft);padding:8px 10px;color:var(--muted);font-weight:700;border-bottom:1px solid var(--border)}
table.log-table td{padding:8px 10px;border-bottom:1px solid var(--border);white-space:nowrap}
.badge-status{display:inline-block;padding:3px 8px;border-radius:6px;font-weight:700;font-size:11px}
.badge-status.safe{background:var(--safe-bg);color:var(--green);border:1px solid var(--safe-border)}
.badge-status.warn{background:var(--warn-bg);color:var(--amber);border:1px solid var(--warn-border)}
.badge-status.danger{background:var(--danger-bg);color:var(--red);border:1px solid var(--danger-border)}

footer{color:var(--muted);font-size:11px;text-align:center;padding:8px 0}
@media(max-width:800px){.grid{grid-template-columns:1fr}.metrics{grid-template-columns:1fr}.big{font-size:28px}.hero-title{font-size:20px}}
</style>
</head>
<body>
<div class="app">
  <div class="top">
    <div class="brand"><b>SIDAKAN · Deteksi MBG</b><span>Sistem Cerdas Kelayakan MBG · Dual MQ</span></div>
    <div class="header-right">
      <div class="view-tabs">
        <button id="tabSimple" class="view-tab active" onclick="switchView('simple')">🌿 Sederhana</button>
        <button id="tabDetail" class="view-tab" onclick="switchView('detail')">⚙️ Detail Teknis</button>
      </div>
      <span id="conn" class="badge">Menghubungkan...</span>
      <span id="powerBadge" class="badge standby">STANDBY</span>
      <button id="powerBtn" class="btn btn-primary" onclick="togglePower()">Nyalakan</button>
      <button class="btn" onclick="calibrate()">Kalibrasi</button>
      <button class="btn" onclick="toggleTheme()">Tema</button>
    </div>
  </div>

  <!-- ============================================================== -->
  <!-- VIEW 1: MODE SEDERHANA (USER-FRIENDLY UNTUK AWAM)              -->
  <!-- ============================================================== -->
  <main id="viewSimple" style="display:grid; gap:14px;">
    <!-- 1. Hero Card Status Kelayakan MBG -->
    <section id="heroCard" class="hero-card">
      <div class="hero-header">
        <span id="heroTag" class="hero-badge">STATUS KELAYAKAN</span>
        <span id="heroMeta" style="font-size:12px; color:var(--muted);">Sensor: Dual MQ-3 &amp; MQ-135</span>
      </div>
      <h2 id="heroTitle" class="hero-title">Alat Sedang Siaga</h2>
      <p id="heroDesc" class="hero-desc">Nyalakan alat untuk memulai pengujian sampel makanan.</p>
      <div id="heroRecom" class="hero-recom" style="display:none;"></div>
    </section>

    <!-- 2. Kontrol & Timer Pengujian Sampel Terpandu -->
    <section class="test-section">
      <div id="testIdleState">
        <button id="btnStartTest" class="btn btn-test" onclick="startTest()" disabled>
          <span>🧪</span> <span>Mulai Uji Sampel Makanan</span>
        </button>
        <div id="testIdleGuide" class="test-guide">Nyalakan alat dan tunggu pemanasan sensor selesai untuk menguji sampel.</div>
      </div>

      <div id="testActiveState" style="display:none;">
        <div id="testPhaseTitle" style="font-size:13px; font-weight:700; color:var(--blue);">UJI SAMPEL MAKANAN SEDANG BERLANGSUNG</div>
        <div id="testTimer" class="timer-display">00:00 / 05:00</div>
        <div class="progress-pulse"><div class="progress-pulse-bar"></div></div>
        <div id="testPhaseDesc" style="font-size:12px; color:var(--muted); margin-bottom:12px;">Mendeteksi kestabilan respons gas (toleransi &le; 3 ADC)...</div>
        <button class="btn btn-cancel" onclick="stopTest()">⏹️ Batalkan Pengujian</button>
      </div>

      <div id="testDoneState" style="display:none;">
        <div style="font-size:14px; font-weight:800; color:var(--green); margin-bottom:4px;">✅ PENGUJIAN SAMPEL SELESAI &amp; TERKUNCI</div>
        <div id="testDoneInfo" style="font-size:12px; color:var(--muted); margin-bottom:12px;">Hasil pengujian telah disimpan otomatis ke log riwayat.</div>
        <button class="btn btn-primary" onclick="resetTestState()">🔄 Uji Sampel Makanan Baru</button>
      </div>
    </section>

    <!-- 3. Metrik Lingkungan Ringkas -->
    <section class="metrics">
      <div class="metric">
        <div><b>Suhu Ruang Uji</b><br><small id="tempSub">DHT11 · Kondisi Sampel</small></div>
        <div id="tempSimple" class="num">-- °C</div>
      </div>
      <div class="metric">
        <div><b>Kelembapan Ruang Uji</b><br><small id="humSub">DHT11 · Data Pendukung</small></div>
        <div id="humSimple" class="num">-- %RH</div>
      </div>
    </section>
  </main>

  <!-- ============================================================== -->
  <!-- VIEW 2: MODE DETAIL (INSTRUMEN TEKNIS & RISET ILMIAH)          -->
  <!-- ============================================================== -->
  <main id="viewDetail" style="display:none; gap:14px;">
    <section id="statusBox" class="card" style="border-left:5px solid var(--blue);">
      <h3 id="statusTitle" style="margin:0 0 4px; font-size:16px;">Alat Siaga</h3>
      <p id="statusDesc" style="margin:0; color:var(--muted); font-size:13px;">Nyalakan alat untuk memulai telemetri digital.</p>
    </section>

    <section class="grid">
      <article class="card">
        <div class="label"><span>Respons Alkohol / Etanol</span><span class="tag">MQ-3 · GPIO35</span></div>
        <div class="big"><span id="mq3Adc">--</span> <span class="unit">ADC</span></div>
        <div class="bar"><div id="mq3Bar" class="fill"></div></div>
        <div class="kv">
          <span>Baseline</span><b id="mq3Base">-- ADC</b>
          <span>Kenaikan relatif</span><b id="mq3Rise">-- %</b>
          <span>Indeks MQ-3</span><b id="mq3Risk">-- %</b>
          <span>Referensi riset</span><b>&le; 0,10 mg/L etanol*</b>
        </div>
      </article>

      <article class="card">
        <div class="label"><span>Respons Gas Berbahaya / VOC</span><span class="tag">MQ-135 · GPIO34</span></div>
        <div class="big"><span id="mq135Adc">--</span> <span class="unit">ADC</span></div>
        <div class="bar"><div id="mq135Bar" class="fill"></div></div>
        <div class="kv">
          <span>Baseline</span><b id="mq135Base">-- ADC</b>
          <span>Kenaikan relatif</span><b id="mq135Rise">-- %</b>
          <span>Indeks MQ-135</span><b id="mq135Risk">-- %</b>
          <span>Referensi riset</span><b>&Delta;NH3 &le; 5 ppm*</b>
        </div>
      </article>

      <article class="card">
        <div class="label"><span>Indeks Gas Gabungan</span><span class="tag">MQ-3 + MQ-135</span></div>
        <div class="big"><span id="combined">--</span> <span class="unit">%</span></div>
        <div class="bar"><div id="combinedBar" class="fill"></div></div>
        <div class="kv">
          <span>Interpretasi</span><b id="combinedText">--</b>
          <span>Formula</span><b>Rata-rata 50:50</b>
          <span>Ambang waspada</span><b>&ge; 50%</b>
          <span>Bahaya</span><b>Salah satu indeks 100%</b>
        </div>
      </article>
    </section>

    <section class="metrics">
      <div class="metric"><div><b>Suhu ruang sampel</b><br><small>DHT11 · data pendukung</small></div><div id="tempDetail" class="num">-- °C</div></div>
      <div class="metric"><div><b>Kelembapan ruang sampel</b><br><small>DHT11 · data pendukung</small></div><div id="humDetail" class="num">-- %RH</div></div>
    </section>

    <section class="chart">
      <div class="legend"><span><i class="sw mq3c"></i>MQ-3 ADC</span><span><i class="sw mq135c"></i>MQ-135 ADC</span></div>
      <canvas id="chart"></canvas>
    </section>

    <div class="note">
      * Nilai 0,10 mg/L etanol dan perubahan 5 ppm amonia adalah referensi penelitian MBG/MBG, bukan hasil konversi langsung ADC alat ini.
      Pada firmware ini, keputusan prototipe menggunakan baseline dan perubahan relatif sensor. Kalibrasi konsentrasi standar diperlukan sebelum menampilkan mg/L atau ppm sebagai hasil ukur.
    </div>
  </main>

  <!-- ============================================================== -->
  <!-- BAGIAN LOG RIWAYAT UJI SAMPEL (TERSEDIA DI KEDUA MODE)         -->
  <!-- ============================================================== -->
  <section class="log-box">
    <div class="log-header">
      <h3 class="log-title">📋 Riwayat Pengujian Sampel Makanan</h3>
      <div class="log-actions">
        <button class="btn" onclick="exportLogCSV()">📥 Unduh Laporan CSV</button>
        <button class="btn" onclick="clearLogHistory()">🗑️ Hapus Log</button>
      </div>
    </div>
    <div class="log-table-wrapper">
      <table class="log-table">
        <thead>
          <tr>
            <th>#</th>
            <th>Waktu Uji</th>
            <th>Durasi</th>
            <th>Status MBG</th>
            <th>Indeks Gas</th>
            <th>Suhu / RH</th>
            <th>Alasan Selesai</th>
          </tr>
        </thead>
        <tbody id="logTableBody">
          <tr><td colspan="7" style="text-align:center; color:var(--muted); padding:16px;">Belum ada riwayat pengujian sampel. Lakukan uji sampel untuk mencatat data.</td></tr>
        </tbody>
      </table>
    </div>
  </section>

  <footer>SIDAKAN · ESP32-S · Dual MQ-3 &amp; MQ-135 · Projek Kepemimpinan PPG UPI</footer>
</div>

<script>
let ws;
let power = false;
let currentMode = localStorage.getItem('sidakan_mode') || 'simple';
let testLogs = [];
try { testLogs = JSON.parse(localStorage.getItem('sidakan_logs')) || []; } catch(e){ testLogs = []; }

let lastLoggedTestId = 0;
let isCurrentlyTesting = false;
const N = 30, h3 = new Array(N).fill(0), h135 = new Array(N).fill(0);

function byId(id){ return document.getElementById(id); }

function switchView(mode){
  currentMode = mode;
  localStorage.setItem('sidakan_mode', mode);
  byId('tabSimple').className = 'view-tab' + (mode==='simple'?' active':'');
  byId('tabDetail').className = 'view-tab' + (mode==='detail'?' active':'');
  byId('viewSimple').style.display = (mode==='simple') ? 'grid' : 'none';
  byId('viewDetail').style.display = (mode==='detail') ? 'grid' : 'none';
  if(mode==='detail') setTimeout(draw, 50);
}

function initWS(){
  const host = location.hostname || '192.168.4.1';
  ws = new WebSocket('ws://' + host + ':81/');
  ws.onopen = () => { byId('conn').innerText = 'Online'; byId('conn').className = 'badge online'; };
  ws.onclose = () => { byId('conn').innerText = 'Terputus'; byId('conn').className = 'badge'; setTimeout(initWS, 2000); };
  ws.onmessage = e => { try { update(JSON.parse(e.data)); } catch(err){ console.log(err); } };
}

function clamp(v,a,b){ return Math.max(a, Math.min(b, v)); }
function setBar(id,v){ const el=byId(id); v=clamp(v,0,100); el.style.width=v+'%'; el.className='fill '+(v>=100?'danger':v>=50?'warn':'safe'); }

function formatTimer(sec){
  const m = Math.floor(sec / 60);
  const s = sec % 60;
  return (m<10?'0':'') + m + ':' + (s<10?'0':'') + s;
}

function update(d){
  power = d.power;
  byId('powerBadge').innerText = power ? 'AKTIF' : 'STANDBY';
  byId('powerBadge').className = 'badge ' + (power ? 'active-power' : 'standby');
  byId('powerBtn').innerText = power ? 'Matikan' : 'Nyalakan';

  // 1. UPDATE MODE DETAIL STATUS BOX
  const statusBox = byId('statusBox');
  if(!power){
    statusBox.className = 'card';
    byId('statusTitle').innerText = 'Alat Siaga';
    byId('statusDesc').innerText = 'Nyalakan alat untuk memulai pemantauan.';
  } else if(d.calibrating){
    statusBox.className = 'card'; statusBox.style.borderLeftColor = 'var(--blue)';
    byId('statusTitle').innerText = 'Kalibrasi Titik Nol (' + (d.calRemain||0) + 's tersisa)';
    byId('statusDesc').innerText = 'Mengunci nilai acuan udara bersih selama 1 menit penuh...';
  } else if(d.stabilizing){
    statusBox.className = 'card'; statusBox.style.borderLeftColor = 'var(--amber)';
    byId('statusTitle').innerText = 'Stabilisasi Sensor (' + (d.stabRemain||0) + 's tersisa)';
    byId('statusDesc').innerText = 'Pemanasan elemen sensor ~1 menit sebelum sampel dinilai.';
  } else if(!d.baselineValid){
    statusBox.className = 'card'; statusBox.style.borderLeftColor = 'var(--amber)';
    byId('statusTitle').innerText = 'Baseline Belum Valid';
    byId('statusDesc').innerText = 'Pembacaan dasar terlalu rendah/tinggi. Cek sensor lalu kalibrasi.';
  } else if(d.testing){
    statusBox.className = 'card'; statusBox.style.borderLeftColor = 'var(--blue)';
    const phaseStr = (d.phase === 1 || d.testSec < 60) ? 'Tahap 1: Difusi Uap (' + (60 - d.testSec) + 's)' : 'Tahap 2: Deteksi Stabil (' + formatTimer(d.testSec) + ')';
    byId('statusTitle').innerText = 'Uji Sampel Aktif · ' + phaseStr;
    byId('statusDesc').innerText = (d.phase === 1 || d.testSec < 60) ? 'Sensor membaca fluktuasi awal (1 menit pertama). Alarm hening.' : 'Mencari kestabilan <= 3 ADC atau batas maksimal 5 menit.';
  } else if(d.status==='AMAN'){
    statusBox.className = 'card'; statusBox.style.borderLeftColor = 'var(--green)';
    byId('statusTitle').innerText = 'Kondisi Normal';
    byId('statusDesc').innerText = 'Respons gas berada pada zona rendah terhadap baseline.';
  } else if(d.status==='WASPADA'){
    statusBox.className = 'card'; statusBox.style.borderLeftColor = 'var(--amber)';
    byId('statusTitle').innerText = 'Waspada';
    byId('statusDesc').innerText = 'Terjadi kenaikan respons gas. Periksa kualitas makanan.';
  } else {
    statusBox.className = 'card'; statusBox.style.borderLeftColor = 'var(--red)';
    byId('statusTitle').innerText = 'Bahaya';
    byId('statusDesc').innerText = 'Indikator gas mencapai zona bahaya prototipe!';
  }

  // 2. UPDATE MODE SEDERHANA (HERO CARD)
  const hero = byId('heroCard');
  const heroRecom = byId('heroRecom');

  if(!power){
    hero.className = 'hero-card';
    byId('heroTag').innerText = 'ALAT SIAGA';
    byId('heroTitle').innerText = 'Alat Belum Dinyalakan';
    byId('heroDesc').innerText = 'Tekan tombol "Nyalakan" di kanan atas untuk memulai sistem SIDAKAN.';
    heroRecom.style.display = 'none';
  } else if(d.calibrating){
    hero.className = 'hero-card warn';
    byId('heroTag').innerText = 'KALIBRASI UDARA BERSIH · ' + formatTimer(d.calRemain);
    byId('heroTitle').innerText = 'Sedang Mengkalibrasi Sensor (' + (d.calRemain||0) + 's tersisa)';
    byId('heroDesc').innerText = 'Sensor sedang mengukur acuan udara bersih selama 1 menit penuh. Harap jangan dekatkan makanan.';
    heroRecom.style.display = 'block';
    heroRecom.innerText = '⚖️ Kalibrasi memastikan titik nol sensor presisi. Harap tunggu hingga waktu habis.';
  } else if(d.stabilizing){
    hero.className = 'hero-card warn';
    byId('heroTag').innerText = 'PEMANASAN SENSOR · ' + formatTimer(d.stabRemain);
    byId('heroTitle').innerText = 'Sensor Sedang Dipanaskan (' + (d.stabRemain||0) + 's tersisa)';
    byId('heroDesc').innerText = 'Sensor memerlukan waktu pemanasan 1 menit agar pembacaan gas akurat dan stabil.';
    heroRecom.style.display = 'block';
    heroRecom.innerText = '💡 Harap tunggu stabilisasi selesai (' + formatTimer(d.stabRemain) + ') sebelum menguji sampel.';
  } else if(!d.baselineValid){
    hero.className = 'hero-card warn';
    byId('heroTag').innerText = 'SENSOR BELUM SIAP';
    byId('heroTitle').innerText = 'Baseline Belum Valid';
    byId('heroDesc').innerText = 'Kabel sensor kendor atau sirkuit belum stabil di udara bersih.';
    heroRecom.style.display = 'block';
    heroRecom.innerText = '⚠️ Pastikan sensor di udara bersih lalu tekan tombol "Kalibrasi".';
  } else if(d.testing){
    hero.className = 'hero-card testing';
    if(d.phase === 1 || d.testSec < 60){
      byId('heroTag').innerText = 'TAHAP 1/2: DIFUSI UAP · ' + formatTimer(d.testSec) + ' / 01:00';
      byId('heroTitle').innerText = 'Membaca Fluktuasi Awal Sampel...';
      byId('heroDesc').innerText = 'Sensor sedang beradaptasi dengan aroma makanan (1 menit pertama). Alarm hening.';
      heroRecom.style.display = 'block';
      heroRecom.innerText = '🔍 Pertahankan sungkup sensor tetap tertutup rapat di atas sampel makanan.';
    } else {
      byId('heroTag').innerText = 'TAHAP 2/2: DETEKSI KESTABILAN · ' + formatTimer(d.testSec) + ' / 05:00';
      byId('heroTitle').innerText = 'Mendeteksi Kestabilan Gas Makanan...';
      byId('heroDesc').innerText = 'Mencari kestabilan pembacaan (toleransi <= 3 ADC) untuk mengunci hasil akhir.';
      heroRecom.style.display = 'block';
      heroRecom.innerText = '⏳ Jika nilai stabil dalam 10 detik atau mencapai 5 menit, hasil otomatis terkunci.';
    }
  } else if(d.testDone){
    const s = d.status;
    hero.className = 'hero-card ' + (s==='AMAN'?'safe':s==='WASPADA'?'warn':'danger');
    byId('heroTag').innerText = 'HASIL UJI SAMPEL TERKUNCI';
    if(s==='AMAN'){
      byId('heroTitle').innerText = '✅ Makanan Segar & Layak Konsumsi';
      byId('heroDesc').innerText = 'Tidak terdeteksi kenaikan gas amonia/alkohol berbahaya. Respons aroma sangat rendah.';
      heroRecom.style.display = 'block';
      heroRecom.innerText = '🍱 Makanan memenuhi standar kelayakan dan aman disajikan.';
    } else if(s==='WASPADA'){
      byId('heroTitle').innerText = '⚠️ Perhatian: Kualitas Makanan Menurun';
      byId('heroDesc').innerText = 'Terdeteksi peningkatan gas aroma makanan di atas ambang wajar (' + (d.lockIdx||d.combined).toFixed(1) + '%).';
      heroRecom.style.display = 'block';
      heroRecom.innerText = '🔎 Cek fisik: periksa aroma, perubahan tekstur, dan waktu masak sebelum disajikan!';
    } else {
      byId('heroTitle').innerText = '🛑 BAHAYA: MAKANAN RUSAK / BASI!';
      byId('heroDesc').innerText = 'Konsentrasi gas pembusukan tinggi terdeteksi (' + (d.lockIdx||d.combined).toFixed(1) + '%).';
      heroRecom.style.display = 'block';
      heroRecom.innerText = '🚫 JANGAN DIKONSUMSI ATAU DISAJIKAN! Makanan berisiko menyebabkan keracunan MBG.';
    }
  } else {
    const s = d.status;
    hero.className = 'hero-card ' + (s==='AMAN'?'safe':s==='WASPADA'?'warn':'danger');
    byId('heroTag').innerText = 'PANTAUAN REAL-TIME';
    if(s==='AMAN'){
      byId('heroTitle').innerText = '🟢 Kondisi Udara / Makanan Normal';
      byId('heroDesc').innerText = 'Sensor siap untuk menguji sampel makanan.';
      heroRecom.style.display = 'none';
    } else if(s==='WASPADA'){
      byId('heroTitle').innerText = '🟡 Terdeteksi Kenaikan Gas Ringan';
      byId('heroDesc').innerText = 'Kenaikan gas mencapai zona waspada (' + d.combined.toFixed(1) + '%).';
      heroRecom.style.display = 'block';
      heroRecom.innerText = 'Cek apakah sampel makanan mulai basi atau terkontaminasi.';
    } else {
      byId('heroTitle').innerText = '🔴 Peringatan: Gas Tinggi Terdeteksi!';
      byId('heroDesc').innerText = 'Indikator gas mencapai zona bahaya!';
      heroRecom.style.display = 'block';
      heroRecom.innerText = 'Segera periksa sumber aroma tajam/makanan.';
    }
  }

  // 3. KONTROL UJI SAMPEL
  const readyForTest = power && !d.stabilizing && !d.calibrating && d.baselineValid;
  const btnStart = byId('btnStartTest');
  if(btnStart) btnStart.disabled = !readyForTest;

  const testIdle = byId('testIdleState');
  const testActive = byId('testActiveState');
  const testDone = byId('testDoneState');

  if(d.testing){
    testIdle.style.display = 'none';
    testActive.style.display = 'block';
    testDone.style.display = 'none';
    if(d.phase === 1 || d.testSec < 60){
      byId('testPhaseTitle').innerText = '🔬 TAHAP 1/2: MEMBACA AROMA SAMPEL (DIFUSI UAP)';
      byId('testTimer').innerText = formatTimer(d.testSec) + ' / 05:00 (Tahap 1: ' + (60 - d.testSec) + 's tersisa)';
      byId('testPhaseDesc').innerText = 'Sensor membaca fluktuasi awal setelah sampel dimasukkan. Alarm senyap.';
    } else {
      byId('testPhaseTitle').innerText = '⚖️ TAHAP 2/2: DETEKSI KESTABILAN RESPON GAS';
      byId('testTimer').innerText = formatTimer(d.testSec) + ' / 05:00';
      byId('testPhaseDesc').innerText = 'Mencari kestabilan (toleransi <= 3 ADC selama 10s) atau batas maksimal 5 menit...';
    }
  } else if(d.testDone){
    testIdle.style.display = 'none';
    testActive.style.display = 'none';
    testDone.style.display = 'block';
    byId('testDoneInfo').innerText = 'Uji selesai dalam ' + formatTimer(d.testSec) + ' (' + (d.testReason || 'Stabil') + ') · Indeks: ' + (d.lockIdx||d.combined).toFixed(1) + '%';
  } else {
    testIdle.style.display = 'block';
    testActive.style.display = 'none';
    testDone.style.display = 'none';
    if(d.calibrating){
      byId('testIdleGuide').innerText = 'Sedang kalibrasi acuan udara bersih (' + (d.calRemain||0) + 's tersisa)... Harap tunggu.';
    } else if(d.stabilizing){
      byId('testIdleGuide').innerText = 'Sensor sedang dipanaskan (' + (d.stabRemain||0) + 's tersisa)... Harap tunggu hingga siap.';
    } else if(readyForTest){
      byId('testIdleGuide').innerText = 'Alat siap. Letakkan sensor di atas makanan lalu klik tombol di atas.';
    } else {
      byId('testIdleGuide').innerText = 'Nyalakan alat dan tunggu pemanasan sensor selesai untuk menguji sampel.';
    }
  }

  // LOGIKA RECORD LOG SAAT TESTDONE BARU AKTIF & TEST ID BARU
  if(d.testing){
    isCurrentlyTesting = true;
  }
  if(d.testDone && isCurrentlyTesting && d.testId && d.testId !== lastLoggedTestId){
    lastLoggedTestId = d.testId;
    isCurrentlyTesting = false;
    saveLogEntry(d);
  }

  // 4. METRIK DATA DETAIL
  byId('mq3Adc').innerText = d.mq3;
  byId('mq3Base').innerText = d.baseMq3 + ' ADC';
  byId('mq3Rise').innerText = d.riseMq3.toFixed(1) + ' %';
  byId('mq3Risk').innerText = d.indexMq3.toFixed(1) + ' %';
  setBar('mq3Bar', d.indexMq3);

  byId('mq135Adc').innerText = d.mq135;
  byId('mq135Base').innerText = d.baseMq135 + ' ADC';
  byId('mq135Rise').innerText = d.riseMq135.toFixed(1) + ' %';
  byId('mq135Risk').innerText = d.indexMq135.toFixed(1) + ' %';
  setBar('mq135Bar', d.indexMq135);

  byId('combined').innerText = d.combined.toFixed(1);
  byId('combinedText').innerText = d.status;
  setBar('combinedBar', d.combined);

  const tStr = d.dhtValid ? d.temp.toFixed(1) + ' °C' : '--';
  const hStr = d.dhtValid ? d.hum.toFixed(1) + ' %RH' : '--';
  byId('tempSimple').innerText = tStr;
  byId('humSimple').innerText = hStr;
  byId('tempDetail').innerText = tStr;
  byId('humDetail').innerText = hStr;

  h3.push(d.mq3); h3.shift(); h135.push(d.mq135); h135.shift();
  if(currentMode==='detail') draw();
}

function togglePower(){ if(ws && ws.readyState===1) ws.send('TOGGLE_POWER'); }
function calibrate(){
  if(!power){ alert('Nyalakan sistem terlebih dahulu.'); return; }
  if(confirm('Kalibrasi kedua sensor sekarang? Pastikan ruang sensor berada di udara bersih.')){
    if(ws && ws.readyState===1) ws.send('CALIBRATE');
  }
}
function startTest(){
  if(!power){ alert('Nyalakan sistem terlebih dahulu.'); return; }
  if(ws && ws.readyState===1){
    isCurrentlyTesting = true;
    ws.send('START_TEST');
  } else {
    alert('Koneksi WebSocket terputus. Muat ulang halaman.');
  }
}
function stopTest(){
  isCurrentlyTesting = false;
  if(ws && ws.readyState===1) ws.send('STOP_TEST');
}
function resetTestState(){
  isCurrentlyTesting = false;
  if(ws && ws.readyState===1) ws.send('CLEAR_READY');
}

function toggleTheme(){
  const r = document.documentElement;
  const n = r.getAttribute('data-theme')==='dark'?'light':'dark';
  r.setAttribute('data-theme', n);
  if(currentMode==='detail') draw();
}

function draw(){
  const c = byId('chart'); if(!c) return;
  const ctx = c.getContext('2d'), r = c.getBoundingClientRect(), dpr = devicePixelRatio || 1;
  c.width = r.width * dpr; c.height = r.height * dpr; ctx.scale(dpr, dpr);
  const w = r.width, h = r.height, p = 28;
  ctx.clearRect(0,0,w,h);
  ctx.strokeStyle = getComputedStyle(document.documentElement).getPropertyValue('--border');
  ctx.lineWidth = 1;
  for(let i=0; i<=3; i++){
    let y = p + (h-2*p)*i/3;
    ctx.beginPath(); ctx.moveTo(p,y); ctx.lineTo(w-p,y); ctx.stroke();
  }
  const max = Math.max(1000, ...h3, ...h135) * 1.15;
  function line(arr, color){
    ctx.strokeStyle = color; ctx.lineWidth = 2; ctx.beginPath();
    arr.forEach((v,i) => {
      const x = p + (w-2*p)*i/(N-1);
      const y = h - p - (h-2*p)*clamp(v,0,max)/max;
      i ? ctx.lineTo(x,y) : ctx.moveTo(x,y);
    });
    ctx.stroke();
  }
  line(h3, '#2563eb'); line(h135, '#ea580c');
  ctx.fillStyle = getComputedStyle(document.documentElement).getPropertyValue('--muted');
  ctx.font = '10px monospace';
  ctx.fillText('0', 4, h-p+3);
  ctx.fillText(Math.round(max), 4, p+3);
}

// LOG SYSTEM (LOCALSTORAGE + TABLE + CSV EXPORT)
function saveLogEntry(d){
  const now = new Date();
  const timeStr = now.getHours().toString().padStart(2,'0') + ':' + now.getMinutes().toString().padStart(2,'0') + ':' + now.getSeconds().toString().padStart(2,'0');
  const dateStr = now.getFullYear() + '-' + (now.getMonth()+1).toString().padStart(2,'0') + '-' + now.getDate().toString().padStart(2,'0');

  const entry = {
    id: testLogs.length + 1,
    time: dateStr + ' ' + timeStr,
    duration: formatTimer(d.testSec),
    status: d.status,
    index: (d.lockIdx || d.combined).toFixed(1),
    temp: d.dhtValid ? d.temp.toFixed(1) + ' °C' : '--',
    hum: d.dhtValid ? d.hum.toFixed(1) + ' %' : '--',
    reason: d.testReason || 'Stabil'
  };
  testLogs.unshift(entry);
  if(testLogs.length > 50) testLogs.pop();
  localStorage.setItem('sidakan_logs', JSON.stringify(testLogs));
  renderLogTable();
}

function renderLogTable(){
  const tbody = byId('logTableBody');
  if(!tbody) return;
  if(!testLogs || testLogs.length === 0){
    tbody.innerHTML = '<tr><td colspan="7" style="text-align:center; color:var(--muted); padding:16px;">Belum ada riwayat pengujian sampel. Lakukan uji sampel untuk mencatat data.</td></tr>';
    return;
  }
  let html = '';
  testLogs.forEach((item, idx) => {
    const cls = item.status==='AMAN'?'safe':item.status==='WASPADA'?'warn':'danger';
    html += '<tr>' +
      '<td><b>#' + (testLogs.length - idx) + '</b></td>' +
      '<td>' + item.time + '</td>' +
      '<td>' + item.duration + '</td>' +
      '<td><span class="badge-status ' + cls + '">' + item.status + '</span></td>' +
      '<td><b>' + item.index + '%</b></td>' +
      '<td>' + item.temp + ' / ' + item.hum + '</td>' +
      '<td>' + item.reason + '</td>' +
      '</tr>';
  });
  tbody.innerHTML = html;
}

function exportLogCSV(){
  if(!testLogs || testLogs.length === 0){ alert('Belum ada data riwayat untuk diekspor.'); return; }
  let csv = 'No,Waktu,Durasi,Status Kelayakan,Indeks Gas (%),Suhu,Kelembapan,Keterangan Penghentian\n';
  testLogs.forEach((item, idx) => {
    csv += (idx + 1) + ',"' + item.time + '","' + item.duration + '","' + item.status + '",' + item.index + ',"' + item.temp + '","' + item.hum + '","' + item.reason + '"\n';
  });
  const blob = new Blob([csv], { type: 'text/csv;charset=utf-8;' });
  const link = document.createElement('a');
  link.href = URL.createObjectURL(blob);
  link.setAttribute('download', 'Laporan_Uji_MBG_SIDAKAN_' + Date.now() + '.csv');
  document.body.appendChild(link);
  link.click();
  document.body.removeChild(link);
}

function clearLogHistory(){
  if(!confirm('Hapus seluruh riwayat pengujian sampel dari perangkat ini?')) return;
  testLogs = [];
  localStorage.removeItem('sidakan_logs');
  renderLogTable();
}

window.addEventListener('resize', () => { if(currentMode==='detail') draw(); });
window.onload = () => {
  switchView(currentMode);
  renderLogTable();
  initWS();
  if(currentMode==='detail') draw();
};
</script>
</body>
</html>
)rawliteral";

// ==============================================================================
// WEB SERVER & WEBSOCKET
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

  // ----------------------------------------------------------------------------
  // >>> [PERBAIKAN 2/5: PENGAMAN WEBSOCKET] <<<
  // Membatasi panjang teks yang diterima (maks 32 char) dan menggunakan perbandingan
  // memcmp langsung tanpa membuat objek String baru yang membebani memori.
  // ----------------------------------------------------------------------------
  if (type != WStype_TEXT || length == 0 || length > 32) return;

  if (length == 12 && memcmp(payload, "TOGGLE_POWER", 12) == 0) {
    toggleSystemPower("Web Dashboard");
  } else if (length == 9 && memcmp(payload, "CALIBRATE", 9) == 0 && systemPowerState) {
    runDualGasCalibration();
  } else if (length == 10 && memcmp(payload, "START_TEST", 10) == 0) {
    startSampleTest();
  } else if (length == 9 && memcmp(payload, "STOP_TEST", 9) == 0) {
    stopSampleTest("Dibatalkan pengguna");
  } else if (length == 11 && memcmp(payload, "CLEAR_READY", 11) == 0) {
    sampleTestDone = false;
    samplePhase = 0;
    noTone(PIN_BUZZER);
    buzzerState = false;
    broadcastSensorData();
  }
}

// ------------------------------------------------------------------------------
// >>> [PERBAIKAN 3/5: BUFFER STATIS ANTI-CRASH / ANTI HEAP FRAGMENTATION] <<<
// Mengganti penyambungan String bertingkat (json += ...) dengan buffer char[640]
// dan snprintf(). Memori ESP32 tetap bersih dan tidak bocor, mencegah alat restart
// sendiri setelah menyala beberapa jam.
// ------------------------------------------------------------------------------
void broadcastSensorData() {
  char json[640];
  unsigned long now = millis();
  int stabRemain = 0;
  if (systemPowerState && sensorIsStabilizing(now)) {
    unsigned long elapsedStab = now - sensorStartMillis;
    if (elapsedStab < RUNTIME_STABILIZATION_MS) {
      stabRemain = (int)((RUNTIME_STABILIZATION_MS - elapsedStab) / 1000UL);
    }
  }

  snprintf(json, sizeof(json),
    "{\"power\":%s,\"stabilizing\":%s,\"stabRemain\":%d,\"baselineValid\":%s,"
    "\"calibrating\":%s,\"calRemain\":%d,"
    "\"mq3\":%d,\"mq135\":%d,"
    "\"baseMq3\":%d,\"baseMq135\":%d,"
    "\"riseMq3\":%.1f,\"riseMq135\":%.1f,"
    "\"indexMq3\":%.1f,\"indexMq135\":%.1f,"
    "\"combined\":%.1f,"
    "\"temp\":%.1f,\"hum\":%.1f,"
    "\"dhtValid\":%s,\"status\":\"%s\","
    "\"testing\":%s,\"phase\":%d,\"testSec\":%d,\"testDone\":%s,"
    "\"testReason\":\"%s\",\"lockIdx\":%.1f,\"testId\":%lu}",
    systemPowerState ? "true" : "false",
    sensorIsStabilizing(now) ? "true" : "false",
    stabRemain,
    sensorBaselineValid ? "true" : "false",
    isCalibrating ? "true" : "false",
    calSecRemaining,
    averageMQ3, averageMQ135,
    baselineMQ3, baselineMQ135,
    mq3RisePct, mq135RisePct,
    mq3Index, mq135Index,
    combinedGasIndex,
    currentTemp, currentHum,
    dhtValid ? "true" : "false",
    statusText(currentStatus),
    sampleTestingActive ? "true" : "false",
    samplePhase,
    sampleDurationSec,
    sampleTestDone ? "true" : "false",
    sampleTriggerReason,
    sampleLockedCombined,
    sampleTestId
  );
  webSocket.broadcastTXT(json);
}

// ==============================================================================
// SETUP
// ==============================================================================
void setup() {
  
  Serial.begin(115200);
  delay(400);

  Serial.println(F("\n===================================================="));
  Serial.println(F(" SIDAKAN v1.4.1 - DUAL MQ (STABILIZED)"));
  Serial.println(F("===================================================="));

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
  Serial.println(F("[ADC] MQ3=GPIO35, MQ135=GPIO34, attenuation=11dB"));

  Wire.begin(21, 22);
  oledReady = display.begin(SSD1306_SWITCHCAPVCC, OLED_I2C_ADDR);
  if (!oledReady) {
    Serial.println(F("[ERROR] OLED SSD1306 tidak terdeteksi."));
  }

  dht.begin();

  // Isi awal moving average kedua sensor.
  for (int i = 0; i < NUM_READINGS; i++) {
    mq3Readings[i] = analogRead(PIN_MQ3);
    mq135Readings[i] = analogRead(PIN_MQ135);
    totalMQ3 += mq3Readings[i];
    totalMQ135 += mq135Readings[i];
    delay(25);
  }
  averageMQ3 = totalMQ3 / NUM_READINGS;
  averageMQ135 = totalMQ135 / NUM_READINGS;

  // Baseline belum dinyatakan valid pada boot karena sensor MOS masih berubah
  // selama pemanasan/stabilisasi. Baseline awal akan diambil otomatis setelah
  // RUNTIME_STABILIZATION_MS, dan dapat dikalibrasi ulang dengan tombol GPIO18.
  baselineMQ3 = 0;
  baselineMQ135 = 0;
  sensorBaselineValid = false;
  sensorStartMillis = 0;
  lastAutoBaselineAttempt = 0;
  resetStatusConfirmations();

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

  // Self-test LED.
  digitalWrite(PIN_LED_HIJAU, HIGH); delay(150); digitalWrite(PIN_LED_HIJAU, LOW);
  digitalWrite(PIN_LED_KUNING, HIGH); delay(150); digitalWrite(PIN_LED_KUNING, LOW);
  digitalWrite(PIN_LED_MERAH, HIGH); delay(150); digitalWrite(PIN_LED_MERAH, LOW);

  Serial.print(F("[WI-FI] SSID : ")); Serial.println(AP_SSID);
  Serial.print(F("[WI-FI] IP   : ")); Serial.println(WiFi.softAPIP());
  Serial.println(F("[READY] Tahan tombol GPIO19 selama 2 detik untuk mengaktifkan."));
  Serial.println(F("[INFO] Tombol GPIO18 = kalibrasi MQ-3 + MQ-135."));
  Serial.println(F("[INFO] Disarankan kalibrasi setelah sensor stabil dan kondisi acuan konsisten."));

  showStandbyOLED();
  Serial.println(F("[OTA] Halaman admin: http://192.168.4.1/update (lihat IP AP aktual)"));
}

// ==============================================================================
// LOOP
// ==============================================================================
void loop() {
  const unsigned long now = millis();

  server.handleClient();
  otaServiceRestart();
  webSocket.loop();
  handleButtonControl();

  if (!systemPowerState) {
    digitalWrite(PIN_LED_HIJAU, LOW);
    digitalWrite(PIN_LED_KUNING, LOW);
    digitalWrite(PIN_LED_MERAH, LOW);
    noTone(PIN_BUZZER);
    return;
  }

  // Sensor MOS butuh waktu stabilisasi. Timer dimulai saat tombol POWER mengaktifkan sistem.
  if (sensorIsStabilizing(now)) {
    digitalWrite(PIN_LED_HIJAU, LOW);
    digitalWrite(PIN_LED_KUNING, HIGH);
    digitalWrite(PIN_LED_MERAH, LOW);
    noTone(PIN_BUZZER);
    showStabilizationOLED(RUNTIME_STABILIZATION_MS - (now - sensorStartMillis));
  }

  if (now - previousMillisSensor >= SENSOR_INTERVAL_MS) {
    previousMillisSensor = now;

    readAndFilterSensors();

    // Ambil baseline awal otomatis hanya jika kedua pembacaan berada pada
    // rentang ADC teknis yang masuk akal. Baseline 0-2 ADC TIDAK boleh menjadi acuan.
    if (!sensorIsStabilizing(now) && !sensorBaselineValid &&
        (lastAutoBaselineAttempt == 0 || now - lastAutoBaselineAttempt >= AUTO_BASELINE_RETRY_MS)) {
      lastAutoBaselineAttempt = now;
      if (dualBaselineValid(averageMQ3, averageMQ135)) {
        baselineMQ3 = averageMQ3;
        baselineMQ135 = averageMQ135;
        sensorBaselineValid = true;
        resetGasFilter(baselineMQ3, baselineMQ135);
        resetStatusConfirmations();
        Serial.print(F("[AUTO-CAL] Baseline VALID | MQ-3=")); Serial.print(baselineMQ3);
        Serial.print(F(" | MQ-135=")); Serial.println(baselineMQ135);
      } else {
        static unsigned long lastInvalidBaselineLog = 0;
        if (now - lastInvalidBaselineLog >= 10000UL) {
          lastInvalidBaselineLog = now;
          Serial.print(F("[AUTO-CAL] Baseline BELUM VALID | MQ-3=")); Serial.print(averageMQ3);
          Serial.print(F(" | MQ-135=")); Serial.print(averageMQ135);
          Serial.println(F(" | Cek wiring/pemanasan sensor."));
        }
      }
    }

    calculateGasIndices();

    if (!sensorIsStabilizing(now) && sensorBaselineValid) {
      evaluateStatus();

      // Evaluasi kestabilan pengujian sampel jika sedang aktif
      if (sampleTestingActive) {
        checkSampleStability(now);
      }

      // Perbarui tampilan OLED sesuai state pengujian
      if (sampleTestingActive) {
        updateOLEDSampleTesting(sampleDurationSec);
      } else if (sampleTestDone) {
        updateOLEDSampleResult(sampleLockedStatus, sampleDurationSec);
      } else {
        updateOLEDSummary(currentStatus, combinedGasIndex);
      }
    } else if (!sensorIsStabilizing(now)) {
      // Baseline tidak valid bukan kondisi BAHAYA makanan. Ini kondisi sensor.
      currentStatus = STATUS_AMAN;
      showBaselineInvalidOLED();
    }

    printSerialLog();
    broadcastSensorData();
  }

  if (!sensorIsStabilizing(now)) {
    if (sensorBaselineValid) {
      updateActuators(currentStatus, now);
    } else {
      // Baseline invalid = indikator sensor belum siap, bukan alarm bahaya makanan.
      digitalWrite(PIN_LED_HIJAU, LOW);
      digitalWrite(PIN_LED_KUNING, HIGH);
      digitalWrite(PIN_LED_MERAH, LOW);
      noTone(PIN_BUZZER);
    }
  }
   otaServiceRestart();
}

// ==============================================================================
// PEMBACAAN & FILTER SENSOR
// ==============================================================================
void readAndFilterSensors() {
  const int rawMQ3 = analogRead(PIN_MQ3);
  const int rawMQ135 = analogRead(PIN_MQ135);

  Serial.print(F("[RAW] MQ3=")); Serial.print(rawMQ3);
  Serial.print(F(" | MQ135=")); Serial.println(rawMQ135);

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
  // Guard teknis: 0-2 ADC dianggap terlalu dekat nol untuk dijadikan acuan.
  // Ini BUKAN ambang keamanan makanan.
  return value >= MIN_VALID_BASELINE_ADC && value <= MAX_VALID_BASELINE_ADC;
}

bool dualBaselineValid(int mq3Base, int mq135Base) {
  return baselineADCValid(mq3Base) && baselineADCValid(mq135Base);
}

bool sensorIsStabilizing(unsigned long now) {
  return systemPowerState &&
         (sensorStartMillis == 0 || (now - sensorStartMillis < RUNTIME_STABILIZATION_MS));
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

void resetStatusConfirmations() {
  warningConfirmations = 0;
  dangerConfirmations = 0;
  safeConfirmations = 0;
}

// ------------------------------------------------------------------------------
// >>> [PERBAIKAN 4/5: ANTI-ALARM PALSU (SYARAT KENAIKAN FISIK MINIMAL)] <<<
// Mensyaratkan kenaikan fisik minimal 35 poin ADC (MIN_SIGNIFICANT_DELTA).
// Jika kenaikan cuma 5 - 25 ADC (misal akibat hembusan angin, napas orang lewat,
// atau riak listrik pada baseline rendah), selisih tersebut diabaikan (dianggap 0%).
// Ini menghilangkan masalah "alat tiba-tiba nyala sendiri saat didiamkan di meja".
// ------------------------------------------------------------------------------
float relativeRisePct(int value, int baseline) {
  if (!baselineADCValid(baseline)) return 0.0f;

  const float delta = (float)value - (float)baseline;
  if (delta <= 0.0f) return 0.0f;

  const float MIN_SIGNIFICANT_DELTA = 35.0f;
  const float adaptiveDeadband = max((float)MIN_SIGNIFICANT_DELTA, (float)baseline * 0.10f);
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
    dangerConfirmations++;
    warningConfirmations = 0;
    safeConfirmations = 0;
  } else if (warningDetected) {
    warningConfirmations++;
    dangerConfirmations = 0;
    safeConfirmations = 0;
  } else {
    safeConfirmations++;
    warningConfirmations = 0;
    dangerConfirmations = 0;
  }

  if (dangerConfirmations >= REQUIRED_DANGER_CONFIRMATIONS) {
    currentStatus = STATUS_BAHAYA;
  } else if (warningConfirmations >= REQUIRED_WARNING_CONFIRMATIONS) {
    currentStatus = STATUS_WASPADA;
  } else if (safeConfirmations >= REQUIRED_SAFE_CONFIRMATIONS) {
    currentStatus = STATUS_AMAN;
  }
}

// ==============================================================================
// TOMBOL
// ==============================================================================
void handleButtonControl() {
  const unsigned long now = millis();

  const bool powerPressed = (digitalRead(PIN_BUTTON_POWER) == LOW);
  if (powerPressed) {
    if (!powerButtonWasPressed) {
      powerButtonWasPressed = true;
      powerHoldExecuted = false;
      powerButtonPressStart = now;
    } else if (!powerHoldExecuted && now - powerButtonPressStart >= HOLD_TIME_REQUIRED) {
      powerHoldExecuted = true;
      toggleSystemPower("Tombol GPIO19");
    }
  } else {
    powerButtonWasPressed = false;
    powerHoldExecuted = false;
  }

  const bool calibPressed = (digitalRead(PIN_BUTTON_CALIB) == LOW);
  if (calibPressed && !calibButtonWasPressed) {
    calibButtonWasPressed = true;
    if (now - lastCalibAction >= BUTTON_DEBOUNCE_MS) {
      lastCalibAction = now;
      if (systemPowerState) {
        runDualGasCalibration();
      } else {
        Serial.println(F("[BUTTON] Kalibrasi diabaikan: sistem standby."));
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

  sampleTestingActive = false;
  sampleTestDone = false;
  sampleTestId = 0;
  samplePhase = 0;
  buzzerState = false;
  noTone(PIN_BUZZER);

  if (systemPowerState) {
    sensorStartMillis = millis();
    previousMillisSensor = sensorStartMillis;
    previousMillisBuzzer = sensorStartMillis;
    lastAutoBaselineAttempt = 0;
    baselineMQ3 = 0;
    baselineMQ135 = 0;
    sensorBaselineValid = false;
    mq3RisePct = mq135RisePct = 0.0f;
    mq3Index = mq135Index = combinedGasIndex = 0.0f;
    currentStatus = STATUS_AMAN;
    resetStatusConfirmations();
  } else {
    sensorStartMillis = 0;
    lastAutoBaselineAttempt = 0;
    sensorBaselineValid = false;
    baselineMQ3 = 0;
    baselineMQ135 = 0;
    resetStatusConfirmations();
    currentStatus = STATUS_AMAN;
    showStandbyOLED();
  }
  broadcastSensorData();
}

// ==============================================================================
// KALIBRASI KEDUA SENSOR (60 DETIK PENUH)
// ==============================================================================
void runDualGasCalibration() {
  Serial.println(F("[CAL] Memulai kalibrasi acuan MQ-3 + MQ-135 selama 60 detik."));
  isCalibrating = true;
  sampleTestingActive = false;
  sampleTestDone = false;
  sampleTestId = 0;
  samplePhase = 0;
  buzzerState = false;
  noTone(PIN_BUZZER);

  for (int sec = 60; sec > 0; sec--) {
    calSecRemaining = sec;
    if (oledReady) {
      display.clearDisplay();
      display.setTextColor(SSD1306_WHITE);
      display.setTextSize(1);
      display.setCursor(12, 7); display.print(F("KALIBRASI DUAL MQ"));
      display.drawLine(0, 18, 127, 18, SSD1306_WHITE);
      display.setCursor(12, 28); display.print(F("Kondisi acuan"));
      display.setCursor(20, 42); display.print(F("Tunggu "));
      display.setTextSize(2); display.print(sec); display.print(F("s"));
      display.display();
    }
    broadcastSensorData();

    unsigned long t0 = millis();
    while (millis() - t0 < 1000UL) {
      server.handleClient();
      webSocket.loop();
      delay(5);
    }
  }

  isCalibrating = false;
  calSecRemaining = 0;

  // ----------------------------------------------------------------------------
  // >>> [PERBAIKAN 5/5: SAMPLING KALIBRASI NON-BLOCKING (WEB TETAP ONLINE)] <<<
  // Mengganti delay(40) statis dengan while-loop yang tetap melayani server & webSocket.
  // Mencegah dashboard web mengalami status 'Terputus' saat 30 sampel diambil.
  // ----------------------------------------------------------------------------
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

  // Jangan menerima baseline 0-2 ADC atau kondisi mendekati saturasi.
  // Nilai di bawah batas teknis tetap dianggap terlalu lemah untuk dijadikan acuan.
  if (!dualBaselineValid(candidateMQ3, candidateMQ135)) {
    sensorBaselineValid = false;
    mq3RisePct = mq135RisePct = 0.0f;
    mq3Index = mq135Index = combinedGasIndex = 0.0f;

    Serial.print(F("[CAL-ERROR] Baseline tidak valid | MQ-3=")); Serial.print(candidateMQ3);
    Serial.print(F(" | MQ-135=")); Serial.println(candidateMQ135);
    Serial.print(F("[CAL-ERROR] Rentang baseline yang diterima: "));
    Serial.print(MIN_VALID_BASELINE_ADC); Serial.print(F("..")); Serial.print(MAX_VALID_BASELINE_ADC);
    Serial.println(F(" ADC. Cek wiring, VCC/GND, divider AO, dan pemanasan sensor."));

    showBaselineInvalidOLED();
    noTone(PIN_BUZZER);
    broadcastSensorData();
    return;
  }

  baselineMQ3 = candidateMQ3;
  baselineMQ135 = candidateMQ135;

  // Reset rolling filter ke baseline baru supaya tidak membawa data lama.
  resetGasFilter(baselineMQ3, baselineMQ135);
  sensorBaselineValid = true;
  lastAutoBaselineAttempt = millis();
  resetStatusConfirmations();

  calculateGasIndices();
  evaluateStatus();

  Serial.print(F("[CAL] Baseline MQ-3   : ")); Serial.println(baselineMQ3);
  Serial.print(F("[CAL] Baseline MQ-135 : ")); Serial.println(baselineMQ135);

  if (oledReady) {
    display.clearDisplay();
    display.setTextColor(SSD1306_WHITE);
    display.setTextSize(1);
    display.setCursor(12, 8); display.print(F("KALIBRASI BERHASIL"));
    display.drawLine(0, 20, 127, 20, SSD1306_WHITE);
    display.setCursor(5, 29); display.print(F("MQ3   : ")); display.print(baselineMQ3);
    display.setCursor(5, 42); display.print(F("MQ135 : ")); display.print(baselineMQ135);
    display.display();
  }

  noTone(PIN_BUZZER);

  // Tetap layani webSocket & web server selama 700ms jeda baca display
  unsigned long tDisplay = millis();
  while (millis() - tDisplay < 700UL) {
    server.handleClient();
    webSocket.loop();
    delay(2);
  }
  updateOLEDSummary(currentStatus, combinedGasIndex);
  broadcastSensorData();
}

// ==============================================================================
// OLED - HANYA RINGKASAN STATUS + INDEKS GABUNGAN
// ==============================================================================
void updateOLEDSummary(SystemStatus status, float combinedIndex) {
  if (!oledReady) return;

  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);
  display.setTextSize(1);
  display.setCursor(38, 0); display.print(F("SIDAKAN"));
  display.drawLine(0, 11, 127, 11, SSD1306_WHITE);

  display.setCursor(4, 18); display.print(F("STATUS"));
  display.setTextSize(2);
  display.setCursor(4, 29); display.print(statusText(status));

  display.setTextSize(1);
  display.setCursor(4, 51); display.print(F("INDEKS GAS: "));
  display.print(combinedIndex, 0);
  display.print(F("%"));
  display.display();
}

void showStandbyOLED() {
  if (!oledReady) return;
  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);
  display.setTextSize(1);
  display.setCursor(38, 3); display.print(F("SIDAKAN"));
  display.drawLine(0, 14, 127, 14, SSD1306_WHITE);
  display.setCursor(21, 24); display.print(F("STANDBY"));
  display.setCursor(8, 38); display.print(F("Tahan POWER 2 detik"));
  display.setCursor(9, 53); display.print(F("IP 192.168.4.1"));
  display.display();
}

void showStabilizationOLED(unsigned long remainingMs) {
  static unsigned long lastDraw = 0;
  if (!oledReady || millis() - lastDraw < 500UL) return;
  lastDraw = millis();

  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);
  display.setTextSize(1);
  display.setCursor(38, 3); display.print(F("SIDAKAN"));
  display.drawLine(0, 14, 127, 14, SSD1306_WHITE);
  display.setCursor(16, 25); display.print(F("STABILISASI SENSOR"));
  display.setCursor(24, 42); display.print(F("Sisa: "));
  display.print((remainingMs + 999UL) / 1000UL);
  display.print(F(" detik"));
  display.display();
}

void showBaselineInvalidOLED() {
  if (!oledReady) return;
  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);
  display.setTextSize(1);
  display.setCursor(38, 3); display.print(F("SIDAKAN"));
  display.drawLine(0, 14, 127, 14, SSD1306_WHITE);
  display.setCursor(10, 23); display.print(F("BASELINE BELUM VALID"));
  display.setCursor(14, 37); display.print(F("Cek sensor/wiring"));
  display.setCursor(10, 51); display.print(F("Tekan CALIB ulang"));
  display.display();
}

// ==============================================================================
// LED & BUZZER
// ==============================================================================
void updateActuators(SystemStatus status, unsigned long currentMillis) {
  // Tentukan status yang ditampilkan oleh lampu indikator LED
  SystemStatus displayStatus = status;
  if (sampleTestDone) {
    displayStatus = sampleLockedStatus;
  }

  // Update LED Indikator
  switch (displayStatus) {
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

  // Aturan Alarm Buzzer:
  // 1. Buzzer HANYA boleh berbunyi ketika dalam mode pengujian sampel (atau setelah uji selesai BAHAYA)
  // 2. Dalam 1 menit pertama uji sampel (samplePhase == 1), buzzer DILARANG berbunyi meskipun status BAHAYA
  // 3. Pada fase 2 (setelah 1 menit) ATAU saat hasil terkunci BAHAYA, buzzer berbunyi BEEP dengan tempo & ketukan konstan (250ms ON / 250ms OFF)
  bool alarmActive = false;
  if (sampleTestingActive && samplePhase == 2 && status == STATUS_BAHAYA) {
    alarmActive = true;
  } else if (sampleTestDone && sampleLockedStatus == STATUS_BAHAYA) {
    alarmActive = true;
  }

  if (alarmActive) {
    const unsigned long BEEP_INTERVAL_MS = 250UL;  // Ketukan konstan: 250ms ON, 250ms OFF (120 BPM)
    if (currentMillis - previousMillisBuzzer >= BEEP_INTERVAL_MS) {
      previousMillisBuzzer = currentMillis;
      buzzerState = !buzzerState;
      if (buzzerState) {
        tone(PIN_BUZZER, 2000);
      } else {
        noTone(PIN_BUZZER);
      }
    }
  } else {
    noTone(PIN_BUZZER);
    buzzerState = false;
    previousMillisBuzzer = currentMillis;
  }
}

// ==============================================================================
// LOG SERIAL
// ==============================================================================
const char* statusText(SystemStatus status) {
  switch (status) {
    case STATUS_WASPADA: return "WASPADA";
    case STATUS_BAHAYA: return "BAHAYA";
    default: return "AMAN";
  }
}

void printSerialLog() {
  Serial.print(F("[DATA] MQ3=")); Serial.print(averageMQ3);
  Serial.print(F(" base=")); Serial.print(baselineMQ3);
  Serial.print(F(" rise=")); Serial.print(mq3RisePct, 1);
  Serial.print(F("% idx=")); Serial.print(mq3Index, 1);

  Serial.print(F(" | MQ135=")); Serial.print(averageMQ135);
  Serial.print(F(" base=")); Serial.print(baselineMQ135);
  Serial.print(F(" rise=")); Serial.print(mq135RisePct, 1);
  Serial.print(F("% idx=")); Serial.print(mq135Index, 1);

  Serial.print(F(" | Combined=")); Serial.print(combinedGasIndex, 1); Serial.print(F("%"));
  Serial.print(F(" | Status=")); Serial.print(statusText(currentStatus));

  if (dhtValid) {
    Serial.print(F(" | T=")); Serial.print(currentTemp, 1);
    Serial.print(F("C H=")); Serial.print(currentHum, 1); Serial.print(F("%"));
  } else {
    Serial.print(F(" | DHT=ERROR"));
  }
  Serial.println();
}

// ==============================================================================
// LOGIKA PENGUJIAN SAMPEL MAKANAN & KESTABILAN (STEADY-STATE)
// ==============================================================================
void startSampleTest() {
  if (!systemPowerState || sensorIsStabilizing(millis()) || !sensorBaselineValid) {
    Serial.println(F("[SAMPLE] Gagal mulai: sistem belum siap atau sensor belum stabil."));
    return;
  }
  sampleTestingActive = true;
  sampleTestDone = false;
  sampleStartMillis = millis();
  sampleTestId = millis();
  if (sampleTestId == 0) sampleTestId = 1;
  samplePhase = 1;
  sampleDurationSec = 0;
  stabilityIndex = 0;
  stabilitySamplesCount = 0;
  strcpy(sampleTriggerReason, "");
  buzzerState = false;
  noTone(PIN_BUZZER);
  Serial.println(F("[SAMPLE] >>> PENGUJIAN SAMPEL DIMULAI (Tahap 1: 1 menit difusi uap, alarm hening)..."));
  broadcastSensorData();
}

void stopSampleTest(const char* reason) {
  if (!sampleTestingActive && !sampleTestDone) return;
  sampleTestingActive = false;
  sampleTestDone = false;
  samplePhase = 0;
  buzzerState = false;
  noTone(PIN_BUZZER);
  Serial.print(F("[SAMPLE] Pengujian dibatalkan/direset: ")); Serial.println(reason);
  broadcastSensorData();
}

void checkSampleStability(unsigned long currentMillis) {
  if (!sampleTestingActive) return;

  unsigned long elapsed = currentMillis - sampleStartMillis;
  sampleDurationSec = (int)(elapsed / 1000UL);

  // FASE 1: 1 MENIT PERTAMA (0 - 60 detik) DIFUSI UAP / BACA NAIK-TURUN ADC
  // Pada fase ini sensor beradaptasi, alarm dilarang bersuara, dan pengujian belum boleh selesai.
  if (elapsed < SAMPLE_MIN_DELAY_MS) {
    samplePhase = 1;
    stabilityIndex = 0;
    stabilitySamplesCount = 0;
    return;
  }

  // FASE 2: DETEKSI KESTABILAN (SETELAH 1 MENIT S/D 5 MENIT)
  samplePhase = 2;

  // Rekam sampel ke circular window stabilitas (5 sampel = 10 detik)
  stabilityMQ3[stabilityIndex] = averageMQ3;
  stabilityMQ135[stabilityIndex] = averageMQ135;
  stabilityIndex = (stabilityIndex + 1) % STABILITY_WINDOW;
  if (stabilitySamplesCount < STABILITY_WINDOW) stabilitySamplesCount++;

  bool shouldFinish = false;
  const char* finishReason = "";

  // 1. Kriteria Stabilitas: toleransi naik-turun <= 3 poin ADC bertahan minimal 10 detik (5 sampel)
  if (stabilitySamplesCount >= STABILITY_WINDOW) {
    int min3 = stabilityMQ3[0], max3 = stabilityMQ3[0];
    int min135 = stabilityMQ135[0], max135 = stabilityMQ135[0];

    for (int i = 1; i < STABILITY_WINDOW; i++) {
      if (stabilityMQ3[i] < min3) min3 = stabilityMQ3[i];
      if (stabilityMQ3[i] > max3) max3 = stabilityMQ3[i];
      if (stabilityMQ135[i] < min135) min135 = stabilityMQ135[i];
      if (stabilityMQ135[i] > max135) max135 = stabilityMQ135[i];
    }

    int delta3 = max3 - min3;
    int delta135 = max135 - min135;

    if (delta3 <= ADC_STABILITY_TOLERANCE && delta135 <= ADC_STABILITY_TOLERANCE) {
      shouldFinish = true;
      finishReason = "Sensor Stabil (<= 3 ADC)";
    }
  }

  // 2. Kriteria Batas Waktu Maksimum: 5 Menit (300 detik, sudah termasuk 1 menit pertama)
  if (!shouldFinish && elapsed >= SAMPLE_MAX_TIMEOUT_MS) {
    shouldFinish = true;
    finishReason = "Batas Waktu Maksimal (5 Menit)";
  }

  if (shouldFinish) {
    sampleTestingActive = false;
    sampleTestDone = true;
    strncpy(sampleTriggerReason, finishReason, sizeof(sampleTriggerReason) - 1);
    sampleDurationSec = (int)(elapsed / 1000UL);

    // Kunci snapshot nilai hasil akhir pengujian
    sampleLockedMQ3 = averageMQ3;
    sampleLockedMQ135 = averageMQ135;
    sampleLockedRiseMQ3 = mq3RisePct;
    sampleLockedRiseMQ135 = mq135RisePct;
    sampleLockedCombined = combinedGasIndex;
    sampleLockedStatus = currentStatus;
    sampleLockedTemp = currentTemp;
    sampleLockedHum = currentHum;

    Serial.println(F("===================================================="));
    Serial.print(F("[SAMPLE SELESAI] Trigger: ")); Serial.println(finishReason);
    Serial.print(F("Status: ")); Serial.print(statusText(sampleLockedStatus));
    Serial.print(F(" | Durasi: ")); Serial.print(sampleDurationSec); Serial.println(F(" detik"));
    Serial.print(F("Indeks Gabungan: ")); Serial.print(sampleLockedCombined, 1); Serial.println(F("%"));
    Serial.println(F("===================================================="));

    // Notifikasi suara penanda rampung:
    // Jika AMAN atau WASPADA: 1 nada beep pendek (200ms) lalu senyap
    // Jika BAHAYA: updateActuators akan langsung membunyikan nada konstan
    if (sampleLockedStatus != STATUS_BAHAYA) {
      tone(PIN_BUZZER, 2200, 200);
      delay(200);
      noTone(PIN_BUZZER);
    }

    broadcastSensorData();
  }
}

void updateOLEDSampleTesting(int sec) {
  if (!oledReady) return;
  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);
  display.setTextSize(1);
  display.setCursor(20, 2); display.print(F("UJI SAMPEL AKTIF"));
  display.drawLine(0, 13, 127, 13, SSD1306_WHITE);

  display.setCursor(4, 18); display.print(F("WAKTU: "));
  int m = sec / 60;
  int s = sec % 60;
  if (m < 10) display.print(F("0"));
  display.print(m); display.print(F(":"));
  if (s < 10) display.print(F("0"));
  display.print(s);
  display.print(F(" / 05:00"));

  display.setCursor(4, 31); display.print(F("STATUS: "));
  display.print(statusText(currentStatus));

  display.setCursor(4, 43); display.print(F("Indeks: "));
  display.print(combinedGasIndex, 0); display.print(F("%"));

  display.setCursor(4, 55);
  if (sec < 60) {
    display.print(F("Tahap 1: Difusi uap"));
  } else {
    display.print(F("Tahap 2: Cek stabil"));
  }
  display.display();
}

void updateOLEDSampleResult(SystemStatus status, int sec) {
  if (!oledReady) return;
  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);
  display.setTextSize(1);
  display.setCursor(16, 2); display.print(F("HASIL UJI SAMPEL"));
  display.drawLine(0, 13, 127, 13, SSD1306_WHITE);

  display.setTextSize(2);
  display.setCursor(4, 18); display.print(statusText(status));

  display.setTextSize(1);
  display.setCursor(4, 38); display.print(F("Indeks: "));
  display.print(sampleLockedCombined, 0); display.print(F("% (Terkunci)"));

  display.setCursor(4, 52); display.print(F("Durasi: "));
  display.print(sec); display.print(F("s (Selesai)"));
  display.display();
}
