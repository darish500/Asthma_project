// ════════════════════════════════════════════════════════════════════════════
//  ASTHMA RISK MONITOR — ESP32
//  Sensors -> Feature Vector -> Scaling -> Logistic Regression -> OLED/Buzzer
//  -> Firebase (online) or LittleFS buffer (offline)
// ════════════════════════════════════════════════════════════════════════════
#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <time.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <DHT.h>
#include <LittleFS.h>
#include <math.h>

#include "secrets.h"              // Copy secrets.example.h and set local values
#include "model_parameters.h"   // NUM_FEATURES, coefficients[], intercept
#include "scaler_parameters.h"  // scaler_mean[], scaler_std[]

// ── CONFIG ─────────────────────────────────────────────────────────────────
#define READ_INTERVAL_MS   5000

// ── PINS ────────────────────────────────────────────────────────────────────
#define DHTPIN      4
#define DHTTYPE     DHT22
#define MQ135_PIN   34
#define MQ7_PIN     35
#define MQ131_PIN   32
#define BUZZER_PIN  25
#define SWITCH_PIN  27
#define PMS_RX      16
#define PMS_TX      17
#define OLED_SDA    21
#define OLED_SCL    22
#define SCREEN_W    128
#define SCREEN_H    64

DHT dht(DHTPIN, DHTTYPE);
HardwareSerial pmsSerial(2);
Adafruit_SSD1306 display(SCREEN_W, SCREEN_H, &Wire, -1);

// ── MQ SENSOR CALIBRATION (PLACEHOLDER — MUST CALIBRATE PER DATASHEET) ─────
// IMPORTANT: Ro is NOT "resistance in zero-gas clean air" for these sensors.
// Per manufacturer datasheets, Ro is defined at a specific reference gas
// concentration:
//   MQ135: Ro = Rs measured at 100ppm NH3 (or 50ppm alcohol) in air
//   MQ131: Ro = Rs measured at 50ppb O3 in air
//   MQ7:   Ro = Rs measured at 100ppm CO in air
// Calibrating against literal clean air will bias every ppm estimate.
//
// Load resistor (RL) also differs per sensor per datasheet recommendations:
//   MQ135: RL ~20 kOhm (10-47 kOhm range)
//   MQ131: RL ~100 kOhm (50-200 kOhm range) -- NOTE: much higher than MQ135/MQ7
//   MQ7:   RL ~10 kOhm (per common breakout modules)
// Check your specific breakout board -- some have RL fixed on-board already.
const float RL_MQ135 = 20.0;   // kOhm
const float RL_MQ131 = 100.0;  // kOhm
const float RL_MQ7   = 10.0;   // kOhm
const float VCC      = 3.3;    // ADC reference

float RO_MQ135 = 76.63;  // TODO calibrate at 100ppm NH3 reference
float RO_MQ7   = 27.0;   // TODO calibrate at 100ppm CO reference
float RO_MQ131 = 20.0;   // TODO calibrate at 50ppb O3 reference

// ── GLOBALS ─────────────────────────────────────────────────────────────────
unsigned long lastRead = 0;
bool wifiOnline = false;
float latestFeatures[NUM_FEATURES];
int latestRisk = 0;

// ════════════════════════════════════════════════════════════════════════════
//  WIFI + TIME
// ════════════════════════════════════════════════════════════════════════════
bool connectWiFi() {
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  for (int i = 0; i < 20; i++) {
    if (WiFi.status() == WL_CONNECTED) return true;
    delay(300);
  }
  return false;
}

void syncTime() {
  configTime(0, 0, "pool.ntp.org", "time.nist.gov");
  time_t now = time(nullptr);
  int tries = 0;
  while (now < 100000 && tries < 20) {
    delay(500);
    now = time(nullptr);
    tries++;
  }
}

unsigned long long getUnixMillis() {
  return (unsigned long long)time(nullptr) * 1000ULL;
}

// ════════════════════════════════════════════════════════════════════════════
//  PMS3003 (PM1.0 / PM2.5 / PM10) — active mode, 24-byte frames
// ════════════════════════════════════════════════════════════════════════════
bool readPMS3003(float &pm1_0, float &pm2_5, float &pm10) {
  if (pmsSerial.available() < 24) return false;
  if (pmsSerial.read() != 0x42) return false;
  if (pmsSerial.peek() != 0x4D) return false;
  pmsSerial.read();

  uint8_t buf[24] = {0x42, 0x4D};
  for (int i = 2; i < 24; i++) buf[i] = pmsSerial.read();

  uint16_t checksum = 0;
  for (int i = 0; i < 22; i++) checksum += buf[i];
  uint16_t received = (buf[22] << 8) | buf[23];
  if (checksum != received) return false;

  pm1_0 = (buf[10] << 8) | buf[11]; // atmospheric
  pm2_5 = (buf[12] << 8) | buf[13];
  pm10  = (buf[14] << 8) | buf[15];
  return true;
}

