# Hersteller Tags - Deutsche Dokumentation

## Überblick

Das FilaMan NFC-System unterstützt **Hersteller Tags**, die es Filament-Produzenten ermöglichen, standardisierte NFC-Tags für ihre Produkte zu erstellen. Beim Scannen dieser Tags werden automatisch die notwendigen Einträge in Spoolman (Marke, Filament-Typ und Spule) erstellt, ohne dass eine manuelle Einrichtung erforderlich ist.

## Funktionsweise der Hersteller Tags

### Ablauf

1. **Tag-Erkennung**: Wenn ein Tag ohne `sm_id` gescannt wird, prüft das System auf Hersteller Tag Format
2. **Marken-Erstellung/Suche**: Das System sucht die Marke in Spoolman oder erstellt sie, falls sie nicht existiert
3. **Filament-Typ-Erstellung/Suche**: Der Filament-Typ wird basierend auf Marke, Material und Spezifikationen erstellt oder gefunden
4. **Spulen-Erstellung**: Ein neuer Spulen-Eintrag wird automatisch mit der Tag-UID als Referenz erstellt
5. **Tag-Update**: Der Tag wird mit der neuen Spoolman Spulen-ID (`sm_id`) aktualisiert

### Warum Hersteller Tags verwenden?

- **Automatische Integration**: Keine manuelle Dateneingabe erforderlich
- **Standardisiertes Format**: Konsistente Produktinformationen verschiedener Hersteller
- **Lagerverwaltung**: Automatische Erstellung vollständiger Spoolman-Einträge
- **Rückverfolgbarkeit**: Direkte Verbindung zwischen physischem Produkt und digitalem Inventar

## Tag-Format Spezifikation

### JSON-Struktur

Hersteller Tags müssen eine JSON-Payload mit spezifischen Feldern enthalten, die **kurze Schlüssel** verwenden, um die Tag-Größe zu minimieren:

```json
{
    "b": "Marke/Hersteller Name",
    "an": "Artikelnummer",
    "t": "Filament Typ (PLA, PETG, etc)",
    "c": "Filament Farbe ohne # (FF5733)",
    "mc": "Optional Mehrfarben-Filament Farben ohne # (FF0000,00FF00,0000FF)",
    "mcd": "Optional Mehrfarben-Richtung als Wort (coaxial, longitudinal)",
    "cn": "Farbname (rot, Blaubeere, Arktisches Blau)",
    "et": "Extruder Temp als Zahl in C° (230)",
    "bt": "Bett Temp als Zahl in C° (60)",
    "di": "Durchmesser als Float (1.75)",
    "de": "Dichte als Float (1.24)",
    "sw": "Leeres Spulengewicht als Zahl in g (180)",
    "u": "URL zum Filament mit der Artikelnummer"
}
```

### Pflichtfelder

- **`b`** (brand): Hersteller/Markenname
- **`an`** (article number): Eindeutige Produktkennung
- **`t`** (type): Materialtyp (PLA, PETG, ABS, etc.)
- **`c`** (color): Hex-Farbcode ohne #
- **`cn`** (color name): Lesbare Farbbezeichnung
- **`et`** (extruder temp): Empfohlene Extruder-Temperatur in Celsius
- **`bt`** (bed temp): Empfohlene Bett-Temperatur in Celsius
- **`di`** (diameter): Filamentdurchmesser in mm
- **`de`** (density): Materialdichte in g/cm³
- **`sw`** (spool weight): Leeres Spulengewicht in Gramm

### Optionale Felder

- **`mc`** (multicolor): Komma-getrennte Hex-Farben für Mehrfarben-Filamente
- **`mcd`** (multicolor direction): Richtung für Mehrfarben (coaxial, longitudinal)
- **`u`** (url): Produkt-URL für zusätzliche Informationen

### Beispiel Tag

```json
{"b":"Recycling Fabrik","an":"FX1_PETG-S175-1000-DAEM00055","t":"PETG","c":"FF5733","cn":"Lebendiges Orange","et":"230","bt":"70","di":"1.75","de":"1.24","sw":"180","u":"https://www.recyclingfabrik.com/search?q="}
```

