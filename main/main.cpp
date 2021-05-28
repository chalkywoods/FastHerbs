// main.cpp
int firmwareVersion = 7;

#include <Wire.h>
#include <Arduino.h>
#include <ESPAsyncWebServer.h>
#include <freertos/FreeRTOS.h>
#include "time.h"
#include "joinme-2021.h" // Provisioning and OTA update - taken from COM3505 exercises.
#include "private.h" // not for pushing; assumed to be at parent dir level
#include <SPIFFS.h> // Filesystem for webpages
#include "RunningMedian.h"

// setting different DBGs true triggers prints
#define dbg(b, s)       if(b) Serial.print(s)
#define dbf(b, ...)     if(b) Serial.printf(__VA_ARGS__)
#define dln(b, s)       if(b) Serial.println(s)
#define startupDBG      true
#define loopDBG         false
#define monitorDBG      false
#define netDBG          true
#define miscDBG         true
#define analogDBG       true
#define otaDBG          false

// MAC ADDRESS
char MAC_ADDRESS[13]; // MAC addresses are 12 chars, plus the NULL terminator
void getMAC(char *);

// wifi access point 
String apSSID = String("ProjectThing-"); // SSID of the AP
String apPassword = _DEFAULT_AP_KEY;

//NTP variables
const char* ntpServer = "pool.ntp.org";
const long  gmtOffset_sec = 0;
const int   daylightOffset_sec = 3600;

// delay/yield macros
#define WAIT_A_SEC   vTaskDelay(    1000/portTICK_PERIOD_MS); // 1 second
#define WAIT_SECS(n) vTaskDelay((n*1000)/portTICK_PERIOD_MS); // n seconds
#define WAIT_MS(n)   vTaskDelay(       n/portTICK_PERIOD_MS); // n millis

#define ECHECK ESP_ERROR_CHECK_WITHOUT_ABORT

// Web server
AsyncWebServer* plantServer;
void initPlantServer();
void hndlIndex(AsyncWebServerRequest *);
void hndlMoisture(AsyncWebServerRequest *);
void hndlLight(AsyncWebServerRequest *);
void hndlNotFound(AsyncWebServerRequest *);

// pump pins
int pump1 = 21;

// constants
int cap_thresh = 375;
int pump_time = 4;
int poll_time = 3;
int active_start = 1000;
int active_stop = 2200;
unsigned int ntpUpdateTime = 6;

// Median filter light and moisture measurements, WiFi interferes with i2c
RunningMedian moisture = RunningMedian(3);
RunningMedian light = RunningMedian(5);
int curr_moisture = cap_thresh;
int curr_light = 0;

// Function prototypes
void pump(int, int);
void provisionAndUpdate();
unsigned int readI2CRegister16bit(int, int);
unsigned int readI2CRegister8bit(int, int);
void writeI2CRegister8bit(int, int);
void readSensors(void *);
void updateTime(void *);
bool isActive();
unsigned int getMoisture(int);
unsigned int getLight(int);

// SensorHandle
TaskHandle_t sensorHandle = NULL;

// I2C Mutex
SemaphoreHandle_t i2cMutex = xSemaphoreCreateMutex();

/////////////////////////////////////////////////////////////////////////////
// arduino-land entry points

void setup() {
  Serial.begin(115200);
  Serial.println("arduino started");
  dln(startupDBG, "\nsetup ProjectThing");
  Wire.begin();
  Wire.setClock(100000);
  writeI2CRegister8bit(0x20, 6); //reset sensor
  getMAC(MAC_ADDRESS);
  Serial.printf("\nsetup...\nESP32 MAC = %s\n", MAC_ADDRESS);
  apSSID.concat(MAC_ADDRESS);
  // WiFi provisioning or connection
  Serial.printf("doing wifi manager\n");
  provisionAndUpdate();

  // Set up leds
  pinMode(pump1, OUTPUT);

  // Config ntp
  configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);

  // Mount filesystem
  if(!SPIFFS.begin()){
    Serial.println("An Error has occurred while mounting SPIFFS");
    return;
  }

  // Start server
  initPlantServer();

  dln(startupDBG, "Starting tasks");

  xTaskCreate(
    updateTime,
    "Update time",
    2048,
    NULL,
    3,
    NULL
  );

  xTaskCreate(
    readSensors,    // Function that should be called
    "Read water sensor",   // Name of the task (for debugging)
    2048,            // Stack size (bytes)
    NULL,  // Parameter to pass
    2,               // Task priority
    &sensorHandle             // Task handle
  );

  dln(startupDBG, "All tasks started");
} // setup

void loop() {
  vTaskDelay(50/portTICK_PERIOD_MS);
} // loop

void provisionAndUpdate() {
  webServer = joinmeManageWiFi(apSSID.c_str(), apPassword.c_str()); // connect
  Serial.printf("wifi manager done\n\n");
  Serial.print("AP SSID: ");
  Serial.print(apSSID);
  Serial.print("; IP address(es): local=");
  Serial.print(WiFi.localIP());
  Serial.print("; AP=");
  Serial.println(WiFi.softAPIP());

  // check for and perform firmware updates as needed
  Serial.printf("firmware is at version %d\n", firmwareVersion);
  vTaskDelay(2000 / portTICK_PERIOD_MS); // let wifi settle
  joinmeOTAUpdate(
    firmwareVersion, _GITLAB_PROJ_ID,
    // "", // for publ repo "" works, else need valid PAT: _GITLAB_TOKEN,
    _GITLAB_TOKEN,
    "ProjectThing%2Ffirmware%2F"
  );
}

