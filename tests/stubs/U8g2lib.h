#pragma once
#include "Arduino.h"
#define U8G2_R0 0
#define U8X8_PIN_NONE 255
inline const uint8_t u8g2_font_5x7_tf[]={5};
inline const uint8_t u8g2_font_logisoso16_tn[]={10};
struct OledText {int x,y;std::string text;};
inline std::vector<std::vector<OledText>> oledFrames;
class U8G2_SSD1306_72X40_ER_F_HW_I2C {
 std::vector<OledText> text;
 const uint8_t* font=u8g2_font_5x7_tf;
 public:
 U8G2_SSD1306_72X40_ER_F_HW_I2C(int,int,int=-1,int=-1){}
 void setI2CAddress(uint8_t){} void setBusClock(uint32_t){} void begin(){}
 void clearBuffer(){text.clear();}
 void setFont(const uint8_t* f){font=f;}
 int getStrWidth(const char* s){return std::strlen(s)*font[0];}
 void drawStr(int x,int y,const char* s){text.push_back({x,y,s});}
 void sendBuffer(){oledFrames.push_back(text);fakeNow+=10;}
};
