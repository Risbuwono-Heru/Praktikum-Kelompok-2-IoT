// ============================================================
//   SMART GARDEN - ESP8266 + BLYNK
//
//   KOMPONEN:
//   - ESP8266 (NodeMCU / Wemos D1 Mini)
//   - BH1750     : Sensor Cahaya
//   - DHT22      : Sensor Suhu & Kelembaban Udara
//   - Soil V2.0  : Sensor Kelembaban Tanah
//   - Relay IN1  : Pompa Air  (pin D6)
//   - Relay IN2  : Lampu      (pin D7)
//
//   SAMBUNGAN PIN:
//   BH1750  : VCC->3V | GND->G | SCL->D1 | SDA->D2
//   DHT22   : (+)->3V | OUT->D5 | (-)->G
//   Soil    : VCC->3V | GND->G | AOUT->A0
//   Relay   : VCC->VIN/5V | GND->G | IN1->D6 | IN2->D7
//
//   BLYNK VIRTUAL PIN:
//   V0 -> Suhu Udara (°C)          [Value Display]
//   V1 -> Kelembaban Udara (%)     [Value Display]
//   V2 -> Intensitas Cahaya (lux)  [Value Display]
//   V3 -> Kelembaban Tanah (%)     [Value Display]
//   V4 -> Tombol Pompa Air         [Button / Switch]
//   V5 -> Tombol Lampu             [Button / Switch]
//   V6 -> Mode Pompa (Auto/Manual) [Switch]
//   V7 -> Mode Lampu (Auto/Manual) [Switch]
//
//   LIBRARY (install via Library Manager):
//   - Blynk by Volodymyr Shymanskyy
//   - DHT sensor library by Adafruit
//   - BH1750 by Christopher Laws
// ============================================================


// ============================================================
// BAGIAN 1 - KONFIGURASI BLYNK & WIFI
// Ganti tiga baris di bawah dengan milik Anda
// ============================================================

#define BLYNK_TEMPLATE_ID "TMPL6pQ7oAAWb"
#define BLYNK_TEMPLATE_NAME "Agriculture Smart Farm Kit"
#define BLYNK_AUTH_TOKEN "zs8EPN8Lhr_0qFvCXPkZZqLV8IFtHd1d"
#define BLYNK_PRINT Serial

#include <ESP8266WiFi.h>
#include <BlynkSimpleEsp8266.h>

char ssid[] = "A14";      // <- Nama WiFi
char pass[] = "heru1234";  // <- Password WiFi


// ============================================================
// BAGIAN 2 - LIBRARY SENSOR
// ============================================================

#include <Wire.h>
#include <BH1750.h>
#include <DHT.h>


// ============================================================
// BAGIAN 3 - DEFINISI PIN
// ============================================================

#define SDA_PIN    D2
#define SCL_PIN    D1

#define DHTPIN     D5
#define DHTTYPE    DHT22

#define SOIL_PIN   A0

#define POMPA_PIN  D6   // Relay IN1 -> Pompa Air
#define LAMPU_PIN  D7   // Relay IN2 -> Lampu

// Relay 2 Channel umumnya ACTIVE LOW
// LOW  = relay ON (pompa/lampu menyala)
// HIGH = relay OFF (pompa/lampu mati)
// Jika terbalik, tukar nilai di bawah ini
#define RELAY_ON   LOW
#define RELAY_OFF  HIGH


// ============================================================
// BAGIAN 4 - KALIBRASI SOIL MOISTURE
//
// Cara kalibrasi:
// 1. Upload kode, buka Serial Monitor
// 2. Celupkan sensor ke air bersih -> lihat nilai ADC -> isi SOIL_BASAH
// 3. Angkat dan keringkan sensor   -> lihat nilai ADC -> isi SOIL_KERING
// ============================================================

#define SOIL_BASAH   300   // nilai ADC saat sensor di air / tanah basah
#define SOIL_KERING  750   // nilai ADC saat sensor di udara / tanah kering


// ============================================================
// BAGIAN 5 - BATAS OTOMATIS (THRESHOLD)
//
// --- Pompa Air (berdasarkan kelembaban tanah) ---
// Pompa ON  jika kelembaban tanah < POMPA_BATAS_NYALA
// Pompa OFF jika kelembaban tanah > POMPA_BATAS_MATI
//
// --- Lampu (berdasarkan intensitas cahaya) ---
// Lampu ON  jika cahaya < LAMPU_BATAS_NYALA  (gelap)
// Lampu OFF jika cahaya > LAMPU_BATAS_MATI   (sudah terang)
// ============================================================

