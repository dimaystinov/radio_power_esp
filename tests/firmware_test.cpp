// Exercise the entire firmware translation unit; only hardware/HTTP/MAVLink
// serialization are stubbed. Each scenario runs in its own process.
#include <Wire.h>
#include <U8g2lib.h>
#include "../src/main.cpp"
#include <cassert>
#include <string>
void tick(uint32_t duration) {
 const uint32_t start=millis();
 while(uint32_t(millis()-start)<duration) {loop();fakeNow+=2;}
}
void rc(uint16_t value,uint8_t system=1,uint8_t component=1) {
 handleMavlinkMessage({MAVLINK_MSG_ID_RC_CHANNELS,system,component,value,1500});
}
void powerButton(const char* index) {
 webServer.args={{"power",String(index)}};handleWebPowerChange();processWebCommands();
}
void autoButton() {
 webServer.args={{"mode",String("auto")}};handleWebPowerChange();processWebCommands();
}
bool sentPower(uint8_t oneBased) {
 for(const auto& f:smartAudioSerial.writes)
  if(f.bytes.size()>=6 && f.bytes[3]==5 && f.bytes[5]==oneBased) return true;
 return false;
}
bool screenHas(const char* value) {
 if(oledFrames.empty()) return false;
 for(const auto& text:oledFrames.back()) if(text.text==value) return true;
 return false;
}
int main(int argc,char** argv) {
 assert(argc==2);std::string name=argv[1];
 handleMavlinkMessage({MAVLINK_MSG_ID_HEARTBEAT,1,1});
 if(name=="invalid_auto") {
  powerButton("0");rc(1000);fakeNow+=10;rc(65535);autoButton();tick(900);
  assert(!sentPower(6));assert(activePowerIndex==0);
 } else if(name=="boot_failsafe") {
  vtxPowerLevel=5;recordSmartAudioSettings();tick(4000);
  assert(sentPower(1));
 } else if(name=="foreign_rc") {
  activePowerIndex=0;rc(2000,42,99);tick(800);
  assert(!sentPower(6));
 } else if(name=="desired_survives_readback") {
  powerButton("5");vtxPowerLevel=0;recordSmartAudioSettings();
  assert(activePowerIndex==5);
  smartAudioSerial.writes.clear();tick(4000);assert(sentPower(6));
 } else if(name=="responsive_loop") {
  rc(2000);
  for(int i=0;i<1500;++i) {auto before=millis();loop();assert(uint32_t(millis()-before)<50);fakeNow+=2;}
 } else if(name=="status_expires") {
  vtxPowerLevel=0;recordSmartAudioSettings();tick(12000);assert(!vtxStatusValid);
 } else if(name=="invalid_http") {
  webServer.args={{"band","abc"},{"channel","0"}};handleWebPowerChange();assert(webServer.response==400);
 } else if(name=="failsafe_cancels_high_power") {
  rc(2000);tick(250);assert(sentPower(6));
  smartAudioSerial.writes.clear();rc(65535);tick(900);
  assert(!sentPower(6));assert(sentPower(1));
 } else if(name=="rc_timeout") {
  rc(2000);tick(900);assert(sentPower(6));
  smartAudioSerial.writes.clear();tick(2100);assert(sentPower(1));
 } else if(name=="manual_ignores_timeout") {
  powerButton("5");rc(1000);tick(5000);
  assert(activePowerIndex==5);assert(!sentPower(1));
 } else if(name=="fresh_auto") {
  powerButton("0");rc(1800);autoButton();tick(900);assert(sentPower(5));
 } else if(name=="heartbeat_binding") {
  handleMavlinkMessage({MAVLINK_MSG_ID_HEARTBEAT,42,1});rc(2000,42,1);tick(900);
  assert(targetSystem==1);assert(!sentPower(6));
 } else if(name=="millis_wrap") {
  fakeNow=UINT32_MAX-500;rc(2000);tick(900);assert(sentPower(6));
  tick(2100);assert(sentPower(1));
 } else if(name=="pwm_edges") {
  assert(powerIndexFromPwm(800)==0);assert(powerIndexFromPwm(1166)==0);
  assert(powerIndexFromPwm(1167)==1);assert(powerIndexFromPwm(1333)==1);
  assert(powerIndexFromPwm(1334)==2);assert(powerIndexFromPwm(1499)==2);
  assert(powerIndexFromPwm(1500)==3);assert(powerIndexFromPwm(1666)==3);
  assert(powerIndexFromPwm(1667)==4);assert(powerIndexFromPwm(1833)==4);
  assert(powerIndexFromPwm(1834)==5);assert(powerIndexFromPwm(2200)==5);
 } else if(name=="status_reply") {
  powerButton("5");tick(650);
  // Wait for GET_SETTINGS, then supply the observed TX3000AC reply after echo.
  for(int i=0;i<1000 && saPhase!=SaPhase::StatusWait;++i) {loop();fakeNow+=2;}
  assert(saPhase==SaPhase::StatusWait);
  for(uint8_t b : {0x00,0xAA,0x55,0x03,0x00,0x9F,0xAA,0x55,0x11,0x0E,0x09,0x05}) smartAudioRxSerial.rx.push_back(b);
  tick(50);assert(vtxStatusValid);assert(vtxPowerLevel==5);assert(vtxChannelIndex==9);
  smartAudioSerial.writes.clear();tick(3000);assert(!sentPower(6));
 } else if(name=="frequency_readback") {
  powerButton("0");sendSmartAudioBandChannel(4,7);
  vtxChannelIndex=0;vtxPowerLevel=0;recordSmartAudioSettings();
  assert(activeBand==4 && activeChannel==7);tick(1600);
  bool found=false;for(const auto& f:smartAudioSerial.writes)
   if(f.bytes.size()>=6 && f.bytes[3]==7 && f.bytes[5]==39) found=true;
  assert(found);
 } else if(name=="parser_rejects_noise") {
  const uint8_t bad[]={0xAA,0x55,0x11,0x0E,40,0,0xAA,0x55,0x11,0x0E,0,6};
  assert(!parseSmartAudioSettings(bad,sizeof bad));
  const uint8_t shortReply[]={0xAA,0x55,0x11,0x0E,0};
  assert(!parseSmartAudioSettings(shortReply,sizeof shortReply));
 } else if(name=="oled_startup") {
  setup();tick(300);
  assert(screenHas("--"));assert(screenHas("SAFE NO VTX"));assert(screenHas("SET 25mW"));
 } else if(name=="oled_reported_not_requested") {
  setup();powerButton("5");vtxPowerLevel=2;recordSmartAudioSettings();tick(300);
  assert(screenHas("500"));assert(!screenHas("3000"));
  assert(screenHas("MANUAL"));assert(screenHas("SET 3000mW"));
 } else if(name=="oled_stale") {
  setup();powerButton("5");vtxPowerLevel=5;recordSmartAudioSettings();tick(300);
  assert(screenHas("3000"));assert(screenHas("VTX OK"));
  tick(6500);assert(screenHas("--"));assert(screenHas("MANUAL NO VTX"));
 } else if(name=="oled_missing") {
  Wire.present=false;setup();tick(3000);
  assert(oledFrames.empty());assert(sentPower(1));
 } else if(name=="oled_updates_bounded") {
  setup();powerButton("5");
  const auto before=oledFrames.size();
  for(int i=0;i<500;++i){vtxPowerLevel=i%6;recordSmartAudioSettings();loop();fakeNow+=2;}
  assert(oledFrames.size()>before);assert(oledFrames.size()-before<15);
 } else {return 2;}
}
