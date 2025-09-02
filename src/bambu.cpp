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
#include <Preferences.h>

WiFiClient espClient;
SSLClient sslClient(&espClient);
PubSubClient client(sslClient);

TaskHandle_t BambuMqttTask;

bool bambuDisabled = false;

bool bambu_connected = false;
uint16_t autoSetToBambuSpoolId = 0;

BambuCredentials bambuCredentials;

// Globale Variablen für AMS-Daten
int ams_count = 0;
String amsJsonData;  // Speichert das fertige JSON für WebSocket-Clients
AMSData ams_data[MAX_AMS];  // Definition des Arrays;

bool removeBambuCredentials() {
    if (BambuMqttTask) {
        vTaskDelete(BambuMqttTask);
        BambuMqttTask = NULL;
    }
    
    Preferences preferences;
    preferences.begin(NVS_NAMESPACE_BAMBU, false); // false = readwrite
    preferences.remove(NVS_KEY_BAMBU_IP);
    preferences.remove(NVS_KEY_BAMBU_SERIAL);
    preferences.remove(NVS_KEY_BAMBU_ACCESSCODE);
    preferences.remove(NVS_KEY_BAMBU_AUTOSEND_ENABLE);
    preferences.remove(NVS_KEY_BAMBU_AUTOSEND_TIME);
    preferences.end();

    // Löschen der globalen Variablen
    bambuCredentials.ip = "";
    bambuCredentials.serial = "";
    bambuCredentials.accesscode = "";
    bambuCredentials.autosend_enable = false;
    bambuCredentials.autosend_time = BAMBU_DEFAULT_AUTOSEND_TIME;

    autoSetToBambuSpoolId = 0;
    ams_count = 0;
    amsJsonData = "";

    bambuDisabled = true;

    return true;
}

bool saveBambuCredentials(const String& ip, const String& serialnr, const String& accesscode, bool autoSend, const String& autoSendTime) {
    if (BambuMqttTask) {
        vTaskDelete(BambuMqttTask);
        BambuMqttTask = NULL;
    }

    bambuCredentials.ip = ip.c_str();
    bambuCredentials.serial = serialnr.c_str();
    bambuCredentials.accesscode = accesscode.c_str();
    bambuCredentials.autosend_enable = autoSend;
    bambuCredentials.autosend_time = autoSendTime.toInt();

    Preferences preferences;
    preferences.begin(NVS_NAMESPACE_BAMBU, false); // false = readwrite
    preferences.putString(NVS_KEY_BAMBU_IP, bambuCredentials.ip);
    preferences.putString(NVS_KEY_BAMBU_SERIAL, bambuCredentials.serial);
    preferences.putString(NVS_KEY_BAMBU_ACCESSCODE, bambuCredentials.accesscode);
    preferences.putBool(NVS_KEY_BAMBU_AUTOSEND_ENABLE, bambuCredentials.autosend_enable);
    preferences.putInt(NVS_KEY_BAMBU_AUTOSEND_TIME, bambuCredentials.autosend_time);
    preferences.end();

    vTaskDelay(100 / portTICK_PERIOD_MS);
    if (!setupMqtt()) return false;

    return true;
}

bool loadBambuCredentials() {
    Preferences preferences;
    preferences.begin(NVS_NAMESPACE_BAMBU, true);
    String ip = preferences.getString(NVS_KEY_BAMBU_IP, "");
    String serial = preferences.getString(NVS_KEY_BAMBU_SERIAL, "");
    String code = preferences.getString(NVS_KEY_BAMBU_ACCESSCODE, "");
    bool autosendEnable = preferences.getBool(NVS_KEY_BAMBU_AUTOSEND_ENABLE, false);
    int autosendTime = preferences.getInt(NVS_KEY_BAMBU_AUTOSEND_TIME, BAMBU_DEFAULT_AUTOSEND_TIME);
    preferences.end();

    if(ip != ""){
        bambuCredentials.ip = ip.c_str();
        bambuCredentials.serial = serial.c_str();
        bambuCredentials.accesscode = code.c_str();
        bambuCredentials.autosend_enable = autosendEnable;
        bambuCredentials.autosend_time = autosendTime;

        Serial.println("credentials loaded loadCredentials!");
        Serial.println(bambuCredentials.ip);
        Serial.println(bambuCredentials.serial);
        Serial.println(bambuCredentials.accesscode);
        Serial.println(String(bambuCredentials.autosend_enable));
        Serial.println(String(bambuCredentials.autosend_time));

        return true;
    }
    else
    {
        Serial.println("Keine gültigen Bambu-Credentials gefunden.");
        return false;
    }
}

