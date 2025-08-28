#include <Arduino.h>
#include <Wire.h>
#include <WiFi.h>

#include "wlan.h"
#include "config.h"
#include "website.h"
#include "api.h"
#include "display.h"
#include "bambu.h"
#include "nfc.h"
#include "scale.h"
#include "esp_task_wdt.h"
#include "commonFS.h"

bool mainTaskWasPaused = 0;
uint8_t scaleTareCounter = 0;
bool touchSensorConnected = false;
bool booting = true;

// ##### SETUP #####
void setup() {
  Serial.begin(115200);

  uint64_t chipid;

  chipid = ESP.getEfuseMac(); //The chip ID is essentially its MAC address(length: 6 bytes).
  Serial.printf("ESP32 Chip ID = %04X", (uint16_t)(chipid >> 32)); //print High 2 bytes
  Serial.printf("%08X\n", (uint32_t)chipid); //print Low 4bytes.

  // Initialize SPIFFS
  initializeFileSystem();

  // Start Display
  setupDisplay();

  // WiFiManager
  initWiFi();

  // Webserver
  setupWebserver(server);

  // Spoolman API
  initSpoolman();

  // Bambu MQTT
  setupMqtt();

  // NFC Reader
  startNfc();

  // Touch Sensor
  pinMode(TTP223_PIN, INPUT_PULLUP);
  if (digitalRead(TTP223_PIN) == LOW) 
  {
    Serial.println("Touch Sensor is connected");
    touchSensorConnected = true;
  }

  // Scale
  start_scale(touchSensorConnected);

  // WDT initialisieren mit 10 Sekunden Timeout
  bool panic = true; // Wenn true, löst ein WDT-Timeout einen System-Panik aus
  esp_task_wdt_init(10, panic);

  booting = false;
  // Aktuellen Task (loopTask) zum Watchdog hinzufügen
  esp_task_wdt_add(NULL);
}


/**
 * Safe interval check that handles millis() overflow
 * @param currentTime Current millis() value
 * @param lastTime Last recorded time
 * @param interval Desired interval in milliseconds
 * @return True if interval has elapsed
 */
bool intervalElapsed(unsigned long currentTime, unsigned long &lastTime, unsigned long interval) {
  if (currentTime - lastTime >= interval || currentTime < lastTime) {
    lastTime = currentTime;
    return true;
  }
  return false;
}

unsigned long lastWeightReadTime = 0;
const unsigned long weightReadInterval = 1000; // 1 second

unsigned long lastAutoSetBambuAmsTime = 0;
const unsigned long autoSetBambuAmsInterval = 1000; // 1 second
uint8_t autoAmsCounter = 0;

uint8_t weightSend = 0;
int16_t lastWeight = 0;

// WIFI check variables
unsigned long lastWifiCheckTime = 0;
unsigned long lastTopRowUpdateTime = 0;
unsigned long lastSpoolmanHealcheckTime = 0;

// Button debounce variables
unsigned long lastButtonPress = 0;
const unsigned long debounceDelay = 500; // 500 ms debounce delay

