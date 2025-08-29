#include "nfc.h"
#include <Arduino.h>
#include <Adafruit_PN532.h>
#include <ArduinoJson.h>
#include "config.h"
#include "website.h"
#include "api.h"
#include "esp_task_wdt.h"
#include "scale.h"
#include "bambu.h"
#include "main.h"

//Adafruit_PN532 nfc(PN532_SCK, PN532_MISO, PN532_MOSI, PN532_SS);
Adafruit_PN532 nfc(PN532_IRQ, PN532_RESET);

TaskHandle_t RfidReaderTask;

JsonDocument rfidData;
String activeSpoolId = "";
String lastSpoolId = "";
String nfcJsonData = "";
bool tagProcessed = false;
volatile bool pauseBambuMqttTask = false;
volatile bool nfcReadingTaskSuspendRequest = false;
volatile bool nfcReadingTaskSuspendState = false;

struct NfcWriteParameterType {
  bool tagType;
  char* payload;
};

volatile nfcReaderStateType nfcReaderState = NFC_IDLE;
// 0 = nicht gelesen
// 1 = erfolgreich gelesen
// 2 = fehler beim Lesen
// 3 = schreiben
// 4 = fehler beim Schreiben
// 5 = erfolgreich geschrieben
// 6 = reading
// ***** PN532

// ##### Funktionen für RFID #####
void payloadToJson(uint8_t *data) {
    const char* startJson = strchr((char*)data, '{');
    const char* endJson = strrchr((char*)data, '}');
  
    if (startJson && endJson && endJson > startJson) {
      String jsonString = String(startJson, endJson - startJson + 1);
      //Serial.print("Bereinigter JSON-String: ");
      //Serial.println(jsonString);
  
      // JSON-Dokument verarbeiten
      JsonDocument doc;  // Passen Sie die Größe an den JSON-Inhalt an
      DeserializationError error = deserializeJson(doc, jsonString);
  
      if (!error) {
        const char* color_hex = doc["color_hex"];
        const char* type = doc["type"];
        int min_temp = doc["min_temp"];
        int max_temp = doc["max_temp"];
        const char* brand = doc["brand"];

        Serial.println();
        Serial.println("-----------------");
        Serial.println("JSON-Parsed Data:");
        Serial.println(color_hex);
        Serial.println(type);
        Serial.println(min_temp);
        Serial.println(max_temp);
        Serial.println(brand);
        Serial.println("-----------------");
        Serial.println();
      } else {
        Serial.print("deserializeJson() failed: ");
        Serial.println(error.f_str());
      }

      doc.clear();
    } else {
        Serial.println("Kein gültiger JSON-Inhalt gefunden oder fehlerhafte Formatierung.");
        //writeJsonToTag("{\"version\":\"1.0\",\"protocol\":\"NFC\",\"color_hex\":\"#FFFFFF\",\"type\":\"Example\",\"min_temp\":10,\"max_temp\":30,\"brand\":\"BrandName\"}");
    }
  }

bool formatNdefTag() {
    uint8_t ndefInit[] = { 0x03, 0x00, 0xFE }; // NDEF Initialisierungsnachricht
    bool success = true;
    int pageOffset = 4; // Startseite für NDEF-Daten auf NTAG2xx
  
    Serial.println();
    Serial.println("Formatiere NDEF-Tag...");
  
    // Schreibe die Initialisierungsnachricht auf die ersten Seiten
    for (int i = 0; i < sizeof(ndefInit); i += 4) {
      if (!nfc.ntag2xx_WritePage(pageOffset + (i / 4), &ndefInit[i])) {
          success = false;
          break;
      }
    }
  
    return success;
}uint16_t readTagSize()
{
  uint8_t buffer[4];
  memset(buffer, 0, 4);
  nfc.ntag2xx_ReadPage(3, buffer);
  return buffer[2]*8;
}

String detectNtagType()
{
  // Read capability container from page 3 to determine exact NTAG type
  uint8_t ccBuffer[4];
  memset(ccBuffer, 0, 4);
  
  if (!nfc.ntag2xx_ReadPage(3, ccBuffer)) {
    Serial.println("Failed to read capability container");
    return "UNKNOWN";
  }

  // Also read configuration pages to get more info
  uint8_t configBuffer[4];
  memset(configBuffer, 0, 4);
  
  Serial.print("Capability Container: ");
  for (int i = 0; i < 4; i++) {
    if (ccBuffer[i] < 0x10) Serial.print("0");
    Serial.print(ccBuffer[i], HEX);
    Serial.print(" ");
  }
  Serial.println();

  // NTAG type detection based on capability container
  // CC[2] contains the data area size in bytes / 8
  uint16_t dataAreaSize = ccBuffer[2] * 8;
  
  Serial.print("Data area size from CC: ");
  Serial.println(dataAreaSize);

  // Try to read different configuration pages to determine exact type
  String tagType = "UNKNOWN";
  
  // Try to read page 41 (NTAG213 ends at page 39, so this should fail)
  uint8_t testBuffer[4];
  bool canReadPage41 = nfc.ntag2xx_ReadPage(41, testBuffer);
  
  // Try to read page 130 (NTAG215 ends at page 129, so this should fail for NTAG213/215)
  bool canReadPage130 = nfc.ntag2xx_ReadPage(130, testBuffer);

  if (dataAreaSize <= 180 && !canReadPage41) {
    tagType = "NTAG213";
    Serial.println("Detected: NTAG213 (cannot read beyond page 39)");
  } else if (dataAreaSize <= 540 && canReadPage41 && !canReadPage130) {
    tagType = "NTAG215";
    Serial.println("Detected: NTAG215 (can read page 41, cannot read page 130)");
  } else if (dataAreaSize <= 928 && canReadPage130) {
    tagType = "NTAG216";
    Serial.println("Detected: NTAG216 (can read page 130)");
  } else {
    // Fallback: use data area size from capability container
    if (dataAreaSize <= 180) {
      tagType = "NTAG213";
      Serial.println("Fallback detection: NTAG213 based on data area size");
    } else if (dataAreaSize <= 540) {
      tagType = "NTAG215";
      Serial.println("Fallback detection: NTAG215 based on data area size");
    } else {
      tagType = "NTAG216";
      Serial.println("Fallback detection: NTAG216 based on data area size");
    }
  }
  
  return tagType;
}

