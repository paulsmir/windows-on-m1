from pathlib import Path
import os, subprocess, tempfile, unittest

ROOT = Path(__file__).resolve().parents[1]
W = ROOT / 'drivers/apple-agx/mesa/winsys'
S = ROOT / 'drivers/apple-agx/shared/include'

class NativeDeviceTests(unittest.TestCase):
 def test_compiles_native_device_owner_contract(self):
  with tempfile.TemporaryDirectory() as d:
   exe = Path(d) / 'native_device'
   subprocess.run([os.environ.get('CC', 'cc'), '-std=c11', '-Wall', '-Wextra', '-Werror',
                   '-I', str(W), '-I', str(S), str(W / 'agx_win32_native_device_test.c'),
                   str(W / 'agx_win32_native_device.c'), str(W / 'agx_win32_native_bo.c'),
                   str(W / 'agx_win32_construction_address.c'), str(W / 'agx_win32_screen.c'),
                   str(W / 'agx_win32_transport.c'), str(ROOT / 'drivers/apple-agx/shared/src/apple_agx_win32_abi.c'),
                   '-o', str(exe)], check=True)
   subprocess.run([str(exe)], check=True)
