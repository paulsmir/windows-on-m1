"""Bounded retry only for a transient vanished proxy port during recovery."""
from pathlib import Path
import os
import subprocess
import sys
import tempfile
import unittest
ROOT=Path(__file__).resolve().parents[1]
HELPER=ROOT/'scripts/agx-recovery-proxy-ready.sh'
class ProxyReadyTest(unittest.TestCase):
    def run_case(self,mode):
        with tempfile.TemporaryDirectory() as td:
            d=Path(td);p1=d/'L41';p2=d/'L43';p1.touch();p2.touch()
            runner=d/'runner.py';count=d/'count';log=d/'proxy.log'
            runner.write_text('''from pathlib import Path
import sys
p=Path(sys.argv[1]);n=int(p.read_text())+1 if p.exists() else 1;p.write_text(str(n))
if sys.argv[2]=='transient' and n==1:print('Air serial port absent: /dev/cu.test');sys.exit(74)
if sys.argv[2]=='foreign':print('serial lock owned by someone else');sys.exit(70)
print('PROXY_NOP_PASS')
''')
            env={**os.environ,'AGX_RECOVERY_PROXY_READY_TIMEOUT_SECONDS':'5',
                 'AGX_RECOVERY_PROXY_SETTLE_SECONDS':'0',
                 'AGX_RECOVERY_PROXY_RETRY_SECONDS':'0'}
            r=subprocess.run(['bash',str(HELPER),str(p1),str(p2),str(log),
                sys.executable,str(runner),str(count),mode],capture_output=True,text=True,timeout=10,env=env)
            return r,int(count.read_text()) if count.exists() else 0,log.read_text() if log.exists() else ''
    def test_transient_reenumeration_retries_once(self):
        r,n,log=self.run_case('transient')
        self.assertEqual(r.returncode,0,r.stderr)
        self.assertEqual(n,2)
        self.assertIn('PROXY_NOP_PASS',log)
    def test_unrelated_failure_does_not_retry(self):
        r,n,log=self.run_case('foreign')
        self.assertNotEqual(r.returncode,0)
        self.assertEqual(n,1)
        self.assertIn('serial lock owned',log)
