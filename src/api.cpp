#include "api.h"
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include "commonFS.h"
#include <Preferences.h>
#include "debug.h"
#include "scale.h"
#include "nfc.h"
#include <time.h>
volatile spoolmanApiStateType spoolmanApiState = API_IDLE;

//bool spoolman_connected = false;
String spoolmanUrl = "";
bool octoEnabled = false;
bool sendOctoUpdate = false;
String octoUrl = "";
String octoToken = "";
uint16_t remainingWeight = 0;
uint16_t createdVendorId = 0;  // Store ID of newly created vendor
uint16_t foundVendorId = 0;    // Store ID of found vendor
uint16_t foundFilamentId = 0;  // Store ID of found filament
uint16_t createdFilamentId = 0;  // Store ID of newly created filament
uint16_t createdSpoolId = 0;  // Store ID of newly created spool
uint16_t updateOctoSpoolId = 0; // Store spool ID for OctoPrint update
bool spoolmanConnected = false;
bool spoolmanExtraFieldsChecked = false;
TaskHandle_t* apiTask;

struct SendToApiParams {
    SpoolmanApiRequestType requestType;
    String httpType;
    String spoolsUrl;
    String updatePayload;
    String octoToken;
    // Weight update parameters for sequential execution
    bool triggerWeightUpdate;
    String spoolIdForWeight;
    uint16_t weightValue;
};

JsonDocument fetchSingleSpoolInfo(int spoolId) {
    HTTPClient http;
    String spoolsUrl = spoolmanUrl + apiUrl + "/spool/" + spoolId;

    Serial.print("Rufe Spool-Daten von: ");
    Serial.println(spoolsUrl);

    http.begin(spoolsUrl);
    int httpCode = http.GET();

    JsonDocument filteredDoc;
    if (httpCode == HTTP_CODE_OK) {
        String payload = http.getString();
        JsonDocument doc;
        DeserializationError error = deserializeJson(doc, payload);
        if (error) {
            Serial.print("Fehler beim Parsen der JSON-Antwort: ");
            Serial.println(error.c_str());
        } else {
            String filamentType = doc["filament"]["material"].as<String>();
            String filamentBrand = doc["filament"]["vendor"]["name"].as<String>();

            int nozzle_temp_min = 0;
            int nozzle_temp_max = 0;
            if (doc["filament"]["extra"]["nozzle_temperature"].is<String>()) {
                String tempString = doc["filament"]["extra"]["nozzle_temperature"].as<String>();
                tempString.replace("[", "");
                tempString.replace("]", "");
                int commaIndex = tempString.indexOf(',');
                
                if (commaIndex != -1) {
                    nozzle_temp_min = tempString.substring(0, commaIndex).toInt();
                    nozzle_temp_max = tempString.substring(commaIndex + 1).toInt();
                }
            } 

            String filamentColor = doc["filament"]["color_hex"].as<String>();
            filamentColor.toUpperCase();

            String tray_info_idx = doc["filament"]["extra"]["bambu_idx"].as<String>();
            tray_info_idx.replace("\"", "");
            
            String cali_idx = doc["filament"]["extra"]["bambu_cali_id"].as<String>(); // "\"153\""
            cali_idx.replace("\"", "");
            
            String bambu_setting_id = doc["filament"]["extra"]["bambu_setting_id"].as<String>(); // "\"PFUSf40e9953b40d3d\""
            bambu_setting_id.replace("\"", "");

            doc.clear();

            filteredDoc["color"] = filamentColor;
            filteredDoc["type"] = filamentType;
            filteredDoc["nozzle_temp_min"] = nozzle_temp_min;
            filteredDoc["nozzle_temp_max"] = nozzle_temp_max;
            filteredDoc["brand"] = filamentBrand;
            filteredDoc["tray_info_idx"] = tray_info_idx;
            filteredDoc["cali_idx"] = cali_idx;
            filteredDoc["bambu_setting_id"] = bambu_setting_id;
        }
    } else {
        Serial.print("Fehler beim Abrufen der Spool-Daten. HTTP-Code: ");
        Serial.println(httpCode);
    }

    http.end();
    return filteredDoc;
}

