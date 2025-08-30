# FilaMan Wiki - English

## Table of Contents

1. [Overview](#overview)
2. [Installation](#installation)
3. [Hardware Requirements](#hardware-requirements)
4. [Initial Setup](#initial-setup)
5. [Configuration](#configuration)
6. [Usage](#usage)
7. [NFC Tags](#nfc-tags)
8. [Bambu Lab Integration](#bambu-lab-integration)
9. [Spoolman Integration](#spoolman-integration)
10. [Octoprint Integration](#octoprint-integration)
11. [Manufacturer Tags](#manufacturer-tags)
12. [Troubleshooting](#troubleshooting)
13. [Support](#support)

---

## Overview

FilaMan is a comprehensive filament management system for 3D printers based on ESP32 hardware. It provides weight measurement, NFC tag management, and seamless integration with Spoolman and Bambu Lab 3D printers.

### Key Features

- **Precise weight measurement** with HX711 load cell amplifier
- **NFC tag reading and writing** for filament identification
- **OLED display** for status information
- **WiFi connectivity** with easy configuration
- **Web-based user interface** with real-time updates
- **Spoolman integration** for inventory management
- **Bambu Lab AMS control** via MQTT
- **OpenSpool NFC format** compatibility
- **Manufacturer tag support** for automatic setup

### System Requirements

- **ESP32 Development Board**
- **Spoolman Instance** (required for full functionality)
- **WiFi Network**
- **Web Browser** (Chrome/Firefox/Safari)

---

## Installation

### Easy Installation (Recommended)

1. **Open the [FilaMan Web Installer](https://www.filaman.app/installer.html)**
   - Use a Chrome-based browser

2. **Prepare ESP32**
   - Connect ESP32 via USB to your computer
   - Click "Connect"

3. **Select Port**
   - Choose the appropriate USB port
   - Confirm selection

4. **Start Installation**
   - Click "Install FilaMan"
   - Wait for installation to complete

### Manual Compilation

For advanced users with PlatformIO:

```bash
git clone https://github.com/ManuelW77/Filaman.git
cd FilaMan/esp32
pio lib install
pio run --target upload
```

---

## Hardware Requirements

### Required Components

| Component | Description | Amazon Link (Affiliate) |
|-----------|-------------|-------------------------|
| ESP32 Development Board | Any ESP32 variant | [Amazon](https://amzn.to/3FHea6D) |
| HX711 + Load Cell | 5kg Load Cell Amplifier | [Amazon](https://amzn.to/4ja1KTe) |
| OLED Display | 0.96" I2C 128x64 SSD1306 | [Amazon](https://amzn.to/445aaa9) |
| PN532 NFC Module | V3 RFID Module | [Amazon](https://amzn.eu/d/gy9vaBX) |
| NFC Tags | NTAG213/NTAG215 | [Amazon](https://amzn.to/3E071xO) |
| TTP223 Touch Sensor | Optional for tare function | [Amazon](https://amzn.to/4hTChMK) |

### Pin Configuration

| Component | ESP32 Pin | Function |
|-----------|-----------|----------|
| HX711 DOUT | 16 | Load cell data output |
| HX711 SCK | 17 | Load cell clock |
| OLED SDA | 21 | I2C data |
| OLED SCL | 22 | I2C clock |
| PN532 IRQ | 32 | Interrupt |
| PN532 RESET | 33 | Reset |
| PN532 SDA | 21 | I2C data (shared) |
| PN532 SCL | 22 | I2C clock (shared) |
| TTP223 I/O | 25 | Touch sensor (optional) |

### Important Notes

- **PN532 DIP switches** must be set to I2C mode
- **3V pin** from ESP32 for touch sensor
- **Load cell wiring**: E+ (red), E- (black), A- (white), A+ (green)

![Wiring Diagram](./img/Schaltplan.png)

---

## Initial Setup

### After Installation

1. **ESP32 Restart**
   - System automatically creates a WiFi hotspot "FilaMan"

2. **WiFi Configuration**
   - Connect to the "FilaMan" network
   - Open browser (automatic portal or <http://192.168.4.1>)
   - Configure your WiFi credentials

3. **First Access**
   - After successful WiFi connection, access system at <http://filaman.local>
   - Alternative: Use IP address assigned by router

### Spoolman Preparation

**Important Note**: Spoolman must run in debug mode:

```env
# Uncomment in Spoolman's .env file:
SPOOLMAN_DEBUG_MODE=TRUE
```

This is required as Spoolman doesn't support CORS domain configuration yet.

---

## Configuration

### Scale Calibration

1. **Start Calibration**
   - Go to "Scale" page
   - Prepare a 500g reference weight (e.g., water glass)

2. **Calibration Steps**
   - Follow instructions on display
   - Place weight when prompted
   - Wait for calibration to complete

3. **Validation**
   - Test accuracy with known weights
   - Use "Tare Scale" for zero adjustment if needed

### Spoolman Connection

1. **Enter Spoolman URL**
   - Go to "Spoolman/Bambu" page
   - Enter complete URL of your Spoolman instance
   - Format: `http://spoolman-server:7912`

2. **Test Connection**
   - System automatically checks connection
   - Successful connection shown by green status

### Bambu Lab Printer (Optional)

1. **Printer Settings**
   - Open settings menu on your Bambu printer
   - Note the following data:
     - Printer IP address
     - Access Code
     - Serial Number

2. **FilaMan Configuration**
   - Enter printer data on "Spoolman/Bambu" page
   - Enable "Auto Send to Bambu" for automatic AMS assignment

3. **Auto-Send Timeout**
   - Configure waiting time for automatic spool detection
   - Recommended value: 10-30 seconds

---

## Usage

### Basic Operation

1. **Weigh Filament**
   - Place spool on scale
   - Weight automatically displayed on screen and web interface

2. **Scan NFC Tag**
   - Hold tag near PN532 module
   - Recognized tags display spool information
   - Weight automatically updated in Spoolman

3. **Status Monitoring**
   - **OLED Display** shows current weight and connection status
   - **Web Interface** provides detailed information and control

### Web Interface Navigation

- **Home**: Main functions and current status
- **Scale**: Scale calibration and settings
- **Spoolman/Bambu**: System configuration
- **Statistics**: Usage statistics (if enabled)

---

## NFC Tags

### Supported Tag Types

- **NTAG213**: 144 bytes (basic functions)
- **NTAG215**: 504 bytes (recommended)
- **NTAG216**: 888 bytes (extended functions)

### Writing Tags

1. **Prepare Spool in Spoolman**
   - Create new spool in Spoolman
   - Ensure all required data is entered

2. **Start Tag Writing**
   - Select spool from list
   - Click "Write Tag"
   - Display shows "Waiting for Tag"

3. **Place Tag**
   - Position NFC tag on PN532 module
   - Wait for confirmation

4. **Success Message**
   - Successful writing shows checkmark
   - Tag is now linked to Spoolman spool

### Reading Tags

1. **Scan Tag**
   - Place the spool with NFC tag on the scale over the NFC reader
   - If reading fails: Reposition spool slightly (not completely at the edge)
   - Spool information automatically loaded

2. **Automatic Updates**
   - Current weight transferred to Spoolman
   - Spool automatically selected in web interface

---

## Bambu Lab Integration

### AMS (Automatic Material System)

1. **Display AMS Status**
   - Web interface shows current state of all AMS slots
   - Loaded slots display filament information

2. **Manual Filament Assignment**
   - Select spool from Spoolman list
   - Click corresponding AMS slot icon
   - Filament assigned to slot

3. **Automatic Assignment**
   - After weighing with "Auto Send to Bambu" enabled
   - System waits for new spools in AMS
   - Calibrated filaments automatically assigned

### Bambu Studio Integration

1. **Sync Filament Profiles**
   - Calibrate filaments in Bambu Studio
   - Use Device → AMS → Pencil icon → Select

2. **Save Setting IDs**
   - FilaMan automatically detects available setting IDs
   - Click "Save Settings to Spoolman"
   - Profiles used for future prints

### Restore Connection

- For connection issues, click red dot in menu bar
- System automatically establishes new connection

---

## Spoolman Integration

### Automatic Functions

1. **Spool Synchronization**
   - Automatic transfer of weight changes
   - Real-time updates of spool data

2. **Extra Fields**
   - FilaMan automatically creates required custom fields
   - NFC tag UID stored as reference

3. **Filtering**
   - "Show only spools without NFC tag" for easy tag assignment
   - Categorization by manufacturers and material types

### Spoolman Octoprint Plugin

For Octoprint users, automatic spool assignment is available:

1. **Install Plugin**

   ```text
   https://github.com/ManuelW77/OctoPrint-Spoolman-Filaman/archive/refs/heads/master.zip
   ```

2. **Configure FilaMan**
   - Enable "Send to Octo-Plugin"
   - Enter Octoprint URL and API key

3. **Automatic Assignment**
   - After weighing, spool automatically activated in Octoprint
   - Currently supports only Tool0 (first nozzle)

---

## Manufacturer Tags

### Overview

Manufacturer tags allow filament producers to provide pre-configured NFC tags that automatically create all necessary entries in Spoolman.

### Getting Started with Manufacturer Tags

1. **Scan Tag**
   - Place spool with manufacturer tag on the scale over the NFC reader
   - If reading fails: Reposition spool slightly (not completely at the edge)
   - System automatically recognizes manufacturer format

2. **Automatic Creation**
   - **Brand** created in Spoolman (if not present)
   - **Filament type** created with all specifications
   - **Spool** automatically registered

3. **Future Scans**
   - After initial setup, tags use fast-path system
   - Immediate weight measurement without re-setup

### Supported Manufacturers

- **RecyclingFabrik**: First official partner
- More manufacturers coming soon

### Benefits

- ✅ **Zero manual setup**
- ✅ **Perfect data accuracy**
- ✅ **Instant integration**
- ✅ **Future-proof**

---

## Troubleshooting

### Common Issues

#### WiFi Connection

**Issue**: Cannot connect to FilaMan hotspot

- Solution: Ensure ESP32 is started
- Alternative: Manually navigate to <http://192.168.4.1>

**Issue**: Web interface not accessible

- Solution: Check IP address in router
- Alternative: Use <http://filaman.local>

#### Scale

**Issue**: Inaccurate weight measurements

- Solution: Repeat calibration
- Tip: Use "Tare Scale" for zero adjustment

**Issue**: Load cell not responding

- Solution: Check wiring (E+, E-, A+, A-)
- Tip: Test with multimeter

#### NFC Tags

**Issue**: Tag not recognized

- Solution: Check PN532 DIP switches (I2C mode)
- Tip: Reposition spool slightly on scale (not completely at the edge)

**Issue**: Cannot write tag

- Solution: Use NTAG215 for better compatibility
- Tip: Ensure tag is not write-protected

#### Spoolman

**Issue**: Connection to Spoolman fails

- Solution: Enable SPOOLMAN_DEBUG_MODE=TRUE
- Tip: Check URL formatting

**Issue**: Spools not displayed

- Solution: Ensure Spoolman is running
- Tip: Check network firewall settings

#### Bambu Lab

**Issue**: Printer won't connect

- Solution: Check access code and IP address
- Tip: Ensure printer is in LAN mode

**Issue**: AMS status not displayed

- Solution: Check MQTT connection
- Note: Bambu may close API at any time

### Debug Information

If you have problems, you can use these steps for diagnosis:

#### Serial Monitor (for developers)

- Connect the ESP32 via USB to your computer
- Open a serial monitor (e.g., Arduino IDE) with 115200 baud
- You will see detailed log messages from the system

#### Browser Console

- Open the FilaMan web interface
- Press F12 to open developer tools
- Check the console for error messages

---

## Maintenance and Updates

### Firmware Update

1. **Via Web Interface**: Access `http://filaman.local/upgrade.html`
2. **Select firmware file** (.bin format)
3. **Upload** - System restarts automatically
4. **Configuration preserved** - Settings remain intact

### System Reset

For persistent issues:

1. Disconnect ESP32 from power
2. Wait 10 seconds
3. Reconnect
4. Wait 30 seconds for complete startup

---

## Support and Information

**Manufacturer**: Your Company Name
**Maintainer**: Manuel W.

### Scale Technology

#### Weight Stabilization

The system uses multiple filters for precise measurements:

```cpp
// Moving Average Filter with 8 values
#define MOVING_AVERAGE_SIZE 8
// Low-Pass Filter for smoothing
#define LOW_PASS_ALPHA 0.3f
// Thresholds for updates
#define DISPLAY_THRESHOLD 0.3f    // Display update
#define API_THRESHOLD 1.5f        // API actions
```

#### Calibration Algorithm

1. **System Pause**: All tasks are temporarily paused
2. **Zero Setting**: Tare scale without weight
3. **Reference Measurement**: 500g weight for 10 measurements
4. **Calculation**: `newValue = rawValue / SCALE_LEVEL_WEIGHT`
5. **NVS Storage**: Permanent value with verification
6. **Filter Reset**: New baseline for stabilization

#### Auto-Tare Logic

```cpp
// Conditions for Auto-Tare
if (autoTare && (weight > 2 && weight < 7) || weight < -2) {
    scale_tare_counter++;
    if (scale_tare_counter >= 5) {
        // Automatic zero setting
        scale.tare();
        resetWeightFilter();
    }
}
```

### NFC Technology

#### PN532 Communication

- **Interface**: I2C at 400kHz
- **IRQ Pin**: Interrupt-based tag detection
- **Reset Handling**: Automatic recovery from communication errors
- **DIP Switches**: Must be set to I2C mode (00)

#### NDEF Implementation

```json
// FilaMan Spoolman Format (with sm_id)
{
  "sm_id": "123",
  "color": "#FF5733",
  "type": "PLA", 
  "brand": "Example Brand"
}
```

#### Manufacturer Tag Schema

Compact JSON format for storage efficiency:

```json
{
  "b": "RecyclingFabrik",           // brand
  "an": "FX1_PLA-S175-1000-RED",  // article number
  "t": "PLA",                      // type
  "c": "FF0000",                   // color (hex without #)
  "cn": "Red",                     // color name
  "et": "210",                     // extruder temp
  "bt": "60",                      // bed temp
  "di": "1.75",                    // diameter
  "de": "1.24",                    // density
  "sw": "240",                      // spool weight
  "u": "https://www.yoururl.com/search?q=" // URL used vor Brand Link and Filament Link
}
```

### Display System

#### OLED Architecture (SSD1306)

- **Resolution**: 128x64 pixels monochrome
- **Areas**:
  - Status bar: 0-16 pixels (version, icons)
  - Main area: 17-64 pixels (weight, messages)
- **Update Interval**: 1 second for status line

#### Icon System

Bitmap icons for various states:

```cpp
// Status Icons (16x16 pixels)
- icon_success: Checkmark for successful operations
- icon_failed: X for errors
- icon_transfer: Arrow for data transmission
- icon_loading: Loading circle for ongoing operations

// Connection Icons with strikethrough indicator
- wifi_on/wifi_off: WLAN status
- bambu_on: Bambu Lab connection
- spoolman_on: Spoolman API status
```

### API Integration

#### Spoolman REST API

FilaMan interacts with the following endpoints:

```http
GET  /api/v1/spool/          # List spools
POST /api/v1/spool/          # Create new spool
PUT  /api/v1/spool/{id}/     # Update spool

GET  /api/v1/vendor/         # List vendors
POST /api/v1/vendor/         # Create new vendor

GET  /api/v1/filament/       # List filaments
POST /api/v1/filament/       # Create new filament
```

#### Request Handling

```cpp
// Sequential API processing
enum spoolmanApiStateType {
    API_IDLE = 0,
    API_PROCESSING = 1,
    API_ERROR = 2
};
```

Prevents simultaneous API calls and deadlocks.

#### Weight Update Logic

```cpp
// Conditions for Spoolman update
if (activeSpoolId != "" && 
    weigthCouterToApi > 3 &&    // 3+ stable measurements
    weightSend == 0 &&          // Not yet sent
    weight > 5 &&               // Minimum weight 5g
    spoolmanApiState == API_IDLE) {
    updateSpoolWeight(activeSpoolId, weight);
}
```

### Bambu Lab MQTT

#### Connection Parameters

```cpp
// SSL/TLS Configuration
#define BAMBU_PORT 8883
#define BAMBU_USERNAME "bblp"

// Topic Structure
String topic = "device/" + bambu_serial + "/report";
String request_topic = "device/" + bambu_serial + "/request";
```

#### AMS Data Structure

```cpp
struct AMSData {
    String tray_id;
    String tray_type;
    String tray_color;
    String tray_material;
    String setting_id;
    String tray_info_idx;
    bool has_spool;
};
```

#### Auto-Send Mechanism

```cpp
// After tag recognition
if (bambuCredentials.autosend_enable) {
    autoSetToBambuSpoolId = activeSpoolId.toInt();
    // Countdown starts automatically
    // Waits for new spool in AMS
}
```

### WebSocket Communication

#### Message Types

```javascript
// Client → Server
{
  "type": "writeNfcTag",
  "tagType": "spool",
  "payload": { /* JSON data */ }
}

{
  "type": "scale",
  "payload": "tare|calibrate|setAutoTare",
  "enabled": true
}

// Server → Client
{
  "type": "heartbeat",
  "freeHeap": 245,
  "bambu_connected": true,
  "spoolman_connected": true
}

{
  "type": "amsData",
  "data": [ /* AMS array */ ]
}
```

#### Connection Management

- **Auto-Reconnect**: Client-side reconnection
- **Heartbeat**: Every 30 seconds for connection monitoring
- **Cleanup**: Automatic removal of dead connections

### Watchdog and Error Handling

#### System Watchdog

```cpp
// WDT Configuration
esp_task_wdt_init(10, true);  // 10s timeout, panic on overflow
esp_task_wdt_add(NULL);       // Add current task
```

#### Error Recovery

- **NFC Reset**: Automatic PN532 restart on communication errors
- **MQTT Reconnect**: Bambu Lab connection automatically restored
- **WiFi Monitoring**: Connection check every 60 seconds

---

## Support

### Community

- **Discord Server**: [https://discord.gg/my7Gvaxj2v](https://discord.gg/my7Gvaxj2v)
- **GitHub Issues**: [Filaman Repository](https://github.com/ManuelW77/Filaman/issues)
- **YouTube Channel**: [German explanation video](https://youtu.be/uNDe2wh9SS8?si=b-jYx4I1w62zaOHU)

### Documentation

- **Official Website**: [www.filaman.app](https://www.filaman.app)
- **GitHub Wiki**: [Detailed documentation](https://github.com/ManuelW77/Filaman/wiki)
- **Hardware Reference**: ESP32 pinout diagrams in `/img/`

### Support Development

If you'd like to support the project:

[![Buy Me A Coffee](https://cdn.buymeacoffee.com/buttons/v2/default-yellow.png)](https://www.buymeacoffee.com/manuelw)

### License

This project is released under the MIT License. See [LICENSE](LICENSE.txt) for details.

---

**Last Updated**: August 2025  
**Version**: 2.0  
**Maintainer**: Manuel W.