"""Real inert process holders prove bootstrap exclusion; no endpoint or process killing."""
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
from tools.ans_launch_lock import acquire,reject_legacy
from unittest.mock import patch
ROOT=Path(__file__).resolve().parents[1]
class LaunchLockTests(unittest.TestCase):
    def test_recovery_bootstrap_holder_blocks_native_before_discovery(self):
        with tempfile.TemporaryDirectory() as temp:
            code='from tools.ans_launch_lock import acquire; import sys; acquire(sys.argv[1]); print("ready",flush=True); sys.stdin.readline()'
            p=subprocess.Popen([sys.executable,'-c',code,temp],cwd=ROOT,stdin=subprocess.PIPE,stdout=subprocess.PIPE,text=True)
            try:
                self.assertEqual(p.stdout.readline().strip(),'ready')
                with patch.dict(os.environ,{},clear=True),self.assertRaisesRegex(ValueError,'bootstrap/guest'):
                    acquire(temp)
            finally:
                p.communicate('\n',timeout=5)
            self.assertEqual(p.returncode,0)
    def test_actual_native_shell_refuses_real_bootstrap_lock_holder(self):
        code='from tools.ans_launch_lock import acquire; import sys; acquire(sys.argv[1]); print("ready",flush=True); sys.stdin.readline()'
        p=subprocess.Popen([sys.executable,'-c',code,str(ROOT)],cwd=ROOT,stdin=subprocess.PIPE,stdout=subprocess.PIPE,text=True)
        try:
            self.assertEqual(p.stdout.readline().strip(),'ready')
            r=subprocess.run(['sh',str(ROOT/'scripts/run-assisted.sh'),'--storage-mode','ANS_NATIVE'],cwd=ROOT,capture_output=True,text=True)
            self.assertNotEqual(r.returncode,0);self.assertIn('proxy lock',r.stderr)
            self.assertNotIn('Connect the Air',r.stderr)
        finally:p.communicate('\n',timeout=5)
        self.assertEqual(p.returncode,0)
    def test_legacy_bootstrap_from_another_tree_is_detected(self):
        with tempfile.TemporaryDirectory() as temp:
            script=Path(temp)/'run-assisted.sh'
            script.write_text('echo ready\nread ignored\n')
            p=subprocess.Popen(['sh',str(script)],stdin=subprocess.PIPE,stdout=subprocess.PIPE,text=True)
            try:
                self.assertEqual(p.stdout.readline().strip(),'ready')
                with patch.dict(os.environ,{},clear=True),self.assertRaisesRegex(ValueError,'legacy assisted/bootstrap'):
                    reject_legacy(ROOT)
            finally:p.communicate('\n',timeout=5)
            self.assertEqual(p.returncode,0)
    def test_legacy_chainload_process_is_detected(self):
        with tempfile.TemporaryDirectory() as temp:
            script=Path(temp)/'chainload.py'
            script.write_text('import sys\nprint("ready",flush=True)\nsys.stdin.readline()\n')
            p=subprocess.Popen([sys.executable,str(script)],stdin=subprocess.PIPE,stdout=subprocess.PIPE,text=True)
            try:
                self.assertEqual(p.stdout.readline().strip(),'ready')
                with patch.dict(os.environ,{},clear=True),self.assertRaisesRegex(ValueError,'legacy assisted/bootstrap'):
                    reject_legacy(ROOT)
            finally:p.communicate('\n',timeout=5)
            self.assertEqual(p.returncode,0)
if __name__=='__main__':unittest.main()
