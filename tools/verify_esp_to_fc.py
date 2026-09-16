#!/usr/bin/env python3
"""Verify that MAVLink STATUSTEXT sent by the ESP reached ArduPilot.

ArduPilot stores received STATUSTEXT packets as DataFlash MSG records.  This
tool downloads the beginning of the latest onboard log into memory and looks
for the exact ESP test marker without changing flight-controller parameters.
"""

import sys
import time

from pymavlink import mavutil


MARKERS = (
    b"ESP MAVLINK DISPLAY TEST",
    b"VTX power:",
    b"VTX request:",
)


def main() -> int:
    device = sys.argv[1] if len(sys.argv) > 1 else "/dev/ttyACM0"
    connection = mavutil.mavlink_connection(device, baud=115200)
    heartbeat = connection.wait_heartbeat(timeout=8)
    if heartbeat is None:
        print("ERROR: no flight-controller heartbeat")
        return 2

    target_system = connection.target_system
    target_component = connection.target_component
    print(f"FC heartbeat: sysid={target_system} compid={target_component}")

    print("Listening 5 seconds for ESP sysid=254 routed through the FC...")
    deadline = time.monotonic() + 5
    while time.monotonic() < deadline:
        message = connection.recv_match(blocking=True, timeout=1)
        if message is None or message.get_srcSystem() != 254:
            continue
        print(
            f"FOUND live ESP packet: type={message.get_type()} "
            f"sysid={message.get_srcSystem()} compid={message.get_srcComponent()}"
        )
        print("PASS: FC received and routed ESP MAVLink traffic")
        return 0

    connection.mav.log_request_list_send(
        target_system, target_component, 0, 0xFFFF
    )
    entries = {}
    deadline = time.monotonic() + 8
    expected_logs = None
    while time.monotonic() < deadline:
        message = connection.recv_match(type="LOG_ENTRY", blocking=True, timeout=1)
        if message is None:
            continue
        entries[message.id] = message
        expected_logs = message.num_logs
        if expected_logs and len(entries) >= expected_logs:
            break

    if not entries:
        print("ERROR: flight controller reported no downloadable logs")
        return 3

    found = False
    for entry_id in sorted(entries, reverse=True):
        entry = entries[entry_id]
        read_size = min(int(entry.size), 256 * 1024)
        print(
            f"Log id={entry.id} size={entry.size}; "
            f"scanning first {read_size} bytes"
        )

        connection.mav.log_request_data_send(
            target_system, target_component, entry.id, 0, read_size
        )
        chunks = {}
        received = 0
        deadline = time.monotonic() + 25
        while time.monotonic() < deadline and received < read_size:
            message = connection.recv_match(
                type="LOG_DATA", blocking=True, timeout=1
            )
            if (
                message is None
                or message.id != entry.id
                or message.ofs >= read_size
            ):
                continue
            data = bytes(message.data[: message.count])
            if message.ofs not in chunks:
                chunks[message.ofs] = data
                received += len(data)

        connection.mav.log_request_end_send(target_system, target_component)
        image = bytearray(read_size)
        present = bytearray(read_size)
        for offset, data in chunks.items():
            end = min(offset + len(data), read_size)
            image[offset:end] = data[: end - offset]
            present[offset:end] = b"\x01" * (end - offset)

        print(f"Received {received}/{read_size} log bytes")
        for marker in MARKERS:
            offset = image.find(marker)
            if offset >= 0 and all(present[offset : offset + len(marker)]):
                print(
                    f"FOUND in log {entry.id} at offset {offset}: "
                    f"{marker.decode()}"
                )
                found = True
        if found:
            break

    if found:
        print("PASS: ESP MAVLink text reached the flight controller")
        return 0
    print("NOT FOUND: ESP marker is absent from the scanned part of latest log")
    return 4


if __name__ == "__main__":
    raise SystemExit(main())
