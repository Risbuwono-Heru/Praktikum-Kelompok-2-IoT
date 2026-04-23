#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <DHT.h>

const char* ssid     = "HOTSPOT@UPNJATIM.AC.ID";
const char* password = "belanegara";

// =========================
// DHT SETTING
// =========================
#define DHTPIN D4
#define DHTTYPE DHT11
DHT dht(DHTPIN, DHTTYPE);

// =========================
// OLED SETTING
// =========================
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);

// =========================
// WEB SERVER
// =========================
ESP8266WebServer server(80);

// =========================
// TIMER
// =========================
unsigned long lastDisplayUpdate = 0;
unsigned long lastWifiRetry = 0;
unsigned long lastSensorRead = 0;

const unsigned long displayInterval   = 2000;   // update OLED tiap 2 detik
const unsigned long wifiRetryInterval = 15000;  // retry WiFi tiap 15 detik
const unsigned long sensorInterval    = 2500;   // DHT11 minimal ~1-2 detik

bool wifiConnected = false;
bool serverStarted = false;

// cache sensor
float cachedTemp = NAN;
float cachedHum  = NAN;

// =========================
// FUNGSI OLED
// =========================
void showMessage(String line1, String line2 = "", String line3 = "", String line4 = "") {
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(WHITE);

  display.setCursor(0, 0);
  display.println(line1);

  display.setCursor(0, 16);
  display.println(line2);

  display.setCursor(0, 32);
  display.println(line3);

  display.setCursor(0, 48);
  display.println(line4);

  display.display();
}

void readDHTCached() {
  float h = dht.readHumidity();
  float t = dht.readTemperature();

  // retry sekali jika gagal
  if (isnan(h) || isnan(t)) {
    delay(100);
    h = dht.readHumidity();
    t = dht.readTemperature();
  }

  if (!isnan(t)) cachedTemp = t;
  if (!isnan(h)) cachedHum = h;

  Serial.print("Temp: ");
  if (isnan(cachedTemp)) Serial.print("ERR");
  else Serial.print(cachedTemp);
  Serial.print(" C | Hum: ");
  if (isnan(cachedHum)) Serial.println("ERR");
  else Serial.println(String(cachedHum) + " %");
}

void showSensorOnOLED() {
  String tempStr = isnan(cachedTemp) ? "ERR" : String(cachedTemp, 1) + " C";
  String humStr  = isnan(cachedHum) ? "ERR" : String(cachedHum, 0) + " %";

  display.clearDisplay();
  display.setTextColor(WHITE);
  display.setTextSize(1);

  display.setCursor(0, 0);
  if (WiFi.status() == WL_CONNECTED) {
    display.println("WiFi: CONNECTED");
  } else {
    display.println("WiFi: DISCONNECTED");
  }

  display.setCursor(0, 14);
  display.print("Temp: ");
  display.println(tempStr);

  display.setCursor(0, 28);
  display.print("Hum : ");
  display.println(humStr);

  display.setCursor(0, 44);
  if (WiFi.status() == WL_CONNECTED) {
    display.print("IP:");
    display.println(WiFi.localIP());
  } else {
    display.println("Retry WiFi...");
  }

  display.display();
}

// =========================
// WIFI CONNECT DENGAN TIMEOUT
// =========================
bool connectWiFi(unsigned long timeoutMs = 15000) {
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);

  Serial.print("Connecting to WiFi");
  showMessage("Connecting WiFi...", ssid, "Please wait");

  unsigned long startTime = millis();

  while (WiFi.status() != WL_CONNECTED && millis() - startTime < timeoutMs) {
    delay(500);
    Serial.print(".");

    display.clearDisplay();
    display.setTextSize(1);
    display.setTextColor(WHITE);
    display.setCursor(0, 0);
    display.println("Connecting WiFi...");
    display.setCursor(0, 16);
    display.println(ssid);
    display.setCursor(0, 32);
    display.print("Time: ");
    display.print((millis() - startTime) / 1000);
    display.println(" s");
    display.display();
  }

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\nWiFi Connected!");
    Serial.print("IP Address: ");
    Serial.println(WiFi.localIP());

    showMessage("WiFi Connected!", "IP Address:", WiFi.localIP().toString());
    delay(1500);
    return true;
  } else {
    Serial.println("\nWiFi Failed!");
    WiFi.disconnect(true);

    showMessage("WiFi Failed!", "Check SSID/Password", "Retry later...");
    delay(1500);
    return false;
  }
}

