"""Actual Bash owner selection: opt-in recovery without any live host launcher.

Only the selected function executes. All selector/process/SSH operations are
stubbed; no guest, registry, service, signal or recovery script is run.
"""
import os
from pathlib import Path
import re
import subprocess
import tempfile
import unittest

ROOT=Path(__file__).resolve().parents[1]


class RecoveryOwnerSelection(unittest.TestCase):
    def call(self, *, opt=False, selector='zero', pgrep_rc=1, pgrep_out='', ssh_rc=0, ssh_out='up\r\n'):
        script=(ROOT/'scripts/agx-recover-visible.sh').read_text()
        matches=re.findall(r'^select_recovery_owner\(\) \{\n.*?^}\n', script, re.M|re.S)
        self.assertEqual(len(matches),1,'one actual source-owned selector function')
        choices={'zero':(3,'','EXACT_OWNER_REFUSED: expected exactly one owner; found 0\n'),
                 'owned':(0,'4242\n',''),
                 'ambiguous':(3,'','EXACT_OWNER_REFUSED: expected exactly one owner; found 2\n'),
                 'malformed':(3,'','EXACT_OWNER_REFUSED: malformed launcher command\n')}
        rc,out,err=choices[selector]
        with tempfile.TemporaryDirectory(prefix='recovery-owner-unit-') as directory:
            trace=Path(directory)/'trace'
            env=os.environ.copy()
            env.update(W='/unused/project',N='/unused/experiment',TRACE=str(trace),
                       SELECTOR_RC=str(rc),SELECTOR_OUT=out,SELECTOR_ERR=err,
                       PGREP_RC=str(pgrep_rc),PGREP_OUT=pgrep_out,SSH_RC=str(ssh_rc),SSH_OUT=ssh_out)
            env.pop('AGX_RECOVERY_ALLOW_NO_OWNER',None)
            if opt:env['AGX_RECOVERY_ALLOW_NO_OWNER']='1'
            stubs="""
python3() { printf 'selector\n' >> "$TRACE"; printf '%s' "$SELECTOR_OUT"; printf '%s' "$SELECTOR_ERR" >&2; return "$SELECTOR_RC"; }
pgrep() { printf 'pgrep\n' >> "$TRACE"; printf '%s' "$PGREP_OUT"; return "$PGREP_RC"; }
ssh_bounded() { printf 'ssh\n' >> "$TRACE"; printf '%s' "$SSH_OUT"; return "$SSH_RC"; }
"""
            result=subprocess.run(['/bin/bash','-c',stubs+matches[0]+'\nselect_recovery_owner\n'],env=env,capture_output=True,text=True,timeout=5)
            return result,trace.read_text().splitlines() if trace.exists() else []

    def test_owned_success_default_unchanged(self):
        result,trace=self.call(selector='owned')
        self.assertEqual(result.returncode,0);self.assertEqual(result.stdout,'4242\n');self.assertEqual(trace,['selector'])
    def test_default_zero_owner_refused(self):
        result,trace=self.call()
        self.assertEqual(result.returncode,3);self.assertEqual(result.stdout,'');self.assertEqual(trace,['selector'])
    def test_opt_in_zero_owner_live_windows_no_launcher(self):
        result,trace=self.call(opt=True)
        self.assertEqual(result.returncode,0);self.assertEqual(result.stdout,'NO_HOST_OWNER\n');self.assertEqual(trace,['selector','pgrep','ssh'])
    def test_foreign_launcher_refused_before_ssh(self):
        result,trace=self.call(opt=True,pgrep_rc=0,pgrep_out='777\n')
        self.assertEqual(result.returncode,3);self.assertEqual(trace,['selector','pgrep'])
    def test_ambiguous_selector_refused(self):
        result,trace=self.call(opt=True,selector='ambiguous')
        self.assertEqual(result.returncode,3);self.assertEqual(trace,['selector'])
    def test_guest_down_refused(self):
        result,trace=self.call(opt=True,ssh_rc=255,ssh_out='')
        self.assertEqual(result.returncode,3);self.assertEqual(trace,['selector','pgrep','ssh'])
    def test_pgrep_error_refused(self):
        result,trace=self.call(opt=True,pgrep_rc=2,pgrep_out='failed')
        self.assertEqual(result.returncode,3);self.assertEqual(trace,['selector','pgrep'])
    def test_other_selector_error_never_qualifies(self):
        result,trace=self.call(opt=True,selector='malformed')
        self.assertEqual(result.returncode,3);self.assertEqual(trace,['selector'])
    def test_pgrep_no_match_with_output_is_not_proven_absent(self):
        result,trace=self.call(opt=True,pgrep_rc=1,pgrep_out='inconsistent')
        self.assertEqual(result.returncode,3);self.assertEqual(trace,['selector','pgrep'])