#define POMPA_BATAS_NYALA  35    // % kelembaban tanah -> pompa menyala
#define POMPA_BATAS_MATI   65    // % kelembaban tanah -> pompa berhenti

#define LAMPU_BATAS_NYALA  500   // lux -> lampu menyala (gelap)
#define LAMPU_BATAS_MATI   1000  // lux -> lampu mati (sudah terang)


// ============================================================
// BAGIAN 6 - OBJEK SENSOR & TIMER
// ============================================================

DHT       dht(DHTPIN, DHTTYPE);
BH1750    lightMeter;
BlynkTimer timer;


// ============================================================
// BAGIAN 7 - VARIABEL GLOBAL
// ============================================================

// Data sensor
float suhu            = 0;
float kelembabanUdara = 0;
float cahaya          = 0;
int   soilPersen      = 0;

// Status aktuator
bool statusPompa = false;  // false = OFF, true = ON
bool statusLampu = false;

// Mode operasi masing-masing aktuator
bool modeOtomatisPompa = false;  // false = Manual, true = Otomatis
bool modeOtomatisLampu = false;

// Anti-spam notifikasi (minimal 5 menit antar notifikasi)
unsigned long waktuNotifSuhu  = 0;
unsigned long waktuNotifTanah = 0;
#define JEDA_NOTIF 300000UL


// ============================================================
// BAGIAN 8 - FUNGSI BANTU SENSOR
// ============================================================

// Rata-rata 10x pembacaan soil untuk mengurangi noise
int bacaSoilRataRata() {
  long total = 0;
  for (int i = 0; i < 10; i++) {
    total += analogRead(SOIL_PIN);
    delay(20);
  }
  return total / 10;
}

// Konversi ADC soil -> persen kelembaban (0-100%)
int hitungPersenSoil(int nilaiADC) {
  int persen = map(nilaiADC, SOIL_KERING, SOIL_BASAH, 0, 100);
  return constrain(persen, 0, 100);
}

// Label status tanah
String labelTanah(int persen) {
  if (persen < 30)      return "Kering";
  else if (persen < 65) return "Lembab";
  else                  return "Basah";
}


// ============================================================
// BAGIAN 9 - FUNGSI KONTROL POMPA AIR
// ============================================================

void nyalakanPompa() {
  digitalWrite(POMPA_PIN, RELAY_ON);
  statusPompa = true;
  Blynk.virtualWrite(V4, 1);  // sinkron tampilan tombol di app
  Serial.println("[POMPA] ON");
}

void matikanPompa() {
  digitalWrite(POMPA_PIN, RELAY_OFF);
  statusPompa = false;
  Blynk.virtualWrite(V4, 0);
  Serial.println("[POMPA] OFF");
}


// ============================================================
// BAGIAN 10 - FUNGSI KONTROL LAMPU
// ============================================================

void nyalakanLampu() {
  digitalWrite(LAMPU_PIN, RELAY_ON);
  statusLampu = true;
  Blynk.virtualWrite(V5, 1);  // sinkron tampilan tombol di app
  Serial.println("[LAMPU] ON");
}

void matikanLampu() {
  digitalWrite(LAMPU_PIN, RELAY_OFF);
  statusLampu = false;
  Blynk.virtualWrite(V5, 0);
  Serial.println("[LAMPU] OFF");
}


// ============================================================
// BAGIAN 11 - LOGIKA MODE OTOMATIS
//
// Pompa : dikontrol berdasarkan kelembaban tanah
// Lampu : dikontrol berdasarkan intensitas cahaya
// ============================================================

void kontrolOtomatisPompa() {
  if (!modeOtomatisPompa) return;

  if (soilPersen < POMPA_BATAS_NYALA && !statusPompa) {
    nyalakanPompa();
    Serial.println("[AUTO POMPA] Tanah kering -> Pompa ON");
  }
  else if (soilPersen >= POMPA_BATAS_MATI && statusPompa) {
    matikanPompa();
    Serial.println("[AUTO POMPA] Tanah cukup lembab -> Pompa OFF");
  }
}

