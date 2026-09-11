import json
import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
CREATE = ROOT / "scripts" / "agent" / "create_task_worktree.py"
VERIFY = ROOT / "scripts" / "agent" / "verify_worker_scope.py"
COLLECT = ROOT / "scripts" / "agent" / "collect_worker_evidence.py"


def run(*args, cwd=None, check=True):
    return subprocess.run(args, cwd=cwd, check=check, text=True,
                          stdout=subprocess.PIPE, stderr=subprocess.PIPE)


class AgentOrchestrationTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.repo = Path(self.temp.name) / "repo"
        self.repo.mkdir()
        run("git", "init", "-q", cwd=self.repo)
        run("git", "config", "user.email", "test@example.invalid", cwd=self.repo)
        run("git", "config", "user.name", "Test", cwd=self.repo)
        (self.repo / "allowed").mkdir()
        (self.repo / ".gitignore").write_text(".worktrees/\n")
        (self.repo / "allowed" / "base.txt").write_text("base\n")
        run("git", "add", ".", cwd=self.repo)
        run("git", "commit", "-qm", "base", cwd=self.repo)
        self.base = run("git", "rev-parse", "HEAD", cwd=self.repo).stdout.strip()

    def tearDown(self):
        self.temp.cleanup()

    def test_create_task_worktree_uses_new_branch_without_touching_main(self):
        destination = self.repo / ".worktrees" / "worker"
        result = run("python3", str(CREATE), "--repo", str(self.repo),
                     "--task-id", "TEST-001", "--branch", "agent/test-worker",
                     "--path", str(destination), "--input-commit", self.base)
        payload = json.loads(result.stdout)
        self.assertEqual(payload["branch"], "agent/test-worker")
        self.assertEqual(Path(payload["worktree"]), destination)
        self.assertEqual(run("git", "branch", "--show-current", cwd=destination).stdout.strip(),
                         "agent/test-worker")
        self.assertEqual(run("git", "status", "--porcelain", cwd=self.repo).stdout, "")

    def test_scope_verifier_rejects_forbidden_changed_path(self):
        (self.repo / "allowed" / "base.txt").write_text("changed\n")
        (self.repo / "forbidden.txt").write_text("bad\n")
        result = run("python3", str(VERIFY), "--repo", str(self.repo),
                     "--input-commit", self.base, "--allowed-path", "allowed/",
                     check=False)
        self.assertNotEqual(result.returncode, 0)
        payload = json.loads(result.stdout)
        self.assertFalse(payload["scope_ok"])
        self.assertIn("forbidden.txt", payload["forbidden_paths"])

    def test_scope_verifier_accepts_allowed_change_and_detects_source_change(self):
        (self.repo / "allowed" / "base.txt").write_text("changed\n")
        result = run("python3", str(VERIFY), "--repo", str(self.repo),
                     "--input-commit", self.base, "--allowed-path", "allowed/")
        payload = json.loads(result.stdout)
        self.assertTrue(payload["scope_ok"])
        self.assertTrue(payload["source_changed"])
        self.assertEqual(payload["forbidden_paths"], [])

    def test_evidence_collector_preserves_raw_log_and_compact_summary(self):
        log = self.repo / "build.log"
        log.write_text("line one\nerror: first failure\nline three\n")
        output = self.repo / "evidence"
        result = run("python3", str(COLLECT), "--task-id", "TEST-002",
                     "--input-commit", self.base, "--worker", "devstral",
                     "--result", "FAIL", "--raw-log", str(log), "--output", str(output),
                     "--first-failure", "error: first failure")
        payload = json.loads(result.stdout)
        self.assertEqual(payload["result"], "FAIL")
        self.assertTrue((output / "raw" / "build.log").is_file())
        self.assertTrue((output / "summary.json").is_file())


if __name__ == "__main__":
    unittest.main()
