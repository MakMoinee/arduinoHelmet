#include <SensirionUartSps30.h>
#include <HardwareSerial.h>
#include <WiFi.h>
#include <WebServer.h>  // built-in ESP32, no extra library needed

#ifdef NO_ERROR
#undef NO_ERROR
#endif
#define NO_ERROR 0

// ---------------------------------------------------------------------------
// Wi-Fi credentials
// ---------------------------------------------------------------------------
const char* WIFI_SSID     = "BWifi4G";
const char* WIFI_PASSWORD = "BorbonFamily@2021";

// ---------------------------------------------------------------------------
// Hardware
// ---------------------------------------------------------------------------
HardwareSerial mySerial(2);  // UART2 – RX=16, TX=17
SensirionUartSps30 sensor;
WebServer server(80);

static char    errorMessage[64];
static int16_t error;

// ---------------------------------------------------------------------------
// Shared snapshot – written by loop() on core 1, read by webServerTask on core 0
// ---------------------------------------------------------------------------
struct SPS30Snapshot {
  float mc1p0, mc2p5, mc4p0, mc10p0;
  float nc0p5, nc1p0, nc2p5, nc4p0, nc10p0;
  float typicalParticleSize;
  bool  valid;
  unsigned long lastUpdatedMs;
};

static SPS30Snapshot latestData = {};
static portMUX_TYPE  dataMux    = portMUX_INITIALIZER_UNLOCKED;

// ---------------------------------------------------------------------------
// GET /data handler
// ---------------------------------------------------------------------------
void handleData() {
  SPS30Snapshot snap;
  taskENTER_CRITICAL(&dataMux);
  snap = latestData;
  taskEXIT_CRITICAL(&dataMux);

  if (!snap.valid) {
    server.send(503, "application/json", "{\"error\":\"No data available yet\"}");
    return;
  }

  char body[512];
  snprintf(body, sizeof(body),
    "{"
      "\"mc1p0\":%.4f,"
      "\"mc2p5\":%.4f,"
      "\"mc4p0\":%.4f,"
      "\"mc10p0\":%.4f,"
      "\"nc0p5\":%.4f,"
      "\"nc1p0\":%.4f,"
      "\"nc2p5\":%.4f,"
      "\"nc4p0\":%.4f,"
      "\"nc10p0\":%.4f,"
      "\"typicalParticleSize\":%.4f,"
      "\"lastUpdatedMs\":%lu"
    "}",
    snap.mc1p0, snap.mc2p5, snap.mc4p0, snap.mc10p0,
    snap.nc0p5, snap.nc1p0, snap.nc2p5, snap.nc4p0,
    snap.nc10p0, snap.typicalParticleSize, snap.lastUpdatedMs);

  server.send(200, "application/json", body);
}

// ---------------------------------------------------------------------------
// Web server task – pinned to core 0, loops forever calling handleClient()
// The sensor loop runs on core 1, so HTTP never blocks readings.
// ---------------------------------------------------------------------------
void webServerTask(void*) {
  server.on("/data", HTTP_GET, handleData);
  server.begin();
  Serial.println("HTTP server ready – GET http://" +
                 WiFi.localIP().toString() + "/data");
  for (;;) {
    server.handleClient();
    vTaskDelay(pdMS_TO_TICKS(5));
  }
}

// ---------------------------------------------------------------------------
// Setup
// ---------------------------------------------------------------------------
void setup() {
  Serial.begin(115200);
  mySerial.begin(115200, SERIAL_8N1, 16, 17);
  delay(1000);

  sensor.begin(mySerial);

  int8_t serialNumber[32] = {0};
  error = sensor.readSerialNumber(serialNumber, 32);
  if (error != NO_ERROR) {
    Serial.print("SPS30 init failed: ");
    errorToString(error, errorMessage, sizeof errorMessage);
    Serial.println(errorMessage);
    while (1);
  }
  Serial.print("SPS30 SN: ");
  Serial.println((const char*)serialNumber);

  sensor.stopMeasurement();
  delay(50);
  error = sensor.startMeasurement(SPS30_OUTPUT_FORMAT_OUTPUT_FORMAT_FLOAT);
  if (error != NO_ERROR) {
    sensor.stopMeasurement();
    delay(50);
    error = sensor.startMeasurement(SPS30_OUTPUT_FORMAT_OUTPUT_FORMAT_FLOAT);
  }
  if (error != NO_ERROR) {
    Serial.print("Start measurement failed: ");
    errorToString(error, errorMessage, sizeof errorMessage);
    Serial.println(errorMessage);
    while (1);
  }

  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  Serial.print("Connecting to WiFi");
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println();
  Serial.print("IP address: ");
  Serial.println(WiFi.localIP());

  // Start the web server on core 0 (sensor loop stays on core 1)
  xTaskCreatePinnedToCore(webServerTask, "webServer", 4096, NULL, 1, NULL, 0);
}

// ---------------------------------------------------------------------------
// Loop (core 1) – reads sensor every 2 s, updates shared snapshot
// ---------------------------------------------------------------------------
void loop() {
  float mc1p0 = 0, mc2p5 = 0, mc4p0 = 0, mc10p0 = 0;
  float nc0p5 = 0, nc1p0 = 0, nc2p5 = 0, nc4p0 = 0, nc10p0 = 0;
  float typicalParticleSize = 0;

  error = sensor.readMeasurementValuesFloat(
    mc1p0, mc2p5, mc4p0, mc10p0,
    nc0p5, nc1p0, nc2p5, nc4p0,
    nc10p0, typicalParticleSize);

  if (error == NO_ERROR) {
    taskENTER_CRITICAL(&dataMux);
    latestData = { mc1p0, mc2p5, mc4p0, mc10p0,
                   nc0p5, nc1p0, nc2p5, nc4p0,
                   nc10p0, typicalParticleSize,
                   true, millis() };
    taskEXIT_CRITICAL(&dataMux);

    Serial.print("PM1.0: ");   Serial.print(mc1p0);
    Serial.print("  PM2.5: "); Serial.print(mc2p5);
    Serial.print("  PM4.0: "); Serial.print(mc4p0);
    Serial.print("  PM10: ");  Serial.print(mc10p0);
    Serial.print("  TPS: ");   Serial.println(typicalParticleSize);
  } else {
    Serial.print("Read failed: ");
    errorToString(error, errorMessage, sizeof errorMessage);
    Serial.println(errorMessage);
  }

  delay(2000);
}