void sendToApi(void *parameter) {
    HEAP_DEBUG_MESSAGE("sendToApi begin");

    // Wait until API is IDLE
    while(spoolmanApiState != API_IDLE){
        vTaskDelay(100 / portTICK_PERIOD_MS);
        yield();
    }
    spoolmanApiState = API_TRANSMITTING;
    SendToApiParams* params = (SendToApiParams*)parameter;

    // Extract values including weight update parameters
    SpoolmanApiRequestType requestType = params->requestType;
    String httpType = params->httpType;
    String spoolsUrl = params->spoolsUrl;
    String updatePayload = params->updatePayload;
    String octoToken = params->octoToken;
    bool triggerWeightUpdate = params->triggerWeightUpdate;
    String spoolIdForWeight = params->spoolIdForWeight;
    uint16_t weightValue = params->weightValue;

    // Retry mechanism with configurable parameters
    const uint8_t MAX_RETRIES = 3;
    const uint16_t RETRY_DELAY_MS = 1000; // 1 second between retries
    const uint16_t HTTP_TIMEOUT_MS = 10000; // 10 second HTTP timeout
    
    bool success = false;
    int httpCode = -1;
    String responsePayload = "";
    
    // Try request with retries
    for (uint8_t attempt = 1; attempt <= MAX_RETRIES && !success; attempt++) {
        Serial.printf("API Request attempt %d/%d to: %s\n", attempt, MAX_RETRIES, spoolsUrl.c_str());
        
        HTTPClient http;
        http.setReuse(false);
        http.setTimeout(HTTP_TIMEOUT_MS); // Set HTTP timeout
        
        http.begin(spoolsUrl);
        http.addHeader("Content-Type", "application/json");
        if (octoEnabled && octoToken != "") http.addHeader("X-Api-Key", octoToken);

        // Execute HTTP request based on type
        if (httpType == "PATCH") httpCode = http.PATCH(updatePayload);
        else if (httpType == "POST") httpCode = http.POST(updatePayload);
        else if (httpType == "GET") httpCode = http.GET();
        else httpCode = http.PUT(updatePayload);

        // Check if request was successful
        if (httpCode == HTTP_CODE_OK || httpCode == HTTP_CODE_CREATED) {
            responsePayload = http.getString();
            success = true;
            Serial.printf("API Request successful on attempt %d, HTTP Code: %d\n", attempt, httpCode);
        } else {
            Serial.printf("API Request failed on attempt %d, HTTP Code: %d (%s)\n", 
                         attempt, httpCode, http.errorToString(httpCode).c_str());
            
            // Don't retry on certain error codes (client errors)
            if (httpCode >= 400 && httpCode < 500 && httpCode != 408 && httpCode != 429) {
                Serial.println("Client error detected, stopping retries");
                break;
            }
            
            // Wait before retry (except on last attempt)
            if (attempt < MAX_RETRIES) {
                Serial.printf("Waiting %dms before retry...\n", RETRY_DELAY_MS);
                http.end();
                vTaskDelay(RETRY_DELAY_MS / portTICK_PERIOD_MS);
                continue;
            }
        }
        
        http.end();
    }

    // Process successful response
    if (success) {
        Serial.println("Spoolman Abfrage erfolgreich");

        // Restgewicht der Spule auslesen
        JsonDocument doc;
        DeserializationError error = deserializeJson(doc, responsePayload);
        if (error) {
            Serial.print("Fehler beim Parsen der JSON-Antwort: ");
            Serial.println(error.c_str());
        } else {
            switch(requestType){
            case API_REQUEST_SPOOL_WEIGHT_UPDATE:
                remainingWeight = doc["remaining_weight"].as<uint16_t>();
                Serial.print("Aktuelles Gewicht: ");
                Serial.println(remainingWeight);
                //oledShowMessage("Remaining: " + String(remaining_weight) + "g");
                if(!octoEnabled){
                    // TBD: Do not use Strings...
                    //oledShowProgressBar(1, 1, "Spool Tag", ("Done: " + String(remainingWeight) + " g remain").c_str());
                    oledShowMessage("Remaining: " + String(remainingWeight) + "g");
                    remainingWeight = 0;
                }else{
                    // ocoto is enabled, trigger octo update
                    sendOctoUpdate = true;
                }
                break;
            case API_REQUEST_SPOOL_LOCATION_UPDATE:
                oledShowProgressBar(1, 1, "Loc. Tag", "Done!");
                break;
            case API_REQUEST_SPOOL_TAG_ID_UPDATE:
                oledShowProgressBar(1, 1, "Write Tag", "Done!");
                break;
            case API_REQUEST_OCTO_SPOOL_UPDATE:
                // TBD: Do not use Strings...
                //oledShowProgressBar(5, 5, "Spool Tag", ("Done: " + String(remainingWeight) + " g remain").c_str());
                oledShowMessage("Remaining: " + String(remainingWeight) + "g");
                remainingWeight = 0;
                break;
            case API_REQUEST_VENDOR_CREATE:
                Serial.println("Vendor successfully created!");
                createdVendorId = doc["id"].as<uint16_t>();
                Serial.print("Created Vendor ID: ");
                Serial.println(createdVendorId);
                oledShowProgressBar(1, 1, "Vendor", "Created!");
                break;
            case API_REQUEST_VENDOR_CHECK:
                if (doc.isNull() || doc.size() == 0) {
                    Serial.println("Vendor not found in response");
                    foundVendorId = 0;
                } else {
                    foundVendorId = doc[0]["id"].as<uint16_t>();
                    Serial.print("Found Vendor ID: ");
                    Serial.println(foundVendorId);
                }
                break;
            case API_REQUEST_FILAMENT_CHECK:
                if (doc.isNull() || doc.size() == 0) {
                    Serial.println("Filament not found in response");
                    foundFilamentId = 0;
                } else {
                    foundFilamentId = doc[0]["id"].as<uint16_t>();
                    Serial.print("Found Filament ID: ");
                    Serial.println(foundFilamentId);
                }
                break;
            case API_REQUEST_FILAMENT_CREATE:
                Serial.println("Filament successfully created!");
                createdFilamentId = doc["id"].as<uint16_t>();
                Serial.print("Created Filament ID: ");
                Serial.println(createdFilamentId);
                oledShowProgressBar(1, 1, "Filament", "Created!");
                break;
            case API_REQUEST_SPOOL_CREATE:
                Serial.println("Spool successfully created!");
                createdSpoolId = doc["id"].as<uint16_t>();
                Serial.print("Created Spool ID: ");
                Serial.println(createdSpoolId);
                oledShowProgressBar(1, 1, "Spool", "Created!");
                break;
            }
        }
        doc.clear();
    } else if (httpCode == HTTP_CODE_CREATED) {
        Serial.println("Spoolman erfolgreich erstellt");
        
        // Parse response for created resources  
        JsonDocument doc;
        DeserializationError error = deserializeJson(doc, responsePayload);
        if (error) {
            Serial.print("Fehler beim Parsen der JSON-Antwort: ");
            Serial.println(error.c_str());
        } else {
            switch(requestType){
            case API_REQUEST_VENDOR_CREATE:
                Serial.println("Vendor successfully created!");
                createdVendorId = doc["id"].as<uint16_t>();
                Serial.print("Created Vendor ID: ");
                Serial.println(createdVendorId);
                oledShowProgressBar(1, 1, "Vendor", "Created!");
                break;
            case API_REQUEST_FILAMENT_CREATE:
                Serial.println("Filament successfully created!");
                createdFilamentId = doc["id"].as<uint16_t>();
                Serial.print("Created Filament ID: ");
                Serial.println(createdFilamentId);
                oledShowProgressBar(1, 1, "Filament", "Created!");
                break;
            case API_REQUEST_SPOOL_CREATE:
                Serial.println("Spool successfully created!");
                createdSpoolId = doc["id"].as<uint16_t>();
                Serial.print("Created Spool ID: ");
                Serial.println(createdSpoolId);
                oledShowProgressBar(1, 1, "Spool", "Created!");
                break;
            default:
                // Handle other create operations if needed
                break;
            }
        }
        doc.clear();

        // Execute weight update if requested and tag update was successful
        if (triggerWeightUpdate && requestType == API_REQUEST_SPOOL_TAG_ID_UPDATE && weightValue > 10) {
            Serial.println("Executing weight update after successful tag update");
            
            // Prepare weight update request
            String weightUrl = spoolmanUrl + apiUrl + "/spool/" + spoolIdForWeight + "/measure";
            JsonDocument weightDoc;
            weightDoc["weight"] = weightValue;
            
            String weightPayload;
            serializeJson(weightDoc, weightPayload);
            
            Serial.print("Weight update URL: ");
            Serial.println(weightUrl);
            Serial.print("Weight update payload: ");
            Serial.println(weightPayload);

            // Execute weight update
            HTTPClient weightHttp;
            weightHttp.setReuse(false);
            weightHttp.setTimeout(HTTP_TIMEOUT_MS);
            weightHttp.begin(weightUrl);
            weightHttp.addHeader("Content-Type", "application/json");
            
            int weightHttpCode = weightHttp.PUT(weightPayload);
            
            if (weightHttpCode == HTTP_CODE_OK) {
                Serial.println("Weight update successful");
                String weightResponse = weightHttp.getString();
                JsonDocument weightResponseDoc;
                DeserializationError weightError = deserializeJson(weightResponseDoc, weightResponse);
                
                if (!weightError) {
                    remainingWeight = weightResponseDoc["remaining_weight"].as<uint16_t>();
                    Serial.print("Updated weight: ");
                    Serial.println(remainingWeight);
                    
                    if (!octoEnabled) {
                        oledShowProgressBar(1, 1, "Spool Tag", ("Done: " + String(remainingWeight) + " g remain").c_str());
                        remainingWeight = 0;
                    } else {
                        sendOctoUpdate = true;
                    }
                }
                weightResponseDoc.clear();
            } else {
                Serial.print("Weight update failed with HTTP code: ");
                Serial.println(weightHttpCode);
                oledShowProgressBar(1, 1, "Failure!", "Weight update");
            }
            
            weightHttp.end();
            weightDoc.clear();
        }
    } else {
        switch(requestType){
        case API_REQUEST_SPOOL_WEIGHT_UPDATE:
        case API_REQUEST_SPOOL_LOCATION_UPDATE:
        case API_REQUEST_SPOOL_TAG_ID_UPDATE:
            oledShowProgressBar(1, 1, "Failure!", "Spoolman update");
            break;
        case API_REQUEST_OCTO_SPOOL_UPDATE:
            oledShowProgressBar(1, 1, "Failure!", "Octoprint update");
            break;
        case API_REQUEST_BAMBU_UPDATE:
            oledShowProgressBar(1, 1, "Failure!", "Bambu update");
            break;
        case API_REQUEST_VENDOR_CHECK:
            oledShowProgressBar(1, 1, "Failure!", "Vendor check");
            foundVendorId = 0; // Set to 0 to indicate error/not found
            break;
        case API_REQUEST_VENDOR_CREATE:
            oledShowProgressBar(1, 1, "Failure!", "Vendor create");
            createdVendorId = 0; // Set to 0 to indicate error
            break;
        case API_REQUEST_FILAMENT_CHECK:
            oledShowProgressBar(1, 1, "Failure!", "Filament check");
            foundFilamentId = 0; // Set to 0 to indicate error/not found
            break;
        case API_REQUEST_FILAMENT_CREATE:
            oledShowProgressBar(1, 1, "Failure!", "Filament create");
            createdFilamentId = 0; // Set to 0 to indicate error
            break;
        case API_REQUEST_SPOOL_CREATE:
            oledShowProgressBar(1, 1, "Failure!", "Spool create");
            createdSpoolId = 0; // Set to 0 to indicate error instead of hanging
            break;
        }
        Serial.println("Fehler beim Senden an Spoolman! HTTP Code: " + String(httpCode));
        vTaskDelay(2000 / portTICK_PERIOD_MS);
        nfcReaderState = NFC_IDLE; // Reset NFC state to allow retry
    }

    vTaskDelay(50 / portTICK_PERIOD_MS);

    // Speicher freigeben
    delete params;
    HEAP_DEBUG_MESSAGE("sendToApi end");
    spoolmanApiState = API_IDLE;
    vTaskDelete(NULL);
}

