#pragma once
#include "Arduino.h"
struct WireStub {
 bool present=true; int probes=0; int sda=-1,scl=-1;
 bool begin(int data=-1,int clock=-1,uint32_t=0){sda=data;scl=clock;return true;}
 void setTimeOut(uint16_t){}
 void beginTransmission(uint8_t){}
 uint8_t endTransmission(){++probes;return present?0:2;}
};
inline WireStub Wire;