void kontrolOtomatisLampu() {
  if (!modeOtomatisLampu) return;

  if (cahaya < LAMPU_BATAS_NYALA && !statusLampu) {
    nyalakanLampu();
    Serial.println("[AUTO LAMPU] Cahaya redup -> Lampu ON");
  }
  else if (cahaya >= LAMPU_BATAS_MATI && statusLampu) {
    matikanLampu();
    Serial.println("[AUTO LAMPU] Cahaya cukup terang -> Lampu OFF");
  }
}


// ============================================================
// BAGIAN 12 - NOTIFIKASI BLYNK
// Event harus dibuat terlebih dahulu di Blynk Dashboard
// (lihat panduan di bawah kode ini)
// ============================================================

void cekDanKirimNotifikasi() {
  unsigned long sekarang = millis();

  if (suhu > 35.0 && (sekarang - waktuNotifSuhu > JEDA_NOTIF)) {
    Blynk.logEvent("suhu_tinggi", String("Suhu ") + suhu + "°C! Tanaman perlu perhatian.");
    waktuNotifSuhu = sekarang;
    Serial.println("[NOTIF] Suhu tinggi -> dikirim ke Blynk");
  }

  if (soilPersen < 20 && (sekarang - waktuNotifTanah > JEDA_NOTIF)) {
    Blynk.logEvent("tanah_kering", String("Tanah sangat kering! Kelembaban: ") + soilPersen + "%");
    waktuNotifTanah = sekarang;
    Serial.println("[NOTIF] Tanah sangat kering -> dikirim ke Blynk");
  }
}


// ============================================================
// BAGIAN 13 - FUNGSI UTAMA: BACA SENSOR & KIRIM KE BLYNK
// Dipanggil oleh timer setiap 3 detik
// ============================================================

void kirimDataSensor() {

  // --- Baca DHT22 ---
  float bacaSuhu = dht.readTemperature();
  float bacaRH   = dht.readHumidity();
  if (!isnan(bacaSuhu) && !isnan(bacaRH)) {
    suhu = bacaSuhu;
    kelembabanUdara = bacaRH;
  } else {
    Serial.println("[ERROR] DHT22 gagal dibaca");
  }

  // --- Baca BH1750 ---
  float bacaCahaya = lightMeter.readLightLevel();
  if (bacaCahaya >= 0) {
    cahaya = bacaCahaya;
  } else {
    Serial.println("[ERROR] BH1750 gagal dibaca");
  }

  // --- Baca Soil Moisture ---
  int nilaiADC = bacaSoilRataRata();
  soilPersen   = hitungPersenSoil(nilaiADC);

  // --- Tampilkan di Serial Monitor ---
  Serial.println();
  Serial.println("========== DATA SENSOR ==========");
  Serial.print("Suhu Udara        : "); Serial.print(suhu);           Serial.println(" °C");
  Serial.print("Kelembaban Udara  : "); Serial.print(kelembabanUdara);Serial.println(" %");
  Serial.print("Cahaya            : "); Serial.print(cahaya);         Serial.println(" lux");
  Serial.print("Kelembaban Tanah  : "); Serial.print(soilPersen);     Serial.println(" %");
  Serial.print("Status Tanah      : "); Serial.println(labelTanah(soilPersen));
  Serial.println("----- STATUS AKTUATOR -----");
  Serial.print("Pompa  : "); Serial.print(statusPompa ? "ON " : "OFF");
  Serial.print("  | Mode: "); Serial.println(modeOtomatisPompa ? "OTOMATIS" : "MANUAL");
  Serial.print("Lampu  : "); Serial.print(statusLampu ? "ON " : "OFF");
  Serial.print("  | Mode: "); Serial.println(modeOtomatisLampu ? "OTOMATIS" : "MANUAL");
  Serial.println("=================================");

  // --- Kirim ke Blynk ---
  Blynk.virtualWrite(V0, suhu);
  Blynk.virtualWrite(V1, kelembabanUdara);
  Blynk.virtualWrite(V2, cahaya);
  Blynk.virtualWrite(V3, soilPersen);

  // --- Jalankan logika otomatis ---
  kontrolOtomatisPompa();
  kontrolOtomatisLampu();

  // --- Kirim notifikasi jika perlu ---
  cekDanKirimNotifikasi();
}


// ============================================================
// BAGIAN 14 - HANDLER INPUT DARI BLYNK (tombol di HP)
// ============================================================