bool updateSpoolTagId(String uidString, const char* payload) {
    oledShowProgressBar(2, 3, "Write Tag", "Update Spoolman");

    JsonDocument doc;
    DeserializationError error = deserializeJson(doc, payload);
    
    if (error) {
        Serial.print("Fehler beim JSON-Parsing: ");
        Serial.println(error.c_str());
        return false;
    }
    
    // Überprüfe, ob die erforderlichen Felder vorhanden sind
    if (!doc["sm_id"].is<String>() || doc["sm_id"].as<String>() == "") {
        Serial.println("Keine Spoolman-ID gefunden.");
        return false;
    }

    String spoolId = doc["sm_id"].as<String>();
    String spoolsUrl = spoolmanUrl + apiUrl + "/spool/" + spoolId;
    Serial.print("Update Spule mit URL: ");
    Serial.println(spoolsUrl);
    
    doc.clear();

    // Update Payload erstellen
    JsonDocument updateDoc;
    updateDoc["extra"]["nfc_id"] = "\""+uidString+"\"";
    
    String updatePayload;
    serializeJson(updateDoc, updatePayload);
    Serial.print("Update Payload: ");
    Serial.println(updatePayload);

    SendToApiParams* params = new SendToApiParams();  
    if (params == nullptr) {
        Serial.println("Fehler: Kann Speicher für Task-Parameter nicht allokieren.");
        return false;
    }
    params->requestType = API_REQUEST_SPOOL_TAG_ID_UPDATE;
    params->httpType = "PATCH";
    params->spoolsUrl = spoolsUrl;
    params->updatePayload = updatePayload;
    
    // Add weight update parameters for sequential execution
    params->triggerWeightUpdate = (weight > 10);
    params->spoolIdForWeight = spoolId;
    params->weightValue = weight;

    // Erstelle die Task mit erhöhter Stackgröße für zusätzliche HTTP-Anfrage
    BaseType_t result = xTaskCreate(
        sendToApi,                // Task-Funktion
        "SendToApiTask",          // Task-Name
        8192,                     // Erhöhte Stackgröße für zusätzliche HTTP-Anfrage
        (void*)params,            // Parameter
        0,                        // Priorität
        apiTask                   // Task-Handle (nicht benötigt)
    );

    updateDoc.clear();

    // Update Spool weight now handled sequentially in sendToApi task
    // to prevent parallel API access issues

    return true;
}

