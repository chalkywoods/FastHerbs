// joinme-wfmgr.cpp
// alternative AP/webserver based on https://github.com/tzapu/WiFiManager

#include "joinme-2021.h"
/*
TODO port to new IDF...

#include "WiFiManager.h"

WiFiManager wm; // global wm instance
WiFiManagerParameter custom_field; // global param (for non blocking w params)
String getParam(String name);
void saveParamCallback();

bool joinmeManageWiFi(const char *apSSID, const char *apKey) {
  // uncomment for testing
  // wm.resetSettings(); // wipe settings

  int customFieldLength = 40; // add a custom input field

  // new (&custom_field) WiFiManagerParameter("customfieldid", "Custom Field
  // Label", "Custom Field Value", customFieldLength,"placeholder=\"Custom
  // Field Placeholder\"");
  
  // test custom html input type(checkbox) new (&custom_field)
  // WiFiManagerParameter("customfieldid", "Custom Field Label", "Custom Field
  // Value", customFieldLength,"placeholder=\"Custom Field Placeholder\"
  // type=\"checkbox\""); // custom html type
  
  // test custom html(radio)
  const char* custom_radio_str = "<br/><label for='customfieldid'>Custom Field Label</label><input type='radio' name='customfieldid' value='1' checked> One<br><input type='radio' name='customfieldid' value='2'> Two<br><input type='radio' name='customfieldid' value='3'> Three";
  new (&custom_field) WiFiManagerParameter(custom_radio_str); // custom i/p
  wm.addParameter(&custom_field);
  wm.setSaveParamsCallback(saveParamCallback);

  // custom menu via array or vector
  // 
  // menu tokens,
  // "wifi","wifinoscan","info","param","close","sep","erase","restart","exit"
  // (sep is seperator) (if param is in menu, params will not show up in wifi
  // page!) const char* menu[] =
  // {"wifi","info","param","sep","restart","exit"}; wm.setMenu(menu,6);
  std::vector<const char *> menu =
    {"wifi","info","param","sep","restart","exit"};
  wm.setMenu(menu);

  wm.setClass("invert"); // set dark theme

  wm.setConnectTimeout(15); // how long to try to connect before continuing
  wm.setConfigPortalTimeout(120); // auto close configportal after n seconds
  // wm.setAPClientCheck(true); // avoid timeout if client connected to softap

  // wifi scan settings
  // wm.setRemoveDuplicateAPs(false); // do not remove duplicate ap names
  // wm.setMinimumSignalQuality(20);  // min RSSI % to show in scans, null=8%
  // wm.setShowInfoErase(false);      // do not show erase button on info page
  wm.setScanDispPerc(true);           // show RSSI as % not graph icons

  bool res = wm.autoConnect(apSSID, apKey); // start password protected AP

  if(!res) {
    Serial.println("Failed to connect or hit timeout");
  } else {
    //if you get here you have connected to the WiFi    
// TODO unnesc?    wm.stopConfigPortal();
//  wm.stopWebPortal();
    Serial.println("connected...yeey :)");
  }

  return res;
}

// read parameter from server, for custom hmtl input
String getParam(String name) {
  String value;
  if(wm.server->hasArg(name)) {
    value = wm.server->arg(name);
  }
  return value;
}

void saveParamCallback() {
  Serial.println("[CALLBACK] saveParamCallback fired");
  Serial.println("PARAM customfieldid = " + getParam("customfieldid"));
}
*/
