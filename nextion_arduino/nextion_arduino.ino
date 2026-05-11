#include <Arduino.h>
#include <WiFiS3.h>
#include <ArduinoHttpClient.h>

// ─────────────────────────────────────────────
//  CONFIG — edit these
// ─────────────────────────────────────────────
const bool  isMock        = true;             // true = use mock sensor values

const char* WIFI_SSID     = "jamhelmethub";
const char* WIFI_PASSWORD = "123456789";

const char* SERVER_HOST   = "your-server.com";
const int   SERVER_PORT   = 80;
const char* ENDPOINT      = "/api/sps30-results";

// Must match Nextion timer: 50 steps × 500ms = 25 000ms
// Arduino waits 25 500ms so Nextion navigates to page 3 first
const unsigned long PROCESS_DURATION_MS = 25500UL;
const uint8_t       NEXTION_PWR_PIN     = 5;         // controls Nextion power (HIGH = on)
// ─────────────────────────────────────────────

WiFiClient wifiClient;
HttpClient http(wifiClient, SERVER_HOST, SERVER_PORT);

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
//  HTTP POST
// ─────────────────────────────────────────────
void postResults() {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("WiFi disconnected — reconnecting...");
    connectWiFi();
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

  Serial.println("Posting results...");
  Serial.println(body);

  http.beginRequest();
  http.post(ENDPOINT);
  http.sendHeader("Content-Type", "application/json");
  http.sendHeader("Content-Length", body.length());
  http.endRequest();
  http.print(body);

  int    statusCode = http.responseStatusCode();
  String response   = http.responseBody();
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
//  SPS30 / MOCK READER
// ─────────────────────────────────────────────
bool readSPS30(float &pm1, float &pm25, float &pm10) {
  if (isMock) {
    pm1  = 5.25;
    pm25 = 12.80;
    pm10 = 18.40;
    delay(1200);  // simulate measurement time
    return true;
  }

  // Real sensor path — add your library calls here when ready
  pm1 = pm25 = pm10 = 0.0;
  return false;
}


// ─────────────────────────────────────────────
//  REMOVE HELMET (shared by printh signal + auto-timer via printh)
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
  // Discard any bytes still in the Serial1 hardware buffer
  while (Serial1.available()) Serial1.read();
  // Reset the parse buffer so the next printh is read cleanly
  nxIdx = 0;
  memset(nxBuf, 0, sizeof(nxBuf));
  Serial.println("Nextion buffer flushed.");
}

void doRemoveHelmet() {
  Serial.println("Removing helmet — posting results...");

  // Trigger relay/servo here if needed
  // e.g. digitalWrite(HELMET_PIN, HIGH);

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

    // Page 1 is already fully loaded by now, labels will display
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
void handlePage2Start() {
  Serial.println("Page 2 loaded — process timer armed.");
  currentState     = PROCESSING;
  processTriggered = false;  // let loop() arm the millis() timer fresh
}

void handleButtonPress(uint8_t page, uint8_t compID) {
  Serial.print("Touch event — page: ");
  Serial.print(page);
  Serial.print("  compID: ");
  Serial.println(compID);

  // Page 0 / Page 1 touch events are fallbacks only.
  // Primary triggers are the printh signals from Preinitialize events.
  // (handlePage1Start and handlePage2Start are the reliable path)
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

  // ── Arm process timer when PROCEED fires ───────────────────────────
  if (currentState == PROCESSING && !processTriggered) {
    processStart     = millis();
    processTriggered = true;
    Serial.println("Process countdown: 25.5s...");
  }

  // ── After PROCESS_DURATION_MS: read after, push to page 3 ──────────
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

  // ── Reset flags when back to IDLE ──────────────────────────────────
  if (currentState == IDLE) {
    afterReadDone    = false;
    processTriggered = false;
  }
}