uint8_t updateSpoolWeight(String spoolId, uint16_t weight) {
    HEAP_DEBUG_MESSAGE("updateSpoolWeight begin");
    oledShowProgressBar(3, octoEnabled?5:4, "Spool Tag", "Spoolman update");
    String spoolsUrl = spoolmanUrl + apiUrl + "/spool/" + spoolId + "/measure";
    Serial.print("Update Spule mit URL: ");
    Serial.println(spoolsUrl);

    // Update Payload erstellen
    JsonDocument updateDoc;
    updateDoc["weight"] = weight;
    
    String updatePayload;
    serializeJson(updateDoc, updatePayload);
    Serial.print("Update Payload: ");
    Serial.println(updatePayload);

    SendToApiParams* params = new SendToApiParams();
    if (params == nullptr) {
        // TBD: reset ESP instead of showing a message
        Serial.println("Fehler: Kann Speicher für Task-Parameter nicht allokieren.");
        return 0;
    }
    params->requestType = API_REQUEST_SPOOL_WEIGHT_UPDATE;
    params->httpType = "PUT";
    params->spoolsUrl = spoolsUrl;
    params->updatePayload = updatePayload;

    // Erstelle die Task
    BaseType_t result = xTaskCreate(
        sendToApi,                // Task-Funktion
        "SendToApiTask",          // Task-Name
        6144,                     // Stackgröße in Bytes
        (void*)params,            // Parameter
        0,                        // Priorität
        apiTask                      // Task-Handle (nicht benötigt)
    );

    updateDoc.clear();
    HEAP_DEBUG_MESSAGE("updateSpoolWeight end");

    return 1;
}

uint8_t updateSpoolLocation(String spoolId, String location){
    HEAP_DEBUG_MESSAGE("updateSpoolLocation begin");

    oledShowProgressBar(3, octoEnabled?5:4, "Loc. Tag", "Spoolman update");

    String spoolsUrl = spoolmanUrl + apiUrl + "/spool/" + spoolId;
    Serial.print("Update Spule mit URL: ");
    Serial.println(spoolsUrl);

    // Update Payload erstellen
    JsonDocument updateDoc;
    updateDoc["location"] = location;
    
    String updatePayload;
    serializeJson(updateDoc, updatePayload);
    Serial.print("Update Payload: ");
    Serial.println(updatePayload);

    SendToApiParams* params = new SendToApiParams();
    if (params == nullptr) {
        Serial.println("Fehler: Kann Speicher für Task-Parameter nicht allokieren.");
        return 0;
    }
    params->requestType = API_REQUEST_SPOOL_LOCATION_UPDATE;
    params->httpType = "PATCH";
    params->spoolsUrl = spoolsUrl;
    params->updatePayload = updatePayload;


    if(apiTask == nullptr){
        // Erstelle die Task
        BaseType_t result = xTaskCreate(
            sendToApi,                // Task-Funktion
            "SendToApiTask",          // Task-Name
            6144,                     // Stackgröße in Bytes
            (void*)params,            // Parameter
            0,                        // Priorität
            apiTask                   // Task-Handle
        );
    }else{
        Serial.println("Not spawning new task, API still active!");
    }

    updateDoc.clear();

    HEAP_DEBUG_MESSAGE("updateSpoolLocation end");
    return 1;
}

bool updateSpoolOcto(int spoolId) {
    oledShowProgressBar(4, octoEnabled?5:4, "Spool Tag", "Octoprint update");

    String spoolsUrl = octoUrl + "/plugin/Spoolman/selectSpool";
    Serial.print("Update Spule in Octoprint mit URL: ");
    Serial.println(spoolsUrl);

    JsonDocument updateDoc;
    updateDoc["spool_id"] = spoolId;
    updateDoc["tool"] = "tool0";

    String updatePayload;
    serializeJson(updateDoc, updatePayload);
    Serial.print("Update Payload: ");
    Serial.println(updatePayload);

    SendToApiParams* params = new SendToApiParams();
    if (params == nullptr) {
        Serial.println("Fehler: Kann Speicher für Task-Parameter nicht allokieren.");
        return false;
    }
    params->requestType = API_REQUEST_OCTO_SPOOL_UPDATE;
    params->httpType = "POST";
    params->spoolsUrl = spoolsUrl;
    params->updatePayload = updatePayload;
    params->octoToken = octoToken;

    // Erstelle die Task
    BaseType_t result = xTaskCreate(
        sendToApi,                // Task-Funktion
        "SendToApiTask",          // Task-Name
        6144,                     // Stackgröße in Bytes
        (void*)params,            // Parameter
        0,                        // Priorität
        apiTask                      // Task-Handle (nicht benötigt)
    );

    updateDoc.clear();

    return true;
}

bool updateSpoolBambuData(String payload) {
    JsonDocument doc;
    DeserializationError error = deserializeJson(doc, payload);
    if (error) {
        Serial.print("Fehler beim JSON-Parsing: ");
        Serial.println(error.c_str());
        return false;
    }

    String spoolsUrl = spoolmanUrl + apiUrl + "/filament/" + doc["filament_id"].as<String>();
    Serial.print("Update Spule mit URL: ");
    Serial.println(spoolsUrl);

    JsonDocument updateDoc;
    updateDoc["extra"]["bambu_setting_id"] = "\"" + doc["setting_id"].as<String>() + "\"";
    updateDoc["extra"]["bambu_cali_id"] = "\"" + doc["cali_idx"].as<String>() + "\"";
    updateDoc["extra"]["bambu_idx"] = "\"" + doc["tray_info_idx"].as<String>() + "\"";
    updateDoc["extra"]["nozzle_temperature"] = "[" + doc["temp_min"].as<String>() + "," + doc["temp_max"].as<String>() + "]";

    String updatePayload;
    serializeJson(updateDoc, updatePayload);

    doc.clear();
    updateDoc.clear();

    Serial.print("Update Payload: ");
    Serial.println(updatePayload);

    SendToApiParams* params = new SendToApiParams();
    if (params == nullptr) {
        Serial.println("Fehler: Kann Speicher für Task-Parameter nicht allokieren.");
        return false;
    }
    params->requestType = API_REQUEST_BAMBU_UPDATE;
    params->httpType = "PATCH";
    params->spoolsUrl = spoolsUrl;
    params->updatePayload = updatePayload;

    // Erstelle die Task
    BaseType_t result = xTaskCreate(
        sendToApi,                // Task-Funktion
        "SendToApiTask",          // Task-Name
        6144,                     // Stackgröße in Bytes
        (void*)params,            // Parameter
        0,                        // Priorität
        apiTask                      // Task-Handle (nicht benötigt)
    );

    return true;
}