// =========================
// WEB HANDLER HTML
// =========================
void handleRoot() {
  digitalWrite(LED_BUILTIN, LOW);

  String tempStr = isnan(cachedTemp) ? "ERR" : String(cachedTemp, 1);
  String humStr  = isnan(cachedHum) ? "ERR" : String(cachedHum, 0);

  String html = "<!DOCTYPE html><html><head>";
  html += "<meta charset='UTF-8'>";
  html += "<meta http-equiv='refresh' content='5'>";
  html += "<meta name='viewport' content='width=device-width, initial-scale=1'>";
  html += "<title>ESP8266 Weather Station</title>";
  html += "<style>";
  html += "*{box-sizing:border-box;margin:0;padding:0}";
  html += "body{font-family:Arial,sans-serif;background:linear-gradient(135deg,#1a1a2e,#16213e,#0f3460);min-height:100vh;display:flex;align-items:center;justify-content:center;padding:24px}";
  html += ".card{background:rgba(255,255,255,0.05);border:1px solid rgba(255,255,255,0.15);border-radius:20px;padding:32px;max-width:520px;width:100%}";
  html += ".badge{display:inline-block;background:rgba(29,158,117,0.25);color:#5dcaa5;font-size:11px;padding:4px 12px;border-radius:20px;border:1px solid #1d9e75;margin-bottom:12px}";
  html += "h1{color:#fff;font-size:20px;font-weight:500;margin-bottom:4px}";
  html += ".sub{color:rgba(255,255,255,0.45);font-size:13px;margin-bottom:24px}";
  html += ".sensors{display:grid;grid-template-columns:1fr 1fr;gap:12px;margin-bottom:24px}";
  html += ".sc{background:rgba(255,255,255,0.07);border:1px solid rgba(255,255,255,0.12);border-radius:14px;padding:18px;text-align:center}";
  html += ".si{font-size:28px;margin-bottom:8px}";
  html += ".sl{font-size:11px;color:rgba(255,255,255,0.45);letter-spacing:0.5px;text-transform:uppercase;margin-bottom:6px}";
  html += ".sv{font-size:28px;font-weight:500}.su{font-size:14px;color:rgba(255,255,255,0.5)}";
  html += ".tv{color:#f0997b}.hv{color:#85b7eb}";
  html += "hr{border:none;border-top:1px solid rgba(255,255,255,0.1);margin:20px 0}";
  html += ".gt{font-size:12px;color:rgba(255,255,255,0.4);text-transform:uppercase;letter-spacing:0.8px;margin-bottom:14px}";
  html += ".m{display:flex;align-items:center;gap:12px;padding:9px 0;border-bottom:1px solid rgba(255,255,255,0.06)}";
  html += ".m:last-child{border-bottom:none}";
  html += ".av{width:34px;height:34px;border-radius:50%;display:flex;align-items:center;justify-content:center;font-size:12px;font-weight:500;flex-shrink:0}";
  html += ".a1{background:rgba(127,119,221,0.25);color:#afa9ec;border:1px solid #7f77dd}";
  html += ".a2{background:rgba(212,83,126,0.25);color:#ed93b1;border:1px solid #d4537e}";
  html += ".a3{background:rgba(29,158,117,0.25);color:#5dcaa5;border:1px solid #1d9e75}";
  html += ".a4{background:rgba(55,138,221,0.25);color:#85b7eb;border:1px solid #378add}";
  html += ".mn{flex:1;font-size:13px;color:rgba(255,255,255,0.85);font-weight:500}";
  html += ".mi{font-size:12px;color:rgba(255,255,255,0.35);font-family:monospace}";
  html += ".ft{margin-top:18px;text-align:center;font-size:11px;color:rgba(255,255,255,0.3)}";
  html += "</style></head><body>";

  html += "<div class='card'>";
  html += "<div class='badge'>Internet of Things B-083</div>";
  html += "<h1>ESP8266 Weather Station</h1>";
  html += "<p class='sub'>Uji Coba Sensor Pada Board dengan ESP8266</p>";

  html += "<div class='sensors'>";
  html += "<div class='sc'><div class='si'>&#127777;</div><div class='sl'>Temperature</div>";
  html += "<div class='sv tv'>" + tempStr + "<span class='su'>&deg;C</span></div></div>";
  html += "<div class='sc'><div class='si'>&#128167;</div><div class='sl'>Humidity</div>";
  html += "<div class='sv hv'>" + humStr + "<span class='su'>%</span></div></div>";
  html += "</div>";

  html += "<hr>";
  html += "<div class='gt'>Kelompok 1 &mdash; Anggota</div>";
  html += "<div class='m'><div class='av a1'>TV</div><div class='mn'>Tiara Valentina</div><div class='mi'>23083010091</div></div>";
  html += "<div class='m'><div class='av a2'>PN</div><div class='mn'>Pinka Nurdiana</div><div class='mi'>23083010057</div></div>";
  html += "<div class='m'><div class='av a3'>RH</div><div class='mn'>Risbuwono Heru Cokro</div><div class='mi'>23083010104</div></div>";
  html += "<div class='m'><div class='av a4'>IK</div><div class='mn'>I Nyoman Kresna Wira Yudha</div><div class='mi'>23083010017</div></div>";
  html += "<div class='ft'>&#9679; Auto-refresh setiap 5 detik</div>";
  html += "</div></body></html>";

  server.send(200, "text/html", html);

  delay(50);
  digitalWrite(LED_BUILTIN, HIGH);
}

