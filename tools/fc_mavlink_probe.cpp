#include <cerrno>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <string>
#include <termios.h>
#include <unistd.h>
#include <vector>

extern "C" {
#include <ardupilotmega/mavlink.h>
}

namespace {

bool writeMessage(int fd, const mavlink_message_t &message) {
    uint8_t bytes[MAVLINK_MAX_PACKET_LEN];
    const uint16_t length = mavlink_msg_to_send_buffer(bytes, &message);
    return write(fd, bytes, length) == length;
}

void requestParameter(int fd, uint8_t targetSystem, uint8_t targetComponent,
                      const std::string &name) {
    mavlink_message_t message{};
    mavlink_msg_param_request_read_pack(
        255, MAV_COMP_ID_MISSIONPLANNER, &message,
        targetSystem, targetComponent, name.c_str(), -1);
    writeMessage(fd, message);
}

void requestBanner(int fd, uint8_t targetSystem, uint8_t targetComponent) {
    mavlink_message_t message{};
    mavlink_msg_command_long_pack(
        255, MAV_COMP_ID_MISSIONPLANNER, &message,
        targetSystem, targetComponent, MAV_CMD_DO_SEND_BANNER, 0,
        0, 0, 0, 0, 0, 0, 0);
    writeMessage(fd, message);
    std::printf("TX COMMAND: MAV_CMD_DO_SEND_BANNER\n");
}

} // namespace

