/*
   Helmet control - ESP32
   Web server: solenoid lock/unlock, two UVC lamps (1ch relays, low-level trigger) on/off,
               misting relay on/off/burst, blower relay on/off
   Air quality: SPS30 particle sensor via UART2 (GPIO16 RX, GPIO17 TX)
*/

#include <WiFi.h>
#include <WebServer.h>
#include <HardwareSerial.h>
#include <SensirionUartSps30.h>

// Resolve NO_ERROR conflict between Sensirion library and ESP-IDF
#ifdef NO_ERROR
#undef NO_ERROR
#endif
#define NO_ERROR 0

// ---- WiFi (set your credentials) ----
#define WIFI_SSID     "BWifi4G"
#define WIFI_PASSWORD "BorbonFamily@2021"

// ---- Pins ----
// UART2 default pins on ESP32 expansion boards — no conflict with relay/solenoid pins
#define SPS30_RX_PIN   16  // UART2 RX
#define SPS30_TX_PIN   17  // UART2 TX

#define SOLENOID_PIN      4   // Solenoid lock (GPIO4)
#define RELAY_UVC1_PIN   13   // Relay UVC lamp 1 (low-level trigger: LOW=on, HIGH=off)
#define RELAY_UVC2_PIN   12   // Relay UVC lamp 2 (low-level trigger)
#define MISTING_RELAY_PIN 5   // Misting relay (high-level trigger: HIGH=on, LOW=off)
#define BLOWER_RELAY_PIN  18  // Blower relay  (high-level trigger: HIGH=on, LOW=off)

#define MIST_BURST_MS 2000UL  // Auto-off duration for /mist/burst

// ---- SPS30 sensor ----
HardwareSerial sps30Serial(2);
SensirionUartSps30  sps30;
static char         spsErrorMsg[64];
static int16_t      spsError;

// Latest readings (updated every SPS30_INTERVAL_MS)
static float        airPm1p0  = 0;
static float        airPm2p5  = 0;
static float        airPm4p0  = 0;
static float        airPm10p0 = 0;
static float        airNc0p5  = 0;
static float        airNc1p0  = 0;
static float        airNc2p5  = 0;
static float        airNc4p0  = 0;
static float        airNc10p0 = 0;
static float        airTypPS  = 0;
static bool         sps30Ready = false;

#define SPS30_INTERVAL_MS 2000UL
static unsigned long lastSps30Read = 0;

// ---- Web server ----
WebServer server(80);

// Solenoid: LOW = unlocked (energized), HIGH = locked (de-energized)
bool solenoidLocked = true;

// UVC: low-level trigger relay → LOW = on, HIGH = off
bool uvc1On = false;
bool uvc2On = false;

// Misting: high-level trigger relay → HIGH = on, LOW = off
bool mistOn = false;
static unsigned long mistBurstUntil = 0;  // non-zero while a timed burst is active

// Blower: high-level trigger relay → HIGH = on, LOW = off
bool blowerOn = false;

// -----------------------------------------------------------------------
// Pin setup
// -----------------------------------------------------------------------
void setupPins() {
  pinMode(SOLENOID_PIN, OUTPUT);
  pinMode(RELAY_UVC1_PIN, OUTPUT);
  pinMode(RELAY_UVC2_PIN, OUTPUT);
  pinMode(MISTING_RELAY_PIN, OUTPUT);
  pinMode(BLOWER_RELAY_PIN, OUTPUT);
  digitalWrite(SOLENOID_PIN, HIGH);     // Start locked
  digitalWrite(RELAY_UVC1_PIN, HIGH);   // UVC1 off
  digitalWrite(RELAY_UVC2_PIN, HIGH);   // UVC2 off
  digitalWrite(MISTING_RELAY_PIN, LOW); // Mist off
  digitalWrite(BLOWER_RELAY_PIN, LOW);  // Blower off
}