struct FilamentResult {
    String key;
    String type;
};

FilamentResult findFilamentIdx(String brand, String type) {
    // JSON-Dokument für die Filament-Daten erstellen
    JsonDocument doc;
    
    // Laden der own_filaments.json
    String ownFilament = "";
    if (!loadJsonValue("/own_filaments.json", doc)) 
    {
        Serial.println("Fehler beim Laden der eigenen Filament-Daten");
    }
    else
    {
        // Durchsuche direkt nach dem Type als Schlüssel
        if (doc[type].is<String>()) {
            ownFilament = doc[type].as<String>();
        }
        doc.clear();
    }
    doc.clear();

    // Laden der bambu_filaments.json
    if (!loadJsonValue("/bambu_filaments.json", doc)) 
    {
        Serial.println("Fehler beim Laden der Filament-Daten");
        return {"GFL99", "PLA"}; // Fallback auf Generic PLA
    }

    // Wenn eigener Typ
    if (ownFilament != "")
    {
        if (doc[ownFilament].is<String>()) 
        {
            return {ownFilament, doc[ownFilament].as<String>()};
        }
    }

    // 1. Erst versuchen wir die exakte Brand + Type Kombination zu finden
    String searchKey;
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
            return {kv.key().c_str(), kv.value().as<String>()};
        }
    }

    // 2. Wenn nicht gefunden, zerlege den type String in Wörter und suche nach jedem Wort
    // Sammle alle vorhandenen Filamenttypen aus der JSON
    std::vector<String> knownTypes;
    for (JsonPair kv : doc.as<JsonObject>()) {
        String value = kv.value().as<String>();
        // Extrahiere den Typ ohne Markennamen
        if (value.indexOf(" ") != -1) {
            value = value.substring(value.indexOf(" ") + 1);
        }
        if (!value.isEmpty()) {
            knownTypes.push_back(value);
        }
    }

    // Zerlege den Input-Type in Wörter
    String typeStr = type;
    typeStr.trim();
    
    // Durchsuche für jedes bekannte Filament, ob es im Input vorkommt
    for (const String& knownType : knownTypes) {
        if (typeStr.indexOf(knownType) != -1) {
            // Suche nach diesem Typ in der Original-JSON
            for (JsonPair kv : doc.as<JsonObject>()) {
                String value = kv.value().as<String>();
                if (value.indexOf(knownType) != -1) {
                    return {kv.key().c_str(), knownType};
                }
            }
        }
    }

    // 3. Wenn immer noch nichts gefunden, gebe GFL99 zurück (Generic PLA)
    return {"GFL99", "PLA"};
}

