#ifndef SIDAKAN_TYPES_H
#define SIDAKAN_TYPES_H

#include <Arduino.h>

// ------------------------------------------------------------------------------
// DEFINISI ENUMERASI STATE MACHINE & STATUS
// ------------------------------------------------------------------------------
enum SystemStatus {
  STATUS_AMAN,
  STATUS_WASPADA,
  STATUS_BAHAYA
};

enum TestState {
  STATE_WARMUP,       // 1. Pemanasan elemen sensor SnO2 (60 detik)
  STATE_IDLE,         // 2. Siaga (Titik nol terkunci, tombol uji aktif)
  STATE_EXPOSURE,     // 3. Pengujian sampel 2-fase (Fase 1: difusi 60s, Fase 2: kestabilan <= 3 ADC, maks 300s)
  STATE_EVALUATION,   // 4. Tahap evaluasi / kestabilan
  STATE_LOCKED,       // 5. Penguncian nilai akhir, vonis kelayakan & simpan log
  STATE_RECOVERY,     // 6. Penetralan sensor (pembersihan ruang uji hingga +/-10% titik nol)
  STATE_CALIBRATING   // 7. Kalibrasi manual titik nol di udara bersih (60 detik)
};

enum PlateauStatus {
  PLATEAU_BELUM_STABIL,
  PLATEAU_MENINGKAT,
  PLATEAU_MENURUN,
  PLATEAU_STABIL
};

// ------------------------------------------------------------------------------
// STRUKTUR DATA RING BUFFER JENDELA KESTABILAN (16 TITIK / 30 DETIK)
// ------------------------------------------------------------------------------
const int PLATEAU_WINDOW_SIZE = 16;

struct RingBuffer16 {
  int data[PLATEAU_WINDOW_SIZE];
  int count;
  int head;
};

// ------------------------------------------------------------------------------
// PENGENDALI BUZZER NON-BLOCKING SEQUENCER
// ------------------------------------------------------------------------------
struct BuzzerPattern {
  int freq;
  unsigned long durationMs;
  unsigned long pauseMs;
  int repeatCount;
  int currentCycle;
  unsigned long stepStartMs;
  bool isPlaying;
  bool inSoundPhase;
};

#endif // SIDAKAN_TYPES_H
