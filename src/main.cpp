#include <Arduino.h>

#include "config.h"

#if CFG_OLED_ENABLED
#include <U8g2lib.h>
#include <Wire.h>
#endif

#ifndef ENABLE_WEB_INTERFACE
#define ENABLE_WEB_INTERFACE 1
#endif

#if ENABLE_WEB_INTERFACE
#include <ESPmDNS.h>
#include <WebServer.h>
#include <WiFi.h>
#endif

// MAVLink C headers generate some intentionally unused inline helpers.
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-parameter"
#include <ardupilotmega/mavlink.h>
#pragma GCC diagnostic pop

namespace {

constexpr int MAV_RX_PIN = CFG_MAV_RX_PIN;  // FC T1 -> ESP
constexpr int MAV_TX_PIN = CFG_MAV_TX_PIN;  // FC R1 <- ESP
constexpr int SMARTAUDIO_PIN = CFG_SMARTAUDIO_PIN;  // ESP -> VTX SmartAudio

constexpr uint32_t MAV_BAUD = CFG_MAV_BAUD;
constexpr uint32_t RC_TIMEOUT_MS = CFG_RC_TIMEOUT_MS;
constexpr uint32_t REQUEST_RETRY_MS = 5000;
constexpr uint32_t REQUEST_PERIOD_US = 100000; // RC_CHANNELS at 10 Hz
constexpr uint32_t RC12_REPEAT_MS = CFG_RC12_REPEAT_MS;
constexpr uint32_t ESP_HEARTBEAT_PERIOD_MS = 1000;
constexpr uint32_t SMARTAUDIO_STATUS_PERIOD_MS = 2000;
constexpr uint32_t SMARTAUDIO_STATUS_TIMEOUT_MS = 150;
constexpr uint32_t SMARTAUDIO_REPLY_GAP_MS = 15;
constexpr uint32_t VTX_STATUS_STALE_MS = 6000;
constexpr uint32_t VTX_RETRY_MS = 2000;

constexpr uint8_t ESP_SYSTEM_ID = 254;
constexpr uint8_t ESP_COMPONENT_ID = MAV_COMP_ID_PERIPHERAL;

#if ENABLE_WEB_INTERFACE
constexpr char WEB_AP_SSID[] = CFG_AP_SSID;
constexpr char WEB_AP_PASSWORD[] = CFG_AP_PASSWORD;
constexpr char WEB_HOSTNAME[] = CFG_AP_HOSTNAME;
#endif

HardwareSerial mavSerial(1);
HardwareSerial smartAudioSerial(2);
HardwareSerial smartAudioRxSerial(0);

uint8_t targetSystem = 0;
uint8_t targetComponent = 0;
uint32_t lastHeartbeatMs = 0;
uint32_t lastRcMs = 0;
uint32_t lastRequestMs = 0;
uint16_t rc11 = 0;
uint16_t rc12 = 0;
// Desired state; never overwritten by SmartAudio readback.
int8_t activePowerIndex = -1;
bool rcSeen = false;
bool pendingPower = false;
bool pendingFrequency = false;
bool frequencyRequested = false;
uint32_t lastPowerAttemptMs = 0;
uint32_t lastFrequencyAttemptMs = 0;
bool manualWebControl = false;

// Standard SmartAudio table: bands A-E, eight channels per band.
constexpr uint8_t VTX_BAND_COUNT = 5;
constexpr uint8_t VTX_CHANNEL_COUNT = 8;
const char VTX_BAND_LETTERS[] = "ABEFR";
// Nominal frequencies (MHz) of the standard 5.8 GHz bands A/B/E/F/R.
const uint16_t VTX_FREQ_MHZ[VTX_BAND_COUNT][VTX_CHANNEL_COUNT] = {
    {5865, 5845, 5825, 5805, 5785, 5765, 5745, 5725},
    {5733, 5752, 5771, 5790, 5809, 5828, 5847, 5866},
    {5705, 5685, 5665, 5645, 5885, 5905, 5925, 5945},
    {5740, 5760, 5780, 5800, 5820, 5840, 5860, 5880},
    {5658, 5695, 5732, 5769, 5806, 5843, 5880, 5917},
};
uint8_t activeBand = 0;
uint8_t activeChannel = 0;
enum class Rc12Mode : uint8_t { None, Band, Channel };
Rc12Mode rc12Mode = Rc12Mode::None;
uint32_t lastRc12ActionMs = 0;

uint32_t lastEspHeartbeatMs = 0;

// Last settings reported by the VTX itself (SmartAudio GET_SETTINGS).
bool vtxStatusValid = false;
uint8_t vtxVersion = 0;
uint8_t vtxChannelIndex = 0;
uint8_t vtxPowerLevel = 0;  // zero-based, as reported by the VTX
uint32_t lastVtxStatusMs = 0;
uint32_t lastSmartAudioStatusMs = 0;
uint32_t vtxStatusBaud = 4800;
bool vtxStatusBaudLocked = false;
uint8_t vtxStatusFailures = 0;
bool vtxStatusRefreshRequested = false;

#if ENABLE_WEB_INTERFACE
WebServer webServer(80);
bool webServerStarted = false;

// Actions queued by the HTTP handlers; loop() executes them so that the
// HTTP response is not held up by the slow SmartAudio transaction.
bool pendingWebAuto = false;
int8_t pendingWebPowerIndex = -1;
int8_t pendingWebBand = -1;
int8_t pendingWebChannel = -1;
#endif

// TX3000AC power levels (zero-based) from src/config.h.
// SmartAudio SET_POWER on this VTX takes the level as a one-based index
// (1..6); dBm-style payloads (0x80|dBm) are ignored.
constexpr uint16_t POWER_MW[] = CFG_POWER_MW;
static_assert(sizeof(POWER_MW) / sizeof(POWER_MW[0]) == 6, "TX3000AC needs six levels");
static_assert(CFG_RC11_ZONE_MAX > CFG_RC11_ZONE_MIN, "Invalid RC11 span");

bool rcIsFresh(uint32_t now) {
    return rcSeen && rc11 >= CFG_RC_VALID_MIN && rc11 <= CFG_RC_VALID_MAX &&
           now - lastRcMs <= RC_TIMEOUT_MS;
}

#if CFG_OLED_ENABLED
U8G2_SSD1306_72X40_ER_F_HW_I2C oled(U8G2_R0, U8X8_PIN_NONE);
bool oledReady = false;
bool oledRendered = false;
uint32_t lastOledMs = 0;

void setupOled() {
    if (!Wire.begin(CFG_OLED_SDA_PIN, CFG_OLED_SCL_PIN, 400000)) return;
    Wire.setTimeOut(5);
    Wire.beginTransmission(CFG_OLED_I2C_ADDRESS);
    if (Wire.endTransmission() != 0) {
        Serial.println("OLED unavailable; continuing without display");
        return;
    }
    oled.setI2CAddress(CFG_OLED_I2C_ADDRESS << 1); // U8g2 uses an 8-bit address.
    oled.setBusClock(400000);
    oled.begin();
    oledReady = true;
}

void processOled() {
    const uint32_t now = millis();
    if (!oledReady || (oledRendered && now - lastOledMs < CFG_OLED_REFRESH_MS)) return;
    lastOledMs = now;
    oledRendered = true;
    // A missing/stuck display must not repeatedly delay RC/failsafe processing.
    Wire.beginTransmission(CFG_OLED_I2C_ADDRESS);
    if (Wire.endTransmission() != 0) {
        oledReady = false;
        Serial.println("OLED disconnected; display disabled until reboot");
        return;
    }
    const bool fresh = vtxStatusValid && now - lastVtxStatusMs <= VTX_STATUS_STALE_MS &&
                       vtxPowerLevel < sizeof(POWER_MW) / sizeof(POWER_MW[0]);
    const char *mode = manualWebControl ? "MANUAL" : (rcIsFresh(now) ? "AUTO" : "SAFE");
    char heading[16]{};
    char power[8]{};
    char detail[16]{};
    snprintf(heading, sizeof(heading), "%s%s", mode, fresh ? "" : " NO VTX");
    if (fresh) snprintf(power, sizeof(power), "%u", POWER_MW[vtxPowerLevel]);
    else snprintf(power, sizeof(power), "--");
    if (fresh && activePowerIndex == vtxPowerLevel) {
        snprintf(detail, sizeof(detail), "VTX OK");
    } else if (activePowerIndex >= 0 && activePowerIndex < sizeof(POWER_MW) / sizeof(POWER_MW[0])) {
        snprintf(detail, sizeof(detail), "SET %umW", POWER_MW[activePowerIndex]);
    } else {
        snprintf(detail, sizeof(detail), "SET --mW");
    }

    oled.clearBuffer();
    oled.setFont(u8g2_font_5x7_tf);
    oled.drawStr(0, 7, heading);
    const int unitWidth = oled.getStrWidth("mW");
    oled.setFont(u8g2_font_logisoso16_tn);
    const int numberWidth = oled.getStrWidth(power);
    const int left = (72 - numberWidth - 3 - unitWidth) / 2;
    oled.drawStr(left, 27, power);
    oled.setFont(u8g2_font_5x7_tf);
    oled.drawStr(left + numberWidth + 3, 27, "mW");
    oled.drawStr(0, 39, detail);
    oled.sendBuffer();
}
#endif

uint8_t crc8DvbS2(const uint8_t *data, size_t length) {
    uint8_t crc = 0;
    for (size_t i = 0; i < length; ++i) {
        crc ^= data[i];
        for (uint8_t bit = 0; bit < 8; ++bit) {
            crc = (crc & 0x80U) ? static_cast<uint8_t>((crc << 1U) ^ 0xD5U)
                                : static_cast<uint8_t>(crc << 1U);
        }
    }
    return crc;
}

// Only the bytes on the wire are synchronous (~14-19 ms at 4800 baud).
// All inter-command gaps, reply timeouts and baud scanning run across loop ticks.
void writeSmartAudioFrame(const uint8_t *frame, size_t length) {
    smartAudioSerial.begin(4800, SERIAL_8N2, -1, SMARTAUDIO_PIN);
    smartAudioSerial.write(frame, length);
    smartAudioSerial.flush();
    smartAudioSerial.end();
    pinMode(SMARTAUDIO_PIN, INPUT);
}

bool parseSmartAudioSettings(const uint8_t *data, size_t length) {
    // TX3000AC GET_SETTINGS reply (no CRC on this VTX):
    //   AA 55 11 0E CH LEVEL ...
    // 0x11/0x0E = fixed markers, CH = zero-based channel index
    // (band*8+channel), LEVEL = zero-based power level.  The trailing bytes
    // after LEVEL vary in count between replies, so only the first six are
    // required and parsed.
    for (size_t start = 0; start + 6 <= length; ++start) {
        if (data[start] != 0xAA || data[start + 1] != 0x55 ||
            data[start + 2] != 0x11 || data[start + 3] != 0x0E) {
            continue;
        }
        const uint8_t channel = data[start + 4];
        const uint8_t level = data[start + 5];
        if (channel >= VTX_BAND_COUNT * VTX_CHANNEL_COUNT ||
            level >= (sizeof(POWER_MW) / sizeof(POWER_MW[0]))) {
            continue;
        }
        vtxVersion = data[start + 2];
        vtxChannelIndex = channel;
        vtxPowerLevel = level;
        return true;
    }
    return false;
}

uint8_t lastReportedChannel = 0xFF;
uint8_t lastReportedLevel = 0xFF;

void recordSmartAudioSettings() {
    vtxStatusValid = true;
    vtxStatusFailures = 0;
    lastVtxStatusMs = millis();
    if (!frequencyRequested) {
        activeBand = static_cast<uint8_t>(vtxChannelIndex / VTX_CHANNEL_COUNT);
        activeChannel = static_cast<uint8_t>(vtxChannelIndex % VTX_CHANNEL_COUNT);
    }
    if (vtxChannelIndex != lastReportedChannel ||
        vtxPowerLevel != lastReportedLevel) {
        lastReportedChannel = vtxChannelIndex;
        lastReportedLevel = vtxPowerLevel;
        Serial.printf("VTX: %c%u %u MHz, %u mW (level %u)\n",
                      VTX_BAND_LETTERS[vtxChannelIndex / VTX_CHANNEL_COUNT],
                      static_cast<unsigned>(vtxChannelIndex % VTX_CHANNEL_COUNT + 1),
                      VTX_FREQ_MHZ[vtxChannelIndex / VTX_CHANNEL_COUNT][vtxChannelIndex % VTX_CHANNEL_COUNT],
                      POWER_MW[vtxPowerLevel],
                      static_cast<unsigned>(vtxPowerLevel + 1));
    }
}

enum class SaPhase { Idle, UnlockWait, CommandWait, StatusWait };
SaPhase saPhase = SaPhase::Idle;
uint32_t saDeadlineMs = 0;
uint32_t saLastByteMs = 0;
uint8_t saCommand = 0;
uint8_t saValue = 0;
uint8_t saRepeats = 0;
uint8_t saReceived[64]{};
size_t saReceivedLength = 0;
bool saReplySeen = false;
uint8_t saBaudIndex = 0;
constexpr uint32_t SA_BAUDS[] = {4800, 4880, 4720, 4960, 4640, 5040, 4560};

void sendSaCommand(uint8_t command, uint8_t value) {
    uint8_t frame[] = {0x00, 0xAA, 0x55, command, 0x01, value, 0x00, 0x00};
    frame[6] = crc8DvbS2(&frame[1], 5);
    writeSmartAudioFrame(frame, sizeof(frame));
}

void processSmartAudioStatus() {
    const uint32_t now = millis();
    if (vtxStatusValid && now - lastVtxStatusMs > VTX_STATUS_STALE_MS) {
        vtxStatusValid = false;
    }

    // A failsafe/new request supersedes unsent repetitions of an old command.
    if ((saPhase == SaPhase::UnlockWait || saPhase == SaPhase::CommandWait) &&
        ((saCommand == 0x05 && saValue != activePowerIndex + 1) ||
         (saCommand == 0x07 && saValue != activeBand * VTX_CHANNEL_COUNT + activeChannel))) {
        saPhase = SaPhase::Idle;
    }

    if (saPhase == SaPhase::StatusWait) {
        // Bound work per tick even if the bus is noisy.
        for (size_t count = 0; count < sizeof(saReceived) && smartAudioRxSerial.available() > 0; ++count) {
            const uint8_t byte = static_cast<uint8_t>(smartAudioRxSerial.read());
            if (saReceivedLength < sizeof(saReceived)) {
                saReceived[saReceivedLength++] = byte;
                if (saReceivedLength >= 3 && saReceived[saReceivedLength - 3] == 0xAA &&
                    saReceived[saReceivedLength - 2] == 0x55 && byte == 0x11) {
                    saReplySeen = true;
                }
            }
            saLastByteMs = now;
        }
        if (static_cast<int32_t>(now - saDeadlineMs) < 0 &&
            !(saReplySeen && now - saLastByteMs >= SMARTAUDIO_REPLY_GAP_MS)) {
            return;
        }
        smartAudioRxSerial.end();
        pinMode(SMARTAUDIO_PIN, INPUT);
        saPhase = SaPhase::Idle;
        if (parseSmartAudioSettings(saReceived, saReceivedLength)) {
            vtxStatusBaudLocked = true;
            recordSmartAudioSettings();
            saBaudIndex = 0;
        } else if (vtxStatusBaudLocked) {
            if (++vtxStatusFailures >= 5) {
                vtxStatusBaudLocked = false;
                saBaudIndex = 0;
                vtxStatusRefreshRequested = true;
            }
        } else {
            saBaudIndex = static_cast<uint8_t>((saBaudIndex + 1) % (sizeof(SA_BAUDS) / sizeof(SA_BAUDS[0])));
            vtxStatusRefreshRequested = saBaudIndex != 0;
        }
        return;
    }

    if (saPhase == SaPhase::UnlockWait || saPhase == SaPhase::CommandWait) {
        if (static_cast<int32_t>(now - saDeadlineMs) < 0) {
            return;
        }
        if (saRepeats < 3) {
            sendSaCommand(saCommand, saValue);
            ++saRepeats;
            saDeadlineMs = millis() + 120;
            saPhase = SaPhase::CommandWait;
        } else {
            saPhase = SaPhase::Idle;
            vtxStatusRefreshRequested = true;
        }
        return;
    }

    if (pendingPower || pendingFrequency) {
        if (pendingPower) {
            saCommand = 0x05;
            saValue = static_cast<uint8_t>(activePowerIndex + 1);
            pendingPower = false;
            lastPowerAttemptMs = now;
        } else {
            saCommand = 0x07;
            saValue = static_cast<uint8_t>(activeBand * VTX_CHANNEL_COUNT + activeChannel);
            pendingFrequency = false;
            lastFrequencyAttemptMs = now;
        }
        sendSaCommand(0x0B, 0x0C); // unlock / quit Pit Mode
        saRepeats = 0;
        saDeadlineMs = millis() + 200;
        saPhase = SaPhase::UnlockWait;
        return;
    }

    if (!vtxStatusRefreshRequested && now - lastSmartAudioStatusMs < SMARTAUDIO_STATUS_PERIOD_MS) {
        return;
    }
    vtxStatusRefreshRequested = false;
    lastSmartAudioStatusMs = now;
    vtxStatusBaud = vtxStatusBaudLocked ? vtxStatusBaud : SA_BAUDS[saBaudIndex];
    smartAudioRxSerial.begin(vtxStatusBaud, SERIAL_8N2, SMARTAUDIO_PIN, -1);
    // Discard only the bytes already buffered, without an unbounded drain.
    const int buffered = smartAudioRxSerial.available();
    for (int i = 0; i < buffered; ++i) smartAudioRxSerial.read();
    uint8_t frame[] = {0x00, 0xAA, 0x55, 0x03, 0x00, 0x00};
    frame[5] = crc8DvbS2(&frame[1], 4);
    writeSmartAudioFrame(frame, sizeof(frame));
    saReceivedLength = 0;
    saReplySeen = false;
    saLastByteMs = millis();
    saDeadlineMs = millis() + SMARTAUDIO_STATUS_TIMEOUT_MS;
    saPhase = SaPhase::StatusWait;
}

void sendMavlinkStatusText(const char *text, uint8_t severity = MAV_SEVERITY_INFO) {
    // The ESP is a separate MAVLink system.  Reusing the autopilot's system id
    // can confuse MAVLink routing and makes link diagnostics ambiguous.
    if (targetSystem == 0) {
        return;
    }

    char statusText[50]{};
    snprintf(statusText, sizeof(statusText), "%s", text);

    mavlink_message_t message;
    uint8_t buffer[MAVLINK_MAX_PACKET_LEN];
    mavlink_msg_statustext_pack(
        ESP_SYSTEM_ID,
        ESP_COMPONENT_ID,
        &message,
        severity,
        statusText,
        0,
        0);

    const uint16_t length = mavlink_msg_to_send_buffer(buffer, &message);
    mavSerial.write(buffer, length);
    mavSerial.flush();
}

void sendEspHeartbeat() {
    mavlink_message_t message;
    uint8_t buffer[MAVLINK_MAX_PACKET_LEN];
    mavlink_msg_heartbeat_pack(
        ESP_SYSTEM_ID,
        ESP_COMPONENT_ID,
        &message,
        MAV_TYPE_ONBOARD_CONTROLLER,
        MAV_AUTOPILOT_INVALID,
        0,
        0,
        MAV_STATE_ACTIVE);
    const uint16_t length = mavlink_msg_to_send_buffer(buffer, &message);
    mavSerial.write(buffer, length);
    mavSerial.flush();
}

void processEspHeartbeat() {
    const uint32_t now = millis();
    if (lastEspHeartbeatMs != 0 &&
        now - lastEspHeartbeatMs < ESP_HEARTBEAT_PERIOD_MS) {
        return;
    }
    sendEspHeartbeat();
    lastEspHeartbeatMs = now;
}

void sendSmartAudioPower(uint8_t index, const char *source,
                         uint8_t severity = MAV_SEVERITY_INFO) {
    if (index >= sizeof(POWER_MW) / sizeof(POWER_MW[0])) return;
    activePowerIndex = static_cast<int8_t>(index);
    pendingPower = true;
    char text[50]{};
    snprintf(text, sizeof(text), "VTX request: %u mW (%s)", POWER_MW[index], source);
    Serial.println(text);
    sendMavlinkStatusText(text, severity);
}

void sendSmartAudioBandChannel(uint8_t band, uint8_t channel) {
    if (band >= VTX_BAND_COUNT || channel >= VTX_CHANNEL_COUNT) return;
    activeBand = band;
    activeChannel = channel;
    frequencyRequested = true;
    pendingFrequency = true;
    char text[50]{};
    snprintf(text, sizeof(text), "VTX request: %c%u", VTX_BAND_LETTERS[band], channel + 1);
    sendMavlinkStatusText(text);
}

void processRc12(uint16_t pwm) {
    Rc12Mode requestedMode = Rc12Mode::None;
    if (pwm >= CFG_RC12_BAND_MIN && pwm <= CFG_RC12_BAND_MAX) {
        requestedMode = Rc12Mode::Band;
    } else if (pwm >= CFG_RC12_CHANNEL_MIN && pwm <= CFG_RC12_CHANNEL_MAX) {
        requestedMode = Rc12Mode::Channel;
    }

    const uint32_t now = millis();
    if (requestedMode != rc12Mode) {
        rc12Mode = requestedMode;
        lastRc12ActionMs = now;
        return;
    }
    if (requestedMode == Rc12Mode::None ||
        now - lastRc12ActionMs < RC12_REPEAT_MS) {
        return;
    }

    if (requestedMode == Rc12Mode::Band) {
        activeBand = static_cast<uint8_t>((activeBand + 1) % VTX_BAND_COUNT);
    } else {
        activeChannel =
            static_cast<uint8_t>((activeChannel + 1) % VTX_CHANNEL_COUNT);
    }
    sendSmartAudioBandChannel(activeBand, activeChannel);
    lastRc12ActionMs = millis();
}

uint8_t powerIndexFromPwm(uint16_t pwm) {
    // Equal power zones over the configured PWM span (src/config.h).
    constexpr uint8_t powerCount =
        sizeof(POWER_MW) / sizeof(POWER_MW[0]);
    if (pwm <= CFG_RC11_ZONE_MIN) {
        return 0;
    }
    if (pwm >= CFG_RC11_ZONE_MAX) {
        return powerCount - 1;
    }
    const uint32_t span = CFG_RC11_ZONE_MAX - CFG_RC11_ZONE_MIN;
    const uint8_t index = static_cast<uint8_t>(
        static_cast<uint32_t>(pwm - CFG_RC11_ZONE_MIN) * powerCount / span);
    return index < powerCount ? index : powerCount - 1;
}

void sendRcChannelsRequest() {
    if (targetSystem == 0) {
        return;
    }

    mavlink_message_t message;
    uint8_t buffer[MAVLINK_MAX_PACKET_LEN];
    mavlink_msg_command_long_pack(
        ESP_SYSTEM_ID,
        ESP_COMPONENT_ID,
        &message,
        targetSystem,
        targetComponent,
        MAV_CMD_SET_MESSAGE_INTERVAL,
        0,
        MAVLINK_MSG_ID_RC_CHANNELS,
        REQUEST_PERIOD_US,
        0, 0, 0, 0, 0);

    const uint16_t length = mavlink_msg_to_send_buffer(buffer, &message);
    mavSerial.write(buffer, length);
    mavSerial.flush();
    lastRequestMs = millis();
}

void handleMavlinkMessage(const mavlink_message_t &message) {
    if (message.msgid == MAVLINK_MSG_ID_HEARTBEAT) {
        mavlink_heartbeat_t heartbeat{};
        mavlink_msg_heartbeat_decode(&message, &heartbeat);
        if (message.compid != MAV_COMP_ID_AUTOPILOT1 || message.sysid == 0 ||
            message.sysid == ESP_SYSTEM_ID || heartbeat.autopilot == MAV_AUTOPILOT_INVALID ||
            (targetSystem != 0 && message.sysid != targetSystem)) return;
        // Bind to the first autopilot until reboot, never to a routed GCS/peripheral.
        targetSystem = message.sysid;
        targetComponent = message.compid;
        lastHeartbeatMs = millis();
        if (lastRequestMs == 0 || millis() - lastRequestMs >= REQUEST_RETRY_MS) sendRcChannelsRequest();
        return;
    }
    if (message.msgid != MAVLINK_MSG_ID_RC_CHANNELS || targetSystem == 0 ||
        message.sysid != targetSystem || message.compid != targetComponent) return;

    mavlink_rc_channels_t channels{};
    mavlink_msg_rc_channels_decode(&message, &channels);
    rc12 = channels.chan12_raw;
    processRc12(rc12);
    const uint16_t sample = channels.chan11_raw;
    if (sample >= CFG_RC_VALID_MIN && sample <= CFG_RC_VALID_MAX) {
        rc11 = sample;
        rcSeen = true;
        lastRcMs = millis();
    } else {
        rc11 = 0;
        rcSeen = false;
    }
}

void processPowerControl() {
    const uint32_t now = millis();
    if (!manualWebControl) {
        const bool fresh = rcIsFresh(now);
        const uint8_t requested = fresh ? powerIndexFromPwm(rc11) : 0;
        if (!fresh) {
            rc11 = 0;
            rc12Mode = Rc12Mode::None;
        }
        if (requested != activePowerIndex) {
            sendSmartAudioPower(requested, fresh ? "RC11" : "failsafe",
                                fresh ? MAV_SEVERITY_INFO : MAV_SEVERITY_WARNING);
        }
    }
    const bool statusFresh = vtxStatusValid && now - lastVtxStatusMs <= VTX_STATUS_STALE_MS;
    const bool powerBusy = saCommand == 0x05 &&
        (saPhase == SaPhase::UnlockWait || saPhase == SaPhase::CommandWait);
    if (activePowerIndex >= 0 && !pendingPower && !powerBusy &&
        (!statusFresh || vtxPowerLevel != activePowerIndex) &&
        now - lastPowerAttemptMs >= VTX_RETRY_MS) pendingPower = true;

    const bool frequencyBusy = saCommand == 0x07 &&
        (saPhase == SaPhase::UnlockWait || saPhase == SaPhase::CommandWait);
    if (frequencyRequested && !pendingFrequency && !frequencyBusy &&
        (!statusFresh || vtxChannelIndex != activeBand * VTX_CHANNEL_COUNT + activeChannel) &&
        now - lastFrequencyAttemptMs >= VTX_RETRY_MS) pendingFrequency = true;
}

void processMavlink() {
    mavlink_message_t message;
    mavlink_status_t status{};
    for (size_t count = 0; count < 512 && mavSerial.available() > 0; ++count) {
        const uint8_t byte = static_cast<uint8_t>(mavSerial.read());
        if (mavlink_parse_char(MAVLINK_COMM_0, byte, &message, &status)) {
            handleMavlinkMessage(message);
        }
    }
}

#if ENABLE_WEB_INTERFACE
String makeVtxStatusHtml() {
    if (!vtxStatusValid) {
        return F("No recent reply from the VTX.");
    }
    const uint8_t band =
        static_cast<uint8_t>(vtxChannelIndex / VTX_CHANNEL_COUNT);
    const uint8_t channel =
        static_cast<uint8_t>(vtxChannelIndex % VTX_CHANNEL_COUNT);
    String html;
    html.reserve(160);
    html += F("Frequency: <b>");
    html += String(VTX_FREQ_MHZ[band][channel]);
    html += F(" MHz (band ");
    html += VTX_BAND_LETTERS[band];
    html += F(", channel ");
    html += String(channel + 1);
    html += F(")</b><br>Power: <b>");
    html += String(POWER_MW[vtxPowerLevel]);
    html += F(" mW (level ");
    html += String(vtxPowerLevel + 1);
    html += F(")</b><br>SmartAudio version: 0x");
    html += String(vtxVersion, HEX);
    html += F("<br>Updated ");
    html += String((millis() - lastVtxStatusMs) / 1000);
    html += F(" s ago");
    return html;
}

String makeVtxStatusJson() {
    String json;
    json.reserve(220);
    json += F("{\"mode\":\"");
    json += manualWebControl ? F("Manual / Web") : F("Auto / MAVLink RC11");
    json += F("\",\"rc11\":");
    json += String(rc11);
    json += F(",\"req\":\"");
    if (activePowerIndex >= 0) {
        json += String(POWER_MW[activePowerIndex]);
        json += F(" mW");
    } else {
        json += F("unknown");
    }
    json += F("\",\"valid\":");
    json += vtxStatusValid ? F("true") : F("false");
    if (vtxStatusValid) {
        const uint8_t band =
            static_cast<uint8_t>(vtxChannelIndex / VTX_CHANNEL_COUNT);
        const uint8_t channel =
            static_cast<uint8_t>(vtxChannelIndex % VTX_CHANNEL_COUNT);
        json += F(",\"freq\":");
        json += String(VTX_FREQ_MHZ[band][channel]);
        json += F(",\"band\":\"");
        json += VTX_BAND_LETTERS[band];
        json += F("\",\"ch\":");
        json += String(channel + 1);
        json += F(",\"mw\":");
        json += String(POWER_MW[vtxPowerLevel]);
        json += F(",\"lvl\":");
        json += String(vtxPowerLevel + 1);
        json += F(",\"ver\":\"");
        json += String(vtxVersion, HEX);
        json += F("\",\"age\":");
        json += String((millis() - lastVtxStatusMs) / 1000);
    }
    json += F("}");
    return json;
}

String makeWebPage() {
    String page;
    page.reserve(4200);
    page += F(
        "<!doctype html><html><head><meta charset='utf-8'>"
        "<meta name='viewport' content='width=device-width,initial-scale=1'>"
        "<title>TX3000AC power</title><style>"
        "body{font-family:sans-serif;max-width:520px;margin:30px auto;padding:0 16px;"
        "background:#111;color:#eee}button{width:100%;padding:14px;margin:5px 0;"
        "font-size:18px;border:0;border-radius:8px;background:#333;color:#fff}"
        "select{font-size:18px;padding:10px;background:#222;color:#eee;border:1px solid #777}"
        ".active{background:#087f5b}.auto{background:#1864ab}.note{color:#aaa}"
        "</style></head><body><h1>TX3000AC</h1><p>Mode: <b><span id='mode'>");
    page += manualWebControl ? F("Manual / Web") : F("Auto / MAVLink RC11");
    page += F("</span></b><br>RC11: <span id='rc11'>");
    page += String(rc11);
    page += F("</span> us<br>Requested power: <b><span id='reqpow'>");
    if (activePowerIndex >= 0) {
        page += String(POWER_MW[activePowerIndex]);
        page += F(" mW");
    } else {
        page += F("unknown");
    }
    page += F("</span></b></p><h2>VTX reported state</h2><p id='vtx'>");
    page += makeVtxStatusHtml();
    page += F("</p><h2>Frequency</h2>"
              "<form action='/set' method='post'>"
              "Band: <select name='band'>");
    for (uint8_t band = 0; band < VTX_BAND_COUNT; ++band) {
        page += F("<option value='");
        page += String(band);
        page += F("'");
        if (band == activeBand) {
            page += F(" selected");
        }
        page += F(">");
        page += VTX_BAND_LETTERS[band];
        page += F("</option>");
    }
    page += F("</select> Channel: <select name='channel'>");
    for (uint8_t channel = 0; channel < VTX_CHANNEL_COUNT; ++channel) {
        page += F("<option value='");
        page += String(channel);
        page += F("'");
        if (channel == activeChannel) {
            page += F(" selected");
        }
        page += F(">");
        page += String(channel + 1);
        page += F("</option>");
    }
    page += F("</select><button type='submit'>Set frequency</button></form>"
              "<form action='/set' method='post'>"
              "<button class='auto' name='mode' value='auto'>AUTO: MAVLink RC11</button></form>");

    for (uint8_t index = 0; index < (sizeof(POWER_MW) / sizeof(POWER_MW[0])); ++index) {
        page += F("<form action='/set' method='post'><button ");
        if (manualWebControl && activePowerIndex == static_cast<int8_t>(index)) {
            page += F("class='active' ");
        }
        page += F("name='power' value='");
        page += String(index);
        page += F("'>");
        page += String(POWER_MW[index]);
        page += F(" mW</button></form>");
    }

    page += F("<p class='note'>Manual mode ignores RC11 and the MAVLink telemetry timeout. "
              "AUTO restores the 25 mW failsafe after 2 seconds without valid RC data. "
              "The VTX state is refreshed from SmartAudio GET_SETTINGS every 2 seconds.</p>"
              "<script>"
              "async function pollVtx(){try{"
              "const j=await(await fetch('/status')).json();"
              "document.getElementById('mode').textContent=j.mode;"
              "document.getElementById('rc11').textContent=j.rc11;"
              "document.getElementById('reqpow').textContent=j.req;"
              "document.getElementById('vtx').innerHTML=j.valid?"
              "'Frequency: <b>'+j.freq+' MHz (band '+j.band+', channel '+j.ch+')</b><br>'"
              "+'Power: <b>'+j.mw+' mW (level '+j.lvl+')</b><br>'"
              "+'SmartAudio version: 0x'+j.ver+'<br>Updated '+j.age+' s ago'"
              ":'No recent reply from the VTX.';}catch(e){}}"
              "setInterval(pollVtx,2000);"
              "</script>"
              "</body></html>");
    return page;
}

void redirectToWebRoot() {
    webServer.sendHeader("Location", "/", true);
    webServer.send(303, "text/plain", "");
}

void handleWebPowerChange() {
    // Queue the action and answer at once; processWebCommands() performs the
    // slow SmartAudio transaction from loop().
    if (webServer.hasArg("mode") && webServer.arg("mode") == "auto") {
        pendingWebAuto = true;
        redirectToWebRoot();
        return;
    }

    if (webServer.hasArg("band") && webServer.hasArg("channel")) {
        const String bandText = webServer.arg("band");
        const String channelText = webServer.arg("channel");
        const int band = bandText.length() == 1 ? bandText[0] - '0' : -1;
        const int channel = channelText.length() == 1 ? channelText[0] - '0' : -1;
        if (band >= 0 && band < VTX_BAND_COUNT &&
            channel >= 0 && channel < VTX_CHANNEL_COUNT) {
            pendingWebBand = static_cast<int8_t>(band);
            pendingWebChannel = static_cast<int8_t>(channel);
            redirectToWebRoot();
            return;
        }
    }

    if (webServer.hasArg("power")) {
        const String value = webServer.arg("power");
        if (value.length() == 1 && value[0] >= '0' && value[0] <= '5') {
            pendingWebPowerIndex = static_cast<int8_t>(value[0] - '0');
            redirectToWebRoot();
            return;
        }
    }

    webServer.send(400, "text/plain", "Invalid power or frequency selection");
}

void processWebCommands() {
    if (pendingWebAuto) {
        pendingWebAuto = false;
        manualWebControl = false;

        const uint32_t now = millis();
        if (rcIsFresh(now)) {
            const uint8_t requestedIndex = powerIndexFromPwm(rc11);
            if (requestedIndex != activePowerIndex) {
                sendSmartAudioPower(requestedIndex, "RC11");
            }
        } else if (activePowerIndex != 0) {
            sendSmartAudioPower(0, "failsafe", MAV_SEVERITY_WARNING);
        }
    }

    if (pendingWebBand >= 0 && pendingWebChannel >= 0) {
        sendSmartAudioBandChannel(static_cast<uint8_t>(pendingWebBand),
                                  static_cast<uint8_t>(pendingWebChannel));
        pendingWebBand = -1;
        pendingWebChannel = -1;
    }

    if (pendingWebPowerIndex >= 0) {
        manualWebControl = true;
        // Explicit requests are sent even when the desired value is unchanged.
        sendSmartAudioPower(static_cast<uint8_t>(pendingWebPowerIndex), "web");
        pendingWebPowerIndex = -1;
    }
}

void setupWebInterface() {
    webServer.on("/", HTTP_GET, []() {
        webServer.send(200, "text/html; charset=utf-8", makeWebPage());
    });
    webServer.on("/status", HTTP_GET, []() {
        webServer.send(200, "application/json", makeVtxStatusJson());
    });
    webServer.on("/set", HTTP_POST, handleWebPowerChange);
    webServer.onNotFound([]() {
        webServer.send(404, "text/plain", "Not found");
    });

    WiFi.mode(WIFI_AP);
    WiFi.softAP(WEB_AP_SSID, WEB_AP_PASSWORD);
    Serial.printf("Web: AP '%s' started, IP %s\n",
                  WEB_AP_SSID,
                  WiFi.softAPIP().toString().c_str());
}

void processWebInterface() {
    // The AP is up as soon as setupWebInterface() ran; start serving at once.
    if (!webServerStarted) {
        webServer.begin();
        webServerStarted = true;
        if (!MDNS.begin(WEB_HOSTNAME)) {
            Serial.println("Web: mDNS failed to start");
        } else {
            MDNS.addService("http", "tcp", 80);
        }
        Serial.printf("Web: open http://%s.local/ or http://%s/\n",
                      WEB_HOSTNAME, WiFi.softAPIP().toString().c_str());
    }

    if (webServerStarted) {
        webServer.handleClient();
    }
}
#endif

} // namespace