// #### Brand Filament
uint16_t createVendor(const JsonDocument& payload) {
    oledShowProgressBar(2, 5, "New Brand", "Create new Vendor");

    // Create new vendor in Spoolman database using task system
    // Note: Due to async nature, the ID will be stored in createdVendorId global variable
    // Note: This function assumes that the caller has already ensured API is IDLE
    createdVendorId = 65535; // Reset previous value
    
    String spoolsUrl = spoolmanUrl + apiUrl + "/vendor";
    Serial.print("Create vendor with URL: ");
    Serial.println(spoolsUrl);

    // Create JSON payload for vendor creation
    JsonDocument vendorDoc;
    vendorDoc["name"] = payload["b"].as<String>();
    
    // Extract domain from URL if present, otherwise use brand name
    String externalId = "";
    if (payload["u"].is<String>()) {
        String url = payload["u"].as<String>();
        // Extract domain from URL (e.g., "https://www.blubb.de/f1234/?suche=irgendwas" -> "https://www.blubb.de")
        int protocolEnd = url.indexOf("://");
        if (protocolEnd != -1) {
            int pathStart = url.indexOf("/", protocolEnd + 3);
            externalId = (pathStart != -1) ? url.substring(0, pathStart) : url;
        } else {
            externalId = url; // No protocol found, use as is
        }
    } else {
        externalId = payload["b"].as<String>();
    }
    vendorDoc["comment"] = externalId;

    String vendorPayload;
    serializeJson(vendorDoc, vendorPayload);
    Serial.print("Vendor Payload: ");
    Serial.println(vendorPayload);

    SendToApiParams* params = new SendToApiParams();
    if (params == nullptr) {
        Serial.println("Fehler: Kann Speicher für Task-Parameter nicht allokieren.");
        vendorDoc.clear();
        return 0;
    }
    params->requestType = API_REQUEST_VENDOR_CREATE;
    params->httpType = "POST";
    params->spoolsUrl = spoolsUrl;
    params->updatePayload = vendorPayload;

    // Create task without additional API state check since caller ensures synchronization
    BaseType_t result = xTaskCreate(
        sendToApi,                // Task-Funktion
        "SendToApiTask",          // Task-Name
        6144,                     // Stackgröße in Bytes
        (void*)params,            // Parameter
        0,                        // Priorität
        NULL                      // Task-Handle (nicht benötigt)
    );

    if (result != pdPASS) {
        Serial.println("Failed to create vendor task!");
        delete params;
        vendorDoc.clear();
        return 0;
    }

    vendorDoc.clear();
    
    // Delay for Display Bar
    vTaskDelay(1000 / portTICK_PERIOD_MS);

    // Wait for task completion and return the created vendor ID
    // Note: createdVendorId will be set by sendToApi when response is received
    while(createdVendorId == 65535) {
        vTaskDelay(50 / portTICK_PERIOD_MS);
    }
    
    return createdVendorId;
}

uint16_t checkVendor(const JsonDocument& payload) {
    oledShowProgressBar(1, 5, "New Brand", "Check Vendor");

    // Check if vendor exists using task system
    foundVendorId = 65535; // Reset to invalid value to detect when API response is received
    
    String vendorName = payload["b"].as<String>();
    vendorName.trim();
    vendorName.replace(" ", "+");
    String spoolsUrl = spoolmanUrl + apiUrl + "/vendor?name=" + vendorName;
    Serial.print("Check vendor with URL: ");
    Serial.println(spoolsUrl);

    SendToApiParams* params = new SendToApiParams();
    if (params == nullptr) {
        Serial.println("Fehler: Kann Speicher für Task-Parameter nicht allokieren.");
        return 0;
    }
    params->requestType = API_REQUEST_VENDOR_CHECK;
    params->httpType = "GET";
    params->spoolsUrl = spoolsUrl;
    params->updatePayload = ""; // Empty for GET request

    // Check if API is idle before creating task
    while (spoolmanApiState != API_IDLE)
    {
        vTaskDelay(100 / portTICK_PERIOD_MS);
    }
    
    // Erstelle die Task
    BaseType_t result = xTaskCreate(
        sendToApi,                // Task-Funktion
        "SendToApiTask",          // Task-Name
        6144,                     // Stackgröße in Bytes
        (void*)params,            // Parameter
        0,                        // Priorität
        NULL                      // Task-Handle (nicht benötigt)
    );
    
    // Wait until foundVendorId is updated by the API response (not 65535 anymore)
    while (foundVendorId == 65535)
    {
        vTaskDelay(50 / portTICK_PERIOD_MS);
    }

    // Check if vendor was found
    if (foundVendorId == 0) {
        Serial.println("Vendor not found, creating new vendor...");
        uint16_t vendorId = createVendor(payload);
        if (vendorId == 0) {
            Serial.println("Failed to create vendor, returning 0.");
            return 0; // Failed to create vendor
        } else {
            Serial.println("Vendor created with ID: " + String(vendorId));
            return vendorId;
        }
    } else {
        Serial.println("Vendor found: " + payload["b"].as<String>());
        Serial.print("Vendor ID: ");
        Serial.println(foundVendorId);
        return foundVendorId;
    }
}

