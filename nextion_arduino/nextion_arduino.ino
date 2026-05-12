#include <Arduino.h>
#include <WiFiS3.h>
#include <ArduinoHttpClient.h>

// ─────────────────────────────────────────────
//  CONFIG — edit these
// ─────────────────────────────────────────────
const bool  isMock        = false;             // true = use mock sensor values

const char* WIFI_SSID     = "jamhelmethub";
const char* WIFI_PASSWORD = "123456789";

// Remote config — fetched once at startup to discover core & sps30 hosts
const char* CONFIG_HOST = "192.168.137.1";
const int   CONFIG_PORT = 3000;
const char* CONFIG_PATH = "/";

// Must match Nextion timer: 50 steps × 500ms = 25 000ms
// Arduino waits 25 500ms so Nextion navigates to page 3 first
const unsigned long PROCESS_DURATION_MS = 25500UL;
const uint8_t       NEXTION_PWR_PIN     = 5;         // controls Nextion power (HIGH = on)
// ─────────────────────────────────────────────

// Dynamic endpoints (populated from fetchConfig at startup)
String coreHost  = "";
int    corePort  = 80;
String sps30Host = "";
int    sps30Port = 80;

// Stored readings
float pm1_before, pm25_before, pm10_before;
float pm1_after,  pm25_after,  pm10_after;

// State machine
enum State { IDLE, SHOWING_BEFORE, PROCESSING, SHOWING_AFTER };
State currentState = IDLE;

// Flags & timers
bool systemReady      = false;
bool processTriggered = false;
bool afterReadDone    = false;
unsigned long processStart = 0;

// Nextion serial buffer
uint8_t nxBuf[10];
uint8_t nxIdx = 0;


// ─────────────────────────────────────────────
//  JSON / URL HELPERS
// ─────────────────────────────────────────────

// Extract the string value for a key from flat JSON like {"key":"value",...}
String extractJsonString(const String& json, const String& key) {
  String searchKey = "\"" + key + "\":\"";
  int start = json.indexOf(searchKey);
  if (start == -1) return "";
  start += searchKey.length();
  int end = json.indexOf('"', start);
  if (end == -1) return "";
  return json.substring(start, end);
}

// Extract a numeric float value for a key from flat JSON like {"key":1.23,...}
float extractJsonFloat(const String& json, const String& key) {
  String searchKey = "\"" + key + "\":";
  int start = json.indexOf(searchKey);
  if (start == -1) return 0.0f;
  start += searchKey.length();
  int end1 = json.indexOf(',', start);
  int end2 = json.indexOf('}', start);
  int end  = (end1 == -1) ? end2 : ((end2 == -1) ? end1 : min(end1, end2));
  if (end == -1) return 0.0f;
  return json.substring(start, end).toFloat();
}

// Parse "http://hostname[:port]" → host string + port int
void parseUrl(const String& url, String& host, int& port) {
  port = 80;
  String s = url;
  if (s.startsWith("https://")) {
    s = s.substring(8);
    port = 443;
  }
  else if (s.startsWith("http://")) {
    s = s.substring(7);
  }

  // strip any trailing path (shouldn't be present but just in case)
  int slashIdx = s.indexOf('/');
  if (slashIdx != -1) s = s.substring(0, slashIdx);

  int colonIdx = s.indexOf(':');
  if (colonIdx != -1) {
    host = s.substring(0, colonIdx);
    port = s.substring(colonIdx + 1).toInt();
  } else {
    host = s;
  }
}


// ─────────────────────────────────────────────
//  WIFI
// ─────────────────────────────────────────────
void connectWiFi() {
  Serial.print("Connecting to WiFi");
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }

  // R4 WiFi fires WL_CONNECTED before DHCP — wait for real IP
  Serial.print(" waiting for IP");
  while (WiFi.localIP() == IPAddress(0, 0, 0, 0)) {
    delay(500);
    Serial.print(".");
  }

  Serial.println();
  Serial.print("Connected — IP: ");
  Serial.println(WiFi.localIP());
}


