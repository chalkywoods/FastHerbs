// main.cpp
int firmwareVersion = 6;

#include <Wire.h>
#include <Arduino.h>
#include <ESPAsyncWebServer.h>
#include <freertos/FreeRTOS.h>
#include "joinme-2021.h" // Provisioning and OTA update - taken from COM3505 exercises.
#include "private.h" // not for pushing; assumed to be at parent dir level

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

// delay/yield macros
#define WAIT_A_SEC   vTaskDelay(    1000/portTICK_PERIOD_MS); // 1 second
#define WAIT_SECS(n) vTaskDelay((n*1000)/portTICK_PERIOD_MS); // n seconds
#define WAIT_MS(n)   vTaskDelay(       n/portTICK_PERIOD_MS); // n millis

#define ECHECK ESP_ERROR_CHECK_WITHOUT_ABORT

// pump pins
int pump1 = 21;

// constants
int cap_thresh = 375;
int pump_time = 4;
int poll_time = 10;

// Function prototypes
void pump(int, int);
void provisionAndUpdate(void *);
unsigned int readI2CRegister16bit(int, int);
void writeI2CRegister8bit(int, int);
void readSensors(void *);

// SensorHandle
TaskHandle_t sensorHandle = NULL;

/////////////////////////////////////////////////////////////////////////////
// arduino-land entry points

void setup() {
  dln(startupDBG, "\nsetup ProjectThing");
  Wire.begin();
  Wire.setClock(100000);
  Serial.begin(115200);
  writeI2CRegister8bit(0x20, 6); //reset sensor
  Serial.println("arduino started");
  getMAC(MAC_ADDRESS);
  Serial.printf("\nsetup...\nESP32 MAC = %s\n", MAC_ADDRESS);
  apSSID.concat(MAC_ADDRESS);
  // WiFi provisioning or connection
  Serial.printf("doing wifi manager\n");

  // Set up leds
  pinMode(pump1, OUTPUT);

  dln(startupDBG, "Starting tasks");

  xTaskCreate(
    provisionAndUpdate,
    "Provision wifi",
    6144,
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

void provisionAndUpdate(void *parameter) {
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
  vTaskDelete(NULL);
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

void writeI2CRegister8bit(int addr, int value) {
  Wire.beginTransmission(addr);
  Wire.write(value);
  Wire.endTransmission();
}

unsigned int readI2CRegister16bit(int addr, int reg) {
  Wire.beginTransmission(addr);
  Wire.write(reg);
  Wire.endTransmission();
  vTaskDelay(50/portTICK_PERIOD_MS);  
  Wire.requestFrom(addr, 2);
  unsigned int t = Wire.read() << 8;
  t = t | Wire.read();
  return t;
}

unsigned int readI2CRegister8bit(int addr, int reg) {
  Wire.beginTransmission(addr);
  Wire.write(reg);
  Wire.endTransmission(); 
  vTaskDelay(10/portTICK_PERIOD_MS);
  Wire.requestFrom(addr, 1);
  unsigned int t = Wire.read();
  return t;
}

void pump(int pin, int time) {
  digitalWrite(pin, HIGH);
  vTaskDelay(time * 1000 / portTICK_PERIOD_MS);
  digitalWrite(pin, LOW);
}

void readSensors(void *parameter) {
  unsigned int cap_val;
  vTaskDelay(2000 / portTICK_PERIOD_MS);
  for(;;){
    cap_val = readI2CRegister16bit(0x20, 0); //read capacitance register
    Serial.println(cap_val); 
    if (cap_val > 500) {
      cap_val = cap_thresh;
    };
    if (cap_val < cap_thresh) {
      pump(pump1, pump_time);
    };
    vTaskDelay(poll_time * 1000 / portTICK_PERIOD_MS);
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