uint16_t createFilament(uint16_t vendorId, const JsonDocument& payload) {
    oledShowProgressBar(4, 5, "New Brand", "Create Filament");

    // Create new filament in Spoolman database using task system
    // Note: Due to async nature, the ID will be stored in createdFilamentId global variable
    // Note: This function assumes that the caller has already ensured API is IDLE
    createdFilamentId = 65535; // Reset previous value
    
    String spoolsUrl = spoolmanUrl + apiUrl + "/filament";
    Serial.print("Create filament with URL: ");
    Serial.println(spoolsUrl);

    // Create JSON payload for filament creation
    JsonDocument filamentDoc;
    filamentDoc["name"] = payload["cn"].as<String>();
    filamentDoc["vendor_id"] = String(vendorId);
    filamentDoc["material"] = payload["t"].as<String>();
    filamentDoc["density"] = (payload["de"].is<String>() && payload["de"].as<String>().length() > 0) ? payload["de"].as<String>() : "1.24";
    filamentDoc["diameter"] = (payload["di"].is<String>() && payload["di"].as<String>().length() > 0) ? payload["di"].as<String>() : "1.75";
    filamentDoc["weight"] = String(weight);
    filamentDoc["spool_weight"] = payload["sw"].as<String>();
    filamentDoc["article_number"] = payload["an"].as<String>();
    filamentDoc["settings_extruder_temp"] = payload["et"].is<String>() ? payload["et"].as<String>() : "";
    filamentDoc["settings_bed_temp"] = payload["bt"].is<String>() ? payload["bt"].as<String>() : "";

    if (payload["an"].is<String>())
    {
        filamentDoc["external_id"] = payload["an"].as<String>();
        filamentDoc["comment"] = payload["u"].is<String>() ? payload["u"].as<String>() + payload["an"].as<String>() : "automatically generated";
    }
    else
    {
        filamentDoc["comment"] = payload["u"].is<String>() ? payload["u"].as<String>() : "automatically generated";
    }

    if (payload["mc"].is<String>()) {
        filamentDoc["multi_color_hexes"] = payload["mc"].as<String>();
        filamentDoc["multi_color_direction"] = payload["mcd"].is<String>() ? payload["mcd"].as<String>() : "";
    }
    else
    {
        filamentDoc["color_hex"] = (payload["c"].is<String>() && payload["c"].as<String>().length() >= 6) ? payload["c"].as<String>() : "FFFFFF";
    }

    String filamentPayload;
    serializeJson(filamentDoc, filamentPayload);
    Serial.print("Filament Payload: ");
    Serial.println(filamentPayload);

    SendToApiParams* params = new SendToApiParams();
    if (params == nullptr) {
        Serial.println("Fehler: Kann Speicher für Task-Parameter nicht allokieren.");
        filamentDoc.clear();
        return 0;
    }
    params->requestType = API_REQUEST_FILAMENT_CREATE;
    params->httpType = "POST";
    params->spoolsUrl = spoolsUrl;
    params->updatePayload = filamentPayload;

    // Create task without additional API state check since caller ensures synchronization
    BaseType_t result = xTaskCreate(
        sendToApi,                // Task-Funktion
        "SendToApiTask",          // Task-Name
        6144,                     // Stackgröße in Bytes
        (void*)params,            // Parameter
        0,                        // Priorität
        NULL                      // Task-Handle (nicht benötigt)
    );

    if (result != pdPASS) {
        Serial.println("Failed to create filament task!");
        delete params;
        filamentDoc.clear();
        return 0;
    }

    filamentDoc.clear();
    
    // Delay for Display Bar
    vTaskDelay(1000 / portTICK_PERIOD_MS);

    // Wait for task completion and return the created filament ID
    // Note: createdFilamentId will be set by sendToApi when response is received
    while(createdFilamentId == 65535) {
        vTaskDelay(50 / portTICK_PERIOD_MS);
    }
    
    return createdFilamentId;
}

uint16_t checkFilament(uint16_t vendorId, const JsonDocument& payload) {
    oledShowProgressBar(3, 5, "New Brand", "Check Filament");

    // Check if filament exists using task system
    foundFilamentId = 65535; // Reset to invalid value to detect when API response is received

    String spoolsUrl = spoolmanUrl + apiUrl + "/filament?vendor.id=" + String(vendorId) + "&external_id=" + String(payload["artnr"].as<String>());
    Serial.print("Check filament with URL: ");
    Serial.println(spoolsUrl);

    SendToApiParams* params = new SendToApiParams();
    if (params == nullptr) {
        Serial.println("Fehler: Kann Speicher für Task-Parameter nicht allokieren.");
        return 0;
    }
    params->requestType = API_REQUEST_FILAMENT_CHECK;
    params->httpType = "GET";
    params->spoolsUrl = spoolsUrl;
    params->updatePayload = ""; // Empty for GET request

     // Erstelle die Task
    BaseType_t result = xTaskCreate(
        sendToApi,                // Task-Funktion
        "SendToApiTask",          // Task-Name
        6144,                     // Stackgröße in Bytes
        (void*)params,            // Parameter
        0,                        // Priorität
        NULL                      // Task-Handle (nicht benötigt)
    );
    
    // Wait until foundFilamentId is updated by the API response (not 65535 anymore)
    while (foundFilamentId == 65535) {
        vTaskDelay(50 / portTICK_PERIOD_MS);
    }

    // Check if filament was found
    if (foundFilamentId == 0) {
        Serial.println("Filament not found, creating new filament...");
        uint16_t filamentId = createFilament(vendorId, payload);
        if (filamentId == 0) {
            Serial.println("Failed to create filament, returning 0.");
            return 0; // Failed to create filament
        } else {
            Serial.println("Filament created with ID: " + String(filamentId));
            return filamentId;
        }
    } else {
        Serial.println("Filament found for vendor ID: " + String(vendorId));
        Serial.print("Filament ID: ");
        Serial.println(foundFilamentId);
        return foundFilamentId;
    }
}

