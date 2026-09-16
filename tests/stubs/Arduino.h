#pragma once
#include <cstdint>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>
#include <deque>
#include <map>
#include <algorithm>
#define F(s) s
#define HEX 16
#define INPUT 0
#define SERIAL_8N2 2
#define SERIAL_8N1 1
inline uint32_t fakeNow = 10000;
inline uint32_t millis() { return fakeNow; }
inline void delay(uint32_t ms) { fakeNow += ms; }
inline void pinMode(int, int) {}
class String {
 std::string value;
 public:
 String() = default;
 String(const char* s):value(s){}
 String(const std::string& s):value(s){}
 String(char c):value(1,c){}
 template<class T> String(T n, int base=10) { char b[40]; if(base==16) std::snprintf(b,sizeof b,"%llx",(unsigned long long)n); else std::snprintf(b,sizeof b,"%lld",(long long)n); value=b; }
 void reserve(size_t n){value.reserve(n);}
 size_t length()const{return value.size();}
 char operator[](size_t n)const{return value[n];}
 long toInt()const{return std::strtol(value.c_str(),nullptr,10);}
 const char* c_str()const{return value.c_str();}
 String& operator+=(const String& s){value+=s.value;return *this;}
 bool operator==(const char* s)const{return value==s;}
};
struct WireFrame { uint32_t at; std::vector<uint8_t> bytes; };
class HardwareSerial {
 public:
 int id; uint32_t baud=115200; std::vector<WireFrame> writes; std::deque<uint8_t> rx;
 explicit HardwareSerial(int n):id(n){}
 void begin(uint32_t b, int=0,int=-1,int=-1,bool=false){baud=b;}
 void end(){}
 int available(){return rx.size();}
 int read(){auto b=rx.front();rx.pop_front();return b;}
 size_t write(const uint8_t* b,size_t n){writes.push_back({fakeNow,{b,b+n}});return n;}
 void flush(){if(!writes.empty()) fakeNow+=(writes.back().bytes.size()*11000+baud-1)/baud;}
 void setTxTimeoutMs(int){}
 template<class... T> void printf(const char*,T...){}
 void println(const char*){}
};
inline HardwareSerial Serial(99);
