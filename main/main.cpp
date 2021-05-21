// main.cpp
int firmwareVersion = 5;

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

// LED pins
int redLED = 16;
int yellowLED = 17;
int greenLED = 21;

// Function prototypes
void flash(void *);
void provisionAndUpdate(void *);

/////////////////////////////////////////////////////////////////////////////
// arduino-land entry points

void setup() {
  dln(startupDBG, "\nsetup ProjectThing");
  Serial.begin(115200);
  Serial.println("arduino started");
  getMAC(MAC_ADDRESS);
  Serial.printf("\nsetup...\nESP32 MAC = %s\n", MAC_ADDRESS);
  apSSID.concat(MAC_ADDRESS);
  // WiFi provisioning or connection
  Serial.printf("doing wifi manager\n");

  // Set up leds
  pinMode(redLED, OUTPUT);
  pinMode(yellowLED, OUTPUT);
  pinMode(greenLED, OUTPUT);

  // Start tasks
  // xTaskCreate(
  //   provisionAndUpdate,    // Function that should be called
  //   "Provision WiFi and OTA",   // Name of the task (for debugging)
  //   4096,            // Stack size (bytes)
  //   NULL,            // Parameter to pass
  //   1,               // Task priority
  //   NULL             // Task handle
  // );
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
    flash,    // Function that should be called
    "Flash red",   // Name of the task (for debugging)
    1000,            // Stack size (bytes)
    (void*)&redLED,  // Parameter to pass
    2,               // Task priority
    NULL             // Task handle
  );

  xTaskCreate(
    flash,    // Function that should be called
    "Flash yellow",   // Name of the task (for debugging)
    1000,            // Stack size (bytes)
    (void*)&yellowLED,            // Parameter to pass
    2,               // Task priority
    NULL             // Task handle
  );
  dln(startupDBG, "All tasks started");
} // setup

void loop() {
  vTaskDelay(10/portTICK_PERIOD_MS);
} // loop

void flash(void *parameter) {
  int pin = *((int*)parameter);
  Serial.println(pin);
  for(;;){
    digitalWrite(pin, HIGH);
    vTaskDelay(1000 / portTICK_PERIOD_MS);
    digitalWrite(pin, LOW);
    vTaskDelay(1000 / portTICK_PERIOD_MS);
  }
}

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