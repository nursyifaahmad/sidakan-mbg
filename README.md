# SISTEM SIDAKAN (Sistem IoT Deteksi Dini Kelayakan Makanan)
> **Projek Kepemimpinan PPG Prajabatan Gelombang 1 Tahun 2026**  
> **Universitas Pendidikan Indonesia (UPI) · Bidang Studi Informatika**

Firmware mikrokontroler **ESP32-S** untuk pemantauan dini kelayakan dan kebusukan makanan (studi kasus: program Makan Bergizi Gratis / MBG) berbasis sensor gas ganda (*Dual MQ-Series*), sensor suhu-kelembapan lingkungan, layar OLED SSD1306, aktuasi hardware lokal, dan antarmuka *Web Dashboard real-time* mandiri (*100% offline access point*) dengan kontrol dua arah via WebSocket.

---

## 👥 Tim Peneliti & Pengembang (10 Mahasiswa)

| No | Nama Mahasiswa | NIM | Peran Utama |
|:--:|:---|:---:|:---|
| 1 | **Ahmad Nursyifa** | 2530712 | IoT Hardware & Architecture Lead |
| 2 | **Aldrian Rizki Kusuma** | 2530726 | Firmware & Signal Processing |
| 3 | **Andi Dwi Irfandi** | 2530689 | Sensor Calibration & Lab Testing |
| 4 | **Anthonio Akbar** | 2540710 | Firmware Development & Algorithm |
| 5 | **Dian Budi Elnursa** | 2530682 | Frontend & Web Dashboard UI/UX |
| 6 | **Farid Taufik Nugraha** | 2530705 | System Integration & Quality Assurance |
| 7 | **Fikri Hadi** | 2530703 | Data Analysis & Statistical Modeling |
| 8 | **Muhammad Chaerul Fajri**| 2530699 | Actuator & Circuit Safety Engineering |
| 9 | **Sajida I'tikafah Laila**| 2530772 | Research Documentation & Field Study |
| 10 | **Salman Haykal Ramadhan**| 2530797 | Field Validation & User Experience |

---

## 🚀 Fitur Unggulan Firmware v1.4.1

1. **Dual-Sensor Gas Komplementer:**
   - **MQ-3 (GPIO 35):** Deteksi dominan terhadap uap alkohol/etanol hasil fermentasi karbohidrat.
   - **MQ-135 (GPIO 34):** Deteksi spektrum luas terhadap amonia ($NH_3$), senyawa sulfida, dan *Volatile Organic Compounds* (VOC) hasil pembusukan protein.
2. **Dual-Mode Web Dashboard (Offline WiFi SoftAP):**
   - **Mode Sederhana (Awam):** Dirancang untuk guru, pengelola kantin, dan juru masak MBG. Menampilkan kartu kelayakan besar (🟢 Layak / 🟡 Perhatian / 🔴 Rusak) dengan bahasa praktis tanpa istilah teknis rumit.
   - **Mode Detail Teknis:** Menyajikan telemetri instrumen lengkap: ADC 12-bit, baseline, persentase kenaikan relatif, formula piecewise, canvas grafik garis realtime 30 sampel, dan catatan riset ilmiah.
3. **Protokol Uji Sampel Terpandu (*Guided Sample Testing*):**
   - Tombol pengujian aktif hanya setelah sensor menyelesaikan pemanasan (*stabilization*) dan baseline valid.
   - **Algoritma Steady-State:** Pengujian selesai otomatis saat fluktuasi ADC stabil ($\Delta \le 3$ ADC berturut-turut setelah jeda difusi awal 15 detik) ATAU batas waktu maksimal 5 menit (300 detik) tercapai.
   - **Notifikasi Aktuator:** Buzzer berbunyi bip 2x dan layar OLED mengunci hasil akhir.
4. **Sistem Pencatatan Log Riwayat & Ekspor CSV:**
   - Menyimpan seluruh riwayat uji sampel ke peramban (*localStorage*).
   - Dilengkapi tombol satu-klik **Unduh Laporan CSV** untuk analisis spreadsheet/Excel.
5. **Over-The-Air (OTA) Firmware Update:**
   - Pembaruan firmware nirkabel via portal web admin (`/update`) tanpa perlu mencolokkan kabel USB ke ESP32.
6. **Keamanan Sirkuit & Stabilitas Memori:**
   - Pagar validitas baseline ($80 \le \text{ADC} \le 4000$) mencegah pemrosesan data palsu akibat kabel kendor.
   - Filter *Adaptive Deadband* $\max(35, 10\% \times \text{Baseline})$ mengeliminasi riak derau hembusan udara.
   - Zero-heap fragmentation: transmisi JSON WebSocket menggunakan buffer statis `char[512]`.

---

## 🔌 Skema Pengkabelan (Hardware Pinout)

