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

// Weight stabilization variables
#define MOVING_AVERAGE_SIZE 20          // Number of samples for moving average
#define LOW_PASS_ALPHA 0.15f           // Low-pass filter coefficient (0.1-0.2 works well)
#define DISPLAY_THRESHOLD 0.5f         // Only update display if change > 0.5g
#define API_THRESHOLD 2.0f             // Only trigger API actions if change > 2g
#define MEASUREMENT_INTERVAL_MS 50     // Measurement interval in milliseconds

float weightBuffer[MOVING_AVERAGE_SIZE];
uint8_t bufferIndex = 0;
bool bufferFilled = false;
float filteredWeight = 0.0f;
int16_t lastDisplayedWeight = 0;
int16_t lastStableWeight = 0;        // For API/action triggering
unsigned long lastMeasurementTime = 0;

uint8_t weigthCouterToApi = 0;
uint8_t scale_tare_counter = 0;
bool scaleTareRequest = false;
uint8_t pauseMainTask = 0;
bool scaleCalibrated;
bool autoTare = true;
bool scaleCalibrationActive = false;

// ##### Weight stabilization functions #####

/**
 * Reset weight filter buffer - call after tare or calibration
 */
void resetWeightFilter() {
  bufferIndex = 0;
  bufferFilled = false;
  filteredWeight = 0.0f;
  lastDisplayedWeight = 0;
  lastStableWeight = 0;            // Reset stable weight for API actions
  
  // Initialize buffer with zeros
  for (int i = 0; i < MOVING_AVERAGE_SIZE; i++) {
    weightBuffer[i] = 0.0f;
  }
}

/**
 * Calculate moving average from weight buffer
 */
float calculateMovingAverage() {
  float sum = 0.0f;
  int count = bufferFilled ? MOVING_AVERAGE_SIZE : bufferIndex;
  
  for (int i = 0; i < count; i++) {
    sum += weightBuffer[i];
  }
  
  return (count > 0) ? sum / count : 0.0f;
}

/**
 * Apply low-pass filter to smooth weight readings
 * Uses exponential smoothing: y_new = alpha * x_new + (1-alpha) * y_old
 */
float applyLowPassFilter(float newValue) {
  filteredWeight = LOW_PASS_ALPHA * newValue + (1.0f - LOW_PASS_ALPHA) * filteredWeight;
  return filteredWeight;
}

/**
 * Process new weight reading with stabilization
 * Returns stabilized weight value
 */
int16_t processWeightReading(float rawWeight) {
  // Add to moving average buffer
  weightBuffer[bufferIndex] = rawWeight;
  bufferIndex = (bufferIndex + 1) % MOVING_AVERAGE_SIZE;
  
  if (bufferIndex == 0) {
    bufferFilled = true;
  }
  
  // Calculate moving average
  float avgWeight = calculateMovingAverage();
  
  // Apply low-pass filter
  float smoothedWeight = applyLowPassFilter(avgWeight);
  
  // Round to nearest gram
  int16_t newWeight = round(smoothedWeight);
  
  // Update displayed weight if display threshold is reached
  if (abs(newWeight - lastDisplayedWeight) >= DISPLAY_THRESHOLD) {
    lastDisplayedWeight = newWeight;
  }
  
  // Update global weight for API actions only if stable threshold is reached
  int16_t weightToReturn = weight; // Default: keep current weight
  
  if (abs(newWeight - lastStableWeight) >= API_THRESHOLD) {
    lastStableWeight = newWeight;
    weightToReturn = newWeight;
    
    Serial.printf("Stable weight change detected: %d -> %d (diff: %d)\n", 
                 weight, newWeight, abs(newWeight - weight));
  }
  
  return weightToReturn;
}

/**
 * Get current filtered weight for display purposes
 * This returns the smoothed weight even if it hasn't triggered API actions
 */
int16_t getFilteredDisplayWeight() {
  return lastDisplayedWeight;
}

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
  resetWeightFilter(); // Reset stabilization filter after tare
  
  return 1;
}

void scale_loop(void * parameter) {
  Serial.println("++++++++++++++++++++++++++++++");
  Serial.println("Scale Loop started");
  Serial.println("++++++++++++++++++++++++++++++");

  // Initialize weight filter
  resetWeightFilter();
  lastMeasurementTime = millis();

  for(;;) {
    unsigned long currentTime = millis();
    
    // Only measure at defined intervals to reduce noise
    if (currentTime - lastMeasurementTime >= MEASUREMENT_INTERVAL_MS) {
      if (scale.is_ready()) 
      {
        // Waage automatisch Taren, wenn zu lange Abweichung
        if (autoTare && scale_tare_counter >= 5) 
        {
          Serial.println("Auto Tare scale");
          scale.tare();
          resetWeightFilter(); // Reset filter after auto tare
          scale_tare_counter = 0;
        }

        // Waage manuell Taren
        if (scaleTareRequest == true) 
        {
          Serial.println("Re-Tare scale");
          oledShowMessage("TARE Scale");
          vTaskDelay(pdMS_TO_TICKS(1000));
          scale.tare();
          resetWeightFilter(); // Reset filter after manual tare
          vTaskDelay(pdMS_TO_TICKS(1000));
          oledShowWeight(0);
          scaleTareRequest = false;
        }

        // Get raw weight reading
        float rawWeight = scale.get_units();
        
        // Process weight with stabilization
        int16_t stabilizedWeight = processWeightReading(rawWeight);
        
        // Update global weight variable only if it changed significantly (for API actions)
        if (stabilizedWeight != weight) {
          weight = stabilizedWeight;
          Serial.printf("API weight updated: %d\n", weight);
        }
        
        // Debug output for monitoring (can be removed in production)
        static unsigned long lastDebugTime = 0;
        if (currentTime - lastDebugTime > 2000) { // Print every 2 seconds
          Serial.printf("Raw: %.2f, Filtered: %.2f, Display: %d, API: %d\n", 
                       rawWeight, filteredWeight, lastDisplayedWeight, weight);
          lastDebugTime = currentTime;
        }
        
        lastMeasurementTime = currentTime;
      }
    }
    
    vTaskDelay(pdMS_TO_TICKS(10)); // Shorter delay for more responsive loop
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
  for (uint16_t i = 0; i < 3000; i++) {
    yield();
    vTaskDelay(pdMS_TO_TICKS(1));
    esp_task_wdt_reset();
  }

  while(!scale.is_ready()) {
    vTaskDelay(pdMS_TO_TICKS(5000));
  }

  scale.set_scale(calibrationValue);
  //vTaskDelay(pdMS_TO_TICKS(5000));
  //scale.tare();

  // Initialize weight stabilization filter
  resetWeightFilter();

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
      resetWeightFilter(); // Reset filter after calibration
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