// =========================
// WEB HANDLER JSON
// =========================
void handleRootJSON() {
  digitalWrite(LED_BUILTIN, LOW);

  String json = "{";
  if (isnan(cachedTemp)) json += "\"temperature\": null,";
  else json += "\"temperature\": " + String(cachedTemp, 1) + ",";
  if (isnan(cachedHum)) json += "\"humidity\": null";
  else json += "\"humidity\": " + String(cachedHum, 1);
  json += "}";

  server.send(200, "application/json", json);

  delay(50);
  digitalWrite(LED_BUILTIN, HIGH);
}

// =========================
// SETUP
// =========================
void setup() {
  Serial.begin(115200);
  delay(100);

  pinMode(LED_BUILTIN, OUTPUT);
  digitalWrite(LED_BUILTIN, HIGH);

  dht.begin();

  if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3D)) {
      Serial.println("OLED tidak ditemukan!");
    }
  }

  // route server didaftarkan sekali di awal
  server.on("/", handleRoot);
  server.on("/json", handleRootJSON);

  showMessage("System Booting...", "Init sensor & OLED");
  delay(1000);

  // tunggu DHT stabil
  delay(2000);
  readDHTCached();

  wifiConnected = connectWiFi();

  if (wifiConnected) {
    server.begin();
    serverStarted = true;
    Serial.println("Web server started!");
  }

  showSensorOnOLED();
}

// =========================
// LOOP
// =========================
void loop() {
  // baca sensor berkala, jangan terlalu cepat
  if (millis() - lastSensorRead >= sensorInterval) {
    lastSensorRead = millis();
    readDHTCached();
  }

  if (WiFi.status() == WL_CONNECTED) {
    if (!serverStarted) {
      server.begin();
      serverStarted = true;
      Serial.println("Web server started after reconnect!");
    }
    server.handleClient();
  } else {
    serverStarted = false;

    if (millis() - lastWifiRetry >= wifiRetryInterval) {
      lastWifiRetry = millis();
      wifiConnected = connectWiFi();
    }
  }

  if (millis() - lastDisplayUpdate >= displayInterval) {
    lastDisplayUpdate = millis();
    showSensorOnOLED();
  }
}