bool sendMqttMessage(const String& payload) {
    Serial.println("Sending MQTT message");
    Serial.println(payload);
    if (client.publish(("device/"+bambuCredentials.serial+"/request").c_str(), payload.c_str())) 
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
    (type == "PLA+") ? type = "PLA" : type;
    String brand = doc["brand"].as<String>();
    String tray_info_idx = (doc["tray_info_idx"].as<String>() != "-1") ? doc["tray_info_idx"].as<String>() : "";
    if (tray_info_idx == "") {
        if (brand != "" && type != "") {
            FilamentResult result = findFilamentIdx(brand, type);
            tray_info_idx = result.key;
            type = result.type;  // Aktualisiere den type mit dem gefundenen Basistyp
        }
    }
    String setting_id = doc["bambu_setting_id"].as<String>();
    String cali_idx = doc["cali_idx"].as<String>();

    doc.clear();

    doc["print"]["sequence_id"] = "0";
    doc["print"]["command"] = "ams_filament_setting";
    doc["print"]["ams_id"] = amsId < 200 ? amsId : 255;
    doc["print"]["tray_id"] = trayId < 200 ? trayId : 254;
    doc["print"]["tray_color"] = color.length() == 8 ? color : color+"FF";
    doc["print"]["nozzle_temp_min"] = minTemp;
    doc["print"]["nozzle_temp_max"] = maxTemp;
    doc["print"]["tray_type"] = type;
    //doc["print"]["cali_idx"] = (cali_idx != "") ? cali_idx : "";
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
        doc["print"]["sequence_id"] = "0";
        doc["print"]["command"] = "extrusion_cali_sel";
        doc["print"]["filament_id"] = tray_info_idx;
        doc["print"]["nozzle_diameter"] = "0.4";
        doc["print"]["cali_idx"] = cali_idx.toInt();
        doc["print"]["tray_id"] = trayId < 200 ? trayId : 254;
        //doc["print"]["ams_id"] = amsId < 200 ? amsId : 255;

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

void autoSetSpool(int spoolId, uint8_t trayId) {
    // wenn neue spule erkannt und autoSetToBambu > 0
    JsonDocument spoolInfo = fetchSingleSpoolInfo(spoolId);

    if (!spoolInfo.isNull())
    {
        // AMS und TRAY id ergänzen
        spoolInfo["amsId"] = 0;
        spoolInfo["trayId"] = trayId;

        Serial.println("Auto set spool");
        Serial.println(spoolInfo.as<String>());

        setBambuSpool(spoolInfo.as<String>());

        oledShowMessage("Spool set");
    }

    // id wieder zurücksetzen damit abgeschlossen
    autoSetToBambuSpoolId = 0;
}

void updateAmsWsData(JsonDocument& doc, JsonArray& amsArray, int& ams_count, JsonObject& vtTray) {
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
            if (trayObj["tray_type"].as<String>() == "") ams_data[i].trays[j].setting_id = "";
            ams_data[i].trays[j].cali_idx = trayObj["cali_idx"].as<String>();
        }
    }
    
    // Setze ams_count auf die Anzahl der normalen AMS
    ams_count = amsArray.size();

    // Wenn externe Spule vorhanden, füge sie hinzu
    if (doc["print"]["vt_tray"].is<JsonObject>()) {
        //JsonObject vtTray = doc["print"]["vt_tray"];
        int extIdx = ams_count;  // Index für externe Spule
        ams_data[extIdx].ams_id = 255;  // Spezielle ID für externe Spule
        ams_data[extIdx].trays[0].id = 254;  // Spezielle ID für externes Tray
        ams_data[extIdx].trays[0].tray_info_idx = vtTray["tray_info_idx"].as<String>();
        ams_data[extIdx].trays[0].tray_type = vtTray["tray_type"].as<String>();
        ams_data[extIdx].trays[0].tray_sub_brands = vtTray["tray_sub_brands"].as<String>();
        ams_data[extIdx].trays[0].tray_color = vtTray["tray_color"].as<String>();
        ams_data[extIdx].trays[0].nozzle_temp_min = vtTray["nozzle_temp_min"].as<int>();
        ams_data[extIdx].trays[0].nozzle_temp_max = vtTray["nozzle_temp_max"].as<int>();

        if (doc["print"]["vt_tray"]["tray_type"].as<String>() != "")
        {
            //ams_data[extIdx].trays[0].setting_id = vtTray["setting_id"].as<String>();
            ams_data[extIdx].trays[0].cali_idx = vtTray["cali_idx"].as<String>();
        }
        else
        {
            ams_data[extIdx].trays[0].setting_id = "";
            ams_data[extIdx].trays[0].cali_idx = "";
        }
        ams_count++;  // Erhöhe ams_count für die externe Spule
    }

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
    wsDoc.clear();
    Serial.println("AMS data updated");
    sendAmsData(nullptr);
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
    message = "";
    if (error) 
    {
        Serial.print("Fehler beim Parsen des JSON: ");
        Serial.println(error.c_str());
        return;
    }

    // Prüfen, ob "print->upgrade_state" und "print.ams.ams" existieren
    if (doc["print"]["upgrade_state"].is<JsonObject>() || (doc["print"]["command"].is<String>() && doc["print"]["command"] == "push_status")) 
    {
        // Prüfen ob AMS-Daten vorhanden sind
        if (!doc["print"]["ams"].is<JsonObject>() || !doc["print"]["ams"]["ams"].is<JsonArray>()) 
        {
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

                if (trayObj["tray_type"].as<String>() == "") ams_data[storedIndex].trays[j].setting_id = "";
                if (trayObj["setting_id"].isNull()) trayObj["setting_id"] = "";
                if (trayObj["tray_info_idx"].as<String>() != ams_data[storedIndex].trays[j].tray_info_idx ||
                    trayObj["tray_type"].as<String>() != ams_data[storedIndex].trays[j].tray_type ||
                    trayObj["tray_color"].as<String>() != ams_data[storedIndex].trays[j].tray_color ||
                    (trayObj["setting_id"].as<String>() != "" && trayObj["setting_id"].as<String>() != ams_data[storedIndex].trays[j].setting_id) ||
                    trayObj["cali_idx"].as<String>() != ams_data[storedIndex].trays[j].cali_idx) {
                    hasChanges = true;

                    if (bambuCredentials.autosend_enable && autoSetToBambuSpoolId > 0 && hasChanges)
                    {
                        autoSetSpool(autoSetToBambuSpoolId, ams_data[storedIndex].trays[j].id);
                    }

                    break;
                }
            }
        }

        // Prüfe die externe Spule
        JsonObject vtTray = doc["print"]["vt_tray"];
        if (doc["print"]["vt_tray"].is<JsonObject>()) {
            for (int i = 0; i < ams_count; i++) {
                if (ams_data[i].ams_id == 255) {
                    if (vtTray["tray_type"].as<String>() == "") ams_data[i].trays[0].setting_id = "";
                    if (vtTray["setting_id"].isNull()) vtTray["setting_id"] = "";
                    if (vtTray["tray_info_idx"].as<String>() != ams_data[i].trays[0].tray_info_idx ||
                        vtTray["tray_type"].as<String>() != ams_data[i].trays[0].tray_type ||
                        vtTray["tray_color"].as<String>() != ams_data[i].trays[0].tray_color ||
                        (vtTray["setting_id"].as<String>() != "" && vtTray["setting_id"].as<String>() != ams_data[i].trays[0].setting_id) ||
                        (vtTray["tray_type"].as<String>() != "" && vtTray["cali_idx"].as<String>() != ams_data[i].trays[0].cali_idx)) {
                        hasChanges = true;

                        if (bambuCredentials.autosend_enable && autoSetToBambuSpoolId > 0 && hasChanges)
                        {
                            autoSetSpool(autoSetToBambuSpoolId, 254);
                        }
                    }
                    break;
                }
            }
        }

        if (!hasChanges) return;

        updateAmsWsData(doc, amsArray, ams_count, vtTray);
    }
    
    // Neue Bedingung für ams_filament_setting
    if (doc["print"]["command"] == "ams_filament_setting") {
        int amsId = doc["print"]["ams_id"].as<int>();
        int trayId = doc["print"]["tray_id"].as<int>();
        String settingId = (doc["print"]["setting_id"].is<String>()) ? doc["print"]["setting_id"].as<String>() : "";

        // Finde das entsprechende AMS und Tray
        for (int i = 0; i < ams_count; i++) {
            if (ams_data[i].ams_id == amsId) {
                if (trayId == 254)
                {
                    // Suche AMS mit ID 255 (externe Spule)
                    for (int j = 0; j < ams_count; j++) {
                        if (ams_data[j].ams_id == 255) {
                            ams_data[j].trays[0].setting_id = settingId;
                            break;
                        }
                    }
                }
                else
                {
                    ams_data[i].trays[trayId].setting_id = settingId;
                }
               
                // Sende an WebSocket Clients
                Serial.println("Filament setting updated");
                sendAmsData(nullptr);
                break;
            }
        }
    }
}