uint16_t getAvailableUserDataSize()
{
  String tagType = detectNtagType();
  uint16_t userDataSize = 0;
  
  if (tagType == "NTAG213") {
    // NTAG213: User data from page 4-39 (36 pages * 4 bytes = 144 bytes)
    userDataSize = 144;
    Serial.println("NTAG213 confirmed - 144 bytes user data available");
  } else if (tagType == "NTAG215") {
    // NTAG215: User data from page 4-129 (126 pages * 4 bytes = 504 bytes)
    userDataSize = 504;
    Serial.println("NTAG215 confirmed - 504 bytes user data available");
  } else if (tagType == "NTAG216") {
    // NTAG216: User data from page 4-225 (222 pages * 4 bytes = 888 bytes)
    userDataSize = 888;
    Serial.println("NTAG216 confirmed - 888 bytes user data available");
  } else {
    // Unknown tag type, use conservative estimate
    uint16_t tagSize = readTagSize();
    userDataSize = tagSize - 60; // Reserve 60 bytes for headers/config
    Serial.print("Unknown NTAG type, using conservative estimate: ");
    Serial.println(userDataSize);
  }
  
  return userDataSize;
}

uint16_t getMaxUserDataPages()
{
  String tagType = detectNtagType();
  uint16_t maxPages = 0;
  
  if (tagType == "NTAG213") {
    maxPages = 39; // Pages 4-39 are user data
  } else if (tagType == "NTAG215") {
    maxPages = 129; // Pages 4-129 are user data
  } else if (tagType == "NTAG216") {
    maxPages = 225; // Pages 4-225 are user data
  } else {
    // Conservative fallback
    maxPages = 39;
    Serial.println("Unknown tag type, using NTAG213 page limit as fallback");
  }
  
  Serial.print("Maximum writable page: ");
  Serial.println(maxPages);
  return maxPages;
}

bool initializeNdefStructure() {
    // Write minimal NDEF structure without destroying the tag
    // This creates a clean slate while preserving tag functionality
    
    Serial.println("Initialisiere sichere NDEF-Struktur...");
    
    // Minimal NDEF structure: TLV with empty message
    uint8_t minimalNdef[8] = {
        0x03,           // NDEF Message TLV Tag
        0x03,           // Length (3 bytes for minimal empty record)
        0xD0,           // NDEF Record Header (TNF=0x0:Empty + SR + ME + MB)
        0x00,           // Type Length (0 = empty record)
        0x00,           // Payload Length (0 = empty record)
        0xFE,           // Terminator TLV
        0x00, 0x00      // Padding
    };
    
    // Write the minimal structure starting at page 4
    uint8_t pageBuffer[4];
    
    for (int i = 0; i < 8; i += 4) {
        memcpy(pageBuffer, &minimalNdef[i], 4);
        
        if (!nfc.ntag2xx_WritePage(4 + (i / 4), pageBuffer)) {
            Serial.print("Fehler beim Initialisieren von Seite ");
            Serial.println(4 + (i / 4));
            return false;
        }
        
        Serial.print("Seite ");
        Serial.print(4 + (i / 4));
        Serial.print(" initialisiert: ");
        for (int j = 0; j < 4; j++) {
            if (pageBuffer[j] < 0x10) Serial.print("0");
            Serial.print(pageBuffer[j], HEX);
            Serial.print(" ");
        }
        Serial.println();
    }
    
    Serial.println("✓ Sichere NDEF-Struktur initialisiert");
    Serial.println("✓ Tag bleibt funktionsfähig und überschreibbar");
    return true;
}

