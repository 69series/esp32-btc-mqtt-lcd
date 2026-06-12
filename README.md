# ESP32 Bitcoin Ticker + MQTT Alert System

> Real-time BTC/USDT price display and instant emergency messaging on a 16×2 LCD —
> built entirely on **ESP-IDF v5.3**, **FreeRTOS**, and a **custom HD44780 driver** written from scratch.
> No Arduino. No libraries. Pure C.

---
## Demo

[![ESP32 Bitcoin Ticker + MQTT Alert System](https://img.youtube.com/vi/fkdwuznjS9E/maxresdefault.jpg)](https://youtu.be/fkdwuznjS9E)

> Click the thumbnail to watch the full video documentary.

## Overview

This project runs two concurrent FreeRTOS tasks on an ESP32 WROOM-32:

1. **BTC Task** — fetches live Bitcoin price from the Binance public API over HTTPS every 10 seconds and displays it on the LCD.
2. **MQTT Task** — subscribes to a HiveMQ Cloud broker topic. When an emergency message is published (from a web dashboard), it appears on the LCD **instantly** — no polling.

The LCD driver implements the **HD44780 protocol** in 4-bit mode directly over GPIO — zero external libraries, zero I2C.

---

## Architecture

```
┌─────────────────────┐        HTTPS         ┌──────────────────┐
│   Your Website      │ ───────────────────> │   Binance API    │
│   (Vercel / React)  │                      └──────────────────┘
│                     │                               ↓
│   /admin/alert      │      MQTT publish     ┌──────────────────┐
│   (admin only)      │ ───────────────────>  │   HiveMQ Cloud   │
└─────────────────────┘      retain: true     │   Broker (TLS)   │
          ↑                                   └────────┬─────────┘
   Express REST API                                    │
   POST /api/admin/alert                        MQTT subscribe
          ↑                                            │
┌─────────────────────┐                               ↓
│   Render Backend    │                      ┌──────────────────┐
│   Node.js + mqtt.js │                      │  ESP32 WROOM-32  │
└─────────────────────┘                      │                  │
                                             │  ┌────────────┐  │
                                             │  │  btc_task  │  │
                                             │  │ (Core 1)   │  │
                                             │  └────────────┘  │
                                             │  ┌────────────┐  │
                                             │  │ mqtt_event │  │
                                             │  │  handler   │  │
                                             │  └────────────┘  │
                                             │  ┌────────────┐  │
                                             │  │lcd_mutex   │  │
                                             │  │(semaphore) │  │
                                             │  └────────────┘  │
                                             └────────┬─────────┘
                                                      │ GPIO
                                                      ↓
                                             ┌──────────────────┐
                                             │  16×2 LCD        │
                                             │  HD44780         │
                                             │  (4-bit mode)    │
                                             └──────────────────┘
```

---

## Features

- **Custom HD44780 LCD driver** — raw GPIO, 4-bit mode, full HD44780 init sequence with datasheet-accurate timing. No `LiquidCrystal` library.
- **Live BTC/USDT price** — Binance public API, HTTPS with TLS certificate bundle verification.
- **Instant MQTT alerts** — HiveMQ Cloud broker, TLS encrypted, QoS 1.
- **Retained messages** — last alert persists on broker. On reboot, ESP32 receives it immediately after subscribing — no message lost.
- **FreeRTOS mutex** — `SemaphoreHandle_t` protects the LCD from concurrent writes between the BTC task and MQTT event handler.
- **Event-driven WiFi** — uses `esp_event` with `EventGroupHandle_t`, not busy-wait polling.
- **Dual-core utilisation** — BTC task pinned to Core 1. WiFi/MQTT stack runs on Core 0.
- **Auto WiFi reconnect** — handles disconnects gracefully, retries automatically.

---

## Hardware

| Component | Details |
|---|---|
| MCU | ESP32 WROOM-32 (NodeMCU) |
| Display | 16×2 LCD — HD44780 controller |
| Interface | 4-bit parallel GPIO (no I2C) |
| Contrast | 10K potentiometer on VO pin |
| Backlight | 5V via 220Ω resistor |

---

## Wiring

| LCD Pin | Name | ESP32 |
|---|---|---|
| 1 | GND | GND |
| 2 | VDD | Vin (5V) |
| 3 | VO | 10K pot wiper |
| 4 | RS | GPIO 23 |
| 5 | RW | GND |
| 6 | E | GPIO 22 |
| 7–10 | D0–D3 | Not connected (4-bit mode) |
| 11 | D4 | GPIO 21 |
| 12 | D5 | GPIO 19 |
| 13 | D6 | GPIO 18 |
| 14 | D7 | GPIO 5 |
| 15 | BLA | 5V via 220Ω |
| 16 | BLK | GND |

---

## Project Structure

```
bitcoin_ticker/
├── main/
│   ├── CMakeLists.txt      # component registration + dependencies
│   ├── main.c              # app_main, wifi, btc_task, mqtt
│   ├── lcd.c               # HD44780 driver implementation
│   └── lcd.h               # driver public API
├── CMakeLists.txt          # top-level project definition
├── sdkconfig               # ESP-IDF build configuration (esp32, 4MB, 240MHz)
└── README.md
```

---

## Software Requirements

| Tool | Version |
|---|---|
| ESP-IDF | v5.3.1 |
| CMake | ≥ 3.16 |
| Ninja | bundled with ESP-IDF |
| Python | 3.11 (bundled with ESP-IDF) |
| esptool | bundled with ESP-IDF |

---

## Setup & Build

### 1. Clone the repository

```bash
git clone https://github.com/69series/esp32-btc-mqtt-lcd.git
cd esp32-btc-mqtt-lcd
```

### 2. Set up ESP-IDF environment

```bash
# Windows (ESP-IDF Command Prompt)
C:\Espressif\frameworks\esp-idf-v5.3.1\export.bat

# Linux / macOS
. $HOME/esp/esp-idf/export.sh
```

### 3. Configure target

```bash
idf.py set-target esp32
```

### 4. Configure credentials

Open `main/main.c` and update:

```c
#define WIFI_SSID       "your_wifi_ssid"
#define WIFI_PASS       "your_wifi_password"

#define MQTT_BROKER     "mqtts://your-cluster.s1.eu.hivemq.cloud"
#define MQTT_PORT       8883
#define MQTT_USER       "your_mqtt_username"
#define MQTT_PASS       "your_mqtt_password"
#define MQTT_TOPIC      "69series/alert"
```

### 5. Build

```bash
idf.py build
```

### 6. Flash and monitor

```bash
# Windows
idf.py -p com3 flash monitor

# Linux
idf.py -p /dev/ttyUSB0 flash monitor
```

---

## Configuration (menuconfig)

```bash
idf.py menuconfig
```

Recommended settings:

```
Serial flasher config → Flash size              → 4MB
Component config → ESP System Settings → CPU frequency → 240MHz
```

---

## MQTT Broker Setup

This project uses [HiveMQ Cloud](https://www.hivemq.com/mqtt-cloud-broker/) free serverless tier.

1. Create a free account at hivemq.com
2. Create a **Serverless** cluster
3. Go to **Access Management → Credentials**
4. Create two users:
   - `esp32-device` — Subscribe Only (for the firmware)
   - `mern-backend` — Publish Only (for the web backend)
5. Note your broker URL and port (`8883` TLS)

---

## How It Works

### LCD Driver (`lcd.c`)

The HD44780 controller is initialised using the datasheet-specified 3-step 8-bit handshake before switching to 4-bit mode. Every byte is sent as two nibbles — upper nibble first — each latched by a pulse on the E pin.

```
lcd_send(0x42, RS=1)   // send 'B'
  → RS = 1             // data mode
  → send upper nibble: bits 4,5,6,7 → D4,D5,D6,D7 → pulse E
  → send lower nibble: bits 0,1,2,3 → D4,D5,D6,D7 → pulse E
```

Row 1 cursor address starts at DDRAM `0x40` (HD44780 hardware quirk) — handled in `lcd_set_cursor()`.

### WiFi (`wifi_init`)

Uses ESP-IDF event-driven WiFi. An `EventGroupHandle_t` blocks `app_main` until IP is assigned — no busy-wait loop. Automatic reconnect is handled in the `WIFI_EVENT_STA_DISCONNECTED` event.

### BTC Task (`btc_task`)

Runs on Core 1. Performs HTTPS GET to Binance every 10 seconds, parses the JSON response with `cJSON`, extracts the `price` field, and writes to LCD under mutex lock.

### MQTT (`mqtt_init` + `mqtt_event_handler`)

ESP-IDF's built-in MQTT client runs its own internal task. On `MQTT_EVENT_CONNECTED`, the device subscribes to `69series/alert` with QoS 1. On `MQTT_EVENT_DATA`, the message is extracted, split across two LCD lines (16 chars each), displayed for 5 seconds, then BTC price resumes.

**Retained messages:** The web backend publishes with `retain: true`. HiveMQ stores the last message permanently on the topic. On every reboot, the ESP32 receives it immediately after subscribing — replicating the "last message on boot" behaviour.

### FreeRTOS Mutex

Both the BTC task and MQTT event handler write to the LCD. A `SemaphoreHandle_t` mutex ensures only one writer accesses the LCD at a time, preventing display corruption.

```c
if (xSemaphoreTake(lcd_mutex, pdMS_TO_TICKS(1000)) == pdTRUE) {
    lcd_clear();
    lcd_print(...);
    xSemaphoreGive(lcd_mutex);
}
```

---

## Web Integration

The companion MERN stack web application provides an admin-only page to push emergency messages to the device.

- **Frontend** — React + Vite, hosted on Vercel
- **Backend** — Express.js, hosted on Render
- **Database** — MongoDB Atlas
- **Auth** — JWT, Google OAuth

The backend publishes to HiveMQ using the `mqtt` npm package:

```javascript
mqttClient.publish('69series/alert', message, { retain: true, qos: 1 })
```

---

## ESP-IDF Components Used

| Component | Purpose |
|---|---|
| `esp_wifi` | WiFi station mode |
| `esp_http_client` | HTTPS GET to Binance |
| `esp_netif` | Network interface / TCP-IP stack |
| `nvs_flash` | Non-volatile storage (WiFi calibration) |
| `json` | cJSON — parse Binance API response |
| `mqtt` | MQTT client — HiveMQ connection |
| `mbedtls` | TLS certificate bundle |
| `esp_rom` | `esp_rom_delay_us()` — hardware timing |
| `esp_driver_gpio` | Raw GPIO control for LCD |
| `freertos` | Tasks, semaphores, event groups |

---

## Author

**Narendra Sagolsem (69series)**
Electronics Engineer — Embedded Systems, IoT, Signal Processing, PCB Design

- GitHub: [@69series](https://github.com/69series)

---

## License

MIT License — see [LICENSE](LICENSE) for details.