// joinme-2021.cpp
// a derivative of the Lua Joinme wifi config utility that started in Jan 2015 here
// https://github.com/hamishcunningham/fishy-wifi/commit/33bb0f352a9172681afaf73ada29b332d69d2b28
// evolved here
// https://github.com/hamishcunningham/fishy-wifi/tree/master/lua/joinme and
// here https://github.com/hamishcunningham/fishy-wifi/tree/master/ardesp/waterelf32
// and now lives here...

#include "joinme-2021.h"
#include <WiFiClient.h>
#include <DNSServer.h>
#include <Update.h>
#include <ESPAsyncWebServer.h>

const byte DNS_PORT = 53;
DNSServer dnsServer;
IPAddress apIP_(192, 168, 4, 1);
void handleOTAProgress(size_t, size_t); // progress tracker
const uint16_t INITIAL_CONNECTION_TRIES = 100;

// web server utils /////////////////////////////////////////////////////////
// the replacement_t type definition allows specification of a subset of the
// "boilerplate" strings, so we can e.g. replace only the title, or etc.
typedef struct { int position; const char *replacement; } replacement_t;
void getHtml(String& html, const char *[], int, replacement_t [], int);
#define ALEN(a) ((int) (sizeof(a) / sizeof(a[0]))) // only in definition scope!
#define GET_HTML(strout, boiler, repls) \
  getHtml(strout, boiler, ALEN(boiler), repls, ALEN(repls));
AsyncWebServer* webServer;              // async web server

// 307 is temporary redirect. if we used 301 we'd probably break the user's
// browser for sites they were captured from until they cleared their cache
int TEMPORARY_REDIRECT = 307;

// function protos ///
void initWebServer();
void hndlRoot(AsyncWebServerRequest *);
void hndlNotFound(AsyncWebServerRequest *);
void hndlWifichz(AsyncWebServerRequest *);
void hndlStatus(AsyncWebServerRequest *);
void hndlWifi(AsyncWebServerRequest *);
void apListForm(String&);

void printIPs() {
  Serial.print("AP SSID: ");
  Serial.print(apSSID);
  Serial.print("; IP address(es): local=");
  Serial.print(WiFi.localIP());
  Serial.print("; AP=");
  Serial.println(WiFi.softAPIP());
}

// this used to be implemented in joinme-wfmgr-2021.cpp, but that's broken in
// recent IDF...
AsyncWebServer* joinmeManageWiFi(const char *apSSID, const char *apKey) {
  WiFi.begin();
  webServer = new AsyncWebServer(80);

  Serial.print("connecting to wifi...");
  uint8_t connectionTries = 0;
  bool wstatus = false;

  // wait a little for stored credentials (if any) to get a connection
  while( WiFi.status() != WL_CONNECTED ) { // wait for connection
    if(connectionTries++ == INITIAL_CONNECTION_TRIES)
      break;
    vTaskDelay(50 / portTICK_PERIOD_MS);  // let wifi settle
  }
  if(WiFi.status() == WL_CONNECTED)
    return webServer;

  // start access point and captive portal to serve provisioning pages
  WiFi.mode(WIFI_AP_STA);
  //WiFi.mode(WIFI_STA);
  WiFi.softAP(apSSID, apKey);
  printIPs();

  // set up webserver
  Serial.printf("setting up a web server\n");
  initWebServer();
  // captive portal
  joinmeDNSSetup(webServer, apIP_);

  // get on the network
  while( WiFi.status() != WL_CONNECTED) { // wait for connection
    Serial.print(".");
    if(connectionTries++ % 80 == 0)       // print a line break for readability
      Serial.println("");
    vTaskDelay(50 / portTICK_PERIOD_MS);  // let wifi settle
    joinmeTurn();                         // service DNS requests
  }
  return webServer;
}

void doRedirect(AsyncWebServerRequest* request) {
  Serial.printf(
    "joinme redirecting captured client to: %s\n",
    apIP_.toString().c_str()
  );
  auto response = request->beginResponse(TEMPORARY_REDIRECT,"text/plain","");
  response->addHeader("Location","http://"+apIP_.toString()+"/");
  request->send(response);
}

