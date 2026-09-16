#!/usr/bin/env python3
"""Small browser GUI for injecting RC11/RC12 over MAVLink USB."""

import argparse
import math
import os
import threading
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

from rc_override import RELEASE, find_heartbeat, open_serial, rc_override_frame


class State:
    def __init__(self, fd, target_system, target_component, rate):
        self.fd = fd
        self.target_system = target_system
        self.target_component = target_component
        self.rate = rate
        self.rc11 = 1500
        self.rc12 = 1500
        self.sequence = 0
        self.lock = threading.Lock()
        self.stopped = threading.Event()

    def send_loop(self):
        period = 1.0 / self.rate
        while not self.stopped.is_set():
            with self.lock:
                rc11, rc12, sequence = self.rc11, self.rc12, self.sequence
                self.sequence = (sequence + 1) & 0xFF
            try:
                os.write(self.fd, rc_override_frame(
                    sequence, self.target_system, self.target_component,
                    rc11, rc12))
            except OSError:
                break
            self.stopped.wait(period)

    def set_values(self, rc11=None, rc12=None):
        with self.lock:
            if rc11 is not None:
                self.rc11 = rc11
            if rc12 is not None:
                self.rc12 = rc12


def make_handler(state):
    class Handler(BaseHTTPRequestHandler):
        def do_GET(self):
            if self.path == "/":
                with state.lock:
                    rc11, rc12 = state.rc11, state.rc12
                page = f"""<!doctype html><meta charset=utf-8>
<meta name=viewport content='width=device-width,initial-scale=1'>
<title>RC test</title><style>
body{{font:18px sans-serif;max-width:520px;margin:25px auto;padding:0 15px;background:#111;color:#eee}}
button{{width:100%;padding:14px;margin:5px 0;font-size:18px;border:0;border-radius:8px;background:#1769aa;color:white}}
input{{font-size:18px;padding:10px;width:120px;background:#222;color:#eee;border:1px solid #777}}
.row{{display:flex;gap:8px;align-items:center;margin:10px 0}}
</style><h1>RC test</h1>
<p>Текущие значения: RC11=<b>{rc11}</b>, RC12=<b>{rc12}</b></p>
<h2>RC12</h2>
<form action=/set><button name=rc12 value=2000>Верх — следующий бэнд</button></form>
<form action=/set><button name=rc12 value=1000>Низ — следующий канал</button></form>
<form action=/set><button name=rc12 value=1500>Центр — остановить переключение</button></form>
<h2>RC11</h2><form action=/set class=row>
<input name=rc11 type=number min=800 max=2200 value={rc11}>
<button type=submit>Установить RC11</button></form>
<p>Порт отправляет override постоянно с частотой {state.rate:g} Гц.</p>"""
                body = page.encode()
                self.send_response(200)
                self.send_header("Content-Type", "text/html; charset=utf-8")
                self.send_header("Content-Length", str(len(body)))
                self.end_headers()
                self.wfile.write(body)
                return
            if self.path.startswith("/set?"):
                from urllib.parse import parse_qs, urlparse
                query = parse_qs(urlparse(self.path).query)
                try:
                    rc11 = int(query["rc11"][0]) if "rc11" in query else None
                    rc12 = int(query["rc12"][0]) if "rc12" in query else None
                    if rc11 is not None and not 800 <= rc11 <= 2200:
                        raise ValueError
                    if rc12 is not None and not 800 <= rc12 <= 2200:
                        raise ValueError
                    state.set_values(rc11, rc12)
                except (KeyError, ValueError):
                    self.send_error(400, "RC values must be 800..2200")
                    return
                self.send_response(303)
                self.send_header("Location", "/")
                self.end_headers()
                return
            self.send_error(404)

        def log_message(self, format, *args):
            return

    return Handler


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", default="/dev/ttyACM1")
    parser.add_argument("--baud", type=int, default=115200)
    parser.add_argument("--listen", default="127.0.0.1")
    parser.add_argument("--web-port", type=int, default=8080)
    parser.add_argument("--rate", type=float, default=10.0)
    args = parser.parse_args()
    if not math.isfinite(args.rate) or args.rate <= 0:
        parser.error("--rate must be positive")

    fd = open_serial(args.port, args.baud)
    state = sender = server = None
    try:
        heartbeat = find_heartbeat(fd, 8.0)
        if heartbeat is None:
            print("ERROR: no heartbeat from flight controller")
            return 2
        state = State(fd, heartbeat[0], heartbeat[1], args.rate)
        server = ThreadingHTTPServer((args.listen, args.web_port), make_handler(state))
        sender = threading.Thread(target=state.send_loop, daemon=True)
        sender.start()
        print(f"FC sysid={heartbeat[0]} compid={heartbeat[1]}")
        print(f"Open http://{args.listen}:{args.web_port}/")
        server.serve_forever()
    except KeyboardInterrupt:
        pass
    finally:
        if state is not None:
            state.stopped.set()
        if sender is not None:
            sender.join()
        try:
            if server is not None:
                server.server_close()
            if sender is not None:
                os.write(fd, rc_override_frame(state.sequence, state.target_system,
                                               state.target_component, RELEASE, RELEASE))
        finally:
            os.close(fd)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