void reconnect() {
    // Loop until we're reconnected
    uint8_t retries = 0;
    while (!client.connected()) {
        Serial.println("Attempting MQTT re/connection...");
        bambu_connected = false;
        oledShowTopRow();

        // Attempt to connect
        String clientId = bambuCredentials.serial + "_" + String(random(0, 100));
        if (client.connect(clientId.c_str(), BAMBU_USERNAME, bambuCredentials.accesscode.c_str())) {
            Serial.println("MQTT re/connected");

            client.subscribe(("device/"+bambuCredentials.serial+"/report").c_str());
            bambu_connected = true;
            oledShowTopRow();
        } else {
            Serial.print("failed, rc=");
            Serial.print(client.state());
            Serial.println(" try again in 5 seconds");
            bambu_connected = false;
            oledShowTopRow();
            
            yield();
            vTaskDelay(5000 / portTICK_PERIOD_MS);
            if (retries > 5) {
                Serial.println("Disable Bambu MQTT Task after 5 retries");
                //vTaskSuspend(BambuMqttTask);
                vTaskDelete(BambuMqttTask);
                BambuMqttTask = NULL;
                break;
            }

            retries++;
        }
    }
}

void mqtt_loop(void * parameter) {
    Serial.println("Bambu MQTT Task gestartet");
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
        esp_task_wdt_reset();
        vTaskDelay(100);
    }
}

