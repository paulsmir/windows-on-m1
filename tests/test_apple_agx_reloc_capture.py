from pathlib import Path
import os
import subprocess
import tempfile
import unittest

ROOT=Path(__file__).resolve().parents[1]
class RelocCaptureTests(unittest.TestCase):
    def test_capture_transport_materializer(self):
        shared=ROOT/'drivers/apple-agx/shared'
        winsys=ROOT/'drivers/apple-agx/mesa/winsys'
        kmd=ROOT/'drivers/apple-agx/render-admission'
        common=Path(subprocess.check_output(['git','rev-parse','--path-format=absolute','--git-common-dir'],cwd=ROOT,text=True).strip()).parent
        mesa=common/'.local/reference/mesa'
        generated=common/'.local/accelerated-desktop-ad03/mesa-build/src/asahi/genxml'
        with tempfile.TemporaryDirectory(prefix='ad04-reloc-') as tmp:
            exe=Path(tmp)/'test'
            command=[os.environ.get('CC','clang'),'-std=c11','-Wall','-Wextra','-Werror',
                '-DHAVE_STRUCT_TIMESPEC','-DHAVE_PTHREAD',
                '-fsanitize=address,undefined','-I',str(shared/'include'),
                '-I',str(winsys),'-I',str(kmd/'include'),
                '-isystem',str(generated),'-isystem',str(mesa/'src'),'-isystem',str(mesa/'include'),
                str(winsys/'agx_win32_reloc_capture_test.c'),
                str(winsys/'agx_win32_reloc_capture.c'),str(winsys/'agx_win32_native_pool_bridge.c'),str(winsys/'agx_win32_transport.c'),
                str(shared/'src/apple_agx_win32_abi.c'),str(kmd/'src/apple_agx_dynamic_job.c'),
                '-o',str(exe)]
            result=subprocess.run(command,capture_output=True,text=True)
            self.assertEqual(result.returncode,0,result.stderr)
            result=subprocess.run([str(exe)],capture_output=True,text=True)
            self.assertEqual(result.returncode,0,result.stdout+result.stderr)
            print(result.stdout.strip())