int main(int argc, char **argv) {
    const char *device = argc > 1 ? argv[1] : "/dev/ttyACM0";
    const int fd = open(device, O_RDWR | O_NOCTTY | O_NONBLOCK);
    if (fd < 0) {
        std::fprintf(stderr, "open %s: %s\n", device, std::strerror(errno));
        return 1;
    }

    termios tty{};
    if (tcgetattr(fd, &tty) != 0) {
        std::fprintf(stderr, "tcgetattr: %s\n", std::strerror(errno));
        close(fd);
        return 1;
    }
    cfmakeraw(&tty);
    cfsetispeed(&tty, B115200);
    cfsetospeed(&tty, B115200);
    tty.c_cflag |= CLOCAL | CREAD;
    tty.c_cflag &= ~CRTSCTS;
    tcsetattr(fd, TCSANOW, &tty);
    tcflush(fd, TCIFLUSH);

    std::vector<std::string> parameters;
    for (int port = 0; port <= 7; ++port) {
        const std::string prefix = "SERIAL" + std::to_string(port) + "_";
        parameters.push_back(prefix + "PROTOCOL");
        parameters.push_back(prefix + "BAUD");
        parameters.push_back(prefix + "OPTIONS");
    }
    parameters.push_back("OSD_TYPE");
    parameters.push_back("OSD1_ENABLE");
    parameters.push_back("OSD1_MESSAGE_EN");
    parameters.push_back("OSD1_MESSAGE_X");
    parameters.push_back("OSD1_MESSAGE_Y");
    parameters.push_back("OSD_MSG_TIME");
    parameters.push_back("OSD_OPTIONS");
    parameters.push_back("OSD_CHAN");
    parameters.push_back("OSD_SW_METHOD");
    parameters.push_back("OSD_ARM_SCR");
    parameters.push_back("OSD_DSARM_SCR");
    parameters.push_back("OSD_H_OFFSET");
    parameters.push_back("OSD_V_OFFSET");
    parameters.push_back("OSD1_ALTITUDE_EN");
    parameters.push_back("OSD1_ARMING_EN");
    parameters.push_back("OSD1_BAT_VOLT_EN");
    parameters.push_back("OSD1_FLTMODE_EN");
    parameters.push_back("OSD1_HORIZON_EN");
    parameters.push_back("OSD1_RSSI_EN");
    parameters.push_back("OSD1_SATS_EN");
    parameters.push_back("OSD2_ENABLE");
    parameters.push_back("OSD2_MESSAGE_EN");
    parameters.push_back("OSD2_MESSAGE_X");
    parameters.push_back("OSD2_MESSAGE_Y");

    mavlink_message_t message{};
    mavlink_status_t status{};
    uint8_t targetSystem = 0;
    uint8_t targetComponent = 0;
    unsigned long bytesRead = 0;
    unsigned long packetsRead = 0;
    unsigned long heartbeatsRead = 0;
    unsigned long paramsRead = 0;
    bool requested = false;

    const auto deadline = std::chrono::steady_clock::now() +
                          std::chrono::seconds(12);
    while (std::chrono::steady_clock::now() < deadline) {
        uint8_t buffer[1024];
        const ssize_t count = read(fd, buffer, sizeof(buffer));
        if (count < 0 && errno != EAGAIN && errno != EWOULDBLOCK) {
            std::fprintf(stderr, "read: %s\n", std::strerror(errno));
            close(fd);
            return 1;
        }

        if (count > 0) {
            bytesRead += static_cast<unsigned long>(count);
            for (ssize_t index = 0; index < count; ++index) {
                if (!mavlink_parse_char(MAVLINK_COMM_0, buffer[index],
                                        &message, &status)) {
                    continue;
                }
                ++packetsRead;

                if (message.msgid == MAVLINK_MSG_ID_HEARTBEAT) {
                    mavlink_heartbeat_t heartbeat{};
                    mavlink_msg_heartbeat_decode(&message, &heartbeat);
                    ++heartbeatsRead;
                    std::printf(
                        "HEARTBEAT sysid=%u compid=%u autopilot=%u type=%u "
                        "status=%u mavlink_version=%u\n",
                        message.sysid, message.compid, heartbeat.autopilot,
                        heartbeat.type, heartbeat.system_status,
                        heartbeat.mavlink_version);

                    if (!requested &&
                        message.compid == MAV_COMP_ID_AUTOPILOT1) {
                        targetSystem = message.sysid;
                        targetComponent = message.compid;
                        for (const auto &name : parameters) {
                            requestParameter(fd, targetSystem,
                                             targetComponent, name);
                            usleep(20000);
                        }
                        requestBanner(fd, targetSystem, targetComponent);
                        requested = true;
                    }
                } else if (message.msgid == MAVLINK_MSG_ID_PARAM_VALUE) {
                    mavlink_param_value_t parameter{};
                    mavlink_msg_param_value_decode(&message, &parameter);
                    char name[17]{};
                    std::memcpy(name, parameter.param_id, 16);
                    if (std::strncmp(name, "SERIAL", 6) == 0 ||
                        std::strncmp(name, "OSD", 3) == 0) {
                        ++paramsRead;
                        std::printf("PARAM %-16s = %.0f (type=%u)\n",
                                    name, parameter.param_value,
                                    parameter.param_type);
                    }
                } else if (message.msgid == MAVLINK_MSG_ID_STATUSTEXT) {
                    mavlink_statustext_t statusText{};
                    mavlink_msg_statustext_decode(&message, &statusText);
                    char text[51]{};
                    std::memcpy(text, statusText.text, 50);
                    std::printf("RX STATUSTEXT severity=%u: %s\n",
                                statusText.severity, text);
                } else if (message.msgid == MAVLINK_MSG_ID_COMMAND_ACK) {
                    mavlink_command_ack_t ack{};
                    mavlink_msg_command_ack_decode(&message, &ack);
                    if (ack.command == MAV_CMD_DO_SEND_BANNER) {
                        std::printf("RX COMMAND_ACK banner result=%u\n",
                                    ack.result);
                    }
                }
            }
        }
        usleep(10000);
    }

    std::printf(
        "SUMMARY bytes=%lu packets=%lu heartbeats=%lu serial_params=%lu "
        "parse_errors=%u drops=%u\n",
        bytesRead, packetsRead, heartbeatsRead, paramsRead,
        status.parse_error, status.packet_rx_drop_count);
    close(fd);
    return heartbeatsRead > 0 ? 0 : 2;
}
