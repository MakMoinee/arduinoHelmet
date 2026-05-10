/*
   Helmet control - ESP32
   Web server: solenoid lock/unlock, two UVC lamps (1ch relays, low-level trigger) on/off
*/

#include <WiFi.h>
#include <WebServer.h>

// ---- WiFi (set your credentials) ----
#define WIFI_SSID     "jamhelmethub"
#define WIFI_PASSWORD "123456789"

// ---- Pins ----
#define SOLENOID_PIN   4   // Solenoid lock (GPIO4 - safe on ESP32)
#define RELAY_UVC1_PIN 13  // Relay for UVC lamp 1 (low-level trigger: LOW=on, HIGH=off)
#define RELAY_UVC2_PIN 12  // Relay for UVC lamp 2 (low-level trigger)
#define BLOWER_RELAY_PIN  18  // Blower relay  (high-level trigger: HIGH=on, LOW=off)
#define MISTING_RELAY_PIN 5   // Misting relay (high-level trigger: HIGH=on, LOW=off)

WebServer server(80);

// Solenoid: LOW = unlocked (energized), HIGH = locked (de-energized)
bool solenoidLocked = true;

// UVC: low-level trigger relay → LOW = on, HIGH = off
bool uvc1On = false;
bool uvc2On = false;
bool mistOn = false;
bool blowerOn = false;

void setupPins() {
  pinMode(SOLENOID_PIN, OUTPUT);
  pinMode(RELAY_UVC1_PIN, OUTPUT);
  pinMode(RELAY_UVC2_PIN, OUTPUT);
  pinMode(MISTING_RELAY_PIN, OUTPUT);
  pinMode(BLOWER_RELAY_PIN, OUTPUT);
  digitalWrite(SOLENOID_PIN, HIGH);   // Start locked
  digitalWrite(RELAY_UVC1_PIN, HIGH); // UVC1 off
  digitalWrite(RELAY_UVC2_PIN, HIGH); // UVC2 off
  digitalWrite(MISTING_RELAY_PIN, LOW); // Mist off
  digitalWrite(BLOWER_RELAY_PIN, LOW);  // Blower off
}

void handleRoot() {
  String html = "<!DOCTYPE html><html><head><meta name='viewport' content='width=device-width,initial-scale=1'>";
  html += "<title>Helmet Control</title></head><body>";
  html += "<h1>Helmet Control</h1>";
  html += "<p><b>Solenoid:</b> " + String(solenoidLocked ? "Locked" : "Unlocked") + "</p>";
  html += "<p><b>UVC 1:</b> " + String(uvc1On ? "On" : "Off") + "</p>";
  html += "<p><b>UVC 2:</b> " + String(uvc2On ? "On" : "Off") + "</p>";
  html += "<h2>Endpoints</h2>";
  html += "<ul>";
  html += "<li><a href='/solenoid/unlock'>/solenoid/unlock</a> - Unlock</li>";
  html += "<li><a href='/solenoid/lock'>/solenoid/lock</a> - Lock</li>";
  html += "<li><a href='/uvc1/on'>/uvc1/on</a> - UVC 1 On</li>";
  html += "<li><a href='/uvc1/off'>/uvc1/off</a> - UVC 1 Off</li>";
  html += "<li><a href='/uvc2/on'>/uvc2/on</a> - UVC 2 On</li>";
  html += "<li><a href='/uvc2/off'>/uvc2/off</a> - UVC 2 Off</li>";
  html += "<li><a href='/blower/on'>/blower/on</a> - BLOWER  On</li>";
  html += "<li><a href='/blower/off'>/blower/off</a> - BLOWER Off</li>";
  html += "<li><a href='/mist/on'>/mist/on</a> - MIST  On</li>";
  html += "<li><a href='/mist/off'>/mist/off</a> - MIST Off</li>";
  html += "</ul></body></html>";
  server.send(200, "text/html", html);
}

void handleSolenoidUnlock() {
  solenoidLocked = false;
  digitalWrite(SOLENOID_PIN, HIGH);  // De-energize = lock
  server.send(200, "application/json", "{\"solenoid\":\"unlocked\"}");
}

void handleSolenoidLock() {
  solenoidLocked = true;
  digitalWrite(SOLENOID_PIN, LOW);   // Energize = unlock
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

void handleStatus() {
  String json = "{\"solenoid\":\"" + String(solenoidLocked ? "locked" : "unlocked") + "\",\"uvc1\":\"" + String(uvc1On ? "on" : "off") + "\",\"uvc2\":\"" + String(uvc2On ? "on" : "off") + "\"}";
  server.send(200, "application/json", json);
}

void turnBlowerOn() {
  blowerOn = true;
  digitalWrite(BLOWER_RELAY_PIN, HIGH);
  server.send(200, "application/json", "{\"blower\":\"on\"}");
}

void turnBlowerOff() {
  blowerOn = false;
  digitalWrite(BLOWER_RELAY_PIN, LOW);
  server.send(200, "application/json", "{\"blower\":\"off\"}");
}

void turnMistOn() {
  mistOn = true;
  digitalWrite(MISTING_RELAY_PIN, HIGH);
  server.send(200, "application/json", "{\"fogging\":\"on\"}");
}

void turnMistOff() {
  mistOn = false;
  digitalWrite(MISTING_RELAY_PIN, LOW);
  server.send(200, "application/json", "{\"fogging\":\"off\"}");
}

void setup() {
  Serial.begin(115200);
  setupPins();

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

  server.on("/", handleRoot);
  server.on("/status", handleStatus);
  server.on("/solenoid/unlock", handleSolenoidUnlock);
  server.on("/solenoid/lock", handleSolenoidLock);
  server.on("/uvc1/on", handleUvc1On);
  server.on("/uvc1/off", handleUvc1Off);
  server.on("/uvc2/on", handleUvc2On);
  server.on("/uvc2/off", handleUvc2Off);
  server.on("/blower/on", turnBlowerOn);
  server.on("/blower/off", turnBlowerOff);
  server.on("/mist/on", turnMistOn);
  server.on("/mist/off", turnMistOff);

  server.begin();
  Serial.println("HTTP server started");
}

void loop() {
  server.handleClient();
}