uint16_t createSpool(uint16_t vendorId, uint16_t filamentId, JsonDocument& payload, String uidString) {
    oledShowProgressBar(5, 5, "New Brand", "Create new Spool");

    // Create new spool in Spoolman database using task system
    // Note: Due to async nature, the ID will be stored in createdSpoolId global variable
    // Note: This function assumes that the caller has already ensured API is IDLE
    createdSpoolId = 65535; // Reset to invalid value to detect when API response is received
    
    String spoolsUrl = spoolmanUrl + apiUrl + "/spool";
    Serial.print("Create spool with URL: ");
    Serial.println(spoolsUrl);

    // Create JSON payload for spool creation
    JsonDocument spoolDoc;
    spoolDoc["filament_id"] = String(filamentId);
    spoolDoc["initial_weight"] = weight > 10 ? String(weight - payload["sw"].as<int>()) : "1000";
    spoolDoc["spool_weight"] = (payload["sw"].is<String>() && payload["sw"].as<String>().length() > 0) ? payload["sw"].as<String>() : "180";
    spoolDoc["remaining_weight"] = spoolDoc["initial_weight"];
    spoolDoc["lot_nr"] = (payload["an"].is<String>() && payload["an"].as<String>().length() > 0) ? payload["an"].as<String>() : "";
    spoolDoc["comment"] = "automatically generated";
    spoolDoc["extra"]["nfc_id"] = "\"" + uidString + "\"";

    String spoolPayload;
    serializeJson(spoolDoc, spoolPayload);
    Serial.print("Spool Payload: ");
    Serial.println(spoolPayload);
    spoolDoc.clear();

    SendToApiParams* params = new SendToApiParams();
    if (params == nullptr) {
        Serial.println("Fehler: Kann Speicher für Task-Parameter nicht allokieren.");
        spoolDoc.clear();
        return 0;
    }
    params->requestType = API_REQUEST_SPOOL_CREATE;
    params->httpType = "POST";
    params->spoolsUrl = spoolsUrl;
    params->updatePayload = spoolPayload;

    // Create task without additional API state check since caller ensures synchronization
    BaseType_t result = xTaskCreate(
        sendToApi,                // Task-Funktion
        "SendToApiTask",          // Task-Name
        6144,                     // Stackgröße in Bytes
        (void*)params,            // Parameter
        0,                        // Priorität
        NULL                      // Task-Handle (nicht benötigt)
    );

    if (result != pdPASS) {
        Serial.println("Failed to create spool task!");
        delete params;
        return 0;
    }
    
    // Wait for task completion and return the created spool ID
    // Note: createdSpoolId will be set by sendToApi when response is received
    while(createdSpoolId == 65535) {
        vTaskDelay(50 / portTICK_PERIOD_MS);
    }
    
    // Check if spool creation was successful
    if (createdSpoolId == 0) {
        Serial.println("ERROR: Spool creation failed");
        nfcReaderState = NFC_IDLE; // Reset NFC state
        return 0;
    }

    // Write data to tag with startWriteJsonToTag
    // void startWriteJsonToTag(const bool isSpoolTag, const char* payload);
    
    // Create optimized JSON structure with sm_id at the beginning for fast-path detection
    JsonDocument optimizedPayload;
    optimizedPayload["sm_id"] = String(createdSpoolId);  // Place sm_id first for fast scanning
    optimizedPayload["b"] = payload["b"].as<String>();
    optimizedPayload["cn"] = payload["an"].as<String>();
    
    String payloadString;
    serializeJson(optimizedPayload, payloadString);
    
    Serial.println("Optimized JSON with sm_id first:");
    Serial.println(payloadString);
    
    optimizedPayload.clear();
    
    nfcReaderState = NFC_IDLE;

    // Delay for Display Bar
    vTaskDelay(1000 / portTICK_PERIOD_MS);
    
    startWriteJsonToTag(true, payloadString.c_str());

    return createdSpoolId;
}

bool createBrandFilament(JsonDocument& payload, String uidString) {
    uint16_t vendorId = checkVendor(payload);
    if (vendorId == 0) {
        Serial.println("ERROR: Failed to create/find vendor");
        return false;
    }
    
    uint16_t filamentId = checkFilament(vendorId, payload);
    if (filamentId == 0) {
        Serial.println("ERROR: Failed to create/find filament");
        return false;
    }
    
    uint16_t spoolId = createSpool(vendorId, filamentId, payload, uidString);
    if (spoolId == 0) {
        Serial.println("ERROR: Failed to create spool");
        return false;
    }
    
    Serial.println("SUCCESS: Brand filament created with Spool ID: " + String(spoolId));
    return true;
}