void handleL0(AsyncWebServerRequest* request) {
  doRedirect(request);
}
void handleL2(AsyncWebServerRequest* request) {
  doRedirect(request);
}
void handleALL(AsyncWebServerRequest* request) {
  doRedirect(request);
}

void joinmeDNSSetup(void* server_p, IPAddress apIP) {
  AsyncWebServer* server = (AsyncWebServer *) server_p;
  assert(server != NULL);
  apIP_ = apIP;
  Serial.printf(
    "joinme will direct captured clients to: %s\n",
    apIP_.toString().c_str()
  );
  dnsServer.setErrorReplyCode(DNSReplyCode::NoError);
  dnsServer.start(DNS_PORT, "*", apIP_);
  Serial.println("joinme captive dns server started");
  /*
  server->on("/generate_204", doRedirect); // android captive portal
  server->on("/L0", handleL0);
  server->on("/L2", handleL2);
  server->on("/ALL", handleALL);
  Serial.println("joinme http handlers added");
  */
}

void joinmeTurn() {
  dnsServer.processNextRequest();
}

// OTA over-the-air update stuff ///////////////////////////////////////////
// if the repo is public set gitToken to "" (otherwise the API must be used
// and a valid personal access token supplied)
void joinmeOTAUpdate(
  int firmwareVersion, String gitProjID, String gitToken, String relPath
) {
  // materials for doing an HTTP GET on github from the firmware/ dir
  HTTPClient http;              // manage the HTTP request process
  http.setTimeout(20000);       // increase the timeout of a request
  http.setReuse(true);          // keep alive
  int respCode;    // the response code from the request (e.g. 404, 200, ...)
  int highestAvailableVersion = -1;  // version of latest firmware on server
  String repoPath;
  if(gitToken.length() == 0) {  // use raw
    relPath.replace("%2F", "/");
    repoPath = relPath;
  } else {                      // use API
    repoPath = "/repository/files/" + relPath;
  }

  // do a GET to read the version file from the cloud
  Serial.println("checking for firmware updates...");
  respCode = joinmeCloudGet(&http, gitProjID, gitToken, repoPath + "version");
  if(respCode == 200) // check response code (-ve on failure)
    highestAvailableVersion = atoi(http.getString().c_str());
  else
    Serial.printf("couldn't get version! rtn code: %d\n", respCode);
  http.end(); // free resources

  // do we know the latest version, and does the firmware need updating?
  if(respCode != 200) {
    Serial.printf("cannot update\n\n");
    return;
  } else if(firmwareVersion >= highestAvailableVersion) {
    Serial.printf("firmware is up to date\n\n");
    return;
  }

  // ok, we need to do a firmware update...
  Serial.printf(
    "upgrading firmware from version %d to version %d\n",
    firmwareVersion, highestAvailableVersion
  );

  // name of the .bin we're looking for
  String binName = String(highestAvailableVersion);
  binName += "%2Ebin";

  // IDF native update
  // TODO choose one or the other...
  if(false) {
    String baseUrl = "https://gitlab.com/api/v4/projects/";
    String url = baseUrl + gitProjID + repoPath + binName +
      "/raw?private_token=" + gitToken + "&ref=master";
    idf_ota_update(url.c_str());
    return;
  }

  // do a GET for the .bin
  respCode = joinmeCloudGet(&http, gitProjID, gitToken, repoPath + binName);
  int updateLength = http.getSize(); // if isn't big enough refuse to update
  if(respCode == 200) {              // check response code (-ve on failure)
    Serial.printf(".bin code/size: %d; %d\n\n", respCode, updateLength);
    if(updateLength < 174992) {      // the size of the Blink example sketch
      Serial.println("update size is too small! refusing to try OTA update");
      return;
    }
  } else {
    Serial.printf("failed to get a .bin! return code is: %d\n", respCode);
    http.end(); // free resources
    return;
  }

  // write the new version of the firmware to flash
  WiFiClient* stream = http.getStreamPtr();
  Update.onProgress(handleOTAProgress); // print out progress
  if(Update.begin(updateLength)) {
    Serial.printf("starting OTA, may take a minute or two...\n");
    size_t written; // how much has been written
    written = Update.writeStream(*stream);
    stream->flush();
    Serial.printf("update written\n");

    // if the written amount is the same length as or longer than the
    // original content length, then it should have been written to the
    // device successfully
    if (written >= updateLength) {
      Serial.println("written " + String(written) + " successfully");
    } else {
      Serial.println(
        "oops, written only " + String(written) + "/" + String(updateLength)
      );
    }

    // if the end function returns a successful boolean then the OTA has been
    // completed, otherwise output the error
    if(Update.end()) {
      Serial.printf("update done, now finishing...\n");
      if(Update.isFinished()) {
        Serial.printf("update successfully finished; rebooting...\n\n");
        ESP.restart();
      } else {
        Serial.printf("update didn't finish correctly :(\n");
      }
    } else {
      Serial.printf("an update error occurred, #: %d\n" + Update.getError());
    }
  } else {
    Serial.printf("not enough space to start OTA update :(\n");
  }
  http.end(); // free resources
}