// V4 = Tombol Pompa Air (hanya aktif saat mode MANUAL)
BLYNK_WRITE(V4) {
  if (modeOtomatisPompa) {
    // Kembalikan tampilan tombol agar tidak membingungkan
    Blynk.virtualWrite(V4, statusPompa ? 1 : 0);
    Serial.println("[V4] Diabaikan: Mode Pompa sedang OTOMATIS");
    return;
  }
  int nilai = param.asInt();
  if (nilai == 1) nyalakanPompa();
  else            matikanPompa();
}

// V5 = Tombol Lampu (hanya aktif saat mode MANUAL)
BLYNK_WRITE(V5) {
  if (modeOtomatisLampu) {
    Blynk.virtualWrite(V5, statusLampu ? 1 : 0);
    Serial.println("[V5] Diabaikan: Mode Lampu sedang OTOMATIS");
    return;
  }
  int nilai = param.asInt();
  if (nilai == 1) nyalakanLampu();
  else            matikanLampu();
}

// V6 = Switch Mode Pompa (0 = Manual, 1 = Otomatis)
BLYNK_WRITE(V6) {
  modeOtomatisPompa = (param.asInt() == 1);
  if (modeOtomatisPompa) {
    matikanPompa();  // reset dulu, biar otomatis yang atur
    Serial.println("[MODE POMPA] -> OTOMATIS");
  } else {
    Serial.println("[MODE POMPA] -> MANUAL");
  }
}

// V7 = Switch Mode Lampu (0 = Manual, 1 = Otomatis)
BLYNK_WRITE(V7) {
  modeOtomatisLampu = (param.asInt() == 1);
  if (modeOtomatisLampu) {
    matikanLampu();  // reset dulu, biar otomatis yang atur
    Serial.println("[MODE LAMPU] -> OTOMATIS");
  } else {
    Serial.println("[MODE LAMPU] -> MANUAL");
  }
}

// Dipanggil saat ESP8266 berhasil konek ke Blynk
// Fungsi: sinkronkan status V6 dan V7 dari server (biar tidak reset tiap restart)
BLYNK_CONNECTED() {
  Serial.println("[BLYNK] Terhubung ke server!");
  Blynk.syncVirtual(V6);
  Blynk.syncVirtual(V7);
}


// ============================================================
// BAGIAN 15 - SETUP
// ============================================================

void setup() {
  Serial.begin(115200);
  delay(1000);

  Serial.println();
  Serial.println("============================================");
  Serial.println("      SMART GARDEN - ESP8266 + BLYNK");
  Serial.println("============================================");

  // Inisialisasi relay (pastikan mati saat pertama nyala)
  pinMode(POMPA_PIN, OUTPUT);
  pinMode(LAMPU_PIN, OUTPUT);
  digitalWrite(POMPA_PIN, RELAY_OFF);
  digitalWrite(LAMPU_PIN, RELAY_OFF);
  Serial.println("[OK] Relay: Pompa=D6, Lampu=D7 | Kondisi awal: MATI");

  // Inisialisasi DHT22
  dht.begin();
  Serial.println("[OK] DHT22 pada pin D5");

  // Inisialisasi BH1750
  Wire.begin(SDA_PIN, SCL_PIN);
  if (lightMeter.begin(BH1750::CONTINUOUS_HIGH_RES_MODE, 0x23, &Wire)) {
    Serial.println("[OK] BH1750 terdeteksi (0x23)");
  } else {
    Serial.println("[ERROR] BH1750 tidak ditemukan! Cek kabel SCL=D1, SDA=D2");
  }

  // Inisialisasi Soil Moisture
  Serial.println("[OK] Soil Moisture pada pin A0");

  // Konek ke Blynk
  Serial.print("[WIFI] Menghubungkan ke: ");
  Serial.println(ssid);
  Blynk.begin(BLYNK_AUTH_TOKEN, ssid, pass, "blynk.cloud", 80);

  // Daftarkan timer: kirim data setiap 3 detik
  timer.setInterval(3000L, kirimDataSensor);

  Serial.println("============================================");
  Serial.println("[SIAP] Sistem berjalan!");
  Serial.println("============================================");
}


// ============================================================
// BAGIAN 16 - LOOP UTAMA
// ============================================================

void loop() {
  Blynk.run();
  timer.run();
}