// ════════════════════════════════════════════════════════════════════════════
//  MQ SENSOR HELPERS
//  ppm = a * (Rs/Ro)^b   — curve-fit constants are approximate, calibrate!
// ════════════════════════════════════════════════════════════════════════════
float readRs(int pin, float rl) {
  int raw = analogRead(pin);               // 0-4095 on ESP32 ADC
  float voltage = (raw / 4095.0) * VCC;
  if (voltage < 0.01) voltage = 0.01;       // avoid divide-by-zero
  float rs = ((VCC * rl) / voltage) - rl;
  return rs;
}

float curveFit(float rs, float ro, float a, float b) {
  float ratio = rs / ro;
  return a * pow(ratio, b);
}

void readMQ135(float &co2, float &nh3, float &benzene) {
  float rs = readRs(MQ135_PIN, RL_MQ135);
  co2     = curveFit(rs, RO_MQ135, 110.47, -2.862);
  nh3     = curveFit(rs, RO_MQ135, 102.2,  -2.473);
  benzene = curveFit(rs, RO_MQ135, 32.66,  -3.98);
}

void readMQ7(float &co, float &h2) {
  float rs = readRs(MQ7_PIN, RL_MQ7);
  co = curveFit(rs, RO_MQ7, 99.042, -1.518);
  h2 = curveFit(rs, RO_MQ7, 69.014, -1.374);
}

void readMQ131(float &o3, float &no2) {
  float rs = readRs(MQ131_PIN, RL_MQ131);
  // NOTE: MQ131 lacks a widely-published curve like MQ135/MQ7 do.
  // These constants are rough placeholders — verify/replace with your
  // board's datasheet curve or your own calibration data.
  o3  = curveFit(rs, RO_MQ131, 23.943, -1.11);
  no2 = curveFit(rs, RO_MQ131, 18.0,   -1.0);
}

// ════════════════════════════════════════════════════════════════════════════
//  FEATURE VECTOR + INFERENCE
// ════════════════════════════════════════════════════════════════════════════
enum FeatureIndex {
  F_PM1_0=0, F_PM2_5, F_PM10, F_MQ135_CO2, F_MQ135_NH3, F_MQ135_BENZ,
  F_MQ7_CO, F_MQ7_H2, F_MQ131_O3, F_MQ131_NO2, F_TEMP, F_HUM, F_HEAT_INDEX
};

float computeHeatIndex(float temp, float hum) {
  return temp + 0.5 * (temp + 61.0 + ((temp - 68.0) * 1.2) + (hum * 0.094));
}

void scaleFeatures(float raw[NUM_FEATURES], float scaled[NUM_FEATURES]) {
  for (int i = 0; i < NUM_FEATURES; i++) {
    scaled[i] = (raw[i] - scaler_mean[i]) / scaler_std[i];
  }
}

int predict_asthma_danger(const float input_features[]) {
  float linear_combination = intercept;
  for (int i = 0; i < NUM_FEATURES; ++i) {
    linear_combination += input_features[i] * coefficients[i];
  }
  float probability = 1.0 / (1.0 + exp(-linear_combination));
  return (probability >= 0.5) ? 1 : 0;
}

// ════════════════════════════════════════════════════════════════════════════
//  OLED + BUZZER
// ════════════════════════════════════════════════════════════════════════════
void updateDisplay(float raw[NUM_FEATURES], int risk, bool online) {
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(0, 0);
  display.printf("PM2.5:%.0f PM10:%.0f\n", raw[F_PM2_5], raw[F_PM10]);
  display.printf("T:%.1fC H:%.0f%%\n", raw[F_TEMP], raw[F_HUM]);
  display.printf("CO:%.1f O3:%.3f\n", raw[F_MQ7_CO], raw[F_MQ131_O3]);
  display.printf("Net: %s\n", online ? "Online" : "Offline");
  display.setTextSize(2);
  display.setCursor(0, 40);
  display.print(risk ? "DANGER!" : "SAFE");
  display.display();
}

void setBuzzer(int risk) {
  digitalWrite(BUZZER_PIN, risk ? HIGH : LOW);
}

// ════════════════════════════════════════════════════════════════════════════
//  FIREBASE + OFFLINE BUFFER (LittleFS)
// ════════════════════════════════════════════════════════════════════════════
String buildPayload(float raw[NUM_FEATURES], int risk, unsigned long long ts) {
  StaticJsonDocument<512> doc;
  doc["pm1_0"]      = raw[F_PM1_0];
  doc["pm2_5"]      = raw[F_PM2_5];
  doc["pm10"]       = raw[F_PM10];
  doc["co2"]        = raw[F_MQ135_CO2];
  doc["nh3"]        = raw[F_MQ135_NH3];
  doc["benzene"]    = raw[F_MQ135_BENZ];
  doc["co"]         = raw[F_MQ7_CO];
  doc["h2"]         = raw[F_MQ7_H2];
  doc["o3"]         = raw[F_MQ131_O3];
  doc["no2"]        = raw[F_MQ131_NO2];
  doc["temp"]       = raw[F_TEMP];
  doc["humidity"]   = raw[F_HUM];
  doc["heatIndex"]  = raw[F_HEAT_INDEX];
  doc["risk"]       = risk;
  doc["timestamp"]  = ts;

  String out;
  serializeJson(doc, out);
  return out;
}