// ─────────────────────────────────────────────
//  CONFIG FETCH  (HTTPS → GitHub)
// ─────────────────────────────────────────────
bool fetchConfig() {
  Serial.println("Fetching config...");

  WiFiClient plainClient;
  HttpClient configHttp(plainClient, CONFIG_HOST, CONFIG_PORT);

  int err = configHttp.get(CONFIG_PATH);
  if (err != 0) {
    Serial.print("ERROR: config GET failed (err=");
    Serial.print(err);
    Serial.println(")");
    return false;
  }

  int    statusCode = configHttp.responseStatusCode();
  String body       = configHttp.responseBody();
  configHttp.stop();

  Serial.print("Config HTTP status: ");
  Serial.println(statusCode);
  Serial.println(body);

  if (statusCode != 200) {
    Serial.println("ERROR: unexpected config HTTP status");
    return false;
  }

  String sps30Url = extractJsonString(body, "sps30");
  String coreUrl  = extractJsonString(body, "core");

  if (sps30Url.length() == 0 || coreUrl.length() == 0) {
    Serial.println("ERROR: could not parse config JSON");
    return false;
  }

  parseUrl(sps30Url, sps30Host, sps30Port);
  parseUrl(coreUrl,  coreHost,  corePort);

  Serial.print("SPS30 → "); Serial.print(sps30Host); Serial.print(":"); Serial.println(sps30Port);
  Serial.print("Core  → "); Serial.print(coreHost);  Serial.print(":"); Serial.println(corePort);

  return true;
}


// ─────────────────────────────────────────────
//  CORE HTTP HELPER  (synchronous GET — blocks until response received)
// ─────────────────────────────────────────────
bool callCoreEndpoint(const String& path) {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("WiFi disconnected — reconnecting...");
    connectWiFi();
  }
  if (coreHost.length() == 0) {
    Serial.println("ERROR: core host not set");
    return false;
  }

  WiFiClient client;
  HttpClient http(client, coreHost.c_str(), corePort);

  Serial.print("→ core"); Serial.println(path);

  int err = http.get(path);
  if (err != 0) {
    Serial.print("  ERROR: GET failed (err="); Serial.print(err); Serial.println(")");
    http.stop();
    return false;
  }

  int    statusCode = http.responseStatusCode();
  String response   = http.responseBody();
  http.stop();

  Serial.print("  status: "); Serial.println(statusCode);
  Serial.print("  body:   "); Serial.println(response);

  return (statusCode >= 200 && statusCode < 300);
}


// ─────────────────────────────────────────────
//  PROCESSING DEVICE CONTROL  (called on proceed from page 1)
//  Each call blocks until the server responds before the next is issued.
// ─────────────────────────────────────────────
void activateProcessingDevices() {
  Serial.println("=== Activating processing devices ===");

  Serial.println("[1/3] UVC1 ON");
  if (callCoreEndpoint("/uvc1/on")) {
    Serial.println("  UVC1 ON — OK");
  } else {
    Serial.println("  UVC1 ON — FAILED (continuing)");
  }

  Serial.println("[2/3] UVC2 ON");
  if (callCoreEndpoint("/uvc2/on")) {
    Serial.println("  UVC2 ON — OK");
  } else {
    Serial.println("  UVC2 ON — FAILED (continuing)");
  }

  Serial.println("[3/5] Mist ON");
  if (callCoreEndpoint("/mist/on")) {
    Serial.println("  Mist ON — OK");
  } else {
    Serial.println("  Mist ON — FAILED (continuing)");
  }

  Serial.println("[4/5] Blower ON");
  if (callCoreEndpoint("/blower/on")) {
    Serial.println("  Blower ON — OK");
  } else {
    Serial.println("  Blower ON — FAILED (continuing)");
  }

  Serial.println("[5/5] Solenoid UNLOCK");
  if (callCoreEndpoint("/solenoid/unlock")) {
    Serial.println("  Solenoid UNLOCK — OK");
  } else {
    Serial.println("  Solenoid UNLOCK — FAILED (continuing)");
  }

  Serial.println("=== Processing devices activated ===");
}


// ─────────────────────────────────────────────
//  HTTP POST results to core
// ─────────────────────────────────────────────
void postResults() {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("WiFi disconnected — reconnecting...");
    connectWiFi();
  }
  if (coreHost.length() == 0) {
    Serial.println("ERROR: core host not set — skipping post");
    return;
  }

  String body = "{";
  body += "\"before\":{";
  body += "\"pm1\":"  + String(pm1_before,  2) + ",";
  body += "\"pm25\":" + String(pm25_before, 2) + ",";
  body += "\"pm10\":" + String(pm10_before, 2);
  body += "},";
  body += "\"after\":{";
  body += "\"pm1\":"  + String(pm1_after,  2) + ",";
  body += "\"pm25\":" + String(pm25_after, 2) + ",";
  body += "\"pm10\":" + String(pm10_after, 2);
  body += "}";
  body += "}";

  Serial.println("Posting results to core...");
  Serial.println(body);

  WiFiClient client;
  HttpClient http(client, coreHost.c_str(), corePort);

  http.beginRequest();
  http.post("/api/sps30-results");
  http.sendHeader("Content-Type", "application/json");
  http.sendHeader("Content-Length", body.length());
  http.endRequest();
  http.print(body);

  int    statusCode = http.responseStatusCode();
  String response   = http.responseBody();
  http.stop();

  Serial.print("Server response: ");
  Serial.println(statusCode);
  Serial.println(response);
}


