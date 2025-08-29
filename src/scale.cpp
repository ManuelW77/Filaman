#include "nfc.h"
#include <Arduino.h>
#include <ArduinoJson.h>
#include "config.h"
#include "HX711.h"
#include "display.h"
#include "esp_task_wdt.h"
#include <Preferences.h>

HX711 scale;

TaskHandle_t ScaleTask;

int16_t weight = 0;

uint8_t weigthCouterToApi = 0;
uint8_t scale_tare_counter = 0;
bool scaleTareRequest = false;
uint8_t pauseMainTask = 0;
bool scaleCalibrated;
bool autoTare = true;
bool scaleCalibrationActive = false;

// ##### Funktionen für Waage #####
uint8_t setAutoTare(bool autoTareValue) {
  Serial.print("Set AutoTare to ");
  Serial.println(autoTareValue);
  autoTare = autoTareValue;

  // Speichern mit NVS
  Preferences preferences;
  preferences.begin(NVS_NAMESPACE_SCALE, false); // false = readwrite
  preferences.putBool(NVS_KEY_AUTOTARE, autoTare);
  preferences.end();

  return 1;
}

uint8_t tareScale() {
  Serial.println("Tare scale");
  scale.tare();
  
  return 1;
}

void scale_loop(void * parameter) {
  Serial.println("++++++++++++++++++++++++++++++");
  Serial.println("Scale Loop started");
  Serial.println("++++++++++++++++++++++++++++++");

  for(;;) {
    if (scale.is_ready()) 
    {
      // Waage automatisch Taren, wenn zu lange Abweichung
      if (autoTare && scale_tare_counter >= 5) 
      {
        Serial.println("Auto Tare scale");
        scale.tare();
        scale_tare_counter = 0;
      }

      // Waage manuell Taren
      if (scaleTareRequest == true) 
      {
        Serial.println("Re-Tare scale");
        oledShowMessage("TARE Scale");
        vTaskDelay(pdMS_TO_TICKS(1000));
        scale.tare();
        vTaskDelay(pdMS_TO_TICKS(1000));
        oledShowWeight(0);
        scaleTareRequest = false;
      }

      // Only update weight if median changed more than 1
      int16_t newWeight = round(scale.get_units());
      if(abs(weight-newWeight) > 1){
        weight = newWeight;
      }
    }
    
    vTaskDelay(pdMS_TO_TICKS(100));
  }
}

void start_scale(bool touchSensorConnected) {
  Serial.println("Prüfe Calibration Value");
  float calibrationValue;

  // NVS lesen
  Preferences preferences;
  preferences.begin(NVS_NAMESPACE_SCALE, true); // true = readonly
  if(preferences.isKey(NVS_KEY_CALIBRATION)){
    calibrationValue = preferences.getFloat(NVS_KEY_CALIBRATION);
    scaleCalibrated = true;
  }else{
    calibrationValue = SCALE_DEFAULT_CALIBRATION_VALUE;
    scaleCalibrated = false;
  }
  
  // auto Tare
  // Wenn Touch Sensor verbunden, dann autoTare auf false setzen
  // Danach prüfen was in NVS gespeichert ist
  autoTare = (touchSensorConnected) ? false : true;
  autoTare = preferences.getBool(NVS_KEY_AUTOTARE, autoTare);

  preferences.end();

  Serial.print("Read Scale Calibration Value ");
  Serial.println(calibrationValue);

  scale.begin(LOADCELL_DOUT_PIN, LOADCELL_SCK_PIN);

  oledShowProgressBar(6, 7, DISPLAY_BOOT_TEXT, "Tare scale");
  for (uint16_t i = 0; i < 2000; i++) {
    yield();
    vTaskDelay(pdMS_TO_TICKS(1));
    esp_task_wdt_reset();
  }

  while(!scale.is_ready()) {
    vTaskDelay(pdMS_TO_TICKS(5000));
  }

  scale.set_scale(calibrationValue); // this value is obtained by calibrating the scale with known weights; see the README for details
  scale.tare();

  // Display Gewicht
  oledShowWeight(0);

  Serial.println("starte Scale Task");
  BaseType_t result = xTaskCreatePinnedToCore(
    scale_loop, /* Function to implement the task */
    "ScaleLoop", /* Name of the task */
    2048,  /* Stack size in words */
    NULL,  /* Task input parameter */
    scaleTaskPrio,  /* Priority of the task */
    &ScaleTask,  /* Task handle. */
    scaleTaskCore); /* Core where the task should run */

  if (result != pdPASS) {
      Serial.println("Fehler beim Erstellen des ScaleLoop-Tasks");
  } else {
      Serial.println("ScaleLoop-Task erfolgreich erstellt");
  }
}