bool sendToFirebase(const String &payload) {
  if (WiFi.status() != WL_CONNECTED) return false;
  HTTPClient http;
  http.setTimeout(10000);
  String url = String(FIREBASE_URL) + "/" + BUCKET_NAME + "/devices/" + DEVICE_ID + "/readings.json";
  http.begin(url);
  http.addHeader("Content-Type", "application/json");
  int code = http.POST(payload); // POST -> auto-generated key, keeps history
  http.end();
  return (code == 200);
}

void bufferOffline(const String &payload) {
  File f = LittleFS.open("/buffer.jsonl", "a");
  if (f) {
    f.println(payload);
    f.close();
  }
}

void flushBuffer() {
  if (WiFi.status() != WL_CONNECTED) return;
  if (!LittleFS.exists("/buffer.jsonl")) return;

  File f = LittleFS.open("/buffer.jsonl", "r");
  if (!f) return;

  String remaining = "";
  while (f.available()) {
    String line = f.readStringUntil('\n');
    if (line.length() == 0) continue;
    if (!sendToFirebase(line)) {
      remaining += line + "\n"; // keep failed lines for next attempt
    }
    delay(50);
  }
  f.close();

  LittleFS.remove("/buffer.jsonl");
  if (remaining.length() > 0) {
    File out = LittleFS.open("/buffer.jsonl", "w");
    if (out) { out.print(remaining); out.close(); }
  }
}

// ════════════════════════════════════════════════════════════════════════════
//  SETUP / LOOP
// ════════════════════════════════════════════════════════════════════════════
void setup() {
  Serial.begin(115200);
  pinMode(BUZZER_PIN, OUTPUT);
  pinMode(SWITCH_PIN, INPUT_PULLUP);
  digitalWrite(BUZZER_PIN, LOW);

  dht.begin();
  pmsSerial.begin(9600, SERIAL_8N1, PMS_RX, PMS_TX);

  Wire.begin(OLED_SDA, OLED_SCL);
  if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    Serial.println("OLED init failed");
  }
  display.clearDisplay();
  display.display();

  if (!LittleFS.begin(true)) {
    Serial.println("LittleFS mount failed");
  }

  wifiOnline = connectWiFi();
  if (wifiOnline) syncTime();

  lastRead = millis();
}

void loop() {
  // Reconnect check (non-blocking-ish)
  if (WiFi.status() != WL_CONNECTED) {
    wifiOnline = false;
  } else if (!wifiOnline) {
    wifiOnline = true;
    syncTime();
    flushBuffer(); // reconnected -> push anything buffered
  }

  if (millis() - lastRead >= READ_INTERVAL_MS) {
    lastRead = millis();

    float raw[NUM_FEATURES] = {0};

    float pm1_0, pm2_5, pm10;
    if (readPMS3003(pm1_0, pm2_5, pm10)) {
      raw[F_PM1_0] = pm1_0;
      raw[F_PM2_5] = pm2_5;
      raw[F_PM10]  = pm10;
    }

    float temp = dht.readTemperature();
    float hum  = dht.readHumidity();
    if (isnan(temp) || isnan(hum)) { temp = 25.0; hum = 50.0; } // fallback
    raw[F_TEMP] = temp;
    raw[F_HUM]  = hum;
    raw[F_HEAT_INDEX] = computeHeatIndex(temp, hum);

    float co2, nh3, benzene;
    readMQ135(co2, nh3, benzene);
    raw[F_MQ135_CO2]  = co2;
    raw[F_MQ135_NH3]  = nh3;
    raw[F_MQ135_BENZ] = benzene;

    float co, h2;
    readMQ7(co, h2);
    raw[F_MQ7_CO] = co;
    raw[F_MQ7_H2] = h2;

    float o3, no2;
    readMQ131(o3, no2);
    raw[F_MQ131_O3]  = o3;
    raw[F_MQ131_NO2] = no2;

    float scaled[NUM_FEATURES];
    scaleFeatures(raw, scaled);
    int risk = predict_asthma_danger(scaled);

    memcpy(latestFeatures, raw, sizeof(raw));
    latestRisk = risk;

    updateDisplay(raw, risk, wifiOnline);
    setBuzzer(risk);

    unsigned long long ts = wifiOnline ? getUnixMillis() : (unsigned long long)millis();
    String payload = buildPayload(raw, risk, ts);

    if (wifiOnline) {
      if (!sendToFirebase(payload)) {
        bufferOffline(payload); // send failed even though "online" -> buffer it
      }
    } else {
      bufferOffline(payload);
    }
  }
}