bool clearUserDataArea() {
    // IMPORTANT: Only clear user data pages, NOT configuration pages
    // NTAG layout: Pages 0-3 (header), 4-N (user data), N+1-N+3 (config) - NEVER touch config!
    String tagType = detectNtagType();
    
    // Calculate safe user data page ranges (NEVER touch config pages!)
    uint16_t firstUserPage = 4;
    uint16_t lastUserPage = 0;
    
    if (tagType == "NTAG213") {
        lastUserPage = 39;  // Pages 40-42 are config - DO NOT TOUCH!
        Serial.println("NTAG213: Sichere Löschung Seiten 4-39");
    } else if (tagType == "NTAG215") {
        lastUserPage = 129; // Pages 130-132 are config - DO NOT TOUCH!
        Serial.println("NTAG215: Sichere Löschung Seiten 4-129");
    } else if (tagType == "NTAG216") {
        lastUserPage = 225; // Pages 226-228 are config - DO NOT TOUCH!
        Serial.println("NTAG216: Sichere Löschung Seiten 4-225");
    } else {
        // Conservative fallback - only clear a small safe area
        lastUserPage = 39;
        Serial.println("UNKNOWN TAG: Konservative Löschung Seiten 4-39");
    }
    
    Serial.println("WARNUNG: Vollständiges Löschen kann Tag beschädigen!");
    Serial.println("Verwende stattdessen selective NDEF-Überschreibung...");
    
    // Instead of clearing everything, just write a minimal NDEF structure
    // This is much safer and preserves tag integrity
    return initializeNdefStructure();
}