// ─────────────────────────────────────────────
//  NEXTION HELPERS
// ─────────────────────────────────────────────
void sendToNextion(const String& cmd) {
  Serial1.print(cmd);
  Serial1.write(0xFF);
  Serial1.write(0xFF);
  Serial1.write(0xFF);
}

void setLabel(const String& component, float value) {
  String cmd = component + ".txt=\"" + String(value, 1) + " ug/m3\"";
  sendToNextion(cmd);
  Serial.println("Nextion ← " + cmd);
}


// ─────────────────────────────────────────────
//  SPS30 READER  (mock or HTTP GET to {sps30Host}/data)
//  Expected real response: {"pm1":X.XX,"pm25":X.XX,"pm10":X.XX}
// ─────────────────────────────────────────────
bool readSPS30(float &pm1, float &pm25, float &pm10) {
  if (isMock) {
    pm1  = 5.25;
    pm25 = 12.80;
    pm10 = 18.40;
    delay(1200);  // simulate measurement time
    return true;
  }

  // Real path — HTTP GET to {sps30Host}/data
  if (sps30Host.length() == 0) {
    Serial.println("ERROR: sps30 host not set");
    return false;
  }

  if (WiFi.status() != WL_CONNECTED) connectWiFi();

  WiFiClient client;
  HttpClient http(client, sps30Host.c_str(), sps30Port);

  int err = http.get("/data");
  if (err != 0) {
    Serial.print("ERROR: SPS30 GET failed (err="); Serial.print(err); Serial.println(")");
    http.stop();
    return false;
  }

  int    statusCode = http.responseStatusCode();
  String body       = http.responseBody();
  http.stop();

  if (statusCode != 200) {
    Serial.print("ERROR: SPS30 HTTP status: "); Serial.println(statusCode);
    return false;
  }

  Serial.println("SPS30 data: " + body);

  pm1  = extractJsonFloat(body, "mc1p0");
  pm25 = extractJsonFloat(body, "mc2p5");
  pm10 = extractJsonFloat(body, "mc10p0");

  return true;
}


// ─────────────────────────────────────────────
//  REMOVE HELMET
// ─────────────────────────────────────────────
void nextionPowerCycle() {
  Serial.println("Nextion: powering off...");
  digitalWrite(NEXTION_PWR_PIN, LOW);
  delay(1500);                          // hold off long enough to fully discharge

  Serial.println("Nextion: powering on...");
  digitalWrite(NEXTION_PWR_PIN, HIGH);
  delay(2000);                          // wait for Nextion to boot to page 0

  flushNextionBuffer();                 // discard any boot messages
  Serial.println("Nextion: ready.");
}

void flushNextionBuffer() {
  while (Serial1.available()) Serial1.read();
  nxIdx = 0;
  memset(nxBuf, 0, sizeof(nxBuf));
  Serial.println("Nextion buffer flushed.");
}

void deactivateProcessingDevices() {
  Serial.println("=== Deactivating processing devices ===");

  Serial.println("[1/5] UVC1 OFF");
  if (callCoreEndpoint("/uvc1/off")) {
    Serial.println("  UVC1 OFF — OK");
  } else {
    Serial.println("  UVC1 OFF — FAILED (continuing)");
  }

  Serial.println("[2/5] UVC2 OFF");
  if (callCoreEndpoint("/uvc2/off")) {
    Serial.println("  UVC2 OFF — OK");
  } else {
    Serial.println("  UVC2 OFF — FAILED (continuing)");
  }

  Serial.println("[3/5] Mist OFF");
  if (callCoreEndpoint("/mist/off")) {
    Serial.println("  Mist OFF — OK");
  } else {
    Serial.println("  Mist OFF — FAILED (continuing)");
  }

  Serial.println("[4/5] Blower OFF");
  if (callCoreEndpoint("/blower/off")) {
    Serial.println("  Blower OFF — OK");
  } else {
    Serial.println("  Blower OFF — FAILED (continuing)");
  }

  Serial.println("[5/5] Solenoid LOCK");
  if (callCoreEndpoint("/solenoid/lock")) {
    Serial.println("  Solenoid LOCK — OK");
  } else {
    Serial.println("  Solenoid LOCK — FAILED (continuing)");
  }

  Serial.println("=== Processing devices deactivated ===");
}

