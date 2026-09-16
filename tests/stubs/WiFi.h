#pragma once
#include "Arduino.h"
#define WIFI_AP 1
struct Address { String toString(){return "192.168.4.1";} };
struct WifiStub { void mode(int){} bool softAP(const char*,const char*){return true;} Address softAPIP(){return {};} };
inline WifiStub WiFi;
