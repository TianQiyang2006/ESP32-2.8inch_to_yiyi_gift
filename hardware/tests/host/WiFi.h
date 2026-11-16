#pragma once
#include "Arduino.h"
#define WL_CONNECTED 3
struct HostWiFi {int status(){return WL_CONNECTED;}String SSID(){return "Host test";}int RSSI(){return -45;}};
inline HostWiFi WiFi;
