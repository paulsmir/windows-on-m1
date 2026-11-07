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
    def call(self, *, opt=False, at_proxy=False, endpoints=True, selector='zero', pgrep_rc=1, pgrep_out='', ssh_rc=0, ssh_out='up\r\n'):
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
                       PGREP_RC=str(pgrep_rc),PGREP_OUT=pgrep_out,SSH_RC=str(ssh_rc),SSH_OUT=ssh_out,
                       ENDPOINTS_RC='0' if endpoints else '1')
            env.pop('AGX_RECOVERY_ALLOW_NO_OWNER',None);env.pop('AGX_RECOVERY_ALLOW_AT_PROXY',None)
            if opt:env['AGX_RECOVERY_ALLOW_NO_OWNER']='1'
            if at_proxy:env['AGX_RECOVERY_ALLOW_AT_PROXY']='1'
            stubs="""
python3() { printf 'selector\n' >> "$TRACE"; printf '%s' "$SELECTOR_OUT"; printf '%s' "$SELECTOR_ERR" >&2; return "$SELECTOR_RC"; }
pgrep() { printf 'pgrep\n' >> "$TRACE"; printf '%s' "$PGREP_OUT"; return "$PGREP_RC"; }
ssh_bounded() { printf 'ssh\n' >> "$TRACE"; printf '%s' "$SSH_OUT"; return "$SSH_RC"; }
proxy_endpoints_present() { printf 'endpoints\n' >> "$TRACE"; return "$ENDPOINTS_RC"; }
"""
            result=subprocess.run(['/bin/bash','-c',stubs+matches[0]+'\nselect_recovery_owner\n'],env=env,capture_output=True,text=True,timeout=5)
            return result,trace.read_text().splitlines() if trace.exists() else []

    def test_guest_copies_are_bounded(self):
        # EXP1890: the owner was alive when recovery started, the guest was
        # hung, and an unbounded airscp to it blocked recovery for 11 minutes.
        script=(ROOT/'scripts/agx-recover-visible.sh').read_text()
        copies=[l for l in script.splitlines() if 'airscp' in l and not l.lstrip().startswith('#')]
        self.assertTrue(copies)
        for line in copies:
            self.assertIn("perl -e 'alarm ", line)
            self.assertIn('VISIBLE_COPY_FAILED', line)

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

    # EXP1876G: a guest bugcheck reset the air to Running proxy; run_uefi exited
    # and Windows SSH is down, so neither the owned nor the live-Windows path applies.
    def test_at_proxy_opt_in_after_guest_reset(self):
        result,trace=self.call(at_proxy=True,ssh_rc=255,ssh_out='')
        self.assertEqual(result.returncode,0);self.assertEqual(result.stdout,'AT_PROXY\n');self.assertEqual(trace,['selector','pgrep','ssh','endpoints'])
    def test_at_proxy_needs_both_endpoints(self):
        result,trace=self.call(at_proxy=True,ssh_rc=255,ssh_out='',endpoints=False)
        self.assertEqual(result.returncode,3);self.assertEqual(trace,['selector','pgrep','ssh','endpoints'])
    def test_at_proxy_never_while_windows_is_up(self):
        result,trace=self.call(at_proxy=True)
        self.assertEqual(result.returncode,3);self.assertEqual(trace,['selector','pgrep','ssh'])
    def test_at_proxy_refuses_a_foreign_launcher(self):
        result,trace=self.call(at_proxy=True,pgrep_rc=0,pgrep_out='777\n')
        self.assertEqual(result.returncode,3);self.assertEqual(trace,['selector','pgrep'])