## Implementierungsrichtlinien

### Für Hersteller

1. **Tag-Kodierung**: NDEF-Format mit MIME-Typ `application/json` verwenden
2. **Datenminimierung**: Kompaktes JSON-Format für Tag-Größenbegrenzungen nutzen
3. **Qualitätskontrolle**: Sicherstellen, dass alle Pflichtfelder vorhanden und korrekt formatiert sind
4. **Testen**: Tags vor der Produktion mit dem FilaMan-System verifizieren

### Tag-Größe Überlegungen

- **NTAG213**: 144 Bytes Nutzerdaten (geeignet für einfache Tags)
- **NTAG215**: 504 Bytes Nutzerdaten (empfohlen für umfassende Daten)
- **NTAG216**: 888 Bytes Nutzerdaten (maximale Kompatibilität)

### Best Practices

- Markennamen über alle Produkte hinweg konsistent halten
- Standardisierte Materialtypnamen verwenden (PLA, PETG, ABS, etc.)
- Genaue Temperaturempfehlungen angeben
- Aussagekräftige Farbnamen für bessere Benutzererfahrung verwenden
- Tags vor Massenproduktion mit dem FilaMan-System testen

## System-Integration

### Spoolman Datenbankstruktur

Bei der Verarbeitung eines Hersteller Tags erstellt das System:

1. **Lieferanten-Eintrag**: Markeninformationen in der Spoolman Lieferanten-Datenbank
2. **Filament-Eintrag**: Materialspezifikationen und Eigenschaften
3. **Spulen-Eintrag**: Einzelne Spule mit Gewicht und NFC-Tag-Referenz

### Fast-Path Erkennung

Sobald ein Tag verarbeitet und mit `sm_id` aktualisiert wurde, nutzt er das Fast-Path-Erkennungssystem für schnelle nachfolgende Scans.

## Fehlerbehebung

### Häufige Probleme

- **Tag zu klein**: NTAG215 oder NTAG216 für größere JSON-Payloads verwenden
- **Fehlende Felder**: Sicherstellen, dass alle Pflichtfelder vorhanden sind
- **Ungültiges Format**: JSON-Syntax und Feldtypen überprüfen
- **Spoolman-Verbindung**: Sicherstellen, dass FilaMan mit der Spoolman API verbinden kann

### Validierung

Das System validiert:

- JSON-Format Korrektheit
- Vorhandensein der Pflichtfelder
- Datentyp-Konformität
- Tag-Größe Kompatibilität

## Technische Details

### Verarbeitungsalgorithmus

1. Tag-Scan erkennt kein `sm_id` Feld
2. System prüft auf `b` (Marke) und `an` (Artikelnummer) Felder
3. `checkVendor()` erstellt oder findet Marke in Spoolman
4. `checkFilament()` erstellt oder findet Filament-Typ
5. `createSpool()` erstellt neuen Spulen-Eintrag
6. Tag wird mit neuer `sm_id` aktualisiert

### Fehlerbehandlung

- Graceful Fallback bei Netzwerkproblemen
- Detaillierte Protokollierung für Debugging
- Benutzer-Feedback bei fehlgeschlagenen Operationen
- Wiederholungsmechanismen für temporäre Fehler

### Systemverhalten

#### Bei fehlendem sm_id:
- System prüft auf `b` (brand) und `an` (artnr) Felder
- Falls vorhanden → Hersteller Tag erkannt
- Automatische Erstellung von Lieferant, Filament und Spule in Spoolman
- Tag wird mit neuer `sm_id` beschrieben

#### Bei vorhandenem sm_id:
- Fast-Path Erkennung für bekannte Spulen
- Sofortige Gewichtsmessung ohne vollständige Tag-Analyse
- Optimierte Performance für häufig verwendete Tags

Dieses System ermöglicht eine nahtlose Integration von Hersteller-Filamentprodukten in das FilaMan-Ökosystem unter Beibehaltung von Datenkonsistenz und Benutzererfahrung.