// ##### PROGRAM START #####
void loop() {
  unsigned long currentMillis = millis();

  // Überprüfe den Status des Touch Sensors
  if (touchSensorConnected && digitalRead(TTP223_PIN) == HIGH && currentMillis - lastButtonPress > debounceDelay) 
  {
    lastButtonPress = currentMillis;
    scaleTareRequest = true;
  }

  // Überprüfe regelmäßig die WLAN-Verbindung
  if (intervalElapsed(currentMillis, lastWifiCheckTime, WIFI_CHECK_INTERVAL)) 
  {
    checkWiFiConnection();
  }

  // Periodic display update
  if (intervalElapsed(currentMillis, lastTopRowUpdateTime, DISPLAY_UPDATE_INTERVAL)) 
  {
    oledShowTopRow();
  }

  // Periodic spoolman health check
  if (intervalElapsed(currentMillis, lastSpoolmanHealcheckTime, SPOOLMAN_HEALTHCHECK_INTERVAL)) 
  {
    checkSpoolmanInstance();
  }

  // Wenn Bambu auto set Spool aktiv
  if (bambuCredentials.autosend_enable && autoSetToBambuSpoolId > 0) 
  {
    if (!bambuDisabled && !bambu_connected) 
    {
      bambu_restart();
    }

    if (intervalElapsed(currentMillis, lastAutoSetBambuAmsTime, autoSetBambuAmsInterval)) 
    {
      if (nfcReaderState == NFC_IDLE)
      {
        lastAutoSetBambuAmsTime = currentMillis;
        oledShowMessage("Auto Set         " + String(bambuCredentials.autosend_time - autoAmsCounter) + "s");
        autoAmsCounter++;

        if (autoAmsCounter >= bambuCredentials.autosend_time) 
        {
          autoSetToBambuSpoolId = 0;
          autoAmsCounter = 0;
          oledShowWeight(weight);
        }
      }
      else
      {
        autoAmsCounter = 0;
      }
    }
  }

  // If scale is not calibrated, only show a warning
  if (!scaleCalibrated) 
  {
    // Do not show the warning if the calibratin process is onging
    if(!scaleCalibrationActive){
      oledShowMessage("Scale not calibrated");
      vTaskDelay(1000 / portTICK_PERIOD_MS);
    }
  } 
  else 
  {
    // Ausgabe der Waage auf Display
    if(pauseMainTask == 0)
    {
      if (mainTaskWasPaused || (weight != lastWeight && nfcReaderState == NFC_IDLE && (!bambuCredentials.autosend_enable || autoSetToBambuSpoolId == 0)))
      {
        (weight < 2) ? ((weight < -2) ? oledShowMessage("!! -0") : oledShowWeight(0)) : oledShowWeight(weight);
      }
      mainTaskWasPaused = false;
    }
    else
    {
      mainTaskWasPaused = true;
    }


    // Wenn Timer abgelaufen und nicht gerade ein RFID-Tag geschrieben wird
    if (currentMillis - lastWeightReadTime >= weightReadInterval && nfcReaderState < NFC_WRITING)
    {
      lastWeightReadTime = currentMillis;

      // Prüfen ob die Waage korrekt genullt ist
      // Abweichung von 2g ignorieren
      if (autoTare && (weight > 2 && weight < 7) || weight < -2)
      {
        scale_tare_counter++;
      }
      else
      {
        scale_tare_counter = 0;
      }

      // Prüfen ob das Gewicht gleich bleibt und dann senden
      if (abs(weight - lastWeight) <= 2 && weight > 5)
      {
        weigthCouterToApi++;
      } 
      else 
      {
        weigthCouterToApi = 0;
        weightSend = 0;
      }
    }

    // reset weight counter after writing tag
    // TBD: what exactly is the logic behind this?
    if (currentMillis - lastWeightReadTime >= weightReadInterval && nfcReaderState != NFC_IDLE && nfcReaderState != NFC_READ_SUCCESS)
    {
      weigthCouterToApi = 0;
    }
    
    lastWeight = weight;

    // Wenn ein Tag mit SM id erkannte wurde und der Waage Counter anspricht an SM Senden
    if (activeSpoolId != "" && weigthCouterToApi > 3 && weightSend == 0 && nfcReaderState == NFC_READ_SUCCESS && tagProcessed == false && spoolmanApiState == API_IDLE) 
    {
      // set the current tag as processed to prevent it beeing processed again
      tagProcessed = true;

      if (updateSpoolWeight(activeSpoolId, weight)) 
      {
        weightSend = 1;
        
        // Set Bambu spool ID for auto-send if enabled
        if (bambuCredentials.autosend_enable) 
        {
          autoSetToBambuSpoolId = activeSpoolId.toInt();
        }
      }
      else
      {
        oledShowIcon("failed");
        vTaskDelay(2000 / portTICK_PERIOD_MS);
      }
    }

    if(octoEnabled && sendOctoUpdate && spoolmanApiState == API_IDLE)
    {
      updateSpoolOcto(autoSetToBambuSpoolId);
      sendOctoUpdate = false;
    }
  }
  
  esp_task_wdt_reset();
}