void getMAC(char *buf) { // the MAC is 6 bytes, so needs careful conversion...
                         // taken from COM3505 ecercises
  uint64_t mac = ESP.getEfuseMac(); // ...to string (high 2, low 4):
  char rev[13];
  sprintf(rev, "%04X%08X", (uint16_t) (mac >> 32), (uint32_t) mac);

  // the byte order in the ESP has to be reversed
  for(int i=0, j=11; i<=10; i+=2, j-=2) {
    buf[i] = rev[j - 1];
    buf[i + 1] = rev[j];
  }
  buf[12] = '\0';
}

void writeI2CRegister8bit(int addr, int reg) {
  xSemaphoreTake(i2cMutex, ( TickType_t ) 0);
  Wire.beginTransmission(addr);
  Wire.write(reg);
  Wire.endTransmission();
  xSemaphoreGive(i2cMutex);
}

unsigned int readI2CRegister16bit(int addr, int reg) {
  xSemaphoreTake(i2cMutex, ( TickType_t ) 0);
  Wire.beginTransmission(addr);
  Wire.write(reg);
  Wire.endTransmission();
  vTaskDelay(50/portTICK_PERIOD_MS);  
  Wire.requestFrom(addr, 2);
  unsigned int t = Wire.read() << 8;
  t = t | Wire.read();
  xSemaphoreGive(i2cMutex);
  return t;
}

unsigned int readI2CRegister8bit(int addr, int reg) {
  xSemaphoreTake(i2cMutex, ( TickType_t ) 0);
  Wire.beginTransmission(addr);
  Wire.write(reg);
  Wire.endTransmission();
  vTaskDelay(50/portTICK_PERIOD_MS);   
  Wire.requestFrom(addr, 1);
  unsigned int t = Wire.read();
  xSemaphoreGive(i2cMutex);
  return t;
}

unsigned int getMoisture(int addr) {
  unsigned int reading = 0;
  reading = readI2CRegister16bit(addr, 0);
  if (reading > 0) {
    moisture.add(reading);
  }
  return moisture.getLowest();
}

unsigned int getLight(int addr) {
  unsigned int reading = 0;
  writeI2CRegister8bit(addr, 3);
  vTaskDelay(2000 / portTICK_PERIOD_MS);
  reading = readI2CRegister16bit(addr, 4);
  if (reading > 0) {
    light.add(reading);
  }
  return light.getMedian();
}

void pump(int pin, int time) {
  digitalWrite(pin, HIGH);
  vTaskDelay(time * 1000 / portTICK_PERIOD_MS);
  digitalWrite(pin, LOW);
}

void readSensors(void *parameter) {
  vTaskDelay(2000 / portTICK_PERIOD_MS);
  for(;;){
    curr_light = getMoisture(0x20);    // When the firmware wants light AND moisture, the registers
    curr_moisture = getLight(0x20);    // swap round for some reason?
    Serial.println((String)"Moisture: " + curr_moisture + (String)" | Light: " + curr_light);
    if (curr_moisture < cap_thresh && isActive()) {
      pump(pump1, pump_time);
    };
    vTaskDelay(poll_time * 1000 / portTICK_PERIOD_MS);
  }
}

bool isActive() {
  struct tm timeinfo;
  if (!getLocalTime(&timeinfo)) {
    Serial.println("Failed to obtain time");
    return false;
  } else if (curr_light > 10000) {
    Serial.println("Sensors initialising");
    return false;
  } else {
    int hour = (100 * timeinfo.tm_hour) + timeinfo.tm_min;
    Serial.println(hour);
    return (hour > active_start && hour < active_stop);
  }
}

void initPlantServer() { // changed naming conventions to avoid clash with Ex06
  // register callbacks to handle different paths
  plantServer = new AsyncWebServer(80);
  plantServer->on("/", hndlIndex);              // slash
  plantServer->on("/moisture", hndlMoisture);   // moisture
  plantServer->on("/light", hndlLight);         // light
  plantServer->onNotFound(hndlNotFound);        // 404s...

  plantServer->begin();
  dln(startupDBG, "HTTP server started");
}

void hndlIndex(AsyncWebServerRequest *request){
  request->send(SPIFFS, "/index.html");
}

void hndlMoisture(AsyncWebServerRequest *request){
  request->send_P(200, "text/plain", ((String)curr_moisture).c_str());
}

void hndlLight(AsyncWebServerRequest *request){
  request->send_P(200, "text/plain", ((String)curr_light).c_str());
}

void updateTime(void *parameter){
  for(;;){
    configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);
    vTaskDelay(ntpUpdateTime*3600000 / portTICK_PERIOD_MS);
  }
}

/////////////////////////////////////////////////////////////////////////////
// if we're an IDF build define app_main
// (TODO probably fails to identify a platformio *idf* build)

#if ! defined(ARDUINO_IDE_BUILD) && ! defined(PLATFORMIO)
  extern "C" { void app_main(); }

  // main entry point
  void app_main() {
    // arduino land
    initArduino();
    setup();
    while(1)
      loop();
  } // app_main()

#endif