| Komponen | Pin ESP32-S | Keterangan & Rangkaian Pengaman |
|:---|:---:|:---|
| **MQ-3 (Analog Out)** | **GPIO 35** (ADC1_CH7) | **Pembagi Tegangan 2/3** (R1=10kΩ ke AO, R2=20kΩ ke GND) |
| **MQ-135 (Analog Out)**| **GPIO 34** (ADC1_CH6) | **Pembagi Tegangan 2/3** (R1=10kΩ ke AO, R2=20kΩ ke GND) |
| **DHT11 (Data Out)** | **GPIO 25** | Sensor suhu dan kelembapan ruang sampel |
| **OLED SSD1306 (SDA)** | **GPIO 21** | Komunikasi I2C (Alamat: `0x3C`) |
| **OLED SSD1306 (SCL)** | **GPIO 22** | Komunikasi I2C (Clock) |
| **LED Status Hijau** | **GPIO 33** | Indikator kondisi AMAN / NORMAL |
| **LED Status Kuning** | **GPIO 26** | Indikator WASPADA / FASE STABILISASI |
| **LED Status Merah** | **GPIO 27** | Indikator BAHAYA / MAKANAN RUSAK |
| **Buzzer Pasif/Aktif** | **GPIO 23** | Alarm nada 2000 Hz & bip pengujian selesai |
| **Tombol Power / Siaga**| **GPIO 19** | INPUT_PULLUP (Tahan 2 detik untuk Toggle ON/OFF) |
| **Tombol Kalibrasi** | **GPIO 18** | INPUT_PULLUP (Kalibrasi baseline udara bersih) |

> [!WARNING]
> Modul sensor MQ-series bekerja pada tegangan catu daya 5V. Pin ADC ESP32 hanya mampu menerima input maksimal 3.3V. Pastikan rangkaian pembagi tegangan 10kΩ/20kΩ terpasang di pin analog output kedua sensor MQ, serta seluruh ground (GND sensor, resistor, dan ESP32) terhubung pada satu titik bersama (*common ground*).

---

## 🛠️ Panduan Instalasi & Kompilasi (Arduino IDE 2.x)

### 1. Board Package
* Buka **Preferences** di Arduino IDE, tambahkan URL Board Manager:
  ```
  https://raw.githubusercontent.com/espressif/arduino-esp32/gh-pages/package_esp32_index.json
  ```
* Pasang paket board **esp32** oleh Espressif Systems (Board: **ESP32 Dev Module** atau **NodeMCU-32S**).

### 2. Pustaka (Libraries) yang Diperlukan
Pasang pustaka berikut melalui *Library Manager*:
* `WebSockets` oleh Markus Sattler (v2.4.x / v2.7.x)
* `Adafruit SSD1306` oleh Adafruit
* `Adafruit GFX Library` oleh Adafruit
* `Adafruit BusIO` oleh Adafruit
* `DHT sensor library` oleh Adafruit
* `Adafruit Unified Sensor` oleh Adafruit

### 3. Konfigurasi Upload
* **Upload Speed:** `921600` (atau `115200`)
* **Flash Frequency:** `80MHz`
* **Partition Scheme:** `Default 4MB with spiffs (1.2MB APP/1.5MB SPIFFS)` atau `Minimal SPIFFS (1.9MB APP)`

---

## 📱 Cara Penggunaan Alat

1. **Nyalakan Alat:** Hubungkan ESP32 ke catu daya 5V (USB/Adaptor).
2. **Koneksi WiFi:** Hubungkan smartphone atau laptop ke WiFi:
   - **SSID:** `SIDAKAN-WIFI`
   - **Password:** `ppginformatikaupi`
3. **Buka Dashboard:** Buka peramban (*Chrome / Edge / Safari*) dan akses alamat:
   ```
   http://192.168.4.1/
   ```
4. **Pemanasan:** Tekan tombol **Nyalakan** di dashboard (atau tahan tombol GPIO 19 selama 2 detik). Tunggu hitung mundur pemanasan sensor 60 detik.
5. **Uji Sampel:** Dekatkan sensor ke makanan, klik **"🧪 Mulai Uji Sampel Makanan"**. Sistem akan mengamati aroma hingga stabil ($\le 3$ ADC) atau 5 menit, lalu mengunci hasil akhir dan mencatatnya ke log riwayat.
6. **Ekspor Data:** Klik **"📥 Unduh Laporan CSV"** untuk mengunduh rekap riwayat uji ke komputer/HP.

---

## 📄 Lisensi
Dikembangkan sebagai bagian dari luaran akademik mata kuliah **Projek Kepemimpinan**, Program Pendidikan Profesi Guru (PPG) Prajabatan, **Universitas Pendidikan Indonesia (UPI)**, 2026.
