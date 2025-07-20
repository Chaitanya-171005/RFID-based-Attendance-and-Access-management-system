# RFID based Attendance & Access Management System

A modern, easy-to-deploy system for secure attendance and access control using ESP32, dual RFID readers, MQTT, and multiple peripherals.

---

## 📦 Project Structure

```
RFID-Attendance-and-Access-Management-System/
├── firmware/         # ESP32 code (main.cpp)
├── server/           # MQTT backend in C (main.c)
└── README.md         # Project documentation
```

---

## 🛠️ Hardware Used

- ESP32 38 pin Dev Board
- 2 × MFRC522 RFID Modules (SPI)
- WS2812/NeoPixel Circular LED Ring
- Passive Buzzer
- 10k Potentiometer
- 0.96" I2C OLED Display (SSD1306)
- Jumper wires, breadboard

---

## 🔌 Wiring & Connections

| Peripheral         | Module Pin      | ESP32 Pin (Example) | Notes                |
|--------------------|-----------------|---------------------|----------------------|
| MFRC522 #1         | SDA (SS)        | GPIO5               | SPI SS / CS (Reader 1)|
|                    | SCK             | GPIO14              | SPI Clock (shared)   |
|                    | MOSI            | GPIO13              | SPI MOSI (shared)    |
|                    | MISO            | GPIO12              | SPI MISO (shared)    |         |
|                    | GND             | GND                 | Ground               |
|                    | 3.3V            | 3.3V                | Power                |
| MFRC522 #2         | SDA (SS)        | GPIO4               | SPI SS / CS (Reader 2)|
|                    | SCK             | GPIO14              | SPI Clock (shared)   |
|                    | MOSI            | GPIO13              | SPI MOSI (shared)    |
|                    | MISO            | GPIO12              | SPI MISO (shared)    |
|                    | GND             | GND                 | Ground               |
|                    | 3.3V            | 3.3V                | Power                |
| NeoPixel Ring      | DIN             | GPIO27              | Data In              |
|                    | VCC             | 3.3V                | Power                |
|                    | GND             | GND                 | Ground               |
| Buzzer             | +               | GPIO26              | Digital Output       |
|                    | -               | GND                 | Ground               |
| Potentiometer      | Center (OUT)    | GPIO34              | Analog Input         |
|                    | Side 1          | 3.3V                | Power                |
|                    | Side 2          | GND                 | Ground               |
| OLED Display (I2C) | SDA             | GPIO21              | I2C Data             |
|                    | SCL             | GPIO22              | I2C Clock            |
|                    | VCC             | 3.3V                | Power                |
|                    | GND             | GND                 | Ground               |


---

## 💡 System Architecture

- **Dual RFID Readers:** Two MFRC522 modules allow for IN/OUT or multi-zone access points. Each reader is uniquely identified by its SS (CS) and RST pins.
- **ESP32:** Central controller, handles RFID scanning, MQTT communication, and peripheral control.
- **NeoPixel Ring:** Visual feedback for access granted/denied.
- **Buzzer:** Audio feedback for events.
- **Potentiometer:** Used to set location.
- **OLED Display:** Shows status, UID, access results, and system info.
- **MQTT:** Reliable communication between ESP32 and server backend.

---

## 💻 Software Requirements

- PlatformIO (recommended) or Arduino IDE for firmware
- C Compiler (GCC) for server
- Mosquitto MQTT broker and development libraries

Install MQTT dependencies on Linux:
```bash
sudo apt install mosquitto libmosquitto-dev
```

---

## ⚡ Quick Start

### 1. Hardware
- Connect both MFRC522 readers and all other peripherals as per the table above.

### 2. ESP32 Firmware
- Flash the firmware using PlatformIO or Arduino IDE.
- Set the correct MQTT server IP in the code.
- Configure SS and RST pins for both RFID readers in your firmware if needed.

### 3. Server Setup
- Go to the `server/` directory.
- Compile the server:
  ```bash
  gcc main.c -o logger -lmosquitto
  ```
- Run the logger:
  ```bash
  ./logger
  ```
- Ensure `access.csv` is present in the same directory.

---

## 📝 CSV File Formats

### `access.csv`

Defines access control rules for each authorized RFID user.

```csv
UID,Name,Role,Locations,ValidFrom,ValidUntil,ValidTimeStart,ValidTimeEnd
A1B2C3D4,Name1,Admin,1|2|3|4,2025-01-01,2025-12-31,00:00,23:59
B2C3D4E5,Name2,Employee,1|2,2025-05-01,2025-12-01,08:00,20:00
C3D4E5F6,Name3,Contractor,3,2025-06-01,2025-09-01,10:00,18:00
```

**Field Descriptions:**

* `UID`: RFID tag in hexadecimal format.
* `Name`: Placeholder name for the individual.
* `Role`: Designation such as Admin, Employee, or Contractor.
* `Locations`: Pipe-separated list of authorized location IDs (e.g., `1|2|3`).
* `ValidFrom` / `ValidUntil`: Date range during which access is permitted (`YYYY-MM-DD`).
* `ValidTimeStart` / `ValidTimeEnd`: Time of day access is allowed (`HH:MM`, 24-hour format).

---

### `attendance.csv`

Automatically generated logs of each access event.

```csv
UID,Name,Location,Direction,Timestamp
A1B2C3D4,Name1,2,IN,2025-07-20 09:12:45
A1B2C3D4,Name1,2,OUT,2025-07-20 18:00:12
B2C3D4E5,Name2,1,IN,2025-07-20 08:15:00
```

**Field Descriptions:**

* `UID`: RFID tag used in the event.
* `Name`: Name corresponding to the UID (e.g., `Name1`, `Name2`).
* `Location`: Zone or reader location ID.
* `Direction`: Entry/Exit marker — `IN` or `OUT`.
* `Timestamp`: Time the event occurred (`YYYY-MM-DD HH:MM:SS`).

---

## 📡 MQTT Topics

| Topic             | Direction       | Description                      |
| ----------------- | --------------- | -------------------------------- |
| `rfid/in`         | ESP32 → Server  | Payload: UID,Location            |
| `rfid/out`        | ESP32 → Server  | Payload: UID,Location            |
| `access/response` | Server → ESP32  | Payload: 1 (granted), 0 (denied) |

---

## 🔒 Access Logic

1. ESP32 scans both RFID readers for tags.
2. When a tag is detected, ESP32 sends UID and reader location (e.g., IN/OUT) to the server via MQTT.
3. Server checks if UID exists in `access.csv` and if current time is within allowed range for that location.
4. Server logs the result (GRANTED/DENIED) to `attendance.csv`.
5. Server responds to ESP32 with 1 (granted) or 0 (denied).
6. ESP32 provides visual (NeoPixel), audio (buzzer), and display (OLED) feedback for each event.

---


