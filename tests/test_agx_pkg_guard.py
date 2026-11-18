"""Decision table of the L4(i) AppleAgx launcher guard (tools/agx_pkg_guard.py).

Catches: a launcher booting the standing profile with an unrecorded, altered or
duplicated GPU driver package, a blind proxy start, a candidate re-used after its
experiment closed, and a recovery that could be refused.
"""
import json
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
import agx_pkg_guard as g  # noqa: E402

PAIR = "fab4356f+fd94b781"
FILES = {"AppleAgxRenderAdmission.sys": "e20e622f" + "0" * 56,
         "AppleAgxRenderAdmission.inf": "51c368df" + "0" * 56,
         "appleagxrenderadmission.cat": "5dce69bc" + "0" * 56}
REC = {"version": "30.0.2481.0", "files": FILES, "valid_on_pairs": [PAIR]}
CAND = dict(REC, experiment_id="EXP2720")
PKG = {"oem": "oem40.inf", "files": dict(FILES)}
BOUND = {"problem": 0, "version": "30.0.2481.0"}
FREE = {"problem": 28, "version": None}


def d(**kw):
    base = dict(mode="standing", phase="pre", pair=PAIR, staged=[PKG], appl0002=None, record=REC)
    base.update(kw)
    return g.decide(**base)


class Decide(unittest.TestCase):
    def test_no_package_accepts(self):
        self.assertEqual(d(staged=[], record=None)[0], "ACCEPT")

    def test_equal_package_accepts(self):
        self.assertEqual(d()[0], "ACCEPT")

    def test_sys_or_inf_or_cat_differs_refuses(self):
        for name in FILES:
            bad = {"oem": "oem40.inf", "files": dict(FILES, **{name: "f" * 64})}
            v, why = d(staged=[bad])
            self.assertEqual(v, "REFUSE", name)
            self.assertIn(name.lower(), why)

    def test_record_field_missing_in_staged_refuses(self):
        part = {"oem": "oem40.inf", "files": {k: v for k, v in FILES.items() if not k.endswith(".cat")}}
        self.assertEqual(d(staged=[part])[0], "REFUSE")

    def test_file_name_case_does_not_matter(self):
        lower = {"oem": "oem40.inf", "files": {k.lower(): v.upper() for k, v in FILES.items()}}
        self.assertEqual(d(staged=[lower])[0], "ACCEPT")

    def test_two_staged_refuses(self):
        self.assertEqual(d(staged=[PKG, PKG])[0], "REFUSE")

    def test_no_record_refuses(self):
        self.assertEqual(d(record=None)[0], "REFUSE")

    def test_pair_not_listed_refuses(self):
        self.assertEqual(d(pair="6a9c2435+d9fe3a34")[0], "REFUSE")

    def test_recovery_with_two_staged_launches(self):
        self.assertEqual(d(mode="recovery", staged=[PKG, PKG], record=None)[0], "ACCEPT")

    def test_recovery_with_no_read_launches(self):
        self.assertEqual(d(mode="recovery", staged=None, record=None)[0], "ACCEPT")

    def test_proxy_start_without_last_known_refuses(self):
        self.assertEqual(d(staged=None)[0], "REFUSE")

    def test_post_bound_version_differs_refuses(self):
        v, why = d(phase="post", appl0002={"problem": 0, "version": "30.0.2480.0"})
        self.assertEqual(v, "REFUSE")
        self.assertIn("bound-version", why)

    def test_post_bound_but_failed_start_refuses(self):
        v, why = d(phase="post", appl0002={"problem": 43, "version": "30.0.2481.0"})
        self.assertEqual(v, "REFUSE")
        self.assertIn("problem-43", why)

    def test_post_bound_equal_accepts(self):
        self.assertEqual(d(phase="post", appl0002=BOUND)[0], "ACCEPT")

    def test_post_no_package_requires_problem_28(self):
        self.assertEqual(d(phase="post", staged=[], record=None, appl0002=FREE)[0], "ACCEPT")
        self.assertEqual(d(phase="post", staged=[], record=None, appl0002=BOUND)[0], "REFUSE")

    def test_experiment_id_match_accepts(self):
        self.assertEqual(d(mode="experiment", record=CAND, experiment_id="EXP2720")[0], "ACCEPT")

    def test_experiment_id_mismatch_or_missing_refuses(self):
        self.assertEqual(d(mode="experiment", record=CAND, experiment_id="EXP2721")[0], "REFUSE")
        self.assertEqual(d(mode="experiment", record=CAND, experiment_id=None)[0], "REFUSE")
        self.assertEqual(d(mode="experiment", record=REC, experiment_id="EXP2720")[0], "REFUSE")

    def test_experiment_with_actual_refuses(self):
        self.assertEqual(d(mode="experiment", record=CAND, experiment_id="EXP2720",
                           experiment_has_actual=True)[0], "REFUSE")

    def test_standing_launch_refuses_candidate_record(self):
        self.assertEqual(d(record=CAND)[0], "REFUSE")


class Cli(unittest.TestCase):
    def run_cli(self, tmp, *args):
        r = subprocess.run([sys.executable, str(ROOT / "tools/agx_pkg_guard.py"), *args],
                           capture_output=True, text=True)
        return r.returncode, r.stdout

    def test_proxy_start_uses_last_known(self):
        with tempfile.TemporaryDirectory() as t:
            t = Path(t)
            lk, rec = t / "lk.json", t / "standing.json"
            rec.write_text(json.dumps({"accepted_gpu_package": REC}))
            rc, out = self.run_cli(t, "--mode", "standing", "--phase", "pre", "--pair", PAIR,
                                   "--last-known", str(lk), "--record", str(rec))
            self.assertEqual(rc, 3)
            self.assertIn("no-live-read", out)
            lk.write_text(json.dumps({"staged": [PKG], "appl0002": BOUND}))
            rc, out = self.run_cli(t, "--mode", "standing", "--phase", "pre", "--pair", PAIR,
                                   "--last-known", str(lk), "--record", str(rec))
            self.assertEqual(rc, 0, out)
            self.assertIn("source=last-known", out)

    def test_live_read_updates_last_known(self):
        with tempfile.TemporaryDirectory() as t:
            t = Path(t)
            lk, rd = t / "lk.json", t / "read.json"
            rd.write_text(json.dumps({"staged": [], "appl0002": FREE}))
            rc, out = self.run_cli(t, "--mode", "standing", "--phase", "post", "--pair", PAIR,
                                   "--read", str(rd), "--last-known", str(lk))
            self.assertEqual(rc, 0, out)
            self.assertEqual(json.loads(lk.read_text())["staged"], [])

    def test_ledger_actual_detection(self):
        self.assertTrue(g.has_actual("x\nEXP2720 ACTUAL 2026: done\n", "EXP2720"))
        self.assertFalse(g.has_actual("EXP2720 HARDWARE BEFORE 2026\nEXP27200 ACTUAL\n", "EXP2720"))


if __name__ == "__main__":
    unittest.main()