uint8_t calibrate_scale() {
  uint8_t returnState = 0;
  float newCalibrationValue;

  scaleCalibrationActive = true;

  vTaskSuspend(RfidReaderTask);
  vTaskSuspend(ScaleTask);

  pauseBambuMqttTask = true;
  pauseMainTask = 1;
  
  if (scale.wait_ready_timeout(1000))
  {
    
    scale.set_scale();
    oledShowProgressBar(0, 3, "Scale Cal.", "Empty Scale");

    for (uint16_t i = 0; i < 5000; i++) {
      yield();
      vTaskDelay(pdMS_TO_TICKS(1));
      esp_task_wdt_reset();
    }

    scale.tare();
    Serial.println("Tare done...");
    Serial.print("Place a known weight on the scale...");

    oledShowProgressBar(1, 3, "Scale Cal.", "Place the weight");

    for (uint16_t i = 0; i < 5000; i++) {
      yield();
      vTaskDelay(pdMS_TO_TICKS(1));
      esp_task_wdt_reset();
    }
    
    float newCalibrationValue = scale.get_units(10);
    Serial.print("Result: ");
    Serial.println(newCalibrationValue);

    newCalibrationValue = newCalibrationValue/SCALE_LEVEL_WEIGHT;

    if (newCalibrationValue > 0)
    {
      Serial.print("New calibration value has been set to: ");
      Serial.println(newCalibrationValue);

      // Speichern mit NVS
      Preferences preferences;
      preferences.begin(NVS_NAMESPACE_SCALE, false); // false = readwrite
      preferences.putFloat(NVS_KEY_CALIBRATION, newCalibrationValue);
      preferences.end();

      // Verifizieren
      preferences.begin(NVS_NAMESPACE_SCALE, true);
      float verifyValue = preferences.getFloat(NVS_KEY_CALIBRATION, 0);
      preferences.end();

      Serial.print("Verified stored value: ");
      Serial.println(verifyValue);

      oledShowProgressBar(2, 3, "Scale Cal.", "Remove weight");

      scale.set_scale(newCalibrationValue);
      for (uint16_t i = 0; i < 2000; i++) {
        yield();
        vTaskDelay(pdMS_TO_TICKS(1));
        esp_task_wdt_reset();
      }
      
      oledShowProgressBar(3, 3, "Scale Cal.", "Completed");

      // For some reason it is not possible to re-tare the scale here, it will result in a wdt timeout. Instead let the scale loop do the taring
      //scale.tare();
      scaleTareRequest = true;

      for (uint16_t i = 0; i < 2000; i++) {
        yield();
        vTaskDelay(pdMS_TO_TICKS(1));
        esp_task_wdt_reset();
      }

      scaleCalibrated = true;
      returnState = 1;
    }
    else
    {
      Serial.println("Calibration value is invalid. Please recalibrate.");

      oledShowProgressBar(3, 3, "Failure", "Calibration error");

      for (uint16_t i = 0; i < 50000; i++) {
        yield();
        vTaskDelay(pdMS_TO_TICKS(1));
        esp_task_wdt_reset();
      }
      returnState = 0;
    } 
  }
  else 
  {
    Serial.println("HX711 not found.");
    
    oledShowMessage("HX711 not found");

    for (uint16_t i = 0; i < 30000; i++) {
      yield();
      vTaskDelay(pdMS_TO_TICKS(1));
      esp_task_wdt_reset();
    }
    returnState = 0;
  }

  vTaskResume(RfidReaderTask);
  vTaskResume(ScaleTask);
  pauseBambuMqttTask = false;
  pauseMainTask = 0;
  scaleCalibrationActive = false;

  return returnState;
}