// -----------------------------------------------------------------------
// SPS30 initialisation
// -----------------------------------------------------------------------
void setupSps30() {
  sps30Serial.begin(115200, SERIAL_8N1, SPS30_RX_PIN, SPS30_TX_PIN);
  delay(100);
  sps30.begin(sps30Serial);

  int8_t serialNumber[32] = {0};
  spsError = sps30.readSerialNumber(serialNumber, 32);
  if (spsError != NO_ERROR) {
    Serial.print("SPS30 init failed: ");
    errorToString(spsError, spsErrorMsg, sizeof spsErrorMsg);
    Serial.println(spsErrorMsg);
    return; // Continue without sensor — web server still runs
  }
  Serial.print("SPS30 detected, SN: ");
  Serial.println((const char*)serialNumber);

  // Ensure sensor is in idle state before starting
  sps30.stopMeasurement();
  delay(50);

  spsError = sps30.startMeasurement(SPS30_OUTPUT_FORMAT_OUTPUT_FORMAT_FLOAT);
  if (spsError != NO_ERROR) {
    // Retry once
    sps30.stopMeasurement();
    delay(50);
    spsError = sps30.startMeasurement(SPS30_OUTPUT_FORMAT_OUTPUT_FORMAT_FLOAT);
  }
  if (spsError != NO_ERROR) {
    Serial.print("SPS30 start measurement failed: ");
    errorToString(spsError, spsErrorMsg, sizeof spsErrorMsg);
    Serial.println(spsErrorMsg);
    return;
  }

  sps30Ready = true;
  Serial.println("SPS30 measuring.");
}

// -----------------------------------------------------------------------
// Non-blocking SPS30 read (call from loop)
// -----------------------------------------------------------------------
void pollSps30() {
  if (!sps30Ready) return;
  if (millis() - lastSps30Read < SPS30_INTERVAL_MS) return;
  lastSps30Read = millis();

  spsError = sps30.readMeasurementValuesFloat(
    airPm1p0, airPm2p5, airPm4p0, airPm10p0,
    airNc0p5, airNc1p0, airNc2p5, airNc4p0,
    airNc10p0, airTypPS
  );
  if (spsError == NO_ERROR) {
    Serial.print("PM1.0: ");  Serial.print(airPm1p0);
    Serial.print("  PM2.5: "); Serial.print(airPm2p5);
    Serial.print("  PM10: ");  Serial.print(airPm10p0);
    Serial.print("  TypSize: "); Serial.println(airTypPS);
  } else {
    Serial.print("SPS30 read failed: ");
    errorToString(spsError, spsErrorMsg, sizeof spsErrorMsg);
    Serial.println(spsErrorMsg);
  }
}

// -----------------------------------------------------------------------
// Misting helpers
// -----------------------------------------------------------------------
void turnMistOn() {
  mistOn = true;
  digitalWrite(MISTING_RELAY_PIN, HIGH);
}

void turnMistOff() {
  mistOn = false;
  mistBurstUntil = 0;
  digitalWrite(MISTING_RELAY_PIN, LOW);
}

// -----------------------------------------------------------------------
// Blower helpers
// -----------------------------------------------------------------------
void turnBlowerOn() {
  blowerOn = true;
  digitalWrite(BLOWER_RELAY_PIN, HIGH);
}

void turnBlowerOff() {
  blowerOn = false;
  digitalWrite(BLOWER_RELAY_PIN, LOW);
}

// Non-blocking burst poller — call from loop()
void pollMist() {
  if (mistBurstUntil != 0 && millis() >= mistBurstUntil) {
    turnMistOff();
  }
}

