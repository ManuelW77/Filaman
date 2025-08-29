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
  }

uint16_t readTagSize()
{
  uint8_t buffer[4];
  memset(buffer, 0, 4);
  nfc.ntag2xx_ReadPage(3, buffer);
  return buffer[2]*8;
}

uint8_t ntag2xx_WriteNDEF(const char *payload) {
  uint16_t tagSize = readTagSize();
  Serial.print("Tag Size: ");Serial.println(tagSize);

  uint8_t pageBuffer[4] = {0, 0, 0, 0};
  Serial.println("Beginne mit dem Schreiben der NDEF-Nachricht...");
  
  // Figure out how long the string is
  uint8_t len = strlen(payload);
  Serial.print("Länge der Payload: ");
  Serial.println(len);
  
  Serial.print("Payload: ");Serial.println(payload);

  // Setup the record header
  // See NFCForum-TS-Type-2-Tag_1.1.pdf for details
  uint8_t pageHeader[21] = {
    /* NDEF Message TLV - JSON Record */
    0x03, /* Tag Field (0x03 = NDEF Message) */
    (uint8_t)(len+3+16), /* Payload Length (including NDEF header) */
    0xD2, /* NDEF Record Header (TNF=0x2:MIME Media + SR + ME + MB) */
    0x10, /* Type Length for the record type indicator */
    (uint8_t)(len), /* Payload len */
    'a', 'p', 'p', 'l', 'i', 'c', 'a', 't', 'i', 'o', 'n', '/', 'j', 's', 'o', 'n'
  };

  // Make sure the URI payload will fit in dataLen (include 0xFE trailer)
  if ((len < 1) || (len + 1 > (tagSize - sizeof(pageHeader)))) 
  {
    Serial.println();
    Serial.println("!!!!!!!!!!!!!!!!!!!!!!!!");
    Serial.println("Fehler: Die Nutzlast passt nicht in die Datenlänge.");
    Serial.println("!!!!!!!!!!!!!!!!!!!!!!!!");
    Serial.println();
    return 0;
  }

  // Kombiniere Header und Payload
  int totalSize = sizeof(pageHeader) + len;
  uint8_t* combinedData = (uint8_t*) malloc(totalSize);
  if (combinedData == NULL) 
  {
    Serial.println("Fehler: Nicht genug Speicher vorhanden.");
    oledShowMessage("Tag too small");
    vTaskDelay(2000 / portTICK_PERIOD_MS);
    return 0;
  }

  // Kombiniere Header und Payload
  memcpy(combinedData, pageHeader, sizeof(pageHeader));
  memcpy(&combinedData[sizeof(pageHeader)], payload, len);

  // Schreibe die Seiten
  uint8_t a = 0;
  uint8_t i = 0;
  while (totalSize > 0) {
    memset(pageBuffer, 0, 4);
    int bytesToWrite = (totalSize < 4) ? totalSize : 4;
    memcpy(pageBuffer, combinedData + a, bytesToWrite);

    //uint8_t uid[] = { 0, 0, 0, 0, 0, 0, 0 };  // Buffer to store the returned UID
    //uint8_t uidLength;
    //nfc.readPassiveTargetID(PN532_MIFARE_ISO14443A, uid, &uidLength, 100);

    if (!(nfc.ntag2xx_WritePage(4+i, pageBuffer))) 
    {
      Serial.println("Fehler beim Schreiben der Seite.");
      free(combinedData);
      return 0;
    }

    yield();
    //esp_task_wdt_reset();

    i++;
    a += 4;
    totalSize -= bytesToWrite;
  }

  // Ensure the NDEF message is properly terminated
  memset(pageBuffer, 0, 4);
  pageBuffer[0] = 0xFE; // NDEF record footer
  if (!(nfc.ntag2xx_WritePage(4+i, pageBuffer))) 
  {
    Serial.println("Fehler beim Schreiben des End-Bits.");
    free(combinedData);
    return 0;
  }

  Serial.println("NDEF-Nachricht erfolgreich geschrieben.");
  free(combinedData);
  return 1;
}

bool decodeNdefAndReturnJson(const byte* encodedMessage, String uidString) {
  oledShowProgressBar(1, octoEnabled?5:4, "Reading", "Decoding data");

  byte typeLength = encodedMessage[3];
  byte payloadLength = encodedMessage[4];

  nfcJsonData = "";

  for (int i = 2; i < payloadLength+2; i++) 
  {
    nfcJsonData += (char)encodedMessage[3 + typeLength + i];
  }

  Serial.println("Decoded JSON Data:");
  Serial.println(nfcJsonData);

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

        //vTaskDelay(500 / portTICK_PERIOD_MS);

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
            
            uint8_t numPages = readTagSize()/4;
            for (uint8_t i = 4; i < 4+numPages; i++) {
              if (!nfc.ntag2xx_ReadPage(i, data+(i-4) * 4))
              {
                break; // Stop if reading fails
              }
              // Check for NDEF message end
              if (data[(i - 4) * 4] == 0xFE) 
              {
                break; // End of NDEF message
              }

              yield();
              esp_task_wdt_reset();
              vTaskDelay(pdMS_TO_TICKS(1));
            }

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