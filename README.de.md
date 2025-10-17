# FilaMan - Filament Management System

FilaMan ist ein Filament-Managementsystem für den 3D-Druck. Es verwendet ESP32-Hardware für Gewichtsmessungen und NFC-Tag-Management. 
Benutzer können Filamentspulen verwalten, den Status des Automatic Material System (AMS) von Bablulab Druckern überwachen und Einstellungen über eine Weboberfläche vornehmen. 
Das System integriert sich nahtlos mit der [Spoolman](https://github.com/Donkie/Spoolman) Filamentverwaltung, zusätzlich mit [Bambulab](https://bambulab.com/en-us) 3D-Druckern und sowie dem [Openspool](https://github.com/spuder/OpenSpool) NFC-TAG Format.

![Scale](./img/scale_trans.png)

Weitere Bilder finden Sie im [img Ordner](/img/)
oder auf meiner Website: [FilaMan Website](https://www.filaman.app)  
Deutsches Erklärvideo: [Youtube](https://youtu.be/uNDe2wh9SS8?si=b-jYx4I1w62zaOHU)  
Discord Server: [https://discord.gg/my7Gvaxj2v](https://discord.gg/my7Gvaxj2v)

## NEU: Recycling Fabrik

<a href="https://www.recyclingfabrik.com" target="_blank">
    <img src="img/rf-logo.png" alt="Recycling Fabrik" width="200">
</a>

FilaMan wird von [Recycling Fabrik](https://www.recyclingfabrik.com) unterstützt.
Recycling Fabrik wird demnächst auf seinen Spulen einen FilaMan tauglichen NFC Tag anbieten. Das hat den Vorteil,
dass die Spulen direkt über FilaMan, ganz automatisch, erkannt und in Spoolman importiert werden können.

**Was ist Recycling Fabrik?**

Die Recycling Fabrik ist ein deutsches Unternehmen, das sich der Entwicklung und Herstellung von nachhaltigem 3D-Druck-Filament verschrieben hat. 
Ihre Filamente bestehen zu 100 % aus recyceltem Material, welches sowohl vom Endkunden, als auch aus der Industrie stammt – für eine umweltbewusste und ressourcenschonende Zukunft.

Mehr Informationen und Produkte findest du hier: [www.recyclingfabrik.com](https://www.recyclingfabrik.com)

---


### Es gibt jetzt auch ein Wiki, dort sind nochmal alle Funktionen beschrieben: [Wiki](https://github.com/ManuelW77/Filaman/wiki)

### ESP32 Hardware-Funktionen
- **Gewichtsmessung:** Verwendung einer Wägezelle mit HX711-Verstärker für präzise Gewichtsverfolgung.
- **NFC-Tag Lesen/Schreiben:** PN532-Modul zum Lesen und Schreiben von Filamentdaten auf NFC-Tags.
- **OLED-Display:** Zeigt aktuelles Gewicht, Verbindungsstatus (WiFi, Bambu Lab, Spoolman).
- **WLAN-Konnektivität:** WiFiManager für einfache Netzwerkkonfiguration.
- **MQTT-Integration:** Verbindet sich mit Bambu Lab Drucker für AMS-Steuerung.
- **NFC-Tag NTAG213 NTAG215:** Verwendung von NTAG213, besser NTAG215 wegen ausreichendem Speicherplatz auf dem Tag

### Weboberflächen-Funktionen
- **Echtzeit-Updates:** WebSocket-Verbindung für Live-Daten-Updates.
- **NFC-Tag-Verwaltung:** 
    - Filamentdaten auf NFC-Tags schreiben.
    - Verwendet das NFC-Tag-Format von [Openspool](https://github.com/spuder/OpenSpool)
    - Ermöglicht automatische Spulenerkennung im AMS
    - **Hersteller Tag Unterstützung:** Automatische Erstellung von Spoolman-Einträgen aus Hersteller NFC-Tags ([Mehr erfahren](README_ManufacturerTags_DE.md))
- **Bambulab AMS-Integration:** 
  - Anzeige der aktuellen AMS-Fachbelegung.
  - Zuordnung von Filamenten zu AMS-Slots.
  - Unterstützung für externe Spulenhalter.
- **Spoolman-Integration:**
  - Auflistung verfügbarer Filamentspulen.
  - Filtern und Auswählen von Filamenten.
  - Automatische Aktualisierung der Spulengewichte.
  - Verfolgung von NFC-Tag-Zuweisungen.
  - Unterstützt das Spoolman Octoprint Plugin

### Wenn Sie meine Arbeit unterstützen möchten, freue ich mich über einen Kaffee

<a href="https://www.buymeacoffee.com/manuelw" target="_blank"><img src="https://cdn.buymeacoffee.com/buttons/v2/default-yellow.png" alt="Buy Me A Coffee" style="height: 30px !important;width: 108px !important;" ></a>

## Hersteller Tags Unterstützung

🎉 **Aufregende Neuigkeiten!** FilaMan unterstützt jetzt **Hersteller Tags** - NFC-Tags, die direkt von Filament-Herstellern vorprogrammiert geliefert werden!

### Erster Hersteller-Partner: RecyclingFabrik

Wir freuen uns anzukündigen, dass [**RecyclingFabrik**](https://www.recyclingfabrik.de) der **erste Filament-Hersteller** sein wird, der FilaMan unterstützt, indem sie NFC-Tags im FilaMan-Format auf ihren Spulen anbieten!

**Demnächst verfügbar:** RecyclingFabrik-Spulen werden NFC-Tags enthalten, die sich automatisch in Ihr FilaMan-System integrieren, manuelle Einrichtung überflüssig machen und perfekte Kompatibilität gewährleisten.

### Wie Hersteller Tags funktionieren

Wenn Sie zum ersten Mal einen Hersteller NFC-Tag scannen:
1. **Automatische Markenerkennung:** FilaMan erkennt den Hersteller und erstellt die Marke in Spoolman
2. **Filament-Typ Erstellung:** Alle Materialspezifikationen werden automatisch hinzugefügt
3. **Spulen-Registrierung:** Ihre spezifische Spule wird mit korrektem Gewicht und Spezifikationen registriert
4. **Zukünftige Schnellerkennung:** Nachfolgende Scans verwenden Fast-Path-Erkennung für sofortige Gewichtsmessung

**Für detaillierte technische Informationen:** [Hersteller Tags Dokumentation](README_ManufacturerTags_DE.md)

### Vorteile für Benutzer
- ✅ **Null manuelle Einrichtung** - Einfach scannen und wiegen
- ✅ **Perfekte Datengenauigkeit** - Hersteller-verifizierte Spezifikationen
- ✅ **Sofortige Integration** - Nahtlose Spoolman-Kompatibilität
- ✅ **Zukunftssicher** - Tags funktionieren mit jedem FilaMan-kompatiblen System

## Detaillierte Funktionalität

### ESP32-Funktionalität
- **Druckaufträge steuern und überwachen:** Der ESP32 kommuniziert mit dem Bambu Lab Drucker.
- **Drucker-Kommunikation:** Nutzt MQTT für Echtzeit-Kommunikation mit dem Drucker.
- **Benutzerinteraktionen:** Das OLED-Display bietet sofortiges Feedback zum Systemstatus.

### Weboberflächen-Funktionalität
- **Benutzerinteraktionen:** Die Weboberfläche ermöglicht Benutzern die Interaktion mit dem System.
- **UI-Elemente:** Enthält Dropdown-Menüs für Hersteller und Filamente, Buttons zum Beschreiben von NFC-Tags und Echtzeit-Statusanzeigen.

## Hardware-Anforderungen

### Komponenten (Affiliate Links)
- **ESP32 Development Board:** Any ESP32 variant.
[Amazon Link](https://amzn.to/3FHea6D)
- **HX711 5kg Load Cell Amplifier:** For weight measurement.
[Amazon Link](https://amzn.to/4ja1KTe)
- **OLED 0.96 Zoll I2C white/yellow Display:** 128x64 SSD1306.
[Amazon Link](https://amzn.to/445aaa9)
- **PN532 NFC NXP RFID-Modul V3:** For NFC tag operations.
[Amazon Link](https://amzn.eu/d/gy9vaBX)
- **NFC Tags NTAG213 NTAG215:** RFID Tag
[Amazon Link](https://amzn.to/3E071xO)
- **TTP223 Touch Sensor (optional):** For reTARE per Button/Touch
[Amazon Link](https://amzn.to/4hTChMK)


### Pin Konfiguration
| Component          | ESP32 Pin |
|-------------------|-----------|
| HX711 DOUT        | 16        |
| HX711 SCK         | 17        |
| OLED SDA          | 21        |
| OLED SCL          | 22        |
| PN532 IRQ         | 32        |
| PN532 RESET       | 33        |
| PN532 SDA         | 21        |
| PN532 SCL         | 22        |
| TTP223 I/O        | 25        |

**!! Achte darauf, dass am PN532 die DIP-Schalter auf I2C gestellt sind**  
**Nutze den 3V Pin vom ESP für den Touch Sensor**

![Wiring](./img/Schaltplan.png)

![myWiring](./img/IMG_2589.jpeg)
![myWiring](./img/IMG_2590.jpeg)

*Die Wägezelle wird bei den meisten HX711 Modulen folgendermaßen verkabelt:  
E+ rot  
E- schwarz  
A- weiß  
A+ grün*

## Software-Abhängigkeiten

### ESP32-Bibliotheken
- `WiFiManager`: Netzwerkkonfiguration
- `ESPAsyncWebServer`: Webserver-Funktionalität
- `ArduinoJson`: JSON-Verarbeitung
- `PubSubClient`: MQTT-Kommunikation
- `Adafruit_PN532`: NFC-Funktionalität
- `Adafruit_SSD1306`: OLED-Display-Steuerung
- `HX711`: Wägezellen-Kommunikation

## Installation

### Voraussetzungen
- **Software:**
  - [PlatformIO](https://platformio.org/) in VS Code
  - [Spoolman](https://github.com/Donkie/Spoolman) Instanz
- **Hardware:**
  - ESP32 Entwicklungsboard
  - HX711 Wägezellen-Verstärker
  - Wägezelle (Gewichtssensor)
  - OLED Display (128x64 SSD1306)
  - PN532 NFC Modul
  - Verbindungskabel

## Wichtiger Hinweis
Für die CORS-Kompatibilität mit FilaMan musst du Spoolman so konfigurieren, dass Cross-Origin-Requests erlaubt werden.

### Docker Installation
Füge diese Umgebungsvariable zu deiner Spoolman Docker-Konfiguration hinzu:

**In deiner `docker-compose.yml`:**
```yaml
services:
  spoolman:
    # ... andere Konfiguration
    environment:
      - SPOOLMAN_CORS_ORIGIN=*
```

**Oder bei Verwendung von `docker run`:**
```bash
docker run -e SPOOLMAN_CORS_ORIGIN=* [andere Optionen] ghcr.io/donkie/spoolman:latest
```

**Sicherheitshinweis:** Die Verwendung von `SPOOLMAN_CORS_ORIGIN=*` erlaubt Requests von jeder Origin. Für Produktionsumgebungen solltest du stattdessen die spezifische IP-Adresse deines FilaMan-Systems angeben (z.B. `SPOOLMAN_CORS_ORIGIN=http://192.168.1.100`) anstelle von `*`, um die Sicherheit zu erhöhen.

## Schritt-für-Schritt Installation
### Einfache Installation
1. **Gehe auf [FilaMan Installer](https://www.filaman.app/installer.html)**

2. **Stecke dein ESP an den Rechner und klicke Connect**

3. **Wähle dein Device Port und klicke Intall**

4. **Ersteinrichtung:**
    - Mit dem "FilaMan" WLAN-Zugangspunkt verbinden.
    - WLAN-Einstellungen über das Konfigurationsportal vornehmen.
    - Weboberfläche unter `http://filaman.local` oder der IP-Adresse aufrufen.

### Compile by yourself
1. **Repository klonen:**
    ```bash
    git clone https://github.com/ManuelW77/Filaman.git
    cd FilaMan
    ```
2. **Abhängigkeiten installieren:**
    ```bash
    pio lib install
    ```
3. **ESP32 flashen:**
    ```bash
    pio run --target upload
    ```
4. **Ersteinrichtung:**
    - Mit dem "FilaMan" WLAN-Zugangspunkt verbinden.
    - WLAN-Einstellungen über das Konfigurationsportal vornehmen.
    - Weboberfläche unter `http://filaman.local` oder der IP-Adresse aufrufen.

## Dokumentation

### Relevante Links
- [PlatformIO Dokumentation](https://docs.platformio.org/)
- [Spoolman Dokumentation](https://github.com/Donkie/Spoolman)
- [Bambu Lab Drucker Dokumentation](https://www.bambulab.com/)

### Tutorials und Beispiele
- [PlatformIO erste Schritte](https://docs.platformio.org/en/latest/tutorials/espressif32/arduino_debugging_unit_testing.html)
- [ESP32 Webserver Tutorial](https://randomnerdtutorials.com/esp32-web-server-arduino-ide/)

## Lizenz

Dieses Projekt ist unter der MIT-Lizenz lizenziert. Siehe [LICENSE](LICENSE) Datei für Details.

## Materialien

### Nützliche Ressourcen
- [ESP32 Offizielle Dokumentation](https://docs.espressif.com/projects/esp-idf/en/latest/esp32/)
- [Arduino Bibliotheken](https://www.arduino.cc/en/Reference/Libraries)
- [NFC Tag Informationen](https://learn.adafruit.com/adafruit-pn532-rfid-nfc/overview)

### Community und Support
- [PlatformIO Community](https://community.platformio.org/)
- [Arduino Forum](https://forum.arduino.cc/)
- [ESP32 Forum](https://www.esp32.com/)

## Verfügbarkeit

Der Code kann getestet und die Anwendung kann vom [GitHub Repository](https://github.com/ManuelW77/Filaman) heruntergeladen werden.

### Wenn Sie meine Arbeit unterstützen möchten, freue ich mich über einen Kaffee
<a href="https://www.buymeacoffee.com/manuelw" target="_blank"><img src="https://cdn.buymeacoffee.com/buttons/v2/default-yellow.png" alt="Buy Me A Coffee" style="height: 30px !important;width: 108px !important;" ></a>