void doRemoveHelmet() {
  Serial.println("Removing helmet — deactivating devices & posting results...");

  // Trigger relay/servo here if needed
  // e.g. digitalWrite(HELMET_PIN, HIGH);

  deactivateProcessingDevices();   // UVC1 off → UVC2 off → Mist off → Blower off → Solenoid lock

  postResults();

  // Hard reset the Nextion — boots clean to page 0, no state to manage
  nextionPowerCycle();

  currentState     = IDLE;
  afterReadDone    = false;
  processTriggered = false;
}


// ─────────────────────────────────────────────
//  NEXTION BUTTON HANDLERS
// ─────────────────────────────────────────────
void handlePrint() {
  Serial.println("PRINT pressed — posting results...");
  postResults();
}

void handleRemoveHelmet() {
  Serial.println("REMOVE HELMET signal received");
  doRemoveHelmet();
}

// Called when Page 1 Preinitialize sends: printh 50 31 53 54
void handlePage1Start() {
  Serial.println("Page 1 loaded — reading before values...");

  if (readSPS30(pm1_before, pm25_before, pm10_before)) {
    Serial.print("Before → pm1: ");  Serial.print(pm1_before);
    Serial.print("  pm25: ");        Serial.print(pm25_before);
    Serial.print("  pm10: ");        Serial.println(pm10_before);

    setLabel("t0", pm1_before);
    setLabel("t1", pm25_before);
    setLabel("t2", pm10_before);
    Serial.println("Before labels sent to page 1.");
    sendToNextion("vis b0,1");
    currentState = SHOWING_BEFORE;
  } else {
    Serial.println("ERROR: sensor read failed");
  }
}

// Called when Page 2 Preinitialize sends: printh 50 32 53 54
// This fires right after the user presses PROCEED on page 1.
// UVC1 → UVC2 → Mist are turned on synchronously before arming the timer.
void handlePage2Start() {
  Serial.println("Page 2 loaded — proceed pressed on page 1.");

  activateProcessingDevices();   // synchronous: UVC1 on, then UVC2 on, then Mist on

  Serial.println("Process timer armed.");
  currentState     = PROCESSING;
  processTriggered = false;      // loop() will arm the millis() timer
}

void handleButtonPress(uint8_t page, uint8_t compID) {
  Serial.print("Touch event — page: ");
  Serial.print(page);
  Serial.print("  compID: ");
  Serial.println(compID);

  // Primary triggers are printh signals from Preinitialize events.
  Serial.println("Touch fallback — no action taken (printh is primary).");
}


// ─────────────────────────────────────────────
//  NEXTION SERIAL PARSER
// ─────────────────────────────────────────────
void checkNextion() {
  while (Serial1.available()) {
    uint8_t b = Serial1.read();

    // Raw byte debug — comment out once everything is working
    Serial.print("NX byte: 0x");
    if (b < 0x10) Serial.print("0");
    Serial.println(b, HEX);

    nxBuf[nxIdx++] = b;

    // ── printh signals: exactly 4 raw bytes, NO 0xFF terminator ──────
    if (nxIdx == 4) {
      bool handled = false;

      // PRINT  (printh 50 52 4E 54)
      if (nxBuf[0] == 0x50 && nxBuf[1] == 0x52 &&
          nxBuf[2] == 0x4E && nxBuf[3] == 0x54) {
        if (systemReady) handlePrint();
        else Serial.println("PRINT ignored — not ready");
        handled = true;
      }

      // REMOVE HELMET  (printh 52 4D 56 00)
      if (nxBuf[0] == 0x52 && nxBuf[1] == 0x4D &&
          nxBuf[2] == 0x56 && nxBuf[3] == 0x00) {
        if (systemReady) handleRemoveHelmet();
        else Serial.println("REMOVE HELMET ignored — not ready");
        handled = true;
      }

      // PAGE 1 START  (printh 50 31 53 54)
      if (nxBuf[0] == 0x50 && nxBuf[1] == 0x31 &&
          nxBuf[2] == 0x53 && nxBuf[3] == 0x54) {
        if (systemReady) handlePage1Start();
        else Serial.println("PAGE1 START ignored — not ready");
        handled = true;
      }

      // PAGE 2 START  (printh 50 32 53 54)
      if (nxBuf[0] == 0x50 && nxBuf[1] == 0x32 &&
          nxBuf[2] == 0x53 && nxBuf[3] == 0x54) {
        if (systemReady) handlePage2Start();
        else Serial.println("PAGE2 START ignored — not ready");
        handled = true;
      }

      if (handled) {
        nxIdx = 0;
        memset(nxBuf, 0, sizeof(nxBuf));
        continue;
      }
    }

    // ── Standard Nextion packets end with 3x 0xFF ────────────────────
    if (nxIdx >= 4
        && nxBuf[nxIdx - 1] == 0xFF
        && nxBuf[nxIdx - 2] == 0xFF
        && nxBuf[nxIdx - 3] == 0xFF) {

      if (!systemReady) {
        Serial.println("NX event ignored — not ready");
      } else {
        // Touch event: 0x65 <page> <compID> <event=0x01 press>
        if (nxBuf[0] == 0x65 && nxBuf[3] == 0x01) {
          handleButtonPress(nxBuf[1], nxBuf[2]);
        }
      }

      nxIdx = 0;
      memset(nxBuf, 0, sizeof(nxBuf));
    }

    if (nxIdx >= sizeof(nxBuf)) nxIdx = 0;
  }
}


// ─────────────────────────────────────────────
//  SETUP & LOOP
// ─────────────────────────────────────────────
void setup() {
  Serial.begin(115200);
  Serial1.begin(9600);

  pinMode(NEXTION_PWR_PIN, OUTPUT);
  digitalWrite(NEXTION_PWR_PIN, HIGH);  // Nextion on from the start

  if (isMock) Serial.println("Running in MOCK mode — no sensor required");

  // Give Nextion time to boot before sending anything
  delay(2000);
  sendToNextion("page 0");
  Serial.println("Nextion: page 0 sent");

  connectWiFi();

  // Fetch dynamic core + sps30 endpoints from remote config
  if (!fetchConfig()) {
    Serial.println("WARN: config fetch failed — falling back to hardcoded hosts");
    coreHost  = "192.168.137.180";  corePort  = 80;
    sps30Host = "192.168.137.1";    sps30Port = 80;
  }

  // Pre-fill mock values so PRINT/REMOVE HELMET always have data during testing
  if (isMock) {
    pm1_before  = 5.25;  pm25_before = 12.80; pm10_before = 18.40;
    pm1_after   = 3.10;  pm25_after  =  7.50; pm10_after  = 11.20;
    Serial.println("Mock values pre-loaded.");
  }

  systemReady = true;
  Serial.println("Setup complete — system ready");
}

void loop() {
  checkNextion();

  // ── Arm process timer once PROCESSING state is entered ────────────────
  if (currentState == PROCESSING && !processTriggered) {
    processStart     = millis();
    processTriggered = true;
    Serial.println("Process countdown: 25.5s...");
  }

  // ── After PROCESS_DURATION_MS: read after values, push to page 3 ──────
  // We wait 25 500ms (Nextion's timer finishes at 25 000ms and navigates
  // to page 3 first; our extra 500ms ensures it's there before we write).
  if (currentState == PROCESSING
      && processTriggered
      && !afterReadDone
      && millis() - processStart >= PROCESS_DURATION_MS) {

    Serial.println("Process done — reading after values...");

    if (readSPS30(pm1_after, pm25_after, pm10_after)) {
      Serial.print("After → pm1: ");  Serial.print(pm1_after);
      Serial.print("  pm25: ");       Serial.print(pm25_after);
      Serial.print("  pm10: ");       Serial.println(pm10_after);

      // Push all six values to page 3
      setLabel("t0", pm1_before);
      setLabel("t1", pm25_before);
      setLabel("t2", pm10_before);

      delay(10000);
      setLabel("t3", pm1_after);
      setLabel("t4", pm25_after);
      setLabel("t5", pm10_after);

      // Show PRINT and REMOVE HELMET now that data is ready
      sendToNextion("vis b0,1");
      sendToNextion("vis b1,1");
      Serial.println("All labels sent to page 3 — buttons visible.");

      afterReadDone    = true;
      processTriggered = false;
      currentState     = SHOWING_AFTER;
    }
  }

  // ── Reset flags when back to IDLE ─────────────────────────────────────
  if (currentState == IDLE) {
    afterReadDone    = false;
    processTriggered = false;
  }
}
