#include "bambu.h"
#include <ArduinoJson.h>
#include <PubSubClient.h>
#include <WiFiManager.h>
#include <SSLClient.h>
#include "bambu_cert.h"
#include "website.h"
#include "nfc.h"
#include "commonFS.h"
#include "esp_task_wdt.h"
#include "config.h"
#include "display.h"

WiFiClient espClient;
SSLClient sslClient(&espClient);
PubSubClient client(sslClient);

TaskHandle_t BambuMqttTask;

String report_topic = "";
//String request_topic = "";
const char* bambu_username = "bblp";
const char* bambu_ip = nullptr;
const char* bambu_accesscode = nullptr;
const char* bambu_serialnr = nullptr;
bool bambu_connected = false;

// Globale Variablen für AMS-Daten
int ams_count = 0;
String amsJsonData;  // Speichert das fertige JSON für WebSocket-Clients
AMSData ams_data[MAX_AMS];  // Definition des Arrays

bool saveBambuCredentials(const String& ip, const String& serialnr, const String& accesscode) {
    if (BambuMqttTask) {
        vTaskDelete(BambuMqttTask);
    }
    
    JsonDocument doc;
    doc["bambu_ip"] = ip;
    doc["bambu_accesscode"] = accesscode;
    doc["bambu_serialnr"] = serialnr;

    if (!saveJsonValue("/bambu_credentials.json", doc)) {
        Serial.println("Fehler beim Speichern der Bambu-Credentials.");
        return false;
    }

    // Dynamische Speicherallokation für die globalen Pointer
    bambu_ip = ip.c_str();
    bambu_accesscode = accesscode.c_str();
    bambu_serialnr = serialnr.c_str();

    vTaskDelay(100 / portTICK_PERIOD_MS);
    if (!setupMqtt()) return false;

    return true;
}

bool loadBambuCredentials() {
    JsonDocument doc;
    if (loadJsonValue("/bambu_credentials.json", doc) && doc["bambu_ip"].is<String>()) {
        // Temporäre Strings für die Werte
        String ip = doc["bambu_ip"].as<String>();
        String code = doc["bambu_accesscode"].as<String>();
        String serial = doc["bambu_serialnr"].as<String>();

        ip.trim();
        code.trim();
        serial.trim();

        // Dynamische Speicherallokation für die globalen Pointer
        bambu_ip = strdup(ip.c_str());
        bambu_accesscode = strdup(code.c_str());
        bambu_serialnr = strdup(serial.c_str());

        report_topic = "device/" + String(bambu_serialnr) + "/report";
        //request_topic = "device/" + String(bambu_serialnr) + "/request";
        return true;
    }
    Serial.println("Keine gültigen Bambu-Credentials gefunden.");
    return false;
}

String findFilamentIdx(String brand, String type) {
    // JSON-Dokument für die Filament-Daten erstellen
    JsonDocument doc;
    
    // Laden der bambu_filaments.json
    if (!loadJsonValue("/bambu_filaments.json", doc)) {
        Serial.println("Fehler beim Laden der Filament-Daten");
        return "GFL99"; // Fallback auf Generic PLA
    }

    String searchKey;
    
    // 1. Suche nach Brand + Type Kombination
    if (brand == "Bambu" || brand == "Bambulab") {
        searchKey = "Bambu " + type;
    } else if (brand == "PolyLite") {
        searchKey = "PolyLite " + type;
    } else if (brand == "eSUN") {
        searchKey = "eSUN " + type;
    } else if (brand == "Overture") {
        searchKey = "Overture " + type;
    } else if (brand == "PolyTerra") {
        searchKey = "PolyTerra " + type;
    }

    // Durchsuche alle Einträge nach der Brand + Type Kombination
    for (JsonPair kv : doc.as<JsonObject>()) {
        if (kv.value().as<String>() == searchKey) {
            return kv.key().c_str();
        }
    }

    // 2. Wenn nicht gefunden, suche nach Generic + Type
    searchKey = "Generic " + type;
    for (JsonPair kv : doc.as<JsonObject>()) {
        if (kv.value().as<String>() == searchKey) {
            return kv.key().c_str();
        }
    }

    // 3. Wenn immer noch nichts gefunden, gebe GFL99 zurück (Generic PLA)
    return "GFL99";
}

