"""Real owned-reader failure/exit cleanup, without UART or other-process signals."""
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch
from tools.ans_launch_lock import acquire
ROOT=Path(__file__).resolve().parents[1]

class AssistedSessionTests(unittest.TestCase):
    def test_owner_exit_and_failure_reap_reader_and_unlock(self):
        for failure in (False,True):
            with self.subTest(failure=failure),tempfile.TemporaryDirectory() as tmp:
                p=Path(tmp);reader=p/'reader.py'
                reader.write_text('import os,sys,time\nfrom pathlib import Path\nPath(sys.argv[1]).write_text(str(os.getpid()))\ntry:\n while True: time.sleep(.1)\nfinally:\n Path(sys.argv[2]).write_text("flushed")\n')
                owner=r'''
import sys,time
from pathlib import Path
from tools.ans_launch_lock import acquire
from tools.assisted_session import AssistedSession
root=Path(sys.argv[1]);s=AssistedSession(acquire(root),timeout=1)
s.start_reader([sys.executable,str(root/'reader.py'),str(root/'reader.pid'),str(root/'reader.closed')],root/'reader.log',root)
for _ in range(100):
 if (root/'reader.pid').exists():break
 time.sleep(.01)
assert (root/'reader.pid').exists()
if sys.argv[2]=='fail':raise RuntimeError('synthetic owner failure')
'''
                env=dict(os.environ);env.pop('ANS_ASSISTED_LOCK_FD',None);env.pop('ANS_ASSISTED_LOCK_OWNER_PID',None)
                r=subprocess.run([sys.executable,'-c',owner,tmp,'fail' if failure else 'exit'],cwd=ROOT,env=env,capture_output=True,text=True,timeout=10)
                self.assertEqual(r.returncode,1 if failure else 0,r.stderr)
                self.assertEqual((p/'reader.closed').read_text(),'flushed')
                pid=int((p/'reader.pid').read_text())
                probe=subprocess.run(['ps','-p',str(pid),'-o','pid='],capture_output=True,text=True)
                self.assertFalse(probe.stdout.strip(),'owned reader still alive')
                with patch.dict(os.environ,{},clear=True):
                    fd=acquire(p);os.close(fd)
    def test_actual_reader_closes_after_abrupt_owner_exit(self):
        import time
        with tempfile.TemporaryDirectory() as tmp:
            root=Path(tmp)
            (root/'serial.py').write_text('import time,os\nfrom pathlib import Path\nclass Serial:\n def __init__(self,*a,**k): Path("reader.ready").write_text(str(os.getpid()))\n def read(self,n): time.sleep(.05); return b""\n def close(self): Path("reader.closed").write_text("flushed")\n')
            owner=r"""
import os,sys,time
from pathlib import Path
from tools.ans_launch_lock import acquire
from tools.assisted_session import AssistedSession
root=Path(sys.argv[1]);s=AssistedSession(acquire(root))
s.start_reader([sys.executable,sys.argv[2],'fake-serial','30'],root/'reader.log',root)
for _ in range(200):
 if (root/'reader.ready').exists():break
 time.sleep(.01)
assert (root/'reader.ready').exists()
os._exit(3)
"""
            env=dict(os.environ);env.pop('ANS_ASSISTED_LOCK_FD',None);env.pop('ANS_ASSISTED_LOCK_OWNER_PID',None)
            env['PYTHONPATH']=str(root)
            r=subprocess.run([sys.executable,'-c',owner,tmp,str(ROOT/'extra/uart-reader.py')],cwd=ROOT,env=env,capture_output=True,text=True,timeout=10)
            self.assertEqual(r.returncode,3,r.stderr)
            for _ in range(100):
                if (root/'reader.closed').exists():break
                time.sleep(.02)
            self.assertEqual((root/'reader.closed').read_text(),'flushed')
            # The reader's exit (and its inherited lock fd) can lag its 'closed' marker by
            # a few scheduler ticks under load; bounded retry, not an unbounded wait.
            with patch.dict(os.environ,{},clear=True):
                for attempt in range(100):
                    try:fd=acquire(root);os.close(fd);break
                    except ValueError:
                        if attempt==99:raise
                        time.sleep(.02)
    def test_failed_start_closes_guard(self):
        from tools.assisted_session import AssistedSession
        with tempfile.TemporaryDirectory() as tmp,patch.dict(os.environ,{},clear=True):
            root=Path(tmp);session=AssistedSession(acquire(root))
            with self.assertRaises(FileNotFoundError):
                session.start_reader([str(root/'missing')],root/'log',root)
            self.assertIsNone(session.lock_fd)
            os.environ.pop('ANS_ASSISTED_LOCK_FD',None);os.environ.pop('ANS_ASSISTED_LOCK_OWNER_PID',None)
            fd=acquire(root);os.close(fd)
if __name__=='__main__':unittest.main()
