#ifndef API_H
#define API_H

#include <Arduino.h>
#include <ESPAsyncWebServer.h> // Include for AsyncWebServerRequest
#include "website.h"
#include "display.h"
#include <ArduinoJson.h>
typedef enum {
    API_INIT,
    API_IDLE,
    API_TRANSMITTING
} spoolmanApiStateType;

typedef enum {
    API_REQUEST_OCTO_SPOOL_UPDATE,
    API_REQUEST_BAMBU_UPDATE,
    API_REQUEST_SPOOL_TAG_ID_UPDATE,
    API_REQUEST_SPOOL_WEIGHT_UPDATE,
    API_REQUEST_SPOOL_LOCATION_UPDATE,
    API_REQUEST_VENDOR_CREATE,
    API_REQUEST_VENDOR_CHECK,
    API_REQUEST_FILAMENT_CHECK,
    API_REQUEST_FILAMENT_CREATE,
    API_REQUEST_SPOOL_CREATE
} SpoolmanApiRequestType;

extern volatile spoolmanApiStateType spoolmanApiState;
extern bool spoolman_connected;
extern String spoolmanUrl;
extern bool octoEnabled;
extern bool sendOctoUpdate;
extern String octoUrl;
extern String octoToken;
extern bool spoolmanConnected;

bool checkSpoolmanInstance();
bool saveSpoolmanUrl(const String& url, bool octoOn, const String& octoWh, const String& octoTk);
String loadSpoolmanUrl(); // Neue Funktion zum Laden der URL
bool checkSpoolmanExtraFields(); // Neue Funktion zum Überprüfen der Extrafelder
JsonDocument fetchSingleSpoolInfo(int spoolId); // API-Funktion für die Webseite
bool updateSpoolTagId(String uidString, const char* payload); // Neue Funktion zum Aktualisieren eines Spools
uint8_t updateSpoolWeight(String spoolId, uint16_t weight); // Neue Funktion zum Aktualisieren des Gewichts
uint8_t updateSpoolLocation(String spoolId, String location);
bool initSpoolman(); // Neue Funktion zum Initialisieren von Spoolman
bool updateSpoolBambuData(String payload); // Neue Funktion zum Aktualisieren der Bambu-Daten
bool updateSpoolOcto(int spoolId); // Neue Funktion zum Aktualisieren der Octo-Daten
bool createBrandFilament(JsonDocument& payload, String uidString);

#endif
