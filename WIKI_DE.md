# FilaMan Wiki - Deutsch

## Inhaltsverzeichnis

1. [Überblick](#überblick)
2. [Installation](#installation)
3. [Hardware-Anforderungen](#hardware-anforderungen)
4. [Ersteinrichtung](#ersteinrichtung)
5. [Konfiguration](#konfiguration)
6. [Benutzung](#benutzung)
7. [NFC-Tags](#nfc-tags)
8. [Bambu Lab Integration](#bambu-lab-integration)
9. [Spoolman Integration](#spoolman-integration)
10. [Octoprint Integration](#octoprint-integration)
11. [Hersteller Tags](#hersteller-tags)
12. [Fehlerbehebung](#fehlerbehebung)
13. [Support](#support)

---

## Überblick

FilaMan ist ein umfassendes Filament-Managementsystem für 3D-Drucker, das auf ESP32-Hardware basiert. Es bietet Gewichtsmessung, NFC-Tag-Management und nahtlose Integration mit Spoolman und Bambu Lab 3D-Druckern.

### Hauptfunktionen

- **Präzise Gewichtsmessung** mit HX711 Wägezellen-Verstärker
- **NFC-Tag Lesen und Schreiben** für Filament-Identifikation
- **OLED-Display** für Status-Anzeigen
- **WiFi-Konnektivität** mit einfacher Konfiguration
- **Webbasierte Benutzeroberfläche** mit Echtzeit-Updates
- **Spoolman-Integration** für Lagerverwaltung
- **Bambu Lab AMS-Steuerung** via MQTT
- **Openspool NFC-Format** Kompatibilität
- **Hersteller Tag Unterstützung** für automatische Einrichtung

### Systemvoraussetzungen

- **ESP32 Development Board**
- **Spoolman Instanz** (erforderlich für volle Funktionalität)
- **WiFi-Netzwerk**
- **Webbrowser** (Chrome/Firefox/Safari)

---

## Installation

### Einfache Installation (Empfohlen)

1. **Öffnen Sie den [FilaMan Web-Installer](https://www.filaman.app/installer.html)**
   - Verwenden Sie einen Chrome-basierten Browser

2. **ESP32 vorbereiten**
   - Verbinden Sie den ESP32 über USB mit Ihrem Computer
   - Klicken Sie auf "Connect"

3. **Port auswählen**
   - Wählen Sie den entsprechenden USB-Port aus
   - Bestätigen Sie die Auswahl

4. **Installation starten**
   - Klicken Sie auf "FilaMan installieren"
   - Warten Sie, bis der Installationsvorgang abgeschlossen ist

### Manuelle Kompilierung

Für erfahrene Benutzer mit PlatformIO:

```bash
git clone https://github.com/ManuelW77/Filaman.git
cd FilaMan/esp32
pio lib install
pio run --target upload
```

---

## Hardware-Anforderungen

### Erforderliche Komponenten

| Komponente | Beschreibung | Amazon Link (Affiliate) |
|------------|--------------|-------------------------|
| ESP32 Development Board | Jede ESP32-Variante | [Amazon](https://amzn.to/3FHea6D) |
| HX711 + Wägezelle | 5kg Load Cell Amplifier | [Amazon](https://amzn.to/4ja1KTe) |
| OLED Display | 0.96" I2C 128x64 SSD1306 | [Amazon](https://amzn.to/445aaa9) |
| PN532 NFC Modul | V3 RFID-Modul | [Amazon](https://amzn.eu/d/gy9vaBX) |
| NFC Tags | NTAG213/NTAG215 | [Amazon](https://amzn.to/3E071xO) |
| TTP223 Touch Sensor | Optional für Tara-Funktion | [Amazon](https://amzn.to/4hTChMK) |

### Pin-Konfiguration

| Komponente | ESP32 Pin | Funktion |
|------------|-----------|----------|
| HX711 DOUT | 16 | Datenausgang Wägezelle |
| HX711 SCK | 17 | Takt Wägezelle |
| OLED SDA | 21 | I2C Daten |
| OLED SCL | 22 | I2C Takt |
| PN532 IRQ | 32 | Interrupt |
| PN532 RESET | 33 | Reset |
| PN532 SDA | 21 | I2C Daten (geteilt) |
| PN532 SCL | 22 | I2C Takt (geteilt) |
| TTP223 I/O | 25 | Touch-Sensor (optional) |

### Wichtige Hinweise

- **PN532 DIP-Schalter** müssen auf I2C-Modus eingestellt sein
- **3V Pin** vom ESP32 für Touch-Sensor verwenden
- **Wägezellen-Verkabelung**: E+ (rot), E- (schwarz), A- (weiß), A+ (grün)

![Schaltplan](./img/Schaltplan.png)

---

## Ersteinrichtung

### Nach der Installation

1. **ESP32 Neustart**
   - Das System erstellt automatisch einen WiFi-Hotspot "FilaMan"

2. **WiFi-Konfiguration**
   - Verbinden Sie sich mit dem "FilaMan" Netzwerk
   - Öffnen Sie einen Browser (automatisches Portal oder http://192.168.4.1)
   - Konfigurieren Sie Ihre WiFi-Zugangsdaten

3. **Erster Zugriff**
   - Nach erfolgreicher WiFi-Verbindung ist das System unter http://filaman.local erreichbar
   - Alternativ über die vom Router zugewiesene IP-Adresse

### Spoolman Vorbereitung

**Wichtiger Hinweis**: Spoolman muss im Debug-Modus laufen:

```env
# In der .env Datei von Spoolman auskommentieren:
SPOOLMAN_DEBUG_MODE=TRUE
```

Dies ist erforderlich, da Spoolman noch keine CORS-Domain-Konfiguration unterstützt.

---

## Konfiguration

### Waagen-Kalibrierung

1. **Kalibrierung starten**
   - Gehen Sie zur "Scale" (Waage) Seite
   - Bereiten Sie ein 500g Referenzgewicht vor (z.B. Wasserglas)

2. **Kalibrierungsschritte**
   - Folgen Sie den Anweisungen auf dem Display
   - Legen Sie das Gewicht auf, wenn gefordert
   - Warten Sie, bis die Kalibrierung abgeschlossen ist

3. **Validierung**
   - Testen Sie die Genauigkeit mit bekannten Gewichten
   - Bei Bedarf "Tare Scale" für Nullstellung verwenden

### Spoolman-Verbindung

1. **Spoolman-URL eingeben**
   - Gehen Sie zur "Spoolman/Bambu" Seite
   - Geben Sie die vollständige URL Ihrer Spoolman-Instanz ein
   - Format: `http://spoolman-server:7912`

2. **Verbindung testen**
   - Das System prüft automatisch die Verbindung
   - Erfolgreiche Verbindung wird durch grünen Status angezeigt

### Bambu Lab Drucker (optional)

1. **Drucker-Einstellungen**
   - Öffnen Sie das Einstellungsmenü auf Ihrem Bambu-Drucker
   - Notieren Sie sich die folgenden Daten:
     - IP-Adresse des Druckers
     - Access Code
     - Serial Number

2. **FilaMan Konfiguration**
   - Geben Sie die Drucker-Daten in der "Spoolman/Bambu" Seite ein
   - Aktivieren Sie "Auto Send to Bambu" für automatische AMS-Zuordnung

3. **Auto-Send Timeout**
   - Konfigurieren Sie die Wartezeit für automatische Spulen-Erkennung
   - Empfohlener Wert: 10-30 Sekunden

---

## Benutzung

### Grundlegende Bedienung

1. **Filament wiegen**
   - Platzieren Sie die Spule auf der Waage
   - Das Gewicht wird automatisch auf dem Display und in der Weboberfläche angezeigt

2. **NFC-Tag scannen**
   - Halten Sie den Tag in die Nähe des PN532-Moduls
   - Bei erkannten Tags wird die Spulen-Information angezeigt
   - Das Gewicht wird automatisch in Spoolman aktualisiert

3. **Status-Überwachung**
   - **OLED-Display** zeigt aktuelles Gewicht und Verbindungsstatus
   - **Weboberfläche** bietet detaillierte Informationen und Steuerung

### Weboberfläche Navigation

- **Startseite**: Hauptfunktionen und aktueller Status
- **Scale**: Waagen-Kalibrierung und -Einstellungen
- **Spoolman/Bambu**: System-Konfiguration
- **Statistics**: Nutzungsstatistiken (falls aktiviert)

---

## NFC-Tags

### Unterstützte Tag-Typen

- **NTAG213**: 144 Bytes (grundlegende Funktionen)
- **NTAG215**: 504 Bytes (empfohlen)
- **NTAG216**: 888 Bytes (erweiterte Funktionen)

### Tag beschreiben

1. **Spule in Spoolman vorbereiten**
   - Erstellen Sie eine neue Spule in Spoolman
   - Stellen Sie sicher, dass alle erforderlichen Daten eingegeben sind

2. **Tag-Beschreibung starten**
   - Wählen Sie die Spule aus der Liste
   - Klicken Sie auf "Write Tag"
   - Das Display zeigt "Waiting for Tag"

3. **Tag auflegen**
   - Platzieren Sie den NFC-Tag auf dem PN532-Modul
   - Warten Sie auf die Bestätigung

4. **Erfolgsmeldung**
   - Bei erfolgreichem Beschreiben wird ein Häkchen angezeigt
   - Der Tag ist nun mit der Spoolman-Spule verknüpft

### Tag lesen

1. **Tag scannen**
   - Platzieren Sie die Spule mit dem NFC-Tag auf die Waage über dem NFC-Reader
   - Bei Problemen beim Lesen: Spule etwas anders positionieren (nicht ganz an den Rand)
   - Die Spulen-Information wird automatisch geladen

2. **Automatische Updates**
   - Das aktuelle Gewicht wird in Spoolman übertragen
   - Die Spule wird in der Weboberfläche automatisch ausgewählt

---

## Bambu Lab Integration

### AMS (Automatic Material System)

1. **AMS-Status anzeigen**
   - Die Weboberfläche zeigt den aktuellen Zustand aller AMS-Fächer
   - Beladene Fächer werden mit Filament-Informationen angezeigt

2. **Filament manuell zuordnen**
   - Wählen Sie eine Spule aus der Spoolman-Liste
   - Klicken Sie auf das entsprechende AMS-Fach-Symbol
   - Das Filament wird dem Fach zugeordnet

3. **Automatische Zuordnung**
   - Nach dem Wiegen mit aktiviertem "Auto Send to Bambu"
   - Das System wartet auf neue Spulen im AMS
   - Kalibrierte Filamente werden automatisch zugeordnet

### Bambu Studio Integration

1. **Filament-Profile synchronisieren**
   - Kalibrieren Sie Filamente in Bambu Studio
   - Verwenden Sie Device → AMS → Bleistift-Symbol → Auswählen

2. **Setting-IDs speichern**
   - FilaMan erkennt verfügbare Setting-IDs automatisch
   - Klicken Sie auf "Settings in Spoolman speichern"
   - Die Profile werden für zukünftige Drucke verwendet

### Verbindung wiederherstellen

- Bei Verbindungsproblemen klicken Sie den roten Punkt in der Menüleiste
- Das System stellt automatisch eine neue Verbindung her

---

## Spoolman Integration

### Automatische Funktionen

1. **Spulen-Synchronisation**
   - Automatische Übertragung von Gewichtsänderungen
   - Echtzeit-Updates der Spulen-Daten

2. **Extra-Felder**
   - FilaMan erstellt automatisch erforderliche benutzerdefinierte Felder
   - NFC-Tag-UID wird als Referenz gespeichert

3. **Filterung**
   - "Nur Spulen ohne NFC-Tag anzeigen" für einfache Tag-Zuordnung
   - Kategorisierung nach Herstellern und Materialtypen

### Spoolman Octoprint Plugin

Für Octoprint-Benutzer ist eine automatische Spulen-Zuordnung verfügbar:

1. **Plugin installieren**
   ```
   https://github.com/ManuelW77/OctoPrint-Spoolman-Filaman/archive/refs/heads/master.zip
   ```

2. **FilaMan konfigurieren**
   - Aktivieren Sie "Send to Octo-Plugin"
   - Geben Sie Octoprint-URL und API-Key ein

3. **Automatische Zuordnung**
   - Nach dem Wiegen wird die Spule automatisch in Octoprint aktiviert
   - Unterstützt aktuell nur Tool0 (erste Düse)

---

## Hersteller Tags

### Überblick

Hersteller Tags ermöglichen es Filament-Produzenten, vorkonfigurierte NFC-Tags zu liefern, die automatisch alle notwendigen Einträge in Spoolman erstellen.

### Erste Schritte mit Hersteller Tags

1. **Tag scannen**
   - Platzieren Sie die Spule mit dem Hersteller-Tag auf die Waage über dem NFC-Reader
   - Bei Problemen beim Lesen: Spule etwas anders positionieren (nicht ganz an den Rand)
   - Das System erkennt automatisch das Hersteller-Format

2. **Automatische Erstellung**
   - **Marke** wird in Spoolman angelegt (falls nicht vorhanden)
   - **Filament-Typ** wird mit allen Spezifikationen erstellt
   - **Spule** wird automatisch registriert

3. **Zukünftige Scans**
   - Nach der ersten Einrichtung nutzen Tags das Fast-Path-System
   - Sofortige Gewichtsmessung ohne erneute Einrichtung

### Unterstützte Hersteller

- **RecyclingFabrik**: Erster offizieller Partner
- Weitere Hersteller folgen

### Vorteile

- ✅ **Null manuelle Einrichtung**
- ✅ **Perfekte Datengenauigkeit**
- ✅ **Sofortige Integration**
- ✅ **Zukunftssicher**

---

## Fehlerbehebung

### Häufige Probleme

#### WiFi-Verbindung

**Problem**: Kann nicht mit FilaMan-Hotspot verbinden
- Lösung: Stellen Sie sicher, dass der ESP32 gestartet ist
- Alternative: Manuell zu http://192.168.4.1 navigieren

**Problem**: Weboberfläche nicht erreichbar
- Lösung: Prüfen Sie die IP-Adresse im Router
- Alternative: Verwenden Sie http://filaman.local

#### Waage

**Problem**: Ungenaue Gewichtsmessungen
- Lösung: Kalibrierung wiederholen
- Tipp: Verwenden Sie "Tare Scale" für Nullstellung

**Problem**: Wägezelle reagiert nicht
- Lösung: Überprüfen Sie die Verkabelung (E+, E-, A+, A-)
- Tipp: Testen Sie mit einem Multimeter

#### NFC-Tags

**Problem**: Tag wird nicht erkannt
- Lösung: Überprüfen Sie die PN532 DIP-Schalter (I2C-Modus)
- Tipp: Spule etwas anders auf der Waage positionieren (nicht ganz an den Rand)

**Problem**: Tag kann nicht beschrieben werden
- Lösung: Verwenden Sie NTAG215 für bessere Kompatibilität
- Tipp: Stellen Sie sicher, dass der Tag nicht schreibgeschützt ist

#### Spoolman

**Problem**: Verbindung zu Spoolman schlägt fehl
- Lösung: Aktivieren Sie SPOOLMAN_DEBUG_MODE=TRUE
- Tipp: Überprüfen Sie die URL-Formatierung

**Problem**: Spulen werden nicht angezeigt
- Lösung: Stellen Sie sicher, dass Spoolman läuft
- Tipp: Prüfen Sie die Netzwerk-Firewall-Einstellungen

#### Bambu Lab

**Problem**: Drucker verbindet nicht
- Lösung: Überprüfen Sie Access Code und IP-Adresse
- Tipp: Stellen Sie sicher, dass der Drucker im LAN-Modus ist

**Problem**: AMS-Status wird nicht angezeigt
- Lösung: Prüfen Sie die MQTT-Verbindung
- Hinweis: Bambu kann die API jederzeit schließen

### Debug-Informationen

Falls Sie Probleme haben, können Sie diese Schritte zur Diagnose verwenden:

#### Serieller Monitor (für Entwickler)
- Verbinden Sie den ESP32 über USB mit Ihrem Computer
- Öffnen Sie einen seriellen Monitor (z.B. Arduino IDE) mit 115200 Baud
- Sie sehen detaillierte Log-Nachrichten des Systems

#### Browser-Konsole
- Öffnen Sie die Weboberfläche von FilaMan
- Drücken Sie F12 um die Entwicklertools zu öffnen  
- Schauen Sie in der Konsole nach Fehlermeldungen

#### Neustart bei anhaltenden Problemen
1. ESP32 vom Strom trennen
2. 10 Sekunden warten
3. Wieder anschließen
4. 30 Sekunden für vollständigen Start warten

---

## Support

### Community

- **Discord Server**: [https://discord.gg/my7Gvaxj2v](https://discord.gg/my7Gvaxj2v)
- **GitHub Issues**: [Filaman Repository](https://github.com/ManuelW77/Filaman/issues)
- **YouTube Kanal**: [Deutsches Erklärvideo](https://youtu.be/uNDe2wh9SS8?si=b-jYx4I1w62zaOHU)

### Dokumentation

- **Offizielle Website**: [www.filaman.app](https://www.filaman.app)
- **GitHub Wiki**: [Detaillierte Dokumentation](https://github.com/ManuelW77/Filaman/wiki)
- **Hardware-Referenz**: ESP32 Pinout-Diagramme in `/img/`

### Entwicklung unterstützen

Wenn Sie das Projekt unterstützen möchten:

[![Buy Me A Coffee](https://cdn.buymeacoffee.com/buttons/v2/default-yellow.png)](https://www.buymeacoffee.com/manuelw)

### Lizenz

Dieses Projekt ist unter der MIT-Lizenz veröffentlicht. Siehe [LICENSE](LICENSE.txt) für Details.

---

**Letzte Aktualisierung**: August 2025
**Version**: 2.0
**Maintainer**: Manuel W.