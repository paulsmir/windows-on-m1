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
    def test_own_wrapper_via_symlinked_root_is_not_legacy(self):
        # ANS001-R3 (2026-11-06): /Users/pavel/public_windows is a symlink to
        # /Volumes/pwdev/public_windows; the guarded run-assisted.sh ancestor was
        # rejected as "legacy" because its argv path was compared unresolved.
        with tempfile.TemporaryDirectory() as temp:
            real=Path(temp)/'real';(real/'scripts').mkdir(parents=True)
            (real/'scripts/run-assisted.sh').write_text('')
            alias=Path(temp)/'alias';alias.symlink_to(real)
            owner=999001
            ps=f"{os.getpid()} {owner} python ans_launch_lock.py --check {alias}\n{owner} 1 sh {alias}/scripts/run-assisted.sh --storage-mode ANS_NATIVE\n"
            other=f"{owner+1} 1 sh {temp}/elsewhere/run-assisted.sh\n"
            fake=lambda *a,**k:subprocess.CompletedProcess(a,0,ps,'')
            with patch('tools.ans_launch_lock.subprocess.run',fake),patch('tools.ans_launch_lock.inherited_fd',lambda r:5),\
                 patch.dict(os.environ,{'ANS_ASSISTED_LOCK_OWNER_PID':str(owner)},clear=True):
                reject_legacy(alias)
                reject_legacy(real)
            fake2=lambda *a,**k:subprocess.CompletedProcess(a,0,ps+other,'')
            with patch('tools.ans_launch_lock.subprocess.run',fake2),patch('tools.ans_launch_lock.inherited_fd',lambda r:5),\
                 patch.dict(os.environ,{'ANS_ASSISTED_LOCK_OWNER_PID':str(owner)},clear=True),\
                 self.assertRaisesRegex(ValueError,'legacy assisted/bootstrap'):
                reject_legacy(alias)
    def test_launch_refuses_non_fresh_proxy_before_discovery(self):
        # ANS001-R3 attempt 4: no launch may chainload over an aborted hv.init.
        import shutil
        with tempfile.TemporaryDirectory() as temp:
            root=Path(temp);(root/'scripts').mkdir();(root/'tools').mkdir();(root/'.local').mkdir()
            shutil.copy(ROOT/'scripts/run-assisted.sh',root/'scripts/run-assisted.sh')
            shutil.copy(ROOT/'tools/ans_launch_lock.py',root/'tools/ans_launch_lock.py')
            marker=root/'.local/ans-proxy-not-fresh';marker.write_text('2026-11-06T10:45:39Z\n')
            env={k:v for k,v in os.environ.items() if not k.startswith('ANS_ASSISTED_LOCK')}
            r=subprocess.run(['sh',str(root/'scripts/run-assisted.sh'),'--storage-mode','ANS_NATIVE'],cwd=root,capture_output=True,text=True,env=env)
            self.assertEqual(r.returncode,3,r.stderr);self.assertIn('Proxy is not fresh',r.stderr)
            self.assertNotIn('Connect the Air',r.stderr)
            r=subprocess.run(['sh',str(root/'scripts/run-assisted.sh'),'--storage-mode','ANS_NATIVE','--dry-run'],cwd=root,capture_output=True,text=True,env=env)
            self.assertNotIn('Proxy is not fresh',r.stderr)
if __name__=='__main__':unittest.main()
