"""Host-only recovery factory preserves exact originals and fails on conflicts."""
import importlib.util
import json
from pathlib import Path
import shlex
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location("agx_install_visible_recovery", ROOT / "scripts/agx-install-visible-recovery.py")
FACTORY = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(FACTORY)


class VisibleRecoveryInstall(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory(prefix="visible recovery's ")
        self.addCleanup(self.tmp.cleanup)
        self.root = Path(self.tmp.name)
        self.worktree = self.root / "worktree's space"
        (self.worktree / "scripts").mkdir(parents=True)
        (self.worktree / "scripts/agx-recover-visible.sh").write_text("#!/bin/bash\nexit 0\n")
        (self.worktree / "scripts/agx-recovery-disable-service.ps1").write_text("# fixture only\n")
        (self.worktree / "scripts/agx-select-exact-launcher.py").write_text("# fixture selector only\n")
        self.experiment = self.root / "EXP9999-new candidate's"
        self.rec = self.experiment / "evidence/rec9999h"
        self.rec.mkdir(parents=True)
        self.script = self.rec / "recover.sh"
        self.legacy = b'''#!/bin/bash
E=/experiments; R=$E/EXP9999-new candidate's/evidence/rec9999h
waitcycle; nohup $R/launch-hidden.sh > $R/launch.log 2>&1 & sshup
# C:\\Users\\pavel\\EXP9999\\cleanup9999\\cleanup-hidden.ps1
# HIDDEN_CLEANUP_UNVERIFIED_STOP_BEFORE_NORMAL
# C:\\Users\\pavel\\EXP9999\\cleanup9999\\durable-transition.ps1 -Mode Preflight
# RECOVERY_DONE
'''
        self.script.write_bytes(self.legacy)
        self.script.chmod(0o755)
        self.backup = self.rec / "recover.sh.legacy-hidden"
        self.manifest = self.rec / "visible-recovery-source.json"

    def install(self):
        return FACTORY.install(self.experiment, "9999", self.worktree)

    def test_preserves_original_quotes_exec_operands_and_records_all_hashes(self):
        result = self.install()
        self.assertEqual(self.backup.read_bytes(), self.legacy)
        command = self.script.read_text().splitlines()[-1]
        self.assertEqual(shlex.split(command), ["exec", "/bin/bash", "--",
            str((self.worktree / "scripts/agx-recover-visible.sh").resolve()), str(self.experiment.resolve()), "9999"])
        self.assertEqual(json.loads(self.manifest.read_text()), result)
        for artifact in result["Files"].values():
            self.assertEqual(FACTORY.digest(Path(artifact["Path"]).read_bytes()), artifact["Sha256"])

    def test_new_manifest_pins_exact_launcher_dependency(self):
        result = self.install()
        self.assertEqual(result["Version"], 2)
        record = result["Files"]["ExactLauncherSelector"]
        path = self.worktree / "scripts/agx-select-exact-launcher.py"
        self.assertEqual(record, {"Path": str(path.resolve()), "Sha256": FACTORY.digest(path.read_bytes())})
        before = self.manifest.read_bytes()
        path.write_text("# changed selector\n")
        with self.assertRaises(FACTORY.RecoveryInstallError): self.install()
        self.assertEqual(self.manifest.read_bytes(), before)

    def test_historical_v1_template_remains_readonly_compatible(self):
        self.install()
        source = json.loads(self.manifest.read_text())
        source["Version"] = 1
        source["Files"].pop("ExactLauncherSelector", None)
        self.manifest.write_text(json.dumps(source))
        before = self.manifest.read_bytes()
        self.assertEqual(FACTORY.legacy_template(self.experiment, "9999", self.worktree), self.backup.resolve())
        self.assertEqual(self.manifest.read_bytes(), before)
        with self.assertRaises(FACTORY.RecoveryInstallError): self.install()

    def test_idempotent_same_input_preserves_all_artifact_bytes_and_mtimes(self):
        first = self.install()
        paths = [self.script, self.backup, self.manifest]
        before = [(p.read_bytes(), p.stat().st_mtime_ns) for p in paths]
        self.assertEqual(self.install(), first)
        self.assertEqual(before, [(p.read_bytes(), p.stat().st_mtime_ns) for p in paths])

    def test_unexpected_script_refuses_without_creating_backup_or_manifest(self):
        self.script.write_text("#!/bin/bash\necho unrelated\n")
        with self.assertRaises(FACTORY.RecoveryInstallError): self.install()
        self.assertFalse(self.backup.exists()); self.assertFalse(self.manifest.exists())
        self.assertIn("unrelated", self.script.read_text())

    def test_different_existing_backup_refuses_without_overwriting_anything(self):
        self.backup.write_bytes(self.legacy + b"# another snapshot\n")
        before = self.script.read_bytes(), self.backup.read_bytes()
        with self.assertRaises(FACTORY.RecoveryInstallError): self.install()
        self.assertEqual(before, (self.script.read_bytes(), self.backup.read_bytes()))
        self.assertFalse(self.manifest.exists())

    def test_existing_manifest_and_changed_source_each_refuse_overwrite(self):
        self.manifest.write_text('{"unexpected":true}\n')
        with self.assertRaises(FACTORY.RecoveryInstallError): self.install()
        self.assertEqual(self.script.read_bytes(), self.legacy); self.assertFalse(self.backup.exists())
        self.manifest.unlink(); self.install()
        before = [(p.read_bytes(), p.stat().st_mtime_ns) for p in [self.script, self.backup, self.manifest]]
        (self.worktree / "scripts/agx-recover-visible.sh").write_text("#!/bin/bash\nexit 9\n")
        with self.assertRaises(FACTORY.RecoveryInstallError): self.install()
        self.assertEqual(before, [(p.read_bytes(), p.stat().st_mtime_ns) for p in [self.script, self.backup, self.manifest]])

    def test_matching_backup_and_interrupted_manifest_install_preserve_original(self):
        self.backup.write_bytes(self.legacy)
        backup_before = self.backup.read_bytes(), self.backup.stat().st_mtime_ns
        self.install()
        self.assertEqual(backup_before, (self.backup.read_bytes(), self.backup.stat().st_mtime_ns))
        self.manifest.unlink()  # interruption after wrapper, before manifest publication
        wrapper_before = self.script.read_bytes(), self.script.stat().st_mtime_ns
        self.install()
        self.assertEqual(wrapper_before, (self.script.read_bytes(), self.script.stat().st_mtime_ns))
        self.assertEqual(backup_before, (self.backup.read_bytes(), self.backup.stat().st_mtime_ns))
        self.assertTrue(self.manifest.is_file())

    def test_tampered_manifest_or_wrapper_refuses_without_rewriting_receipts(self):
        self.install()
        good_manifest = self.manifest.read_bytes()
        self.manifest.write_bytes(good_manifest + b" ")
        wrapper_before = self.script.read_bytes(), self.script.stat().st_mtime_ns
        with self.assertRaises(FACTORY.RecoveryInstallError): self.install()
        self.assertEqual(self.manifest.read_bytes(), good_manifest + b" ")
        self.assertEqual(wrapper_before, (self.script.read_bytes(), self.script.stat().st_mtime_ns))
        self.manifest.write_bytes(good_manifest)
        self.script.write_text("#!/bin/bash\necho changed-wrapper\n")
        with self.assertRaises(FACTORY.RecoveryInstallError): self.install()
        self.assertEqual(self.manifest.read_bytes(), good_manifest)
        self.assertEqual(self.backup.read_bytes(), self.legacy)

    def test_legacy_template_only_selects_original_without_writes(self):
        before = [(p.relative_to(self.rec), p.read_bytes(), p.stat().st_mtime_ns)
                  for p in self.rec.iterdir()]
        selected = FACTORY.legacy_template(self.experiment, "9999", self.worktree)
        self.assertEqual(selected, self.script.resolve())
        self.assertEqual(before, [(p.relative_to(self.rec), p.read_bytes(), p.stat().st_mtime_ns)
                                 for p in self.rec.iterdir()])

    def test_legacy_template_only_selects_visible_backup_and_validates_hashes(self):
        self.install()
        before = [(p, p.read_bytes(), p.stat().st_mtime_ns) for p in self.rec.iterdir()]
        self.assertEqual(FACTORY.legacy_template(self.experiment, "9999", self.worktree), self.backup.resolve())
        self.assertEqual(before, [(p, p.read_bytes(), p.stat().st_mtime_ns) for p in self.rec.iterdir()])
        # Remains structurally plausible: only immutable source hash detects it.
        self.backup.write_bytes(self.legacy + b"# tampered after installation\n")
        with self.assertRaises(FACTORY.RecoveryInstallError):
            FACTORY.legacy_template(self.experiment, "9999", self.worktree)

    def test_legacy_template_identity_refusal_and_historical_source_provenance(self):
        self.install()
        with self.assertRaises(FACTORY.RecoveryInstallError):
            FACTORY.legacy_template(self.experiment, "9998", self.worktree)
        self.backup.write_bytes(self.legacy.replace(b"cleanup9999", b"cleanup9998"))
        with self.assertRaises(FACTORY.RecoveryInstallError):
            FACTORY.legacy_template(self.experiment, "9999", self.worktree)
        self.backup.unlink(); target = self.rec / "outside.sh"; target.write_bytes(self.legacy)
        self.backup.symlink_to(target)
        with self.assertRaises(FACTORY.RecoveryInstallError):
            FACTORY.legacy_template(self.experiment, "9999", self.worktree)
        self.backup.unlink(); self.backup.write_bytes(self.legacy)
        before_manifest = self.manifest.read_bytes()
        (self.worktree / "scripts/agx-recovery-disable-service.ps1").write_text("# changed source\n")
        (self.worktree / "scripts/agx-recover-visible.sh").write_text("#!/bin/bash\n# new workflow revision\n")
        self.assertEqual(FACTORY.legacy_template(self.experiment, "9999", self.worktree), self.backup.resolve())
        self.assertEqual(self.manifest.read_bytes(), before_manifest)
        # Historical selection is safe; same-run source-manifest replacement
        # still constitutes an overwrite and must remain refused.
        with self.assertRaises(FACTORY.RecoveryInstallError): self.install()

    def test_legacy_template_cli_prints_only_path_and_never_writes(self):
        self.install()
        before = [(p, p.read_bytes(), p.stat().st_mtime_ns) for p in self.rec.iterdir()]
        run = subprocess.run([sys.executable, str(ROOT / "scripts/agx-install-visible-recovery.py"),
            str(self.experiment), "9999", "--worktree", str(self.worktree), "--legacy-template-only"],
            capture_output=True, text=True)
        self.assertEqual(run.returncode, 0, run.stderr)
        self.assertEqual(run.stdout, str(self.backup.resolve()) + "\n")
        self.assertEqual(before, [(p, p.read_bytes(), p.stat().st_mtime_ns) for p in self.rec.iterdir()])

    def test_other_experiment_or_symlinked_original_refuses(self):
        with self.assertRaises(FACTORY.RecoveryInstallError): FACTORY.install(self.experiment, "9998", self.worktree)
        self.script.write_bytes(self.legacy.replace(b"cleanup9999", b"cleanup9998"))
        with self.assertRaises(FACTORY.RecoveryInstallError): self.install()
        target = self.rec / "other.sh"; target.write_bytes(self.legacy)
        self.script.unlink(); self.script.symlink_to(target)
        with self.assertRaises(FACTORY.RecoveryInstallError): self.install()
        self.assertEqual(target.read_bytes(), self.legacy)
