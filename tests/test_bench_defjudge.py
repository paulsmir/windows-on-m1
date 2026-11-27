# SPDX-License-Identifier: MIT
"""defjudge.py: a Defender update is judged by its own install stamp, not by the watcher poll that first sees it (EXP3015)."""
import os, subprocess, sys, tempfile, unittest
TOOL = os.path.join(os.path.dirname(__file__), '..', 'tools', 'bench', 'defjudge.py')
DW = """14:36:51Z DEFWATCH sig=1.463.493.0 sigupd=x engine=1.1.26100.4 ev19=2026-11-27T14:33:33 ev19msg=Virtualisation Based Security new timer creation status
14:40:01Z DEFWATCH sig=1.463.493.0 sigupd=x engine=1.1.26100.4 ev19=2026-11-27T14:38:39 ev19msg=Virtualisation Based Security new timer creation status
14:50:11Z DEFWATCH sig=1.463.495.0 sigupd=x engine=1.1.26100.4 ev19=2026-11-27T14:49:31 ev19msg=Installation Successful: Windows successfully installed the following update: Security
"""
RUN = """14:43:40Z GM_START gm3015p1 kind=perf
14:45:22Z GM_RESULT gm3015p1 VALID
14:47:04Z GM_START gm3015p2 kind=perf
14:48:46Z GM_RESULT gm3015p2 VALID
14:49:12Z GM_START gm3015kg kind=perf
14:52:30Z GM_RESULT gm3015kg VALID
"""
class DefJudge(unittest.TestCase):
    def run_tool(self, dw, run):
        with tempfile.TemporaryDirectory() as d:
            a, b = os.path.join(d, 'dw.log'), os.path.join(d, 'run.log')
            open(a, 'w').write(dw); open(b, 'w').write(run)
            out = subprocess.run([sys.executable, '-I', TOOL, a, b], capture_output=True, text=True, check=True).stdout
        return {l.split()[1]: l.split()[2] for l in out.splitlines()}
    def test_update_after_arm_end_is_not_charged_to_the_arm(self):
        r = self.run_tool(DW, RUN)
        self.assertEqual(r['gm3015p1'], 'OK'); self.assertEqual(r['gm3015p2'], 'OK')
    def test_update_inside_arm_is_invalid(self):
        self.assertEqual(self.run_tool(DW, RUN)['gm3015kg'], 'INVALID')
    def test_vbs_timer_lines_alone_never_invalidate(self):
        r = self.run_tool(DW.splitlines(True)[0] + DW.splitlines(True)[1], RUN)
        self.assertTrue(all(v in ('OK', 'NO_DATA') for v in r.values()))
if __name__ == '__main__':
    unittest.main()