// -----------------------------------------------------------------------
// Web handlers
// -----------------------------------------------------------------------
void handleRoot() {
  String html = "<!DOCTYPE html><html><head><meta name='viewport' content='width=device-width,initial-scale=1'>";
  html += "<title>Helmet Control</title></head><body>";
  html += "<h1>Helmet Control</h1>";

  html += "<h2>Actuators</h2>";
  html += "<p><b>Solenoid:</b> " + String(solenoidLocked ? "Locked" : "Unlocked") + "</p>";
  html += "<p><b>UVC 1:</b> " + String(uvc1On ? "On" : "Off") + "</p>";
  html += "<p><b>UVC 2:</b> " + String(uvc2On ? "On" : "Off") + "</p>";
  html += "<p><b>Misting:</b> " + String(mistOn ? "On" : "Off") + (mistBurstUntil ? " (burst)" : "") + "</p>";
  html += "<p><b>Blower:</b> "  + String(blowerOn ? "On" : "Off") + "</p>";

  html += "<h2>Air Quality (SPS30)</h2>";
  if (sps30Ready) {
    html += "<p><b>PM1.0:</b> "  + String(airPm1p0,  2) + " &micro;g/m&sup3;</p>";
    html += "<p><b>PM2.5:</b> "  + String(airPm2p5,  2) + " &micro;g/m&sup3;</p>";
    html += "<p><b>PM4.0:</b> "  + String(airPm4p0,  2) + " &micro;g/m&sup3;</p>";
    html += "<p><b>PM10:</b> "   + String(airPm10p0, 2) + " &micro;g/m&sup3;</p>";
    html += "<p><b>Typical Particle Size:</b> " + String(airTypPS, 2) + " &micro;m</p>";
  } else {
    html += "<p><i>SPS30 not available.</i></p>";
  }

  html += "<h2>Endpoints</h2><ul>";
  html += "<li><a href='/solenoid/unlock'>/solenoid/unlock</a></li>";
  html += "<li><a href='/solenoid/lock'>/solenoid/lock</a></li>";
  html += "<li><a href='/uvc1/on'>/uvc1/on</a></li>";
  html += "<li><a href='/uvc1/off'>/uvc1/off</a></li>";
  html += "<li><a href='/uvc2/on'>/uvc2/on</a></li>";
  html += "<li><a href='/uvc2/off'>/uvc2/off</a></li>";
  html += "<li><a href='/mist/on'>/mist/on</a> - Misting on</li>";
  html += "<li><a href='/mist/off'>/mist/off</a> - Misting off</li>";
  html += "<li><a href='/mist/burst'>/mist/burst</a> - Mist for 2 s then auto-off</li>";
  html += "<li><a href='/blower/on'>/blower/on</a> - Blower on</li>";
  html += "<li><a href='/blower/off'>/blower/off</a> - Blower off</li>";
  html += "<li><a href='/air'>/air</a> - Air quality JSON</li>";
  html += "<li><a href='/status'>/status</a> - Full status JSON</li>";
  html += "</ul></body></html>";
  server.send(200, "text/html", html);
}

void handleSolenoidUnlock() {
  solenoidLocked = false;
  digitalWrite(SOLENOID_PIN, LOW);
  server.send(200, "application/json", "{\"solenoid\":\"unlocked\"}");
}

void handleSolenoidLock() {
  solenoidLocked = true;
  digitalWrite(SOLENOID_PIN, HIGH);
  server.send(200, "application/json", "{\"solenoid\":\"locked\"}");
}

void handleUvc1On() {
  uvc1On = true;
  digitalWrite(RELAY_UVC1_PIN, LOW);
  server.send(200, "application/json", "{\"uvc1\":\"on\"}");
}

void handleUvc1Off() {
  uvc1On = false;
  digitalWrite(RELAY_UVC1_PIN, HIGH);
  server.send(200, "application/json", "{\"uvc1\":\"off\"}");
}

void handleUvc2On() {
  uvc2On = true;
  digitalWrite(RELAY_UVC2_PIN, LOW);
  server.send(200, "application/json", "{\"uvc2\":\"on\"}");
}

void handleUvc2Off() {
  uvc2On = false;
  digitalWrite(RELAY_UVC2_PIN, HIGH);
  server.send(200, "application/json", "{\"uvc2\":\"off\"}");
}

void handleMistOn() {
  turnMistOn();
  server.send(200, "application/json", "{\"mist\":\"on\"}");
}

void handleMistOff() {
  turnMistOff();
  server.send(200, "application/json", "{\"mist\":\"off\"}");
}

void handleMistBurst() {
  turnMistOn();
  mistBurstUntil = millis() + MIST_BURST_MS;
  server.send(200, "application/json", "{\"mist\":\"burst\"}");
}

