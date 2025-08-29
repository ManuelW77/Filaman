#ifndef NFC_H
#define NFC_H

#include <Arduino.h>

typedef enum{
    NFC_IDLE,
    NFC_READING,
    NFC_READ_SUCCESS,
    NFC_READ_ERROR,
    NFC_WRITING,
    NFC_WRITE_SUCCESS,
    NFC_WRITE_ERROR
} nfcReaderStateType;

void startNfc();
void scanRfidTask(void * parameter);
void startWriteJsonToTag(const bool isSpoolTag, const char* payload);
bool quickSpoolIdCheck(String uidString);

extern TaskHandle_t RfidReaderTask;
extern String nfcJsonData;
extern String activeSpoolId;
extern String lastSpoolId;
extern volatile nfcReaderStateType nfcReaderState;
extern volatile bool pauseBambuMqttTask;
extern bool tagProcessed;



#endif