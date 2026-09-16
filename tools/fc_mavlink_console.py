#!/usr/bin/env python3
"""Live human-readable MAVLink console for the connected flight controller."""

import datetime
import sys

from pymavlink import mavutil


def main() -> int:
    device = sys.argv[1] if len(sys.argv) > 1 else "/dev/ttyACM0"
    print(f"Opening flight controller MAVLink: {device} @ 115200")
    print("Close this window or press Ctrl+C to stop.\n")
    connection = mavutil.mavlink_connection(
        device, baud=115200, robust_parsing=True
    )

    try:
        while True:
            message = connection.recv_match(blocking=True, timeout=1)
            if message is None or message.get_type() == "BAD_DATA":
                continue
            timestamp = datetime.datetime.now().strftime("%H:%M:%S.%f")[:-3]
            print(
                f"{timestamp}  {message.get_type():<24} "
                f"sys={message.get_srcSystem():>3} "
                f"comp={message.get_srcComponent():>3}  {message}",
                flush=True,
            )
    except KeyboardInterrupt:
        print("\nMAVLink console stopped.")
    finally:
        connection.close()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
