#pragma once
#include <cstdint>
constexpr int MAV_COMP_ID_PERIPHERAL=158,MAV_COMP_ID_AUTOPILOT1=1;
constexpr int MAV_SEVERITY_INFO=6,MAV_SEVERITY_WARNING=4;
constexpr int MAVLINK_MSG_ID_HEARTBEAT=0,MAVLINK_MSG_ID_RC_CHANNELS=65;
constexpr int MAV_TYPE_ONBOARD_CONTROLLER=18,MAV_AUTOPILOT_INVALID=8,MAV_STATE_ACTIVE=4;
constexpr int MAV_CMD_SET_MESSAGE_INTERVAL=511,MAVLINK_COMM_0=0,MAVLINK_MAX_PACKET_LEN=280;
struct mavlink_message_t {int msgid; uint8_t sysid,compid; uint16_t rc11=0,rc12=0; uint8_t autopilot=3;};
struct mavlink_rc_channels_t {uint16_t chan11_raw,chan12_raw;};
struct mavlink_heartbeat_t {uint8_t autopilot;};
struct mavlink_status_t {};
inline void mavlink_msg_rc_channels_decode(const mavlink_message_t* m,mavlink_rc_channels_t* c){c->chan11_raw=m->rc11;c->chan12_raw=m->rc12;}
inline void mavlink_msg_heartbeat_decode(const mavlink_message_t* m,mavlink_heartbeat_t* h){h->autopilot=m->autopilot;}
template<class... T> void mavlink_msg_statustext_pack(T...){}
template<class... T> void mavlink_msg_heartbeat_pack(T...){}
template<class... T> void mavlink_msg_command_long_pack(T...){}
inline uint16_t mavlink_msg_to_send_buffer(uint8_t* b,const mavlink_message_t*){b[0]=0;return 1;}
inline bool mavlink_parse_char(int,uint8_t,mavlink_message_t*,mavlink_status_t*){return false;}