bool sendMqttMessage(String payload) {
    Serial.println("Sending MQTT message");
    Serial.println(payload);
    if (client.publish(report_topic.c_str(), payload.c_str())) 
    {
        return true;
    }
    
    return false;
}

bool setBambuSpool(String payload) {
    Serial.println("Spool settings in");
    Serial.println(payload);

    // Parse the JSON
    JsonDocument doc;
    DeserializationError error = deserializeJson(doc, payload);
    if (error) {
        Serial.print("Error parsing JSON: ");
        Serial.println(error.c_str());
        return false;
    }

    int amsId = doc["amsId"];
    int trayId = doc["trayId"];
    String color = doc["color"].as<String>();
    color.toUpperCase();
    int minTemp = doc["nozzle_temp_min"];
    int maxTemp = doc["nozzle_temp_max"];
    String type = doc["type"].as<String>();
    String brand = doc["brand"].as<String>();
    String tray_info_idx = (doc["tray_info_idx"].as<String>() != "-1") ? doc["tray_info_idx"].as<String>() : "";
    if (tray_info_idx == "") tray_info_idx = (brand != "" && type != "") ? findFilamentIdx(brand, type) : "";
    String setting_id = doc["bambu_setting_id"].as<String>();
    String cali_idx = doc["cali_idx"].as<String>();

    doc.clear();

    doc["print"]["sequence_id"] = 0;
    doc["print"]["command"] = "ams_filament_setting";
    doc["print"]["ams_id"] = amsId < 200 ? amsId : 255;
    doc["print"]["tray_id"] = trayId < 200 ? trayId : 254;
    doc["print"]["tray_color"] = color.length() == 8 ? color : color+"FF";
    doc["print"]["nozzle_temp_min"] = minTemp;
    doc["print"]["nozzle_temp_max"] = maxTemp;
    doc["print"]["tray_type"] = type;
    doc["print"]["cali_idx"] = (cali_idx != "") ? cali_idx : "";
    doc["print"]["tray_info_idx"] = tray_info_idx;
    doc["print"]["setting_id"] = setting_id;

    // Serialize the JSON
    String output;
    serializeJson(doc, output);

    if (sendMqttMessage(output)) {
        Serial.println("Spool successfully set");
    }
    else
    {
        Serial.println("Failed to set spool");
        return false;
    }
    
    doc.clear();
    yield();

    if (cali_idx != "") {
        yield();
        doc["print"]["sequence_id"] = 0;
        doc["print"]["command"] = "extrusion_cali_sel";
        doc["print"]["filament_id"] = tray_info_idx;
        doc["print"]["nozzle_diameter"] = "0.4";
        doc["print"]["cali_idx"] = cali_idx.toInt();
        doc["print"]["tray_id"] = trayId < 200 ? trayId : 254;
        doc["print"]["ams_id"] = amsId < 200 ? amsId : 255;

        // Serialize the JSON
        String output;
        serializeJson(doc, output);

        if (sendMqttMessage(output)) {
            Serial.println("Extrusion calibration successfully set");
        }
        else
        {
            Serial.println("Failed to set extrusion calibration");
            return false;
        }

        doc.clear();
        yield();
    }

    return true;
}

