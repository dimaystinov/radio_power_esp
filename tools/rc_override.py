#!/usr/bin/env python3
"""Inject RC11/RC12 values into ArduPilot over a MAVLink USB link.

Examples:
  python3 tools/rc_override.py --port /dev/ttyACM1 --rc12 2000
  python3 tools/rc_override.py --port /dev/ttyACM1 --rc11 1500 --rc12 1000 --duration 10

The tool uses only Python's standard library and sends MAVLink 2
RC_CHANNELS_OVERRIDE at 10 Hz.  RC11/RC12 are MAVLink 2 extension fields.
Press Ctrl+C (or wait for --duration) to release the overridden channels.
"""

import argparse
import math
import os
import select
import struct
import termios
import time
import tty


MAVLINK2_STX = 0xFD
MSG_ID_HEARTBEAT = 0
MSG_ID_RC_CHANNELS_OVERRIDE = 70
CRC_EXTRA_RC_CHANNELS_OVERRIDE = 124
IGNORE = 0xFFFF
RELEASE = 0xFFFE
GCS_SYSTEM_ID = 255
GCS_COMPONENT_ID = 190


def crc_x25(data: bytes) -> int:
    crc = 0xFFFF
    for value in data:
        tmp = value ^ (crc & 0xFF)
        tmp = (tmp ^ ((tmp << 4) & 0xFF)) & 0xFF
        crc = ((crc >> 8) ^ (tmp << 8) ^ (tmp << 3) ^ (tmp >> 4)) & 0xFFFF
    return crc


def mavlink2_frame(sequence: int, system_id: int, component_id: int,
                   payload: bytes, message_id: int, crc_extra: int) -> bytes:
    header = struct.pack(
        "<BBBBBBB",
        len(payload), 0, 0, sequence & 0xFF,
        system_id, component_id, message_id & 0xFF,
    ) + bytes((message_id >> 8 & 0xFF, message_id >> 16 & 0xFF))
    checksum = crc_x25(header + payload + bytes((crc_extra,)))
    return bytes((MAVLINK2_STX,)) + header + payload + struct.pack("<H", checksum)


def rc_override_frame(sequence: int, target_system: int, target_component: int,
                      rc11: int, rc12: int) -> bytes:
    channels = [IGNORE] * 18
    channels[10] = rc11
    channels[11] = rc12
    # MAVLink wire order: chan1..chan8, target IDs, chan9..chan18.
    payload = struct.pack(
        "<8HBB10H", *channels[:8], target_system, target_component,
        *channels[8:]
    )
    return mavlink2_frame(
        sequence, GCS_SYSTEM_ID, GCS_COMPONENT_ID, payload,
        MSG_ID_RC_CHANNELS_OVERRIDE, CRC_EXTRA_RC_CHANNELS_OVERRIDE
    )


def open_serial(path: str, baud: int) -> int:
    fd = os.open(path, os.O_RDWR | os.O_NOCTTY | os.O_NONBLOCK)
    tty.setraw(fd)
    attrs = termios.tcgetattr(fd)
    speed = getattr(termios, f"B{baud}")
    attrs[4] = speed
    attrs[5] = speed
    attrs[2] |= termios.CLOCAL | termios.CREAD
    termios.tcsetattr(fd, termios.TCSANOW, attrs)
    return fd


def find_heartbeat(fd: int, timeout: float):
    data = bytearray()
    deadline = time.monotonic() + timeout
    first_heartbeat = None
    while time.monotonic() < deadline:
        readable, _, _ = select.select([fd], [], [], 0.2)
        if not readable:
            continue
        data.extend(os.read(fd, 4096))
        while data:
            if data[0] not in (0xFE, MAVLINK2_STX):
                del data[0]
                continue
            header_size = 6 if data[0] == 0xFE else 10
            if len(data) < header_size:
                break
            length = data[1]
            total = header_size + length + 2
            if len(data) < total:
                break
            frame = data[:total]
            del data[:total]
            if frame[0] == 0xFE:
                message_id = frame[5]
                system_id, component_id = frame[3], frame[4]
            else:
                message_id = frame[7] | (frame[8] << 8) | (frame[9] << 16)
                system_id, component_id = frame[5], frame[6]
            if message_id == MSG_ID_HEARTBEAT:
                if frame[0] == 0xFE:
                    payload = frame[6:-2]
                else:
                    payload = frame[10:-2]
                # HEARTBEAT.autopilot is byte 5. Prefer ArduPilot over the
                # ESP peripheral heartbeat routed through the same FC USB.
                autopilot = payload[5] if len(payload) > 5 else 0
                candidate = (system_id, component_id)
                if first_heartbeat is None:
                    first_heartbeat = candidate
                if autopilot == 3:  # MAV_AUTOPILOT_ARDUPILOTMEGA
                    return candidate
    return first_heartbeat


def bounded_pwm(value: int) -> int:
    try:
        value = int(value)
    except (TypeError, ValueError) as exc:
        raise argparse.ArgumentTypeError("RC value must be an integer") from exc
    if not 800 <= value <= 2200:
        raise argparse.ArgumentTypeError("RC value must be between 800 and 2200 us")
    return value


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", default="/dev/ttyACM1")
    parser.add_argument("--baud", type=int, default=115200)
    parser.add_argument("--rc11", type=bounded_pwm, default=1500,
                        help="RC11 PWM in microseconds (default: 1500)")
    parser.add_argument("--rc12", type=bounded_pwm, default=1500,
                        help="RC12 PWM in microseconds (default: 1500)")
    parser.add_argument("--rate", type=float, default=10.0)
    parser.add_argument("--duration", type=float, default=0.0,
                        help="seconds; 0 means until Ctrl+C")
    parser.add_argument("--target-system", type=int, default=None)
    parser.add_argument("--target-component", type=int, default=None)
    args = parser.parse_args()

    if not math.isfinite(args.rate) or args.rate <= 0 or not math.isfinite(args.duration) or args.duration < 0:
        parser.error("--rate must be positive and --duration non-negative")

    fd = open_serial(args.port, args.baud)
    try:
        heartbeat = find_heartbeat(fd, 8.0)
        if heartbeat is None and (args.target_system is None or args.target_component is None):
            print("ERROR: no MAVLink 2 heartbeat; specify --target-system and --target-component")
            return 2
        target_system = args.target_system if args.target_system is not None else heartbeat[0]
        target_component = args.target_component if args.target_component is not None else heartbeat[1]
        print(f"Target FC: sysid={target_system} compid={target_component}")
        print(f"Sending RC11={args.rc11} RC12={args.rc12} at {args.rate:g} Hz")

        sequence = 0
        period = 1.0 / args.rate
        started = time.monotonic()
        next_send = started
        sent = 0
        while args.duration == 0 or time.monotonic() - started < args.duration:
            now = time.monotonic()
            if now < next_send:
                time.sleep(next_send - now)
                continue
            os.write(fd, rc_override_frame(sequence, target_system, target_component,
                                           args.rc11, args.rc12))
            sequence = (sequence + 1) & 0xFF
            sent += 1
            next_send += period
            if sent % max(1, int(args.rate)) == 0:
                print(f"sent={sent} RC11={args.rc11} RC12={args.rc12}", flush=True)
    except KeyboardInterrupt:
        print("\nStopping; releasing RC11/RC12...")
    finally:
        # Extension channels 9..18 use 65534 to release to the receiver.
        try:
            os.write(fd, rc_override_frame(sequence, target_system, target_component,
                                           RELEASE, RELEASE))
        except (UnboundLocalError, OSError):
            pass
        os.close(fd)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
