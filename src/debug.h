#include <Arduino.h>


#ifdef ENABLE_HEAP_DEBUGGING
    #define HEAP_DEBUG_MESSAGE(location) printHeapDebugData(location);
#else
    #define HEAP_DEBUG_MESSAGE(location) 
#endif

inline void printHeapDebugData(String location){
    Serial.println("Heap: " + String(ESP.getMinFreeHeap()/1024) + "\t" + String(ESP.getFreeHeap()/1024) + "\t" + String(ESP.getMaxAllocHeap()/1024) + "\t" + location);
}