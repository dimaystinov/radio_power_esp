#pragma once
struct MDNSStub {bool begin(const char*){return true;} void addService(const char*,const char*,int){} };
inline MDNSStub MDNS;
