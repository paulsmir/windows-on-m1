import json
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
LANE = ROOT / "scripts" / "agent" / "patch_proposal_lane.py"


def run(*args, cwd=None, check=True):
    return subprocess.run(args, cwd=cwd, check=check, text=True,
                          stdout=subprocess.PIPE, stderr=subprocess.PIPE)


class PatchProposalLaneTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.temp_path = Path(self.temp.name).resolve()
        self.main = self.temp_path / "main"
        self.worktree = self.temp_path / "worker"
        self.main.mkdir()
        run("git", "init", "-q", cwd=self.main)
        run("git", "config", "user.email", "test@example.invalid", cwd=self.main)
        run("git", "config", "user.name", "Test", cwd=self.main)
        (self.main / "allowed").mkdir()
        (self.main / "investigation" / "agent_tasks").mkdir(parents=True)
        (self.main / "allowed" / "demo.txt").write_text("before\n")
        (self.main / "allowed" / "linked.txt").symlink_to("demo.txt")
        (self.main / "forbidden.txt").write_text("base\n")
        run("git", "add", ".", cwd=self.main)
        run("git", "commit", "-qm", "base", cwd=self.main)
        initial = run("git", "rev-parse", "HEAD", cwd=self.main).stdout.strip()
        run("git", "update-index", "--add", "--cacheinfo", "160000," + initial + ",allowed/link", cwd=self.main)
        run("git", "commit", "-qm", "add gitlink fixture", cwd=self.main)
        self.base = run("git", "rev-parse", "HEAD", cwd=self.main).stdout.strip()
        run("git", "worktree", "add", "-q", "-b", "agent/patch-test", str(self.worktree), self.base,
            cwd=self.main)
        self.output = self.temp_path / "evidence"
        self.output_number = 0

    def tearDown(self):
        run("git", "worktree", "remove", "--force", str(self.worktree), cwd=self.main, check=False)
        self.temp.cleanup()

    def contract(self, **overrides):
        value = {
            "task_id": "PATCH-001", "input_commit": self.base,
            "repo": str(self.worktree), "worktree": str(self.worktree),
            "branch": "agent/patch-test", "allowed_paths": ["allowed/"],
            "forbidden_paths": ["forbidden.txt"], "max_files": 1,
            "max_diff_lines": 4, "source_allowed": True, "hardware_allowed": False,
            "allowed_command_ids": ["RUN_HARMLESS_CHECK"], "expected_text": "after\n",
        }
        value.update(overrides)
        path = self.temp_path / "contract.json"
        path.write_text(json.dumps(value))
        return path

    def proposal(self, patch=None, **overrides):
        value = {
            "result": "PATCH_PROPOSED", "rationale": "replace demo text",
            "patch": patch if patch is not None else self.good_patch(),
            "requested_command_ids": [], "unresolved": [], "architecture_question": None,
        }
        value.update(overrides)
        path = self.temp_path / "proposal.json"
        path.write_text(json.dumps(value))
        return path

    @staticmethod
    def good_patch():
        return """diff --git a/allowed/demo.txt b/allowed/demo.txt\n--- a/allowed/demo.txt\n+++ b/allowed/demo.txt\n@@ -1 +1 @@\n-before\n+after\n"""

    @staticmethod
    def demo_patch():
        return """diff --git a/investigation/agent_tasks/demo.txt b/investigation/agent_tasks/demo.txt\nnew file mode 100644\nindex 0000000..d0d0d0d\n--- /dev/null\n+++ b/investigation/agent_tasks/demo.txt\n@@ -0,0 +1 @@\n+runner-marker\n"""

    def invoke(self, contract=None, proposal=None, output=None):
        if output is None:
            self.output_number += 1
            output = self.output if self.output_number == 1 else self.temp_path / f"evidence-{self.output_number}"
        return run(sys.executable, str(LANE), "--contract", str(contract or self.contract()),
                   "--proposal", str(proposal or self.proposal()), "--output", str(output or self.output),
                   check=False)

    def payload(self, result):
        self.assertTrue(result.stdout, result.stderr)
        return json.loads(result.stdout)

    def rejected(self, result, reason=None):
        payload = self.payload(result)
        self.assertNotEqual(result.returncode, 0)
        self.assertEqual(payload["result"], "REJECTED")
        if reason:
            self.assertEqual(payload["reason"], reason)
        return payload

    def test_applies_valid_patch_and_runs_only_fixed_allowed_check(self):
        main_status = run("git", "status", "--porcelain", cwd=self.main).stdout
        result = self.invoke(
            contract=self.contract(allowed_paths=["investigation/agent_tasks/demo.txt"], forbidden_paths=[]),
            proposal=self.proposal(self.demo_patch(), requested_command_ids=["RUN_HARMLESS_CHECK"]))
        payload = self.payload(result)
        self.assertEqual(result.returncode, 0, result.stderr + repr(payload))
        self.assertEqual(payload["result"], "APPLIED")
        self.assertEqual((self.worktree / "investigation" / "agent_tasks" / "demo.txt").read_text(), "runner-marker\n")
        self.assertEqual(payload["commands_run"], ["RUN_HARMLESS_CHECK"])
        self.assertEqual(run("git", "rev-parse", "HEAD", cwd=self.main).stdout.strip(), self.base)
        self.assertEqual(run("git", "status", "--porcelain", cwd=self.main).stdout, main_status)
        self.assertTrue((self.output / "proposal.json").is_file())

    def test_rejects_arbitrary_shell_as_command_id(self):
        self.rejected(self.invoke(proposal=self.proposal(requested_command_ids=["sh -c touch pwned"])), "command_id_unapproved")
        self.assertFalse((self.worktree / "pwned").exists())

    def test_rejects_missing_extra_and_wrong_typed_schema_fields(self):
        value = json.loads(self.proposal().read_text())
        del value["rationale"]
        path = self.temp_path / "bad.json"; path.write_text(json.dumps(value))
        self.rejected(self.invoke(proposal=path), "proposal_schema_invalid")
        value = json.loads(self.proposal().read_text()); value["extra"] = True
        path.write_text(json.dumps(value)); self.rejected(self.invoke(proposal=path), "proposal_schema_invalid")
        value = json.loads(self.proposal().read_text()); value["unresolved"] = "no"
        path.write_text(json.dumps(value)); self.rejected(self.invoke(proposal=path), "proposal_schema_invalid")

    def test_rejects_read_only_task_disallowed_path_binary_git_and_gitlink(self):
        self.rejected(self.invoke(contract=self.contract(source_allowed=False)), "source_not_allowed")
        bad = self.good_patch().replace("allowed/demo.txt", "forbidden.txt")
        self.rejected(self.invoke(proposal=self.proposal(bad)), "patch_path_forbidden")
        binary = "diff --git a/allowed/demo.txt b/allowed/demo.txt\nGIT binary patch\n"
        self.rejected(self.invoke(proposal=self.proposal(binary)), "patch_binary_forbidden")
        dotgit = self.good_patch().replace("allowed/demo.txt", ".git/config")
        self.rejected(self.invoke(proposal=self.proposal(dotgit)), "patch_path_invalid")
        gitlink = self.good_patch().replace("demo.txt", "link")
        self.rejected(self.invoke(proposal=self.proposal(gitlink)), "patch_gitlink_forbidden")

    def test_rejects_symlink_target(self):
        symlink = self.good_patch().replace("demo.txt", "linked.txt")
        self.rejected(self.invoke(proposal=self.proposal(symlink)), "patch_symlink_forbidden")

    def test_rejects_deletion_malformed_traversal_and_caps(self):
        deletion = "diff --git a/allowed/demo.txt b/allowed/demo.txt\ndeleted file mode 100644\n--- a/allowed/demo.txt\n+++ /dev/null\n@@ -1 +0,0 @@\n-before\n"
        self.rejected(self.invoke(proposal=self.proposal(deletion)), "patch_deletion_forbidden")
        self.rejected(self.invoke(proposal=self.proposal("not a diff\n")), "patch_malformed")
        traversal = self.good_patch().replace("allowed/demo.txt", "allowed/../forbidden.txt")
        self.rejected(self.invoke(proposal=self.proposal(traversal)), "patch_path_invalid")
        self.rejected(self.invoke(proposal=self.proposal(self.good_patch().replace("allowed/demo.txt", "allowed//demo.txt"))), "patch_path_invalid")
        self.rejected(self.invoke(proposal=self.proposal(self.good_patch().replace("allowed/demo.txt", "allowed/./demo.txt"))), "patch_path_invalid")
        self.rejected(self.invoke(proposal=self.proposal(self.good_patch().replace("allowed/demo.txt", ".Git/config"))), "patch_path_invalid")
        self.rejected(self.invoke(contract=self.contract(max_diff_lines=1)), "patch_line_limit")
        second = self.good_patch() + self.good_patch().replace("demo.txt", "other.txt")
        self.rejected(self.invoke(contract=self.contract(max_files=1, max_diff_lines=10), proposal=self.proposal(second)), "patch_file_limit")

    def test_rejects_wrong_head_dirty_preflight_and_symlinked_output(self):
        self.rejected(self.invoke(contract=self.contract(input_commit="0" * 40)), "input_commit_invalid")
        (self.worktree / "allowed" / "demo.txt").write_text("dirty\n")
        self.rejected(self.invoke(), "worktree_not_clean")
        (self.worktree / "allowed" / "demo.txt").write_text("before\n")
        link = self.temp_path / "linked-output"
        link.symlink_to(self.temp_path / "real-output")
        self.rejected(self.invoke(output=link), "output_path_unsafe")

    def test_rejects_symlink_ancestor_and_primary_checkout_contract(self):
        wrapper = self.temp_path / "wrapper"
        wrapper.symlink_to(self.temp_path, target_is_directory=True)
        self.rejected(self.invoke(contract=self.contract(repo=str(wrapper / "worker"), worktree=str(wrapper / "worker"))), "worktree_unsafe")
        main_branch = run("git", "branch", "--show-current", cwd=self.main).stdout.strip()
        self.rejected(self.invoke(contract=self.contract(repo=str(self.main), worktree=str(self.main), branch=main_branch)), "worktree_primary_checkout")

    def test_question_never_applies_or_merges(self):
        proposal = self.proposal(result="QUESTION", patch="", architecture_question="Need design", unresolved=["missing fact"])
        result = self.invoke(proposal=proposal)
        payload = self.payload(result)
        self.assertEqual(result.returncode, 0)
        self.assertEqual(payload["result"], "QUESTION")
        self.assertEqual((self.worktree / "allowed" / "demo.txt").read_text(), "before\n")
        self.assertEqual(run("git", "rev-parse", "HEAD", cwd=self.main).stdout.strip(), self.base)


if __name__ == "__main__":
    unittest.main()
