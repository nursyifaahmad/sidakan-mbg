
#ifndef SIDAKAN_OTA_H
#define SIDAKAN_OTA_H

#include <WiFi.h>
#include <WebServer.h>
#include <Update.h>

// Ubah sesuai jaringan lokal
const char* OTA_WIFI_SSID = "U_SIDAKAN";
const char* OTA_WIFI_PASS = "updatenih";

// Ubah username dan password ini
const char* OTA_USER = "admin";
const char* OTA_PASS = "admin123";

WebServer otaServer(80);

bool otaStarted = false;
bool otaAuthorized = false;
bool otaSuccess = false;
bool otaRestartPending = false;
unsigned long otaRestartAt = 0;

const char OTA_PAGE[] PROGMEM = R"HTML(
<!DOCTYPE html>
<html lang="id">
<head>
<meta charset="UTF-8">
<meta name="viewport"
 content="width=device-width,initial-scale=1">
<title>SIDAKAN Firmware Update</title>
<style>
body{
 font-family:Arial,sans-serif;
 background:#edf3fa;
 padding:25px;
}
main{
 background:white;
 max-width:430px;
 margin:35px auto;
 padding:25px;
 border-radius:15px;
 box-shadow:0 4px 18px #0002;
}
h2{color:#173d66}
button{
 background:#173d66;
 color:white;
 border:0;
 padding:12px;
 border-radius:8px;
 cursor:pointer;
 width:100%;
 margin-top:16px;
}
input{width:100%;margin:15px 0}
small{color:#666}
</style>
</head>
<body>
<main>
<h2>SIDAKAN - Web OTA</h2>
<p>Pembaruan Firmware ESP32</p>
<form method="POST" action="/update"
 enctype="multipart/form-data">
<input type="file" name="firmware"
 accept=".bin" required>
<button type="submit">Upload Firmware</button>
</form>
<p><small>
Pastikan file .bin dibuat untuk board ESP32
dan skema partisi yang sama.
</small></p>
</main>
</body>
</html>
)HTML";

// Autentikasi seluruh akses OTA
bool otaCheckAuth() {
  if (otaServer.authenticate(OTA_USER, OTA_PASS)) {
    return true;
  }
  otaServer.requestAuthentication();
  return false;
}

void otaHandleUpload() {
  HTTPUpload& upload = otaServer.upload();

  if (upload.status == UPLOAD_FILE_START) {
    otaAuthorized =
      otaServer.authenticate(OTA_USER, OTA_PASS);

    otaSuccess = false;

    if (!otaAuthorized) return;

    Serial.printf(
      "[OTA] Mulai: %s\n",
      upload.filename.c_str()
    );

    if (!Update.begin(UPDATE_SIZE_UNKNOWN)) {
      Update.printError(Serial);
    }
  }

  else if (upload.status == UPLOAD_FILE_WRITE) {
    if (!otaAuthorized || Update.hasError()) return;

    if (Update.write(upload.buf, upload.currentSize)
        != upload.currentSize) {
      Update.printError(Serial);
    }
  }

  else if (upload.status == UPLOAD_FILE_END) {
    if (!otaAuthorized) return;

    otaSuccess = Update.end(true);

    if (otaSuccess) {
      Serial.printf(
        "[OTA] Berhasil: %u bytes\n",
        (unsigned int)upload.totalSize
      );
    } else {
      Update.printError(Serial);
    }
  }

  else if (upload.status == UPLOAD_FILE_ABORTED) {
    if (otaAuthorized) {
      Update.abort();
      Serial.println("[OTA] Dibatalkan");
    }
    otaSuccess = false;
  }
}

void otaBegin() {
  WiFi.mode(WIFI_STA);
  WiFi.begin(OTA_WIFI_SSID, OTA_WIFI_PASS);

  Serial.print("[OTA] Menghubungkan WiFi");

  unsigned long start = millis();

  while (WiFi.status() != WL_CONNECTED &&
         millis() - start < 15000) {
    delay(250);
    Serial.print(".");
  }

  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("\n[OTA] WiFi belum terhubung");
    return;
  }

  Serial.println();
  Serial.print("[OTA] IP: ");
  Serial.println(WiFi.localIP());

  otaServer.on("/", HTTP_GET, []() {
    if (!otaCheckAuth()) return;
    otaServer.send(200, "text/html", OTA_PAGE);
  });

  otaServer.on(
    "/update",
    HTTP_POST,
    []() {
      if (!otaCheckAuth()) return;

      if (otaSuccess && !Update.hasError()) {
        otaServer.send(
          200, "text/plain",
          "Firmware berhasil. ESP32 akan restart."
        );
        otaRestartPending = true;
        otaRestartAt = millis();
      } else {
        otaServer.send(
          500, "text/plain",
          "Update gagal. Lihat Serial Monitor."
        );
      }
    },
    otaHandleUpload
  );

  otaServer.begin();
  otaStarted = true;

  Serial.println("[OTA] Web OTA siap");
}

void otaLoop() {
  if (!otaStarted && WiFi.status() == WL_CONNECTED) {
    // Mulai server jika WiFi terhubung belakangan
    otaServer.on("/", HTTP_GET, []() {
      if (!otaCheckAuth()) return;
      otaServer.send(200, "text/html", OTA_PAGE);
    });

    otaServer.on(
      "/update",
      HTTP_POST,
      []() {
        if (!otaCheckAuth()) return;

        if (otaSuccess && !Update.hasError()) {
          otaServer.send(200, "text/plain",
                         "Update berhasil. Restart.");
          otaRestartPending = true;
          otaRestartAt = millis();
        } else {
          otaServer.send(500, "text/plain",
                         "Update gagal.");
        }
      },
      otaHandleUpload
    );

    otaServer.begin();
    otaStarted = true;
  }

  if (otaStarted && WiFi.status() == WL_CONNECTED) {
    otaServer.handleClient();
  }

  if (otaRestartPending &&
      millis() - otaRestartAt >= 1500) {
    ESP.restart();
  }
}

#endif
