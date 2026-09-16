#pragma once

// User settings for the TX3000AC VTX controller.
// After changing this file rebuild and reflash the ESP32:
//   pio run -t upload

// ---- Wi-Fi access point --------------------------------------------------
#define CFG_AP_SSID "TX3000AC"        // network name (SSID)
#define CFG_AP_PASSWORD "robossembler" // WPA2 password, 8+ characters
#define CFG_AP_HOSTNAME "tx3000ac"    // mDNS name -> http://<hostname>.local/

// ---- Flight controller MAVLink UART --------------------------------------
#define CFG_MAV_RX_PIN 9    // FC T1 -> ESP (through 1 kOhm)
#define CFG_MAV_TX_PIN 10   // FC R1 <- ESP (through 1 kOhm)
#define CFG_MAV_BAUD 115200 // must match the FC UART baud rate

// ---- SmartAudio one-wire bus ---------------------------------------------
#define CFG_SMARTAUDIO_PIN 17 // ESP -> VTX SmartAudio pad (through 1 kOhm)

// ---- RC channel validity window ------------------------------------------
// RC11/RC12 PWM values outside this range are treated as no signal.
#define CFG_RC_VALID_MIN 800
#define CFG_RC_VALID_MAX 2200

// ---- RC11 power-switch zones ---------------------------------------------
// The PWM span is split into six equal zones, one per power level
// (zone 0 = 25 mW ... zone 5 = 3000 mW).
#define CFG_RC11_ZONE_MIN 1000
#define CFG_RC11_ZONE_MAX 2000

// ---- RC12 band/channel stepping ------------------------------------------
// PWM in the BAND range steps to the next band, in the CHANNEL range to the
// next channel; steps repeat while the switch is held.
#define CFG_RC12_BAND_MIN 1800
#define CFG_RC12_BAND_MAX 2200
#define CFG_RC12_CHANNEL_MIN 800
#define CFG_RC12_CHANNEL_MAX 1200
#define CFG_RC12_REPEAT_MS 1000 // delay between repeated steps

// ---- Failsafe -------------------------------------------------------------
// After this long without valid RC11 the VTX is set back to 25 mW.
#define CFG_RC_TIMEOUT_MS 2000

// ---- VTX power levels -----------------------------------------------------
// mW per SmartAudio power level; level 1 (first entry) is the startup and
// failsafe power.  TX3000AC supports exactly 6 levels; do not add or
// remove entries without changing the SmartAudio command code accordingly.
#define CFG_POWER_MW {25, 250, 500, 1000, 2000, 3000}

// ---- Built-in OLED: 01Space ESP32-S3-0.42OLED (SSD1306 72x40) --------------
// Manufacturer example: SDA=41, SCL=40. Address is the 7-bit I2C address.
#ifndef CFG_OLED_ENABLED
#define CFG_OLED_ENABLED 1
#endif
#define CFG_OLED_SDA_PIN 41
#define CFG_OLED_SCL_PIN 40
#define CFG_OLED_I2C_ADDRESS 0x3C
#define CFG_OLED_REFRESH_MS 250