// #### Spoolman init
bool checkSpoolmanExtraFields() {
    // Only check extra fields if they have not been checked before
    if(!spoolmanExtraFieldsChecked){
        HTTPClient http;
        String checkUrls[] = {
            spoolmanUrl + apiUrl + "/field/spool",
            spoolmanUrl + apiUrl + "/field/filament"
        };

        String spoolExtra[] = {
            "nfc_id"
        };

        String filamentExtra[] = {
            "nozzle_temperature",
            "price_meter",
            "price_gramm",
            "bambu_setting_id",
            "bambu_cali_id",
            "bambu_idx",
            "bambu_k",
            "bambu_flow_ratio",
            "bambu_max_volspeed"
        };

        String spoolExtraFields[] = {
            "{\"name\": \"NFC ID\","
            "\"key\": \"nfc_id\","
            "\"field_type\": \"text\"}"
        };

        String filamentExtraFields[] = {
            "{\"name\": \"Nozzle Temp\","
            "\"unit\": \"°C\","
            "\"field_type\": \"integer_range\","
            "\"default_value\": \"[190,230]\","
            "\"key\": \"nozzle_temperature\"}",

            "{\"name\": \"Price/m\","
            "\"unit\": \"€\","
            "\"field_type\": \"float\","
            "\"key\": \"price_meter\"}",
            
            "{\"name\": \"Price/g\","
            "\"unit\": \"€\","
            "\"field_type\": \"float\","
            "\"key\": \"price_gramm\"}",

            "{\"name\": \"Bambu Setting ID\","
            "\"field_type\": \"text\","
            "\"key\": \"bambu_setting_id\"}",

            "{\"name\": \"Bambu Cali ID\","
            "\"field_type\": \"text\","
            "\"key\": \"bambu_cali_id\"}",

            "{\"name\": \"Bambu Filament IDX\","
            "\"field_type\": \"text\","
            "\"key\": \"bambu_idx\"}",

            "{\"name\": \"Bambu k\","
            "\"field_type\": \"float\","
            "\"key\": \"bambu_k\"}",

            "{\"name\": \"Bambu Flow Ratio\","
            "\"field_type\": \"float\","
            "\"key\": \"bambu_flow_ratio\"}",

            "{\"name\": \"Bambu Max Vol. Speed\","
            "\"unit\": \"mm3/s\","
            "\"field_type\": \"integer\","
            "\"default_value\": \"12\","
            "\"key\": \"bambu_max_volspeed\"}"
        };

        Serial.println("Überprüfe Extrafelder...");

        int urlLength = sizeof(checkUrls) / sizeof(checkUrls[0]);

        for (uint8_t i = 0; i < urlLength; i++) {
            Serial.println();
            Serial.println("-------- Prüfe Felder für "+checkUrls[i]+" --------");
            http.begin(checkUrls[i]);
            int httpCode = http.GET();
        
            if (httpCode == HTTP_CODE_OK) {
                String payload = http.getString();
                JsonDocument doc;
                DeserializationError error = deserializeJson(doc, payload);
                if (!error) {
                    String* extraFields;
                    String* extraFieldData;
                    u16_t extraLength;

                    if (i == 0) {
                        extraFields = spoolExtra;
                        extraFieldData = spoolExtraFields;
                        extraLength = sizeof(spoolExtra) / sizeof(spoolExtra[0]);
                    } else {
                        extraFields = filamentExtra;
                        extraFieldData = filamentExtraFields;
                        extraLength = sizeof(filamentExtra) / sizeof(filamentExtra[0]);
                    }

                    for (uint8_t s = 0; s < extraLength; s++) {
                        bool found = false;
                        for (JsonObject field : doc.as<JsonArray>()) {
                            if (field["key"].is<String>() && field["key"] == extraFields[s]) {
                                Serial.println("Feld gefunden: " + extraFields[s]);
                                found = true;
                                break;
                            }
                        }
                        if (!found) {
                            Serial.println("Feld nicht gefunden: " + extraFields[s]);

                            // Extrafeld hinzufügen
                            http.begin(checkUrls[i] + "/" + extraFields[s]);
                            http.addHeader("Content-Type", "application/json");
                            int httpCode = http.POST(extraFieldData[s]);

                            if (httpCode > 0) {
                                // Antwortscode und -nachricht abrufen
                                String response = http.getString();
                                //Serial.println("HTTP-Code: " + String(httpCode));
                                //Serial.println("Antwort: " + response);
                                if (httpCode != HTTP_CODE_OK) {

                                    return false;
                                }
                            } else {
                                // Fehler beim Senden der Anfrage
                                Serial.println("Fehler beim Senden der Anfrage: " + String(http.errorToString(httpCode)));
                                return false;
                            }
                            //http.end();
                        }
                        yield();
                        vTaskDelay(100 / portTICK_PERIOD_MS);
                    }
                }
                doc.clear();
            }
        }
        
        Serial.println("-------- ENDE Prüfe Felder --------");
        Serial.println();

        http.end();

        spoolmanExtraFieldsChecked = true;
        return true;
    }else{
        return true;
    }
}

bool checkSpoolmanInstance() {
    HTTPClient http;
    bool returnValue = false;

    // Only do the spoolman instance check if there is no active API request going on
    if(spoolmanApiState == API_IDLE){
        spoolmanApiState = API_TRANSMITTING;
        String healthUrl = spoolmanUrl + apiUrl + "/health";

        Serial.print("Checking spoolman instance: ");
        Serial.println(healthUrl);

        http.begin(healthUrl);
        int httpCode = http.GET();

        if (httpCode > 0) {
            if (httpCode == HTTP_CODE_OK) {
                String payload = http.getString();
                JsonDocument doc;
                DeserializationError error = deserializeJson(doc, payload);
                if (!error && doc["status"].is<String>()) {
                    const char* status = doc["status"];
                    http.end();

                    if (!checkSpoolmanExtraFields()) {
                        Serial.println("Fehler beim Überprüfen der Extrafelder.");

                        // TBD
                        oledShowMessage("Spoolman Error creating Extrafields");
                        vTaskDelay(2000 / portTICK_PERIOD_MS);
                        
                        return false;
                    }

                    spoolmanApiState = API_IDLE;
                    oledShowTopRow();
                    spoolmanConnected = true;
                    returnValue = strcmp(status, "healthy") == 0;
                }else{
                    spoolmanConnected = false;
                }

                doc.clear();
            }else{
                spoolmanConnected = false;
            }
        } else {
            spoolmanConnected = false;
            Serial.println("Error contacting spoolman instance! HTTP Code: " + String(httpCode));
        }
        http.end();
        spoolmanApiState = API_IDLE;
    }
    else
    {
        // If the check is skipped, return the previous status
        Serial.println("Skipping spoolman healthcheck, API is active.");
        returnValue = spoolmanConnected;
    }
    Serial.println("Healthcheck completed!");
    return returnValue;
}

bool saveSpoolmanUrl(const String& url, bool octoOn, const String& octo_url, const String& octoTk) {
    Preferences preferences;
    preferences.begin(NVS_NAMESPACE_API, false); // false = readwrite
    preferences.putString(NVS_KEY_SPOOLMAN_URL, url);
    preferences.putBool(NVS_KEY_OCTOPRINT_ENABLED, octoOn);
    preferences.putString(NVS_KEY_OCTOPRINT_URL, octo_url);
    preferences.putString(NVS_KEY_OCTOPRINT_TOKEN, octoTk);
    preferences.end();

    //TBD: This could be handled nicer in the future
    spoolmanExtraFieldsChecked = false;
    spoolmanUrl = url;
    octoEnabled = octoOn;
    octoUrl = octo_url;
    octoToken = octoTk;

    return checkSpoolmanInstance();
}

String loadSpoolmanUrl() {
    Preferences preferences;
    preferences.begin(NVS_NAMESPACE_API, true);
    String spoolmanUrl = preferences.getString(NVS_KEY_SPOOLMAN_URL, "");
    octoEnabled = preferences.getBool(NVS_KEY_OCTOPRINT_ENABLED, false);
    if(octoEnabled)
    {
        octoUrl = preferences.getString(NVS_KEY_OCTOPRINT_URL, "");
        octoToken = preferences.getString(NVS_KEY_OCTOPRINT_TOKEN, "");
    }
    preferences.end();
    return spoolmanUrl;
}

bool initSpoolman() {
    oledShowProgressBar(3, 7, DISPLAY_BOOT_TEXT, "Spoolman init");
    spoolmanUrl = loadSpoolmanUrl();
    
    bool success = checkSpoolmanInstance();
    if (!success) {
        Serial.println("Spoolman not available");
        return false;
    }

    oledShowTopRow();
    return true;
}