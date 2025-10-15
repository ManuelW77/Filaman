#include <Arduino.h>
#include "wlan.h"
#include <WiFi.h>
#include <esp_wifi.h>
#include <WiFiManager.h>
#include <DNSServer.h>
#include <ESPmDNS.h>
#include "display.h"
#include "config.h"

WiFiManager wm;
bool wm_nonblocking = false;
uint8_t wifiErrorCounter = 0;

void wifiSettings() {
    // Optimierte WiFi-Einstellungen
    WiFi.mode(WIFI_STA); // explicitly set mode, esp defaults to STA+AP
    WiFi.setSleep(false); // disable sleep mode
    WiFi.setHostname("FilaMan");
    esp_wifi_set_ps(WIFI_PS_NONE);
    
    // Maximale Sendeleistung
    WiFi.setTxPower(WIFI_POWER_19_5dBm); // Set maximum transmit power
  
    // Optimiere TCP/IP Stack
    esp_wifi_set_protocol(WIFI_IF_STA, WIFI_PROTOCOL_11B | WIFI_PROTOCOL_11G | WIFI_PROTOCOL_11N);
    
    // Aktiviere WiFi-Roaming für bessere Stabilität
    esp_wifi_set_rssi_threshold(-80);
}

void startMDNS() {
  if (!MDNS.begin("filaman")) {
    Serial.println("Error setting up MDNS responder!");
    while(1) {
      vTaskDelay(1000 / portTICK_PERIOD_MS);
    }
  }
  MDNS.addService("http", "tcp", 80);
  Serial.println("mDNS responder started");
}

void configModeCallback (WiFiManager *myWiFiManager) {
  Serial.println("Entered config mode");
  oledShowTopRow();
  oledShowMessage("WiFi Config Mode");
}

void initWiFi() {
  // load Wifi settings
  wifiSettings();

  wm.setAPCallback(configModeCallback);

  wm.setSaveConfigCallback([]() {
    Serial.println("Configurations updated");
    ESP.restart();
  });

  if(wm_nonblocking) wm.setConfigPortalBlocking(false);
  //wm.setConfigPortalTimeout(320); // Portal nach 5min schließen
  wm.setWiFiAutoReconnect(true);
  wm.setConnectTimeout(10);

  oledShowProgressBar(1, 7, DISPLAY_BOOT_TEXT, "WiFi init");
  
  //bool res = wm.autoConnect("FilaMan"); // anonymous ap
  if(!wm.autoConnect("FilaMan")) {
    Serial.println("Failed to connect or hit timeout");
    // ESP.restart();
    oledShowTopRow();
    oledShowMessage("WiFi not connected Check Portal");
  } 
  else {
    wifiOn = true;

    //if you get here you have connected to the WiFi    
    Serial.println("connected...yeey :)");
    Serial.print("IP address: ");
    Serial.println(WiFi.localIP());

    oledShowTopRow();

    // mDNS
    startMDNS();
  }
}

void checkWiFiConnection() {
  if (WiFi.status() != WL_CONNECTED) 
  {
    Serial.println("WiFi connection lost. Reconnecting...");
    wifiOn = false;
    oledShowTopRow();
    oledShowMessage("WiFi reconnecting");
    WiFi.reconnect(); // Versuche, die Verbindung wiederherzustellen
    vTaskDelay(5000 / portTICK_PERIOD_MS); // Warte 5 Sekunden, bevor erneut geprüft wird
    if (WiFi.status() != WL_CONNECTED) 
    {
      Serial.println("Failed to reconnect. Restarting WiFi...");
      WiFi.disconnect();
      Serial.println("WiFi disconnected.");
      vTaskDelay(1000 / portTICK_PERIOD_MS);
      wifiErrorCounter++;

      //wifiSettings();
      WiFi.reconnect();
      Serial.println("WiFi reconnecting...");
      WiFi.waitForConnectResult();
    } 
    else 
    {
      Serial.println("WiFi reconnected.");
      wifiErrorCounter = 0;
      wifiOn = true;
      oledShowTopRow();
      startMDNS();
    }
  }

  if (wifiErrorCounter >= 5) 
  {
    Serial.println("Too many WiFi errors. Restarting...");
    ESP.restart();
  }
}