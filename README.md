# arduinoHelmet

ESP32-based smart helmet controller with a built-in web server. Control a solenoid lock, two UVC sterilisation lamps, a misting relay, and a blower — all over Wi-Fi — while monitoring air quality with a Sensirion SPS30 particle sensor.

---

## Features

- Wi-Fi web server (port 80) with a browser UI and REST endpoints
- Solenoid lock — lock / unlock on demand
- Two UVC lamps via low-level-trigger relays
- Misting relay — on / off / timed burst (2 s auto-off)
- Blower relay — on / off
- Sensirion SPS30 air quality sensor (PM1.0, PM2.5, PM4.0, PM10, particle count & typical size)
- Non-blocking sensor polling and mist-burst timer

---

## Wiring Guide

### ESP32 Pin Summary

| ESP32 GPIO | Component              | Signal direction | Notes                              |
|------------|------------------------|------------------|------------------------------------|
| GPIO 4     | Solenoid lock          | Output           | HIGH = locked, LOW = unlocked      |
| GPIO 5     | Misting relay IN       | Output           | HIGH-level trigger (HIGH = on)     |
| GPIO 12    | UVC Lamp 2 relay IN    | Output           | Low-level trigger (LOW = on)       |
| GPIO 13    | UVC Lamp 1 relay IN    | Output           | Low-level trigger (LOW = on)       |
| GPIO 16    | SPS30 UART TX → ESP RX | Input (UART2 RX) | Connect to SPS30 pin 4 (TX)        |
| GPIO 17    | SPS30 UART RX → ESP TX | Output (UART2 TX)| Connect to SPS30 pin 3 (RX)        |
| GPIO 18    | Blower relay IN        | Output           | HIGH-level trigger (HIGH = on)     |

---

### 1. Sensirion SPS30 Particle Sensor (UART mode)

The SPS30 communicates over UART at 115 200 baud. Pulling the SEL pin to GND selects UART mode.

| SPS30 Pin | SPS30 Label | Connect to           |
|-----------|-------------|----------------------|
| 1         | VDD         | 5 V                  |
| 2         | GND         | GND                  |
| 3         | RX          | ESP32 GPIO 17 (TX2)  |
| 4         | TX          | ESP32 GPIO 16 (RX2)  |
| 5         | SEL         | GND (UART mode)      |

> **Note:** The SPS30 is a 5 V device but its UART logic is 3.3 V compatible, so no level shifter is required for the data lines.

---

### 2. Solenoid Lock

The solenoid is controlled through a relay or a MOSFET transistor module driven by GPIO 4.

| ESP32 GPIO | Relay / Driver signal | Solenoid state         |
|------------|-----------------------|------------------------|
| GPIO 4 HIGH| Relay de-energised    | **Locked** (default)   |
| GPIO 4 LOW | Relay energised       | **Unlocked**           |

**Relay module wiring:**

```
ESP32 GPIO 4 ──► Relay IN
ESP32 3.3 V  ──► Relay VCC
ESP32 GND    ──► Relay GND

Relay COM    ──► +12 V (or solenoid supply voltage)
Relay NO     ──► Solenoid (+)
Solenoid (–) ──► GND
```

> **Important:** Place a flyback diode (e.g. 1N4007) across the solenoid terminals (cathode to +V) to suppress inductive kickback.

---

### 3. UVC Lamp 1 — Low-Level Trigger Relay (GPIO 13)

| GPIO 13 state | Relay state   | UVC Lamp 1 |
|---------------|---------------|------------|
| LOW           | Energised     | **ON**     |
| HIGH          | De-energised  | **OFF**    |

```
ESP32 GPIO 13 ──► Relay IN  (low-level trigger board)
ESP32 5 V     ──► Relay VCC
ESP32 GND     ──► Relay GND

Relay COM ──► AC/DC live feed for UVC lamp
Relay NO  ──► UVC Lamp 1 live terminal
UVC Lamp 1 neutral ──► Neutral
```

---

### 4. UVC Lamp 2 — Low-Level Trigger Relay (GPIO 12)