// init
void mqtt_callback(char* topic, byte* payload, unsigned int length) {
    String message;
    for (int i = 0; i < length; i++) {
        message += (char)payload[i];
    }

    // JSON-Dokument parsen
    JsonDocument doc;
    DeserializationError error = deserializeJson(doc, message);
    if (error) {
        Serial.print("Fehler beim Parsen des JSON: ");
        Serial.println(error.c_str());
        return;
    }

    // Prüfen, ob "print->upgrade_state" und "print.ams.ams" existieren
    if (doc["print"]["upgrade_state"].is<String>()) {
        // Prüfen ob AMS-Daten vorhanden sind
        if (!doc["print"]["ams"].is<String>() || !doc["print"]["ams"]["ams"].is<String>()) {
            return;
        }

        JsonArray amsArray = doc["print"]["ams"]["ams"].as<JsonArray>();
        
        // Prüfe ob sich die AMS-Daten geändert haben
        bool hasChanges = false;
        
        // Vergleiche jedes AMS und seine Trays
        for (int i = 0; i < amsArray.size() && !hasChanges; i++) {
            JsonObject amsObj = amsArray[i];
            int amsId = amsObj["id"].as<uint8_t>();
            JsonArray trayArray = amsObj["tray"].as<JsonArray>();
            
            // Finde das entsprechende AMS in unseren Daten
            int storedIndex = -1;
            for (int k = 0; k < ams_count; k++) {
                if (ams_data[k].ams_id == amsId) {
                    storedIndex = k;
                    break;
                }
            }
            
            if (storedIndex == -1) {
                hasChanges = true;
                break;
            }

            // Vergleiche die Trays
            for (int j = 0; j < trayArray.size() && j < 4 && !hasChanges; j++) {
                JsonObject trayObj = trayArray[j];
                if (trayObj["tray_info_idx"].as<String>() != ams_data[storedIndex].trays[j].tray_info_idx ||
                    trayObj["tray_type"].as<String>() != ams_data[storedIndex].trays[j].tray_type ||
                    trayObj["tray_color"].as<String>() != ams_data[storedIndex].trays[j].tray_color ||
                    trayObj["cali_idx"].as<String>() != ams_data[storedIndex].trays[j].cali_idx) {
                    hasChanges = true;
                    break;
                }
            }
        }

        // Prüfe die externe Spule
        if (!hasChanges && doc["print"]["vt_tray"].is<String>()) {
            JsonObject vtTray = doc["print"]["vt_tray"];
            bool foundExternal = false;
            
            for (int i = 0; i < ams_count; i++) {
                if (ams_data[i].ams_id == 255) {
                    foundExternal = true;
                    if (vtTray["tray_info_idx"].as<String>() != ams_data[i].trays[0].tray_info_idx ||
                        vtTray["tray_type"].as<String>() != ams_data[i].trays[0].tray_type ||
                        vtTray["tray_color"].as<String>() != ams_data[i].trays[0].tray_color ||
                        vtTray["cali_idx"].as<String>() != ams_data[i].trays[0].cali_idx) {
                        hasChanges = true;
                    }
                    break;
                }
            }
            if (!foundExternal) hasChanges = true;
        }

        if (!hasChanges) return;

        // Fortfahren mit der bestehenden Verarbeitung, da Änderungen gefunden wurden
        ams_count = amsArray.size();
        
        for (int i = 0; i < ams_count && i < 16; i++) {
            JsonObject amsObj = amsArray[i];
            JsonArray trayArray = amsObj["tray"].as<JsonArray>();

            ams_data[i].ams_id = i; // Setze die AMS-ID
            for (int j = 0; j < trayArray.size() && j < 4; j++) { // Annahme: Maximal 4 Trays pro AMS
                JsonObject trayObj = trayArray[j];

                ams_data[i].trays[j].id = trayObj["id"].as<uint8_t>();
                ams_data[i].trays[j].tray_info_idx = trayObj["tray_info_idx"].as<String>();
                ams_data[i].trays[j].tray_type = trayObj["tray_type"].as<String>();
                ams_data[i].trays[j].tray_sub_brands = trayObj["tray_sub_brands"].as<String>();
                ams_data[i].trays[j].tray_color = trayObj["tray_color"].as<String>();
                ams_data[i].trays[j].nozzle_temp_min = trayObj["nozzle_temp_min"].as<int>();
                ams_data[i].trays[j].nozzle_temp_max = trayObj["nozzle_temp_max"].as<int>();
                ams_data[i].trays[j].setting_id = trayObj["setting_id"].as<String>();
                ams_data[i].trays[j].cali_idx = trayObj["cali_idx"].as<String>();
            }
        }
        
        // Setze ams_count auf die Anzahl der normalen AMS
        ams_count = amsArray.size();

        // Wenn externe Spule vorhanden, füge sie hinzu
        if (doc["print"]["vt_tray"].is<String>()) {
            JsonObject vtTray = doc["print"]["vt_tray"];
            int extIdx = ams_count;  // Index für externe Spule
            ams_data[extIdx].ams_id = 255;  // Spezielle ID für externe Spule
            ams_data[extIdx].trays[0].id = 254;  // Spezielle ID für externes Tray
            ams_data[extIdx].trays[0].tray_info_idx = vtTray["tray_info_idx"].as<String>();
            ams_data[extIdx].trays[0].tray_type = vtTray["tray_type"].as<String>();
            ams_data[extIdx].trays[0].tray_sub_brands = vtTray["tray_sub_brands"].as<String>();
            ams_data[extIdx].trays[0].tray_color = vtTray["tray_color"].as<String>();
            ams_data[extIdx].trays[0].nozzle_temp_min = vtTray["nozzle_temp_min"].as<int>();
            ams_data[extIdx].trays[0].nozzle_temp_max = vtTray["nozzle_temp_max"].as<int>();
            ams_data[extIdx].trays[0].setting_id = vtTray["setting_id"].as<String>();
            ams_data[extIdx].trays[0].cali_idx = vtTray["cali_idx"].as<String>();
            ams_count++;  // Erhöhe ams_count für die externe Spule
        }

        // Sende die aktualisierten AMS-Daten
        //sendAmsData(nullptr);

        // Erstelle JSON für WebSocket-Clients
        JsonDocument wsDoc;
        JsonArray wsArray = wsDoc.to<JsonArray>();

        for (int i = 0; i < ams_count; i++) {
            JsonObject amsObj = wsArray.add<JsonObject>();
            amsObj["ams_id"] = ams_data[i].ams_id;

            JsonArray trays = amsObj["tray"].to<JsonArray>();
            int maxTrays = (ams_data[i].ams_id == 255) ? 1 : 4;
            
            for (int j = 0; j < maxTrays; j++) {
                JsonObject trayObj = trays.add<JsonObject>();
                trayObj["id"] = ams_data[i].trays[j].id;
                trayObj["tray_info_idx"] = ams_data[i].trays[j].tray_info_idx;
                trayObj["tray_type"] = ams_data[i].trays[j].tray_type;
                trayObj["tray_sub_brands"] = ams_data[i].trays[j].tray_sub_brands;
                trayObj["tray_color"] = ams_data[i].trays[j].tray_color;
                trayObj["nozzle_temp_min"] = ams_data[i].trays[j].nozzle_temp_min;
                trayObj["nozzle_temp_max"] = ams_data[i].trays[j].nozzle_temp_max;
                trayObj["setting_id"] = ams_data[i].trays[j].setting_id;
                trayObj["cali_idx"] = ams_data[i].trays[j].cali_idx;
            }
        }

        serializeJson(wsArray, amsJsonData);
        sendAmsData(nullptr);
    }
    // Neue Bedingung für ams_filament_setting
    else if (doc["print"]["command"] == "ams_filament_setting") {
        int amsId = doc["print"]["ams_id"].as<int>();
        int trayId = doc["print"]["tray_id"].as<int>();
        String settingId = doc["print"]["setting_id"].as<String>();
        
        // Finde das entsprechende AMS und Tray
        for (int i = 0; i < ams_count; i++) {
            if (ams_data[i].ams_id == amsId) {
                // Update setting_id im entsprechenden Tray
                ams_data[i].trays[trayId].setting_id = settingId;
                
                // Erstelle neues JSON für WebSocket-Clients
                JsonDocument wsDoc;
                JsonArray wsArray = wsDoc.to<JsonArray>();

                for (int j = 0; j < ams_count; j++) {
                    JsonObject amsObj = wsArray.add<JsonObject>();
                    amsObj["ams_id"] = ams_data[j].ams_id;

                    JsonArray trays = amsObj["tray"].to<JsonArray>();
                    int maxTrays = (ams_data[j].ams_id == 255) ? 1 : 4;
                    
                    for (int k = 0; k < maxTrays; k++) {
                        JsonObject trayObj = trays.add<JsonObject>();
                        trayObj["id"] = ams_data[j].trays[k].id;
                        trayObj["tray_info_idx"] = ams_data[j].trays[k].tray_info_idx;
                        trayObj["tray_type"] = ams_data[j].trays[k].tray_type;
                        trayObj["tray_sub_brands"] = ams_data[j].trays[k].tray_sub_brands;
                        trayObj["tray_color"] = ams_data[j].trays[k].tray_color;
                        trayObj["nozzle_temp_min"] = ams_data[j].trays[k].nozzle_temp_min;
                        trayObj["nozzle_temp_max"] = ams_data[j].trays[k].nozzle_temp_max;
                        trayObj["setting_id"] = ams_data[j].trays[k].setting_id;
                        trayObj["cali_idx"] = ams_data[j].trays[k].cali_idx;
                    }
                }

                // Aktualisiere das globale amsJsonData
                amsJsonData = "";
                serializeJson(wsArray, amsJsonData);
                
                // Sende an WebSocket Clients
                sendAmsData(nullptr);
                break;
            }
        }
    }
}