// helper for downloading from cloud firmware server via HTTP GET
int joinmeCloudGet(
  HTTPClient *http, String gitProjID, String gitToken, String fileName
) {
  // build up URL from components; for example:
  // https://gitlab.com/api/v4/projects/_GITLAB_PROJ_ID/repository/files/
  // examples%2FOTAThing%2Ffirmware%2F4.bin/raw?private_token=_GITLAB_TOKEN
  // &ref=master

  // the gitlab root certificate (for HTTPS); see
  // https://techtutorialsx.com/2017/11/18/esp32-arduino-https-get-request/
  // for how to pick these up using firefox
  /*
  // 2019 version
  const char* gitlabRootCA =
    "-----BEGIN CERTIFICATE-----\n" \
    "MIIDdTCCAl2gAwIBAgILBAAAAAABFUtaw5QwDQYJKoZIhvcNAQEFBQAwVzELMAkG\n" \
    "A1UEBhMCQkUxGTAXBgNVBAoTEEdsb2JhbFNpZ24gbnYtc2ExEDAOBgNVBAsTB1Jv\n" \
    "b3QgQ0ExGzAZBgNVBAMTEkdsb2JhbFNpZ24gUm9vdCBDQTAeFw05ODA5MDExMjAw\n" \
    "MDBaFw0yODAxMjgxMjAwMDBaMFcxCzAJBgNVBAYTAkJFMRkwFwYDVQQKExBHbG9i\n" \
    "YWxTaWduIG52LXNhMRAwDgYDVQQLEwdSb290IENBMRswGQYDVQQDExJHbG9iYWxT\n" \
    "aWduIFJvb3QgQ0EwggEiMA0GCSqGSIb3DQEBAQUAA4IBDwAwggEKAoIBAQDaDuaZ\n" \
    "jc6j40+Kfvvxi4Mla+pIH/EqsLmVEQS98GPR4mdmzxzdzxtIK+6NiY6arymAZavp\n" \
    "xy0Sy6scTHAHoT0KMM0VjU/43dSMUBUc71DuxC73/OlS8pF94G3VNTCOXkNz8kHp\n" \
    "1Wrjsok6Vjk4bwY8iGlbKk3Fp1S4bInMm/k8yuX9ifUSPJJ4ltbcdG6TRGHRjcdG\n" \
    "snUOhugZitVtbNV4FpWi6cgKOOvyJBNPc1STE4U6G7weNLWLBYy5d4ux2x8gkasJ\n" \
    "U26Qzns3dLlwR5EiUWMWea6xrkEmCMgZK9FGqkjWZCrXgzT/LCrBbBlDSgeF59N8\n" \
    "9iFo7+ryUp9/k5DPAgMBAAGjQjBAMA4GA1UdDwEB/wQEAwIBBjAPBgNVHRMBAf8E\n" \
    "BTADAQH/MB0GA1UdDgQWBBRge2YaRQ2XyolQL30EzTSo//z9SzANBgkqhkiG9w0B\n" \
    "AQUFAAOCAQEA1nPnfE920I2/7LqivjTFKDK1fPxsnCwrvQmeU79rXqoRSLblCKOz\n" \
    "yj1hTdNGCbM+w6DjY1Ub8rrvrTnhQ7k4o+YviiY776BQVvnGCv04zcQLcFGUl5gE\n" \
    "38NflNUVyRRBnMRddWQVDf9VMOyGj/8N7yy5Y0b2qvzfvGn9LhJIZJrglfCm7ymP\n" \
    "AbEVtQwdpf5pLGkkeB6zpxxxYu7KyJesF12KwvhHhm4qxFYxldBniYUr+WymXUad\n" \
    "DKqC5JlR3XC321Y9YeRq4VzW9v493kHMB65jUr9TU/Qr6cf9tveCX4XSQRjbgbME\n" \
    "HMUfpIBvFSDJ3gyICh3WZlXi/EjJKSZp4A==\n" \
    "-----END CERTIFICATE-----\n"
    ;
*/

  // version of Feb 2021
  const char* gitlabRootCA =
    "-----BEGIN CERTIFICATE-----\n" \
    "MIIGBzCCBO+gAwIBAgIQP8Jvo234xjti44c2dq2FnDANBgkqhkiG9w0BAQsFADCB\n" \
    "jzELMAkGA1UEBhMCR0IxGzAZBgNVBAgTEkdyZWF0ZXIgTWFuY2hlc3RlcjEQMA4G\n" \
    "A1UEBxMHU2FsZm9yZDEYMBYGA1UEChMPU2VjdGlnbyBMaW1pdGVkMTcwNQYDVQQD\n" \
    "Ey5TZWN0aWdvIFJTQSBEb21haW4gVmFsaWRhdGlvbiBTZWN1cmUgU2VydmVyIENB\n" \
    "MB4XDTIxMDEyMTAwMDAwMFoXDTIxMDUxMTIzNTk1OVowFTETMBEGA1UEAxMKZ2l0\n" \
    "bGFiLmNvbTCCASIwDQYJKoZIhvcNAQEBBQADggEPADCCAQoCggEBANXnhcvOl289\n" \
    "8oMglaax6bDz988oNMpXZCH6sI7Fzx9G/isEPObN6cyP+fjFa0dvwRmOHnepk2eo\n" \
    "bzcECdgdBLCa7E29p7lLF0NFFTuIb52ew58fK/209XJ3amvjJ/m5rPP00uHrT+9v\n" \
    "ky2jkQUQszuC9R4vK+tfs2S5z9w6qh3hwIJecChzWKce8hRZdiO9S7ix/6ZNiAgw\n" \
    "Y2h8AiG0VruPOJ6PbNXOFUTsajK0EP8AzJfNDIjvWHjUOawR352m4eKxXvXm9knd\n" \
    "B/w1gY90jmAQ9JIiyOm+QlmHwO+qQUpWYOxt5Xnb0Pp/RRHEtxDgjygQWajAwsxG\n" \
    "obx6sCf6+qcCAwEAAaOCAtYwggLSMB8GA1UdIwQYMBaAFI2MXsRUrYrhd+mb+ZsF\n" \
    "4bgBjWHhMB0GA1UdDgQWBBTFjbuGoOUrgk9Dhr35DblkBZCj1jAOBgNVHQ8BAf8E\n" \
    "BAMCBaAwDAYDVR0TAQH/BAIwADAdBgNVHSUEFjAUBggrBgEFBQcDAQYIKwYBBQUH\n" \
    "AwIwSQYDVR0gBEIwQDA0BgsrBgEEAbIxAQICBzAlMCMGCCsGAQUFBwIBFhdodHRw\n" \
    "czovL3NlY3RpZ28uY29tL0NQUzAIBgZngQwBAgEwgYQGCCsGAQUFBwEBBHgwdjBP\n" \
    "BggrBgEFBQcwAoZDaHR0cDovL2NydC5zZWN0aWdvLmNvbS9TZWN0aWdvUlNBRG9t\n" \
    "YWluVmFsaWRhdGlvblNlY3VyZVNlcnZlckNBLmNydDAjBggrBgEFBQcwAYYXaHR0\n" \
    "cDovL29jc3Auc2VjdGlnby5jb20wggEEBgorBgEEAdZ5AgQCBIH1BIHyAPAAdwB9\n" \
    "PvL4j/+IVWgkwsDKnlKJeSvFDngJfy5ql2iZfiLw1wAAAXcibDhmAAAEAwBIMEYC\n" \
    "IQDo47YwgocdXSKo7GoYpGrClgU/2wwzTV4hmE1KohthRwIhAIzTCXpVxdr+PSnT\n" \
    "A/a23xroYGkKalUp8qxVXeSXpySiAHUAlCC8Ho7VjWyIcx+CiyIsDdHaTV5sT5Q9\n" \
    "YdtOL1hNosIAAAF3Imw5nQAABAMARjBEAiBMQ+C7xsEmzlvjfGtnXZkMxovN3uYa\n" \
    "c90g/vAqrND5vQIgOAvNRPWZxbPuoynPGwZMzeKBdxfAdRrWNoV1b5+bi7kweQYD\n" \
    "VR0RBHIwcIIKZ2l0bGFiLmNvbYIPYXV0aC5naXRsYWIuY29tghRjdXN0b21lcnMu\n" \
    "Z2l0bGFiLmNvbYIaZW1haWwuY3VzdG9tZXJzLmdpdGxhYi5jb22CD2dwcmQuZ2l0\n" \
    "bGFiLmNvbYIOd3d3LmdpdGxhYi5jb20wDQYJKoZIhvcNAQELBQADggEBAIowyGA1\n" \
    "w0OOJE/5zSZuMbKxlgSbvNUNjd+uEvCYvVT8qs9rQB4EK8Cw7Q7dNscH9A5h6Bd/\n" \
    "L0WQTFYo4fTAi3EA21Yh8lYtgTaE/zaWZSD4mz/VcAyQbRT9vSRKLkw5A2PrgYhx\n" \
    "gkesMoSX/v3Xmf61SeqTgFWSUn1LD4UqEYhRHjHG3ROsmN5Mb5PYJFwOJ1ABhCnM\n" \
    "gxwIc0vobt/s3cY+rglfC7euME8H1CSE1ICDmDx0BAJ5SypuQalBv2gPzUztk5OM\n" \
    "uaGH+DjY4KkPVpjs2Ak79B/f41h1ldA0UJ51vUbFAfFOSYYOvEpljwllRbwKnWfl\n" \
    "2SUxaCNrl1vKTl4=\n" \
    "-----END CERTIFICATE-----\n" \
    ;

  // set up URL for download via either "raw" or the API with access tok
  String baseUrl = "https://gitlab.com";
  String url;
  if(gitToken == NULL || gitToken.length() == 0) { // use raw URL ////
    baseUrl += "/hamishcunningham/iot4/raw/master/";
    url = baseUrl + fileName;
  } else {                                         // use API ////////
    baseUrl += "/api/v4/projects/";
    url = baseUrl + gitProjID + fileName +
      "/raw?private_token=" + gitToken + "&ref=master";
  }

  // make GET request and return the response code
  Serial.printf("joinmeCloudGet, url = %s\n", url.c_str());

  // TODO certificate checking seems broken in 4.2; use first version to skip
  // the check
  http->begin(url);
  // http->begin(url, gitlabRootCA);

  http->addHeader("User-Agent", "ESP32");
  return http->GET();
}