uint8_t ntag2xx_WriteNDEF(const char *payload) {
  // Determine exact tag type and capabilities first
  String tagType = detectNtagType();
  uint16_t tagSize = readTagSize();
  uint16_t availableUserData = getAvailableUserDataSize();
  uint16_t maxWritablePage = getMaxUserDataPages();
  
  Serial.println("=== NFC TAG ANALYSIS ===");
  Serial.print("Tag Type: ");Serial.println(tagType);
  Serial.print("Total Tag Size: ");Serial.println(tagSize);
  Serial.print("Available User Data: ");Serial.println(availableUserData);
  Serial.print("Max Writable Page: ");Serial.println(maxWritablePage);
  Serial.println("========================");

  uint8_t pageBuffer[4] = {0, 0, 0, 0};
  Serial.println("Beginne mit dem Schreiben der NDEF-Nachricht...");
  
  // Figure out how long the string is
  uint16_t payloadLen = strlen(payload);
  Serial.print("Länge der Payload: ");
  Serial.println(payloadLen);
  
  Serial.print("Payload: ");Serial.println(payload);

  // MIME type for JSON
  const char mimeType[] = "application/json";
  uint8_t mimeTypeLen = strlen(mimeType);
  
  // Calculate NDEF record size
  uint8_t ndefRecordHeaderSize = 3; // Header byte + Type Length + Payload Length (short record)
  uint16_t ndefRecordSize = ndefRecordHeaderSize + mimeTypeLen + payloadLen;
  
  // Calculate TLV size - need to check if we need extended length format
  uint8_t tlvHeaderSize;
  uint16_t totalTlvSize;
  
  if (ndefRecordSize <= 254) {
    // Standard TLV format: Tag (1) + Length (1) + Value (ndefRecordSize)
    tlvHeaderSize = 2;
    totalTlvSize = tlvHeaderSize + ndefRecordSize + 1; // +1 for terminator TLV
  } else {
    // Extended TLV format: Tag (1) + 0xFF + Length (2) + Value (ndefRecordSize)  
    tlvHeaderSize = 4;
    totalTlvSize = tlvHeaderSize + ndefRecordSize + 1; // +1 for terminator TLV
  }

  Serial.print("NDEF Record Size: ");
  Serial.println(ndefRecordSize);
  Serial.print("Total TLV Size: ");
  Serial.println(totalTlvSize);

  // Check if the message fits in the available user data space
  if (totalTlvSize > availableUserData) {
    Serial.println();
    Serial.println("!!!!!!!!!!!!!!!!!!!!!!!!");
    Serial.println("FEHLER: Payload zu groß für diesen Tag-Typ!");
    Serial.print("Tag-Typ: ");Serial.println(tagType);
    Serial.print("Benötigt: ");Serial.print(totalTlvSize);Serial.println(" Bytes");
    Serial.print("Verfügbar: ");Serial.print(availableUserData);Serial.println(" Bytes");
    Serial.print("Überschuss: ");Serial.print(totalTlvSize - availableUserData);Serial.println(" Bytes");
    
    if (tagType == "NTAG213") {
      Serial.println("EMPFEHLUNG: Verwenden Sie einen NTAG215 (504 Bytes) oder NTAG216 (888 Bytes) Tag!");
      Serial.println("Oder kürzen Sie die Payload um mindestens " + String(totalTlvSize - availableUserData) + " Bytes.");
    }
    Serial.println("!!!!!!!!!!!!!!!!!!!!!!!!");
    Serial.println();
    
    oledShowMessage("Tag zu klein für Payload");
    vTaskDelay(3000 / portTICK_PERIOD_MS);
    return 0;
  }

  Serial.println("✓ Payload passt in den Tag - Schreibvorgang wird fortgesetzt");

  // IMPORTANT: Use safe NDEF initialization instead of aggressive clearing
  Serial.println("Schritt 1: Sichere NDEF-Initialisierung...");
  if (!initializeNdefStructure()) {
    Serial.println("FEHLER: Konnte NDEF-Struktur nicht initialisieren!");
    oledShowMessage("NDEF init failed");
    vTaskDelay(2000 / portTICK_PERIOD_MS);
    return 0;
  }
  Serial.println("✓ NDEF-Struktur sicher initialisiert");

  // Allocate memory for the complete TLV structure
  uint8_t* tlvData = (uint8_t*) malloc(totalTlvSize);
  if (tlvData == NULL) {
    Serial.println("Fehler: Nicht genug Speicher für TLV-Daten vorhanden.");
    oledShowMessage("Memory error");
    vTaskDelay(2000 / portTICK_PERIOD_MS);
    return 0;
  }

  // Build TLV structure
  uint16_t offset = 0;
  
  // TLV Header
  tlvData[offset++] = 0x03; // NDEF Message TLV Tag
  
  if (ndefRecordSize <= 254) {
    // Standard length format
    tlvData[offset++] = (uint8_t)ndefRecordSize;
  } else {
    // Extended length format
    tlvData[offset++] = 0xFF;
    tlvData[offset++] = (uint8_t)(ndefRecordSize >> 8);  // High byte
    tlvData[offset++] = (uint8_t)(ndefRecordSize & 0xFF); // Low byte
  }

  // NDEF Record Header
  tlvData[offset++] = 0xD2; // NDEF Record Header (TNF=0x2:MIME Media + SR + ME + MB)
  tlvData[offset++] = mimeTypeLen; // Type Length
  tlvData[offset++] = (uint8_t)payloadLen; // Payload Length (short record format)

  // MIME Type
  memcpy(&tlvData[offset], mimeType, mimeTypeLen);
  offset += mimeTypeLen;

  // JSON Payload
  memcpy(&tlvData[offset], payload, payloadLen);
  offset += payloadLen;

  // Terminator TLV
  tlvData[offset] = 0xFE;

  Serial.print("Gesamt-TLV-Länge: ");
  Serial.println(offset + 1);

  // Debug: Print first 64 bytes of TLV data
  Serial.println("TLV Daten (erste 64 Bytes):");
  for (int i = 0; i < min((int)(offset + 1), 64); i++) {
    if (tlvData[i] < 0x10) Serial.print("0");
    Serial.print(tlvData[i], HEX);
    Serial.print(" ");
    if ((i + 1) % 16 == 0) Serial.println();
  }
  Serial.println();

  // Write data to tag pages (starting from page 4)
  uint16_t bytesWritten = 0;
  uint8_t pageNumber = 4;
  uint16_t totalBytes = offset + 1;

  Serial.println("Schritt 2: Schreibe neue NDEF-Daten...");
  Serial.print("Schreibe ");
  Serial.print(totalBytes);
  Serial.print(" Bytes in ");
  Serial.print((totalBytes + 3) / 4); // Round up division
  Serial.println(" Seiten...");

  while (bytesWritten < totalBytes && pageNumber <= maxWritablePage) {
    // Clear page buffer
    memset(pageBuffer, 0, 4);
    
    // Calculate how many bytes to write to this page
    uint16_t bytesToWrite = min(4, (int)(totalBytes - bytesWritten));
    
    // Copy data to page buffer
    memcpy(pageBuffer, &tlvData[bytesWritten], bytesToWrite);

    // Write page to tag
    if (!nfc.ntag2xx_WritePage(pageNumber, pageBuffer)) {
      Serial.print("FEHLER beim Schreiben der Seite ");
      Serial.println(pageNumber);
      Serial.print("Möglicherweise Page-Limit erreicht für ");
      Serial.println(tagType);
      free(tlvData);
      return 0;
    }

    Serial.print("Seite ");
    Serial.print(pageNumber);
    Serial.print(" ✓: ");
    for (int i = 0; i < 4; i++) {
      if (pageBuffer[i] < 0x10) Serial.print("0");
      Serial.print(pageBuffer[i], HEX);
      Serial.print(" ");
    }
    Serial.println();

    bytesWritten += bytesToWrite;
    pageNumber++;
    
    yield();
    vTaskDelay(5 / portTICK_PERIOD_MS); // Small delay between page writes
  }

  free(tlvData);
  
  if (bytesWritten < totalBytes) {
    Serial.println("WARNUNG: Nicht alle Daten konnten geschrieben werden!");
    Serial.print("Geschrieben: ");
    Serial.print(bytesWritten);
    Serial.print(" von ");
    Serial.print(totalBytes);
    Serial.println(" Bytes");
    Serial.print("Gestoppt bei Seite: ");
    Serial.println(pageNumber - 1);
    return 0;
  }
  
  Serial.println();
  Serial.println("✓ NDEF-Nachricht erfolgreich geschrieben!");
  Serial.print("✓ Tag-Typ: ");Serial.println(tagType);
  Serial.print("✓ Insgesamt ");Serial.print(bytesWritten);Serial.println(" Bytes geschrieben");
  Serial.print("✓ Verwendete Seiten: 4-");Serial.println(pageNumber - 1);
  Serial.print("✓ Speicher-Auslastung: ");
  Serial.print((bytesWritten * 100) / availableUserData);
  Serial.println("%");
  Serial.println("✓ Bestehende Daten wurden überschrieben");
  Serial.println();
  
  return 1;
}

