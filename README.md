# TX3000AC power controller — ESP32-S3

Controls a TX3000AC video transmitter using MAVLink RC11/RC12 from a flight
controller, or a Wi-Fi web interface. There is no GPS/distance-based power
algorithm: RC11 selects the power level directly.

## Wiring

| Flight controller / VTX | ESP32-S3 |
|---|---|
| FC T1 | GPIO9 through 1 kOhm |
| FC R1 | GPIO10 through 1 kOhm |
| TX3000AC SmartAudio | GPIO17 through 1 kOhm |
| FC and VTX GND | ESP GND |

SmartAudio is a bidirectional one-wire bus. Both TX and RX use GPIO17;
GPIO44 is not used. The firmware releases GPIO17 after each transmission.
Disconnect any other SmartAudio controller from this bus. See [schema.md](schema.md).

## Control and failsafe

The firmware binds to the first valid autopilot heartbeat (component 1) until
reboot and accepts RC_CHANNELS only from that system/component. It requests
RC_CHANNELS at 10 Hz over a 115200-baud UART.

| RC11 (microseconds) | Requested power |
|---|---|
| 800–1166 | 25 mW |
| 1167–1333 | 250 mW |
| 1334–1499 | 500 mW |
| 1500–1666 | 1000 mW |
| 1667–1833 | 2000 mW |
| 1834–2200 | 3000 mW |

In AUTO, startup/no RC data, an invalid RC11 sample, or more than 2 seconds
without valid RC11 requests 25 mW. Switching back to AUTO uses only a fresh,
valid RC11 value; otherwise it requests 25 mW. Pending high-power repetitions
are cancelled when the target changes. Hardware application still requires a
working SmartAudio bus and a responding VTX.

RC12 held at 1800–2200 steps to the next band; 800–1200 steps to the next
channel. The first step occurs after 1 second, then repeats each second.
1500 stops stepping. Manual power mode does not disable RC12 frequency control.

Desired power/frequency and VTX-reported settings are separate. Unconfirmed
settings are retried at intervals of at least 2 seconds between attempts.
GET_SETTINGS normally runs every 2 seconds, with extra queries after commands
and during baud discovery. Readback expires after 6 seconds without a reply.
Commands use an unlock followed by three transmissions; the 200/120 ms gaps
and reply timeouts are scheduled across loop iterations. Only UART frame
transmission is synchronous (about 14–19 ms per SmartAudio frame).

STATUSTEXT messages say **VTX request**, not confirmation of applied power.
GET_SETTINGS readback is the source for reported state. Parsing is specific to
the observed TX3000AC reply (AA 55 11 0E, zero-based channel/power); it is not a
general SmartAudio implementation for arbitrary transmitters.

## Web interface

Connect to Wi-Fi `TX3000AC` (default password `robossembler`) and open
http://tx3000ac.local/ or http://192.168.4.1/.

- AUTO: power follows RC11 and the failsafe rules above.
- Manual power buttons: ignore RC11 and its telemetry timeout until AUTO is selected.
- Band/channel: queue a frequency change.
- Requested power and VTX-reported state show target and readback separately.

The Wi-Fi password controls access; the HTTP UI has no separate login.
Change credentials, pins and thresholds in `src/config.h` and reflash.

## Build and upload

Install Python 3 and PlatformIO Core. The ESP32 platform and MAVLink headers
are pinned in `platformio.ini`; no machine-specific include paths are needed.
The first build downloads dependencies.

```sh
python3 -m pip install platformio==6.1.18
pio run -e esp32-s3
pio run -e esp32-s3-no-web
pio run -e esp32-s3 -t upload
pio device monitor
```

Upload only the environment you intend to use. `esp32-s3-no-web` omits Wi-Fi,
mDNS and the HTTP UI. Builds appear in `.pio/build/<environment>/`.

## Tests

```sh
python3 -m unittest discover -s tests -v
```

Tests require a C++17 compiler and Python 3, with no Python test dependencies.
They compile the full firmware source against simulated serial/clock/HTTP
adapters and exercise control, failsafe, SmartAudio scheduling and parsing.
MAVLink serialization is stubbed in host firmware tests; both real ESP32 builds
validate integration with the pinned headers. Utility tests inspect actual
MAVLink override frames, including cleanup. Tests do not verify electrical
signals, VTX behavior, RF power, or OSD display on hardware.

GitHub Actions runs the host tests and both firmware builds.

## RC test utilities

Linux/macOS, with the flight controller connected by USB:

```sh
python3 tools/rc_override.py --port /dev/ttyACM1 --rc11 1500 --rc12 1500
python3 tools/rc_override_web.py --port /dev/ttyACM1
```

For the latter, open http://127.0.0.1:8080/. Both tools send override continuously;
closing the browser does not stop the server. Stop with Ctrl+C. RC11/RC12 are
released using 65534 (the MAVLink extension-channel release value). FC override
acceptance and timeout depend on the FC configuration.

The console/log diagnostic scripts additionally require `pymavlink`.
To regenerate the Russian PDF manual, install `reportlab` and run
`python3 tools/make_user_manual.py`. The generator uses DejaVu fonts on Linux (`fonts-dejavu-core`) or system fonts on macOS.

## Hardware acceptance checks

Before relying on the firmware, check the actual TX3000AC: every power level,
frequency changes, unplugged RC UART, no RC since boot, AUTO/manual transitions,
VTX disconnect/reconnect, and receiver display. Confirm actual reported settings
and measure bus timing; a successful build is not an on-device test.