void reconnect() {
    // Loop until we're reconnected
    while (!client.connected()) {
        Serial.print("Attempting MQTT connection...");
        bambu_connected = false;
        oledShowTopRow();

        // Attempt to connect
        if (client.connect(bambu_serialnr, bambu_username, bambu_accesscode)) {
            Serial.println("... re-connected");
            // ... and resubscribe
            client.subscribe(report_topic.c_str());
            bambu_connected = true;
            oledShowTopRow();
        } else {
            Serial.print("failed, rc=");
            Serial.print(client.state());
            Serial.println(" try again in 5 seconds");
            bambu_connected = false;
            oledShowTopRow();
            // Wait 5 seconds before retrying
            yield();
            vTaskDelay(5000 / portTICK_PERIOD_MS);
        }
    }
}

void mqtt_loop(void * parameter) {
    for(;;) {
        if (pauseBambuMqttTask) {
            vTaskDelay(10000);
        }

        if (!client.connected()) {
            reconnect();
            yield();
            esp_task_wdt_reset();
            vTaskDelay(100);
        }
        client.loop();
        yield();
        vTaskDelay(100);
    }
}

bool setupMqtt() {
    // Wenn Bambu Daten vorhanden
    bool success = loadBambuCredentials();
    vTaskDelay(100 / portTICK_PERIOD_MS);

    if (!success) {
        Serial.println("Failed to load Bambu credentials");
        oledShowMessage("Bambu Credentials Missing");
        vTaskDelay(2000 / portTICK_PERIOD_MS);
        return false;
    }

    if (success && bambu_ip != "" && bambu_accesscode != "" && bambu_serialnr != "") 
    {
        sslClient.setCACert(root_ca);
        sslClient.setInsecure();
        client.setServer(bambu_ip, 8883);

        // Verbinden mit dem MQTT-Server
        bool connected = true;
        if (client.connect(bambu_serialnr, bambu_username, bambu_accesscode)) 
        {
            client.setCallback(mqtt_callback);
            client.setBufferSize(5120);
            // Optional: Topic abonnieren
            client.subscribe(report_topic.c_str());
            //client.subscribe(request_topic.c_str());
            Serial.println("MQTT-Client initialisiert");

            oledShowMessage("Bambu Connected");
            bambu_connected = true;
            oledShowTopRow();

            xTaskCreatePinnedToCore(
                mqtt_loop, /* Function to implement the task */
                "BambuMqtt", /* Name of the task */
                10000,  /* Stack size in words */
                NULL,  /* Task input parameter */
                mqttTaskPrio,  /* Priority of the task */
                &BambuMqttTask,  /* Task handle. */
                mqttTaskCore); /* Core where the task should run */
        } 
        else 
        {
            Serial.println("Fehler: Konnte sich nicht beim MQTT-Server anmelden");
            oledShowMessage("Bambu Connection Failed");
            vTaskDelay(2000 / portTICK_PERIOD_MS);
            connected = false;
            oledShowTopRow();
        }

        if (!connected) return false;
    } 
    else 
    {
        Serial.println("Fehler: Keine MQTT-Daten vorhanden");
        oledShowMessage("Bambu Credentials Missing");
        oledShowTopRow();
        vTaskDelay(2000 / portTICK_PERIOD_MS);
        return false;
    }
    return true;
}

void bambu_restart() {
    if (BambuMqttTask) {
        vTaskDelete(BambuMqttTask);
    }
    setupMqtt();
}