Identical wiring to UVC Lamp 1, using GPIO 12.

```
ESP32 GPIO 12 ──► Relay IN  (low-level trigger board)
ESP32 5 V     ──► Relay VCC
ESP32 GND     ──► Relay GND

Relay COM ──► AC/DC live feed for UVC lamp
Relay NO  ──► UVC Lamp 2 live terminal
UVC Lamp 2 neutral ──► Neutral
```

---

### 5. Misting Relay — High-Level Trigger (GPIO 5)

| GPIO 5 state | Relay state  | Mist |
|--------------|--------------|------|
| HIGH         | Energised    | **ON**  |
| LOW          | De-energised | **OFF** |

```
ESP32 GPIO 5 ──► Relay IN  (high-level trigger board)
ESP32 5 V    ──► Relay VCC
ESP32 GND    ──► Relay GND

Relay COM ──► Misting pump / solenoid supply +
Relay NO  ──► Misting pump / solenoid (+)
Pump (–)  ──► GND
```

---

### 6. Blower Relay — High-Level Trigger (GPIO 18)

| GPIO 18 state | Relay state  | Blower |
|---------------|--------------|--------|
| HIGH          | Energised    | **ON**  |
| LOW           | De-energised | **OFF** |

```
ESP32 GPIO 18 ──► Relay IN  (high-level trigger board)
ESP32 5 V     ──► Relay VCC
ESP32 GND     ──► Relay GND

Relay COM ──► Blower motor supply +
Relay NO  ──► Blower motor (+)
Motor (–) ──► GND
```

---

### Power

| Rail  | Powers                                       |
|-------|----------------------------------------------|
| 3.3 V | ESP32 (from on-board regulator)              |
| 5 V   | SPS30, relay module VCC lines                |
| 12 V+ | Solenoid, misting pump, blower (as required) |

Share a common GND between all power rails and the ESP32.

---

## Software Setup

1. Install the **Arduino IDE** and add ESP32 board support via the Boards Manager URL:  
   `https://raw.githubusercontent.com/espressif/arduino-esp32/gh-pages/package_esp32_index.json`
2. Install the **Sensirion UART SPS30** library from the Arduino Library Manager.
3. Open `helmet/helmet.ino` and update the Wi-Fi credentials:
   ```cpp
   #define WIFI_SSID     "your-network"
   #define WIFI_PASSWORD "your-password"
   ```
4. Select your ESP32 board and the correct COM port, then upload.
5. Open the Serial Monitor at **115200 baud** to find the assigned IP address.

---

## Web API Endpoints

Navigate to `http://<ESP32-IP>/` for the browser UI, or call endpoints directly:

| Endpoint            | Action                                  |
|---------------------|-----------------------------------------|
| `GET /`             | Browser control panel & status          |
| `GET /status`       | Full status JSON                        |
| `GET /air`          | Air quality JSON (SPS30)                |
| `GET /solenoid/lock`   | Lock the solenoid                    |
| `GET /solenoid/unlock` | Unlock the solenoid                  |
| `GET /uvc1/on`      | Turn UVC Lamp 1 on                      |
| `GET /uvc1/off`     | Turn UVC Lamp 1 off                     |
| `GET /uvc2/on`      | Turn UVC Lamp 2 on                      |
| `GET /uvc2/off`     | Turn UVC Lamp 2 off                     |
| `GET /mist/on`      | Misting on                              |
| `GET /mist/off`     | Misting off                             |
| `GET /mist/burst`   | Mist for 2 seconds then auto-off        |
| `GET /blower/on`    | Blower on                               |
| `GET /blower/off`   | Blower off                              |

---

## Safety Notes

- UVC radiation is harmful to eyes and skin. Ensure lamps are fully enclosed and cannot activate when the helmet is open.
- Use appropriately rated relays and wiring for whatever voltage/current your solenoid, pump, and blower draw.
- Inductive loads (solenoids, motors) require flyback/snubber protection on the relay output.
- The SPS30 requires a warm-up period of approximately 8 seconds before readings are stable.