// callback handler for tracking OTA progress
void handleOTAProgress(size_t done, size_t total) {
  float progress = (float) done / (float) total;
  // dbf(otaDBG, "OTA written %d of %d, progress = %f\n", done, total, progress);

  int barWidth = 70;
  Serial.printf("[");
  int pos = barWidth * progress;
  for(int i = 0; i < barWidth; ++i) {
    if(i < pos)
      Serial.printf("=");
    else if(i == pos)
      Serial.printf(">");
    else 
      Serial.printf(" ");
  }
  Serial.printf(
    "] %d %%%c", int(progress * 100.0), (progress == 1.0) ? '\n' : '\r'
  );
  Serial.flush();
}

// web server utils /////////////////////////////////////////////////////////
void getHtml( // turn array of strings & set of replacements into a String
  String& html, const char *boiler[], int boilerLen,
  replacement_t repls[], int replsLen
) {
  for(int i = 0, j = 0; i < boilerLen; i++) {
    if(j < replsLen && repls[j].position == i)
      html.concat(repls[j++].replacement);
    else
      html.concat(boiler[i]);
  }
}
const char *templatePage[] = {    // we'll use Ex07 templating to build pages
  "<html><head><title>",                                                //  0
  "default title",                                                      //  1
  "</title>\n",                                                         //  2
  "<meta charset='utf-8'>",                                             //  3
  "<meta name='viewport' content='width=device-width, initial-scale=1.0'>\n"
  "<style>body{background:#FFF; color: #000; font-family: sans-serif;", //  4
  "font-size: 150%;}</style>\n",                                        //  5
  "</head><body>\n",                                                    //  6
  "<h2>Welcome to ProUpdThing!</h2>\n",                                 //  7
  "<!-- page payload goes here... -->\n",                               //  8
  "<!-- ...and/or here... -->\n",                                       //  9
  "\n<p><a href='/'>Home</a>&nbsp;&nbsp;&nbsp;</p>\n",                  // 10
  "</body></html>\n\n",                                                 // 11
};
void initWebServer() { // changed naming conventions to avoid clash with Ex06
  // register callbacks to handle different paths
  webServer->on("/", hndlRoot);              // slash
  webServer->onNotFound(hndlNotFound);       // 404s...
  webServer->on("/generate_204", hndlRoot);  // Android captive portal support
  webServer->on("/L0", hndlRoot);            // erm, is this...
  webServer->on("/L2", hndlRoot);            // ...IoS captive portal...
  webServer->on("/ALL", hndlRoot);           // ...stuff?
  webServer->on("/wifi", hndlWifi);          // page for choosing an AP
  webServer->on("/wifichz", hndlWifichz);    // landing page for AP form submit
  webServer->on("/status", hndlStatus);      // status check, e.g. IP address

  webServer->begin();
  dln(startupDBG, "HTTP server started");
}

