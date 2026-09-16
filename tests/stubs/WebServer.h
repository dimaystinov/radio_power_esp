#pragma once
#include "Arduino.h"
#define HTTP_GET 0
#define HTTP_POST 1
class WebServer {
 public:
 std::map<std::string,String> args; int response=0;
 explicit WebServer(int){}
 template<class T> void on(const char*,int,T){}
 template<class T> void onNotFound(T){}
 void begin(){} void handleClient(){}
 bool hasArg(const char* s){return args.count(s);}
 String arg(const char* s){return args[s];}
 void sendHeader(const char*,const char*,bool){}
 void send(int code,const char*,const String&){response=code;}
};