bool decodeNdefAndReturnJson(const byte* encodedMessage, String uidString) {
  oledShowProgressBar(1, octoEnabled?5:4, "Reading", "Decoding data");

  // Debug: Print first 32 bytes of the raw data
  Serial.println("Raw NDEF data (first 32 bytes):");
  for (int i = 0; i < 32; i++) {
    if (encodedMessage[i] < 0x10) Serial.print("0");
    Serial.print(encodedMessage[i], HEX);
    Serial.print(" ");
    if ((i + 1) % 16 == 0) Serial.println();
  }
  Serial.println();

  // Look for the NDEF TLV structure starting from the beginning
  int tlvOffset = 0;
  bool foundNdefTlv = false;
  
  // Search for NDEF TLV (0x03) in the first few bytes
  for (int i = 0; i < 16; i++) {
    if (encodedMessage[i] == 0x03) {
      tlvOffset = i;
      foundNdefTlv = true;
      Serial.print("Found NDEF TLV at offset: ");
      Serial.println(tlvOffset);
      break;
    }
  }

  if (!foundNdefTlv) {
    Serial.println("No NDEF TLV found in tag data");
    return false;
  }

  // Get the NDEF message length from TLV
  uint16_t ndefMessageLength = 0;
  int ndefRecordOffset = 0;
  
  if (encodedMessage[tlvOffset + 1] == 0xFF) {
    // Extended length format: next 2 bytes contain the actual length
    ndefMessageLength = (encodedMessage[tlvOffset + 2] << 8) | encodedMessage[tlvOffset + 3];
    ndefRecordOffset = tlvOffset + 4; // Skip TLV tag + 0xFF + 2 length bytes
    Serial.print("NDEF Message Length (extended): ");
  } else {
    // Standard length format: single byte contains the length
    ndefMessageLength = encodedMessage[tlvOffset + 1];
    ndefRecordOffset = tlvOffset + 2; // Skip TLV tag + 1 length byte
    Serial.print("NDEF Message Length (standard): ");
  }
  Serial.println(ndefMessageLength);

  // Get pointer to NDEF record
  const byte* ndefRecord = &encodedMessage[ndefRecordOffset];
  
  // Parse NDEF record header
  byte recordHeader = ndefRecord[0];
  byte typeLength = ndefRecord[1];
  
  Serial.print("NDEF Record Header: 0x");
  Serial.println(recordHeader, HEX);
  Serial.print("Type Length: ");
  Serial.println(typeLength);

  // Determine payload length (can be 1 or 4 bytes depending on SR flag)
  uint32_t payloadLength = 0;
  byte payloadLengthBytes = 1;
  byte payloadLengthOffset = 2;
  
  // Check if Short Record (SR) flag is set (bit 4)
  if (recordHeader & 0x10) { // SR flag
    payloadLength = ndefRecord[2];
    payloadLengthBytes = 1;
    payloadLengthOffset = 2;
  } else {
    // Long record format (4 bytes for payload length)
    payloadLength = (ndefRecord[2] << 24) | (ndefRecord[3] << 16) | 
                   (ndefRecord[4] << 8) | ndefRecord[5];
    payloadLengthBytes = 4;
    payloadLengthOffset = 2;
  }

  Serial.print("Payload Length: ");
  Serial.println(payloadLength);
  Serial.print("Payload Length Bytes: ");
  Serial.println(payloadLengthBytes);

  // Check for ID field (if IL flag is set)
  byte idLength = 0;
  if (recordHeader & 0x08) { // IL flag
    idLength = ndefRecord[payloadLengthOffset + payloadLengthBytes];
    Serial.print("ID Length: ");
    Serial.println(idLength);
  }

  // Calculate offset to payload
  byte payloadOffset = 1 + 1 + payloadLengthBytes + typeLength + idLength;
  
  Serial.print("Calculated payload offset: ");
  Serial.println(payloadOffset);

  // Verify we have enough data
  if (payloadOffset + payloadLength > ndefMessageLength) {
    Serial.println("Invalid NDEF structure - payload extends beyond message");
    Serial.print("Payload offset + length: ");
    Serial.print(payloadOffset + payloadLength);
    Serial.print(", NDEF message length: ");
    Serial.println(ndefMessageLength);
    return false;
  }

  // Print the record type for debugging
  Serial.print("Record Type: ");
  for (int i = 0; i < typeLength; i++) {
    Serial.print((char)ndefRecord[1 + 1 + payloadLengthBytes + i]);
  }
  Serial.println();

  nfcJsonData = "";

  // Extract JSON payload with validation
  uint32_t actualJsonLength = 0;
  for (uint32_t i = 0; i < payloadLength; i++) {
    byte currentByte = ndefRecord[payloadOffset + i];
    
    // Stop at null terminator or if we find the end of JSON
    if (currentByte == 0x00) {
      Serial.print("Found null terminator at position: ");
      Serial.println(i);
      break;
    }
    
    // Only add printable characters and common JSON characters
    if (currentByte >= 32 && currentByte <= 126) {
      nfcJsonData += (char)currentByte;
      actualJsonLength++;
    } else {
      Serial.print("Skipping non-printable byte at position ");
      Serial.print(i);
      Serial.print(": 0x");
      Serial.println(currentByte, HEX);
    }
    
    // Check if we've reached the end of a JSON object
    if (currentByte == '}') {
      // Count opening and closing braces to detect complete JSON
      int braceCount = 0;
      for (uint32_t j = 0; j <= i; j++) {
        if (ndefRecord[payloadOffset + j] == '{') braceCount++;
        else if (ndefRecord[payloadOffset + j] == '}') braceCount--;
      }
      
      if (braceCount == 0) {
        Serial.print("Found complete JSON object at position: ");
        Serial.println(i);
        actualJsonLength = i + 1;
        break;
      }
    }
  }

  Serial.print("Actual JSON length extracted: ");
  Serial.println(actualJsonLength);
  Serial.print("Total nfcJsonData length: ");
  Serial.println(nfcJsonData.length());
  Serial.println("=== DECODED JSON DATA START ===");
  Serial.println(nfcJsonData);
  Serial.println("=== DECODED JSON DATA END ===");
  
  // Check if JSON was truncated
  if (nfcJsonData.length() < payloadLength && !nfcJsonData.endsWith("}")) {
    Serial.println("WARNING: JSON payload appears to be truncated!");
    Serial.print("Expected payload length: ");
    Serial.println(payloadLength);
    Serial.print("Actual extracted length: ");
    Serial.println(nfcJsonData.length());
  }
  
  // Trim any trailing whitespace or invalid characters
  nfcJsonData.trim();

  // JSON-Dokument verarbeiten
  JsonDocument doc;
  DeserializationError error = deserializeJson(doc, nfcJsonData);
  if (error) 
  {
    nfcJsonData = "";
    Serial.println("Fehler beim Verarbeiten des JSON-Dokuments");
    Serial.print("deserializeJson() failed: ");
    Serial.println(error.f_str());
    return false;
  } 
  else 
  {
    // If spoolman is unavailable, there is no point in continuing
    if(spoolmanConnected){
      // Sende die aktualisierten AMS-Daten an alle WebSocket-Clients
      Serial.println("JSON-Dokument erfolgreich verarbeitet");
      Serial.println(doc.as<String>());
      if (doc["sm_id"].is<String>() && doc["sm_id"] != "" && doc["sm_id"] != "0")
      {
        oledShowProgressBar(2, octoEnabled?5:4, "Spool Tag", "Weighing");
        Serial.println("SPOOL-ID gefunden: " + doc["sm_id"].as<String>());
        activeSpoolId = doc["sm_id"].as<String>();
        lastSpoolId = activeSpoolId;
      }
      else if(doc["location"].is<String>() && doc["location"] != "")
      {
        Serial.println("Location Tag found!");
        String location = doc["location"].as<String>();
        if(lastSpoolId != ""){
          updateSpoolLocation(lastSpoolId, location);
        }
        else
        {
          Serial.println("Location update tag scanned without scanning spool before!");
          oledShowProgressBar(1, 1, "Failure", "Scan spool first");
        }
      }
      // Brand Filament not registered to Spoolman
      else if ((!doc["sm_id"].is<String>() || (doc["sm_id"].is<String>() && (doc["sm_id"] == "0" || doc["sm_id"] == "")))
              && doc["brand"].is<String>() && doc["artnr"].is<String>())
      {
        doc["sm_id"] = "0"; // Ensure sm_id is set to 0
        // If no sm_id is present but the brand is Brand Filament then
        // create a new spool, maybe brand too, in Spoolman
        Serial.println("New Brand Filament Tag found!");
        createBrandFilament(doc, uidString);
      }
      else 
      {
        Serial.println("Keine SPOOL-ID gefunden.");
        activeSpoolId = "";
        oledShowProgressBar(1, 1, "Failure", "Unkown tag");
      }
    }else{
      oledShowProgressBar(octoEnabled?5:4, octoEnabled?5:4, "Failure!", "Spoolman unavailable");
    }
  }

  doc.clear();

  return true;
}