// webserver handler callbacks
void hndlNotFound(AsyncWebServerRequest *request) {
  dbg(netDBG, "URI Not Found: ");
  dln(netDBG, request->url());
  request->send(404, "text/plain", "URI Not Found");
}
void hndlRoot(AsyncWebServerRequest *request) {
  dln(netDBG, "serving page notionally at /");
  replacement_t repls[] = { // the elements to replace in the boilerplate
    {  1, apSSID.c_str() },
    {  8, "" },
    {  9, "<p>Choose a <a href=\"wifi\">wifi access point</a>.</p>" },
    { 10, "<p>Check <a href='/status'>wifi status</a>.</p>" },
  };
  String htmlPage = ""; // a String to hold the resultant page
  GET_HTML(htmlPage, templatePage, repls);
  request->send(200, "text/html", htmlPage);
}
void hndlWifi(AsyncWebServerRequest *request) {
  dln(netDBG, "serving page at /wifi");

  String form = ""; // a form for choosing an access point and entering key
  apListForm(form);
  replacement_t repls[] = { // the elements to replace in the boilerplate
    { 1, apSSID.c_str() },
    { 7, "<h2>Network configuration</h2>\n" },
    { 8, "" },
    { 9, form.c_str() },
  };
  String htmlPage = ""; // a String to hold the resultant page
  GET_HTML(htmlPage, templatePage, repls);

  request->send(200, "text/html", htmlPage);
}
void hndlWifichz(AsyncWebServerRequest *request) {
  dln(netDBG, "serving page at /wifichz");

  String title = "<h2>Joining wifi network...</h2>";
  String message = "<p>Check <a href='/status'>wifi status</a>.</p>";

  String ssid = "";
  String key = "";
  for(uint8_t i = 0; i < request->args(); i++ ) {
    if(request->argName(i) == "ssid")
      ssid = request->arg(i);
    else if(request->argName(i) == "key")
      key = request->arg(i);
  }

  if(ssid == "") {
    message = "<h2>Ooops, no SSID...?</h2>\n<p>Looks like a bug :-(</p>";
  } else {
    char ssidchars[ssid.length()+1];
    char keychars[key.length()+1];
    ssid.toCharArray(ssidchars, ssid.length()+1);
    key.toCharArray(keychars, key.length()+1);
    WiFi.begin(ssidchars, keychars);
  }

  replacement_t repls[] = { // the elements to replace in the template
    { 1, apSSID.c_str() },
    { 7, title.c_str() },
    { 8, "" },
    { 9, message.c_str() },
  };
  String htmlPage = "";     // a String to hold the resultant page
  GET_HTML(htmlPage, templatePage, repls);

  request->send(200, "text/html", htmlPage);
}
void hndlStatus(AsyncWebServerRequest *request) { // UI to check connectivity
  dln(netDBG, "serving page at /status");

  String s = "";
  s += "<ul>\n";
  s += "\n<li>SSID: ";
  s += WiFi.SSID();
  s += "</li>";
  s += "\n<li>Status: ";
  switch(WiFi.status()) {
    case WL_IDLE_STATUS:
      s += "WL_IDLE_STATUS</li>"; break;
    case WL_NO_SSID_AVAIL:
      s += "WL_NO_SSID_AVAIL</li>"; break;
    case WL_SCAN_COMPLETED:
      s += "WL_SCAN_COMPLETED</li>"; break;
    case WL_CONNECTED:
      s += "WL_CONNECTED</li>"; break;
    case WL_CONNECT_FAILED:
      s += "WL_CONNECT_FAILED</li>"; break;
    case WL_CONNECTION_LOST:
      s += "WL_CONNECTION_LOST</li>"; break;
    case WL_DISCONNECTED:
      s += "WL_DISCONNECTED</li>"; break;
    default:
      s += "unknown</li>";
  }

  s += "\n<li>Local IP: ";     s += ip2str(WiFi.localIP());
  s += "</li>\n";
  s += "\n<li>Soft AP IP: ";   s += ip2str(WiFi.softAPIP());
  s += "</li>\n";
  s += "\n<li>AP SSID name: "; s += apSSID;
  s += "</li>\n";

  s += "</ul></p>";

  replacement_t repls[] = { // the elements to replace in the boilerplate
    { 1, apSSID.c_str() },
    { 7, "<h2>Status</h2>\n" },
    { 8, "" },
    { 9, s.c_str() },
  };
  String htmlPage = ""; // a String to hold the resultant page
  GET_HTML(htmlPage, templatePage, repls);

  request->send(200, "text/html", htmlPage);
}
void apListForm(String& f) { // utility to create a form for choosing AP
  const char *checked = " checked";
  int n = WiFi.scanNetworks();
  dbg(netDBG, "scan done: ");

  if(n == 0) {
    dln(netDBG, "no networks found");
    f += "No wifi access points found :-( ";
    f += "<a href='/'>Back</a><br/><a href='/wifi'>Try again?</a></p>\n";
  } else {
    dbg(netDBG, n); dln(netDBG, " networks found");
    f += "<p>Wifi access points available:</p>\n"
         "<p><form method='POST' action='wifichz'> ";
    for(int i = 0; i < n; ++i) {
      f.concat("<input type='radio' name='ssid' value='");
      f.concat(WiFi.SSID(i));
      f.concat("'");
      f.concat(checked);
      f.concat(">");
      f.concat(WiFi.SSID(i));
      f.concat(" (");
      f.concat(WiFi.RSSI(i));
      f.concat(" dBm)");
      f.concat("<br/>\n");
      checked = "";
    }
    f += "<br/>Pass key: <input type='textarea' name='key'><br/><br/> ";
    f += "<input type='submit' value='Submit'></form></p>";
  }
}
String ip2str(IPAddress address) { // utility for printing IP addresses
  return
    String(address[0]) + "." + String(address[1]) + "." +
    String(address[2]) + "." + String(address[3]);
}