void setup() {
    // USB CDC is used for diagnostics; GPIO43/44 UART0 is not started.
    Serial.begin(115200);
    // Do not let log output stall the loop when no host reads the USB serial.
    Serial.setTxTimeoutMs(10);

    mavSerial.begin(MAV_BAUD, SERIAL_8N1, MAV_RX_PIN, MAV_TX_PIN);
    pinMode(SMARTAUDIO_PIN, INPUT);

    delay(1500);
    rc11 = 0;

    // Queue startup power. It remains pending until confirmed by VTX readback.
    sendSmartAudioPower(0, "startup");
#if CFG_OLED_ENABLED
    setupOled();
    processOled();
#endif

#if ENABLE_WEB_INTERFACE
    setupWebInterface();
#endif

    Serial.printf("VTX controller ready: MAVLink RC11, SmartAudio GPIO%d\n",
                  SMARTAUDIO_PIN);
}

void loop() {
    processMavlink();

    const uint32_t now = millis();
    if (targetSystem != 0 && now - lastRequestMs >= REQUEST_RETRY_MS) {
        sendRcChannelsRequest();
    }
    processEspHeartbeat();
#if ENABLE_WEB_INTERFACE
    processWebInterface();
    processWebCommands();
#endif
    // Decide failsafe before advancing any pending SmartAudio transmission.
    processPowerControl();
    processSmartAudioStatus();
#if CFG_OLED_ENABLED
    processOled();
#endif
    delay(2);
}