void handleBlowerOn() {
  turnBlowerOn();
  server.send(200, "application/json", "{\"blower\":\"on\"}");
}

void handleBlowerOff() {
  turnBlowerOff();
  server.send(200, "application/json", "{\"blower\":\"off\"}");
}

void handleAir() {
  String json = "{";
  json += "\"pm1p0\":"  + String(airPm1p0,  2) + ",";
  json += "\"pm2p5\":"  + String(airPm2p5,  2) + ",";
  json += "\"pm4p0\":"  + String(airPm4p0,  2) + ",";
  json += "\"pm10p0\":" + String(airPm10p0, 2) + ",";
  json += "\"nc0p5\":"  + String(airNc0p5,  2) + ",";
  json += "\"nc1p0\":"  + String(airNc1p0,  2) + ",";
  json += "\"nc2p5\":"  + String(airNc2p5,  2) + ",";
  json += "\"nc4p0\":"  + String(airNc4p0,  2) + ",";
  json += "\"nc10p0\":" + String(airNc10p0, 2) + ",";
  json += "\"typicalParticleSize\":" + String(airTypPS, 2) + ",";
  json += "\"sensorReady\":" + String(sps30Ready ? "true" : "false");
  json += "}";
  server.send(200, "application/json", json);
}

void handleStatus() {
  String json = "{";
  json += "\"solenoid\":\"" + String(solenoidLocked ? "locked" : "unlocked") + "\",";
  json += "\"uvc1\":\""     + String(uvc1On ? "on" : "off") + "\",";
  json += "\"uvc2\":\""     + String(uvc2On ? "on" : "off") + "\",";
  json += "\"mist\":\""     + String(mistOn    ? "on" : "off") + "\",";
  json += "\"blower\":\""   + String(blowerOn  ? "on" : "off") + "\",";
  json += "\"pm2p5\":"      + String(airPm2p5, 2) + ",";
  json += "\"pm10p0\":"     + String(airPm10p0, 2) + ",";
  json += "\"typicalParticleSize\":" + String(airTypPS, 2) + ",";
  json += "\"sensorReady\":" + String(sps30Ready ? "true" : "false");
  json += "}";
  server.send(200, "application/json", json);
}

// -----------------------------------------------------------------------
// setup / loop
// -----------------------------------------------------------------------
void setup() {
  Serial.begin(115200);
  setupPins();
  setupSps30();

  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  Serial.print("Connecting to WiFi");
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println();
  Serial.print("IP: ");
  Serial.println(WiFi.localIP());

  server.on("/",               handleRoot);
  server.on("/status",         handleStatus);
  server.on("/air",            handleAir);
  server.on("/solenoid/unlock",handleSolenoidUnlock);
  server.on("/solenoid/lock",  handleSolenoidLock);
  server.on("/uvc1/on",        handleUvc1On);
  server.on("/uvc1/off",       handleUvc1Off);
  server.on("/uvc2/on",        handleUvc2On);
  server.on("/uvc2/off",       handleUvc2Off);
  server.on("/mist/on",        handleMistOn);
  server.on("/mist/off",       handleMistOff);
  server.on("/mist/burst",     handleMistBurst);
  server.on("/blower/on",      handleBlowerOn);
  server.on("/blower/off",     handleBlowerOff);

  server.begin();
  Serial.println("HTTP server started");
}

void loop() {
  server.handleClient();
  pollSps30();
  pollMist();

  // Re-assert outputs every loop so pins stay driven
  digitalWrite(SOLENOID_PIN,    solenoidLocked ? HIGH : LOW);
  digitalWrite(RELAY_UVC1_PIN,  uvc1On ? LOW : HIGH);
  digitalWrite(RELAY_UVC2_PIN,  uvc2On ? LOW : HIGH);
  digitalWrite(MISTING_RELAY_PIN, mistOn   ? HIGH : LOW);
  digitalWrite(BLOWER_RELAY_PIN,  blowerOn ? HIGH : LOW);
}