void writeJsonToTag(void *parameter) {
  NfcWriteParameterType* params = (NfcWriteParameterType*)parameter;

  // Gib die erstellte NDEF-Message aus
  Serial.println("Erstelle NDEF-Message...");
  Serial.println(params->payload);

  nfcReaderState = NFC_WRITING;

  // First request the reading task to be suspended and than wait until it responds
  nfcReadingTaskSuspendRequest = true;
  while(nfcReadingTaskSuspendState == false){
    vTaskDelay(100 / portTICK_PERIOD_MS);
  }

  //pauseBambuMqttTask = true;
  // aktualisieren der Website wenn sich der Status ändert
  sendNfcData();
  vTaskDelay(100 / portTICK_PERIOD_MS);
  // Wait 10sec for tag
  uint8_t success = 0;
  String uidString = "";
  for (uint16_t i = 0; i < 20; i++) {
    uint8_t uid[] = { 0, 0, 0, 0, 0, 0, 0 };  // Buffer to store the returned UID
    uint8_t uidLength;
    // yield before potentially waiting for 400ms
    yield();
    esp_task_wdt_reset();
    success = nfc.readPassiveTargetID(PN532_MIFARE_ISO14443A, uid, &uidLength, 400);
    if (success) {
      for (uint8_t i = 0; i < uidLength; i++) {
        //TBD: Rework to remove all the string operations
        uidString += String(uid[i], HEX);
        if (i < uidLength - 1) {
            uidString += ":"; // Optional: Trennzeichen hinzufügen
        }
      }
      foundNfcTag(nullptr, success);
      break;
    }

    yield();
    esp_task_wdt_reset();
    vTaskDelay(pdMS_TO_TICKS(1));
  }

  if (success)
  {
    oledShowProgressBar(1, 3, "Write Tag", "Writing");

    // Schreibe die NDEF-Message auf den Tag
    success = ntag2xx_WriteNDEF(params->payload);
    if (success) 
    {
        Serial.println("NDEF-Message erfolgreich auf den Tag geschrieben");
        //oledShowMessage("NFC-Tag written");
        //vTaskDelay(1000 / portTICK_PERIOD_MS);
        nfcReaderState = NFC_WRITE_SUCCESS;
        // aktualisieren der Website wenn sich der Status ändert
        sendNfcData();
        pauseBambuMqttTask = false;
        
        if(params->tagType){
          // TBD: should this be simplified?
          if (updateSpoolTagId(uidString, params->payload) && params->tagType) {
            
          }else{
            // Potentially handle errors
          }
        }else{
          oledShowProgressBar(1, 1, "Write Tag", "Done!");
        }
        uint8_t uid[] = { 0, 0, 0, 0, 0, 0, 0 };  // Buffer to store the returned UID
        uint8_t uidLength;
        yield();
        esp_task_wdt_reset();
        while (nfc.readPassiveTargetID(PN532_MIFARE_ISO14443A, uid, &uidLength, 400)) {
          yield();
        } 
        vTaskResume(RfidReaderTask);
        vTaskDelay(500 / portTICK_PERIOD_MS);        
    } 
    else 
    {
        Serial.println("Fehler beim Schreiben der NDEF-Message auf den Tag");
        oledShowIcon("failed");
        vTaskDelay(2000 / portTICK_PERIOD_MS);
        nfcReaderState = NFC_WRITE_ERROR;
    }
  }
  else
  {
    Serial.println("Fehler: Kein Tag zu schreiben gefunden.");
    oledShowProgressBar(1, 1, "Failure!", "No tag found");
    vTaskDelay(2000 / portTICK_PERIOD_MS);
    nfcReaderState = NFC_IDLE;
  }
  
  sendWriteResult(nullptr, success);
  sendNfcData();

  nfcReadingTaskSuspendRequest = false;
  pauseBambuMqttTask = false;

  free(params->payload);
  delete params;

  vTaskDelete(NULL);
}