bool setupMqtt() {
    // Wenn Bambu Daten vorhanden
    //bool success = loadBambuCredentials();

    if (bambuCredentials.ip != "" && bambuCredentials.accesscode != "" && bambuCredentials.serial != "") 
    {
        oledShowProgressBar(4, 7, DISPLAY_BOOT_TEXT, "Bambu init");
        bambuDisabled = false;
        sslClient.setCACert(root_ca);
        sslClient.setInsecure();
        client.setServer(bambuCredentials.ip.c_str(), 8883);

        // Verbinden mit dem MQTT-Server
        bool connected = true;
        String clientId = String(bambuCredentials.serial) + "_" + String(random(0, 100));
        if (client.connect(bambuCredentials.ip.c_str(), BAMBU_USERNAME, bambuCredentials.accesscode.c_str())) 
        {
            client.setCallback(mqtt_callback);
            client.setBufferSize(15488);
            client.subscribe(("device/"+bambuCredentials.serial+"/report").c_str());
            Serial.println("MQTT-Client initialisiert");

            oledShowMessage("Bambu Connected");
            bambu_connected = true;
            oledShowTopRow();

            xTaskCreatePinnedToCore(
                mqtt_loop, /* Function to implement the task */
                "BambuMqtt", /* Name of the task */
                8192,  /* Stack size in words */
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
            autoSetToBambuSpoolId = 0;
        }

        if (!connected) return false;
    } 
    else 
    {
        bambuDisabled = true;
        return false;
    }
    return true;
}

void bambu_restart() {
    Serial.println("Bambu restart");

    if (BambuMqttTask) {
        vTaskDelete(BambuMqttTask);
        BambuMqttTask = NULL;
        delay(10);
    }
    setupMqtt();
}