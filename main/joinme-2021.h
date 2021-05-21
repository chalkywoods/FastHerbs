// joinme-2021.h
// IoT device provisioning over wifi, captive portal and OTA for the ESP32

#ifndef JOINME_H
#define JOINME_H

#include <HTTPClient.h>
#include <ESPAsyncWebServer.h>

// WiFi provisioning ////////////////////////////////////////////////////////
AsyncWebServer* joinmeManageWiFi(const char *apSSID, const char *apKey);
extern AsyncWebServer* webServer;              // async web server
String ip2str(IPAddress);               // helper for printing IP addresses
void printIPs();

// DNS stuff ////////////////////////////////////////////////////////////////
void joinmeDNSSetup(void* server, IPAddress apIP); // capture clients
void joinmeTurn(); // run once per main loop iteration
// note: you *must* regularly call joinmeTurn;
// server *must* live until after you last call joinmeTurn

// OTA stuff ////////////////////////////////////////////////////////////////
void joinmeOTAUpdate(int, String, String, String);         // main OTA logic
int  joinmeCloudGet(HTTPClient *, String, String, String); // download 'ware
extern "C" {
  void idf_ota_update(const char *);                       // IDF version
}

// utilities ////////////////////////////////////////////////////////////////

// delay/yield macros
#define WAIT_A_SEC   vTaskDelay(    1000/portTICK_PERIOD_MS); // 1 second
#define WAIT_SECS(n) vTaskDelay((n*1000)/portTICK_PERIOD_MS); // n seconds
#define WAIT_MS(n)   vTaskDelay(       n/portTICK_PERIOD_MS); // n millis

#define ECHECK ESP_ERROR_CHECK_WITHOUT_ABORT

// debugging infrastructure; setting different DBGs true triggers prints ////
#define dbg(b, s)       if(b) Serial.print(s)
#define dbf(b, ...)     if(b) Serial.printf(__VA_ARGS__)
#define dln(b, s)       if(b) Serial.println(s)
#define startupDBG      true
#define loopDBG         true
#define monitorDBG      true
#define netDBG          true
#define miscDBG         true
#define analogDBG       true
#define otaDBG          true

// IDF logging
static const char *TAG = "main";
extern String apSSID;

#endif
