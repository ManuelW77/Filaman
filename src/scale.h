#ifndef SCALE_H
#define SCALE_H

#include <Arduino.h>
#include "HX711.h"

uint8_t setAutoTare(bool autoTareValue);
uint8_t start_scale(bool touchSensorConnected);
uint8_t calibrate_scale();
uint8_t tareScale();

// Weight stabilization functions
void resetWeightFilter();
float calculateMovingAverage();
float applyLowPassFilter(float newValue);
int16_t processWeightReading(float rawWeight);
int16_t getFilteredDisplayWeight();

extern HX711 scale;
extern int16_t weight;
extern uint8_t weightCounterToApi;
extern uint8_t scale_tare_counter;
extern uint8_t scaleTareRequest;
extern uint8_t pauseMainTask;
extern bool scaleCalibrated;
extern bool autoTare;
extern bool scaleCalibrationActive;

extern TaskHandle_t ScaleTask;

#endif