void startWriteJsonToTag(const bool isSpoolTag, const char* payload) {
  NfcWriteParameterType* parameters = new NfcWriteParameterType();
  parameters->tagType = isSpoolTag;
  parameters->payload = strdup(payload);
  
  // Task nicht mehrfach starten
  if (nfcReaderState == NFC_IDLE || nfcReaderState == NFC_READ_ERROR || nfcReaderState == NFC_READ_SUCCESS) {
    oledShowProgressBar(0, 1, "Write Tag", "Place tag now");
    // Erstelle die Task
    xTaskCreate(
        writeJsonToTag,        // Task-Funktion
        "WriteJsonToTagTask",       // Task-Name
        5115,                        // Stackgröße in Bytes
        (void*)parameters,         // Parameter
        rfidWriteTaskPrio,           // Priorität
        NULL                         // Task-Handle (nicht benötigt)
    );
  }else{
    oledShowProgressBar(0, 1, "FAILURE", "NFC busy!");
    // TBD: Add proper error handling (website)
  }
}

void scanRfidTask(void * parameter) {
  Serial.println("RFID Task gestartet");
  for(;;) {
    // Wenn geschrieben wird Schleife aussetzen
    if (nfcReaderState != NFC_WRITING && !nfcReadingTaskSuspendRequest && !booting)
    {
      nfcReadingTaskSuspendState = false;
      yield();

      uint8_t success;
      uint8_t uid[] = { 0, 0, 0, 0, 0, 0, 0 };  // Buffer to store the returned UID
      uint8_t uidLength;

      success = nfc.readPassiveTargetID(PN532_MIFARE_ISO14443A, uid, &uidLength, 500);

      foundNfcTag(nullptr, success);
      
      // As long as there is still a tag on the reader, do not try to read it again
      if (success && nfcReaderState == NFC_IDLE)
      {
        // Set the current tag as not processed
        tagProcessed = false;

        // Display some basic information about the card
        Serial.println("Found an ISO14443A card");

        nfcReaderState = NFC_READING;

        oledShowProgressBar(0, octoEnabled?5:4, "Reading", "Detecting tag");

        // Wait 1 second after tag detection to stabilize connection
        Serial.println("Tag detected, waiting 1 second for stabilization...");
        vTaskDelay(1000 / portTICK_PERIOD_MS);

        // create Tag UID string
        String uidString = "";
        for (uint8_t i = 0; i < uidLength; i++) {
          //TBD: Rework to remove all the string operations
          uidString += String(uid[i], HEX);
          if (i < uidLength - 1) {
              uidString += ":"; // Optional: Trennzeichen hinzufügen
          }
        }
        
        if (uidLength == 7)
        {
          uint16_t tagSize = readTagSize();
          if(tagSize > 0)
          {
            // Create a buffer depending on the size of the tag
            uint8_t* data = (uint8_t*)malloc(tagSize);
            memset(data, 0, tagSize);

            // We probably have an NTAG2xx card (though it could be Ultralight as well)
            Serial.println("Seems to be an NTAG2xx tag (7 byte UID)");
            Serial.print("Tag size: ");
            Serial.print(tagSize);
            Serial.println(" bytes");
            
            uint8_t numPages = readTagSize()/4;
            
            for (uint8_t i = 4; i < 4+numPages; i++) {
              
              if (!nfc.ntag2xx_ReadPage(i, data+(i-4) * 4))
              {
                break; // Stop if reading fails
              }
             
              // Check for NDEF message end
              if (data[(i - 4) * 4] == 0xFE) 
              {
                Serial.println("Found NDEF message end marker");
                break; // End of NDEF message
              }

              yield();
              esp_task_wdt_reset();
              // Increased delay to ensure stable reading
              vTaskDelay(pdMS_TO_TICKS(5)); // Increased from 1ms to 5ms
            }
            
            Serial.println("Tag reading completed, starting NDEF decode...");
            
            if (!decodeNdefAndReturnJson(data, uidString)) 
            {
              oledShowProgressBar(1, 1, "Failure", "Unknown tag");
              nfcReaderState = NFC_READ_ERROR;
            }
            else 
            {
              nfcReaderState = NFC_READ_SUCCESS;
            }

            free(data);
          }
          else
          {
            oledShowProgressBar(1, 1, "Failure", "Tag read error");
            nfcReaderState = NFC_READ_ERROR;
          }
        }
        else
        {
          //TBD: Show error here?!
          oledShowProgressBar(1, 1, "Failure", "Unkown tag type");
          Serial.println("This doesn't seem to be an NTAG2xx tag (UUID length != 7 bytes)!");
        }
      }

      if (!success && nfcReaderState != NFC_IDLE && !nfcReadingTaskSuspendRequest)
      {
        nfcReaderState = NFC_IDLE;
        //uidString = "";
        nfcJsonData = "";
        activeSpoolId = "";
        Serial.println("Tag entfernt");
        if (!bambuCredentials.autosend_enable) oledShowWeight(weight);
      }
      // Reset state after successful read when tag is removed
      else if (!success && nfcReaderState == NFC_READ_SUCCESS)
      {
        nfcReaderState = NFC_IDLE;
        Serial.println("Tag nach erfolgreichem Lesen entfernt - bereit für nächsten Tag");
      }

      // Add a longer pause after successful reading to prevent immediate re-reading
      if (nfcReaderState == NFC_READ_SUCCESS) {
        Serial.println("Tag erfolgreich gelesen - warte 5 Sekunden vor nächstem Scan");
        vTaskDelay(5000 / portTICK_PERIOD_MS); // 5 second pause
      }

      // aktualisieren der Website wenn sich der Status ändert
      sendNfcData();
    }
    else
    {
      nfcReadingTaskSuspendState = true;
      Serial.println("NFC Reading disabled");
      vTaskDelay(1000 / portTICK_PERIOD_MS);
    }
    yield();
  }
}

