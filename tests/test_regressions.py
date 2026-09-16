import importlib.util
import os
from pathlib import Path
import shutil
import struct
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]

class FirmwareTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp = tempfile.TemporaryDirectory(prefix="vtx-tests-")
        cls.binary = Path(cls.temp.name) / "firmware_test"
        compiler = os.environ.get("CXX") or shutil.which("c++")
        if not compiler:
            raise RuntimeError("A C++17 compiler is required")
        subprocess.run([compiler, "-std=c++17", "-I", str(ROOT / "tests/stubs"),
                        str(ROOT / "tests/firmware_test.cpp"), "-o", str(cls.binary)], check=True)

    @classmethod
    def tearDownClass(cls):
        cls.temp.cleanup()

def firmware_case(case):
    def test(self):
        result = subprocess.run([str(self.binary), case], capture_output=True, text=True)
        self.assertEqual(result.returncode, 0, result.stderr)
    return test

for case in ("invalid_auto", "boot_failsafe", "foreign_rc", "desired_survives_readback",
             "responsive_loop", "status_expires", "invalid_http", "failsafe_cancels_high_power",
             "rc_timeout", "manual_ignores_timeout", "fresh_auto", "heartbeat_binding", "millis_wrap",
             "pwm_edges", "status_reply", "frequency_readback", "parser_rejects_noise"):
    setattr(FirmwareTests, "test_" + case, firmware_case(case))

class OverrideTests(unittest.TestCase):
    def test_extension_channels_release(self):
        spec = importlib.util.spec_from_file_location("rc_override", ROOT / "tools/rc_override.py")
        module = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(module)
        frame = module.rc_override_frame(7, 1, 1, module.RELEASE, module.RELEASE)
        payload = struct.unpack("<8HBB10H", frame[10:-2])
        self.assertEqual(payload[12:14], (65534, 65534))
        self.assertEqual(payload[:8], (65535,) * 8)

class UtilityLifecycleTests(unittest.TestCase):
    def run_script(self, script):
        env = dict(os.environ, PYTHONDONTWRITEBYTECODE="1", PYTHONPATH=str(ROOT / "tools"))
        try:
            result = subprocess.run([os.sys.executable, "-c", script], env=env,
                                    capture_output=True, text=True, timeout=3)
        except subprocess.TimeoutExpired:
            self.fail("Utility did not shut down promptly")
        self.assertEqual(result.returncode, 0, result.stderr)

    def test_cli_releases_on_duration(self):
        self.run_script("""
import os,struct,tempfile
from unittest.mock import patch
import rc_override as m
fd,path=tempfile.mkstemp()
try:
 with patch.object(m,'open_serial',return_value=fd), patch.object(m,'find_heartbeat',return_value=(1,1)), patch('sys.argv',['rc','--rate','1000','--duration','0.002']):
  assert m.main()==0
 data=open(path,'rb').read()
 assert struct.unpack('<8HBB10H',data[-50:][10:-2])[12:14]==(65534,65534)
finally: os.unlink(path)
""")

    def test_web_releases_after_stopping_slow_sender(self):
        self.run_script("""
import os,struct,tempfile
from unittest.mock import patch,MagicMock
import rc_override_web as m
fd,path=tempfile.mkstemp()
server=MagicMock();server.serve_forever.side_effect=KeyboardInterrupt
try:
 with patch.object(m,'open_serial',return_value=fd), patch.object(m,'find_heartbeat',return_value=(1,1)), patch.object(m,'ThreadingHTTPServer',return_value=server), patch('sys.argv',['web','--rate','0.001']):
  assert m.main()==0
 data=open(path,'rb').read()
 assert struct.unpack('<8HBB10H',data[-50:][10:-2])[12:14]==(65534,65534)
finally: os.unlink(path)
""")

    def test_web_closes_port_without_heartbeat(self):
        self.run_script("""
import os,tempfile
from unittest.mock import patch
import rc_override_web as m
fd,path=tempfile.mkstemp()
try:
 with patch.object(m,'open_serial',return_value=fd), patch.object(m,'find_heartbeat',return_value=None), patch('sys.argv',['web']):
  assert m.main()==2
 try: os.fstat(fd)
 except OSError: pass
 else: raise AssertionError('Serial fd leaked')
finally: os.unlink(path)
""")

    def test_nonfinite_rate_rejected_before_serial(self):
        self.run_script("""
from unittest.mock import patch
import rc_override as cli,rc_override_web as web
for module in (cli,web):
 for value in ('nan','inf'):
  with patch.object(module,'open_serial',side_effect=AssertionError('opened serial')), patch('sys.argv',['tool','--rate',value]):
   try: module.main()
   except SystemExit as e: assert e.code==2
   else: raise AssertionError('invalid rate accepted')
""")

if __name__ == "__main__":
    unittest.main()