void startNfc() {
  oledShowProgressBar(5, 7, DISPLAY_BOOT_TEXT, "NFC init");
  nfc.begin();                                           // Beginne Kommunikation mit RFID Leser
  delay(1000);
  unsigned long versiondata = nfc.getFirmwareVersion();  // Lese Versionsnummer der Firmware aus
  if (! versiondata) {                                   // Wenn keine Antwort kommt
    Serial.println("Kann kein RFID Board finden !");            // Sende Text "Kann kein..." an seriellen Monitor
    oledShowMessage("No RFID Board found");
    vTaskDelay(2000 / portTICK_PERIOD_MS);
  }
  else {
    Serial.print("Chip PN5 gefunden"); Serial.println((versiondata >> 24) & 0xFF, HEX); // Sende Text und Versionsinfos an seriellen
    Serial.print("Firmware ver. "); Serial.print((versiondata >> 16) & 0xFF, DEC);      // Monitor, wenn Antwort vom Board kommt
    Serial.print('.'); Serial.println((versiondata >> 8) & 0xFF, DEC);                  // 

    nfc.SAMConfig();
    // Set the max number of retry attempts to read from a card
    // This prevents us from waiting forever for a card, which is
    // the default behaviour of the PN532.
    //nfc.setPassiveActivationRetries(0x7F);
    //nfc.setPassiveActivationRetries(0xFF);

    BaseType_t result = xTaskCreatePinnedToCore(
      scanRfidTask, /* Function to implement the task */
      "RfidReader", /* Name of the task */
      5115,  /* Stack size in words */
      NULL,  /* Task input parameter */
      rfidTaskPrio,  /* Priority of the task */
      &RfidReaderTask,  /* Task handle. */
      rfidTaskCore); /* Core where the task should run */

      if (result != pdPASS) {
        Serial.println("Fehler beim Erstellen des RFID Tasks");
    } else {
        Serial.println("RFID Task erfolgreich erstellt");
    }
  }
}