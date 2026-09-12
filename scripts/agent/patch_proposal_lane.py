#!/usr/bin/env python3
"""Fail-closed application of a narrowly contracted, untrusted unified diff.

This is a policy validator, not an operating-system sandbox.  It intentionally
never interprets proposal text as a shell command and has one fixed check only.
"""
import argparse
import json
import os
import re
import stat
import subprocess
import sys
from pathlib import Path, PurePosixPath


CONTRACT_KEYS = {"task_id", "input_commit", "repo", "worktree", "branch",
                 "allowed_paths", "forbidden_paths", "max_files", "max_diff_lines",
                 "source_allowed", "hardware_allowed", "allowed_command_ids", "expected_text"}
PROPOSAL_KEYS = {"result", "rationale", "patch", "requested_command_ids", "unresolved",
                 "architecture_question"}
RESULTS = {"PATCH_PROPOSED", "QUESTION", "CANNOT_SOLVE"}
RUNNER_MARKER = "runner-marker\n"


class Reject(Exception):
    def __init__(self, reason):
        self.reason = reason


def git(repo, *args, input_text=None):
    return subprocess.run(["git", "-C", str(repo), *args], input=input_text, text=True,
                          stdout=subprocess.PIPE, stderr=subprocess.PIPE)


def fail(reason):
    raise Reject(reason)


def load_object(path, label):
    if not path.is_file() or path.is_symlink():
        fail(f"{label}_missing")
    try:
        value = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, UnicodeError, json.JSONDecodeError):
        fail(f"{label}_not_json")
    if not isinstance(value, dict):
        fail(f"{label}_schema_invalid")
    return value


def is_string_list(value):
    return isinstance(value, list) and all(isinstance(item, str) for item in value)


def schema(contract, proposal):
    if set(contract) != CONTRACT_KEYS:
        fail("contract_schema_invalid")
    if not all(isinstance(contract[key], str) for key in
               ("task_id", "input_commit", "repo", "worktree", "branch", "expected_text")):
        fail("contract_schema_invalid")
    if (not is_string_list(contract["allowed_paths"]) or not is_string_list(contract["forbidden_paths"])
            or not is_string_list(contract["allowed_command_ids"])
            or not all(isinstance(contract[key], int) and not isinstance(contract[key], bool) and contract[key] > 0
                       for key in ("max_files", "max_diff_lines"))
            or not isinstance(contract["source_allowed"], bool)
            or not isinstance(contract["hardware_allowed"], bool)):
        fail("contract_schema_invalid")
    if (not re.fullmatch(r"[0-9a-f]{40}", contract["input_commit"])
            or not contract["task_id"] or not contract["branch"]
            or not contract["allowed_paths"]
            or not all(safe_path(rule[:-1] if rule.endswith("/") else rule) for rule in
                       contract["allowed_paths"] + contract["forbidden_paths"])):
        fail("contract_schema_invalid")
    if set(proposal) != PROPOSAL_KEYS or proposal.get("result") not in RESULTS:
        fail("proposal_schema_invalid")
    if (not all(isinstance(proposal[key], str) for key in ("rationale", "patch"))
            or not is_string_list(proposal["requested_command_ids"])
            or not is_string_list(proposal["unresolved"])
            or proposal["architecture_question"] is not None and not isinstance(proposal["architecture_question"], str)):
        fail("proposal_schema_invalid")
    if contract["hardware_allowed"]:
        fail("hardware_not_supported")


def no_symlink_ancestors(path):
    if path.is_symlink():
        return False
    path = path.resolve(strict=False)
    current = Path(path.anchor)
    for part in path.parts[1:]:
        current /= part
        try:
            if stat.S_ISLNK(os.lstat(current).st_mode):
                return False
        except FileNotFoundError:
            continue
        except OSError:
            return False
    return True


def safe_path(raw):
    if not raw or "\\" in raw or any(ord(char) < 32 or ord(char) == 127 for char in raw):
        return False
    path = PurePosixPath(raw)
    return not path.is_absolute() and all(part not in {"", ".", "..", ".git"} for part in path.parts)


def under(path, rules):
    return any(path == rule or rule.endswith("/") and path.startswith(rule) for rule in rules)


def safe_target(repo, target):
    current = repo
    for part in PurePosixPath(target).parts:
        current /= part
        if current.exists() and current.is_symlink():
            fail("patch_symlink_forbidden")
        if git(repo, "ls-files", "-s", "--", str(current.relative_to(repo))).stdout.startswith("160000 "):
            fail("patch_gitlink_forbidden")


def preflight(contract, output):
    repo, worktree = Path(contract["repo"]), Path(contract["worktree"])
    if not repo.is_absolute() or not worktree.is_absolute() or repo != worktree:
        fail("worktree_invalid")
    if not repo.is_dir() or repo.is_symlink() or not no_symlink_ancestors(repo):
        fail("worktree_unsafe")
    if not output.is_absolute() or output.exists() or not no_symlink_ancestors(output):
        fail("output_path_unsafe")
    try:
        repo.relative_to(output)
        fail("output_inside_repo")
    except ValueError:
        pass
    if output.is_relative_to(repo):
        fail("output_inside_repo")
    if git(repo, "rev-parse", "--is-inside-work-tree").stdout.strip() != "true":
        fail("repo_invalid")
    if git(repo, "rev-parse", "--verify", f"{contract['input_commit']}^{{commit}}").returncode:
        fail("input_commit_invalid")
    if git(repo, "rev-parse", "HEAD").stdout.strip() != contract["input_commit"]:
        fail("worktree_head_mismatch")
    if git(repo, "branch", "--show-current").stdout.strip() != contract["branch"]:
        fail("worktree_branch_mismatch")
    entries = git(repo, "worktree", "list", "--porcelain").stdout.split("\n\n")
    matching = [entry for entry in entries if f"worktree {repo.resolve()}\n" in entry and f"branch refs/heads/{contract['branch']}" in entry]
    if len(matching) != 1:
        fail("worktree_not_linked_contract")
    if git(repo, "status", "--porcelain", "--untracked-files=all", "--ignored").stdout:
        fail("worktree_not_clean")
    return repo


def patch_path(token, prefix, *, null_allowed=False):
    if token == "/dev/null":
        if null_allowed:
            return None
        fail("patch_deletion_forbidden")
    if not token.startswith(prefix):
        fail("patch_path_invalid")
    value = token[len(prefix):]
    if not safe_path(value):
        fail("patch_path_invalid")
    return value


def parse_patch(text, contract, repo):
    if not text or "GIT binary patch" in text or "Binary files " in text:
        fail("patch_binary_forbidden" if "binary" in text.lower() else "patch_malformed")
    lines = text.splitlines(keepends=True)
    i, files, changed_lines = 0, [], 0
    while i < len(lines):
        line = lines[i]
        if not line.startswith("diff --git a/"):
            fail("patch_malformed")
        fields = line.rstrip("\n").split(" ")
        if len(fields) != 4:
            fail("patch_malformed")
        old = patch_path(fields[2], "a/")
        new = patch_path(fields[3], "b/")
        if old != new:
            fail("patch_rename_forbidden")
        i += 1
        new_file = False
        if i < len(lines) and lines[i].startswith("new file mode "):
            if lines[i] != "new file mode 100644\n":
                fail("patch_mode_forbidden")
            new_file = True
            i += 1
        if i < len(lines) and lines[i].startswith("index "):
            index = lines[i].rstrip("\n")
            if ".." not in index or not (index.endswith(" 100644") or new_file and " " not in index[6:]):
                fail("patch_mode_forbidden")
            i += 1
        if i >= len(lines):
            fail("patch_malformed")
        if lines[i].startswith(("old mode ", "new mode ", "new file mode ", "deleted file mode ", "similarity index ", "rename ", "copy ")):
            fail("patch_deletion_forbidden" if lines[i].startswith("deleted") else "patch_mode_forbidden")
        if not lines[i].startswith("--- "):
            fail("patch_malformed")
        header_old = patch_path(lines[i][4:].rstrip("\n"), "a/", null_allowed=True)
        i += 1
        if i >= len(lines) or not lines[i].startswith("+++ "):
            fail("patch_malformed")
        header_new_token = lines[i][4:].rstrip("\n")
        header_new = patch_path(header_new_token, "b/")
        if header_old is None:
            if not new_file or header_new is None or new != header_new:
                fail("patch_malformed")
            target = header_new
        else:
            if new_file or header_new is None or header_old != header_new or old != header_old:
                fail("patch_malformed")
            target = old
        if not under(target, contract["allowed_paths"]) or under(target, contract["forbidden_paths"]):
            fail("patch_path_forbidden")
        safe_target(repo, target)
        hunk_seen = False
        while i + 1 < len(lines) and lines[i + 1].startswith("@@ "):
            i += 1
            if not re.fullmatch(r"@@ -\d+(?:,\d+)? \+\d+(?:,\d+)? @@\n?", lines[i]):
                fail("patch_malformed")
            hunk_seen = True
            i += 1
            while i < len(lines) and not lines[i].startswith(("diff --git ", "@@ ")):
                if lines[i].startswith("\\") or not lines[i].startswith((" ", "+", "-")):
                    fail("patch_malformed")
                if lines[i][0] in "+-":
                    changed_lines += 1
                i += 1
        if not hunk_seen:
            fail("patch_malformed")
        files.append(target)
    if len(set(files)) != len(files) or len(files) > contract["max_files"]:
        fail("patch_file_limit")
    if changed_lines > contract["max_diff_lines"]:
        fail("patch_line_limit")
    return sorted(files)


def run_fixed_check(repo, command_id):
    if command_id != "RUN_HARMLESS_CHECK":
        fail("command_id_invalid")
    helper = Path(__file__).with_name("harmless_patch_check.py")
    result = subprocess.run([sys.executable, str(helper), "--repo", str(repo)], text=True,
                            stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    if result.returncode:
        fail("fixed_check_failed")


def changed_paths(repo):
    tracked = git(repo, "diff", "--name-only", "HEAD", "--").stdout.splitlines()
    untracked = git(repo, "ls-files", "--others", "--exclude-standard").stdout.splitlines()
    ignored = git(repo, "ls-files", "--others", "--ignored", "--exclude-standard").stdout.splitlines()
    return sorted(set(tracked + untracked + ignored))


def write_output(output, proposal, payload):
    output.mkdir(parents=True, exist_ok=False)
    (output / "proposal.json").write_text(json.dumps(proposal, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    (output / "summary.json").write_text(json.dumps(payload, indent=2, sort_keys=True) + "\n", encoding="utf-8")


def main(argv=None):
    parser = argparse.ArgumentParser()
    parser.add_argument("--contract", required=True, type=Path)
    parser.add_argument("--proposal", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    args = parser.parse_args(argv)
    output = args.output
    proposal = None
    try:
        contract = load_object(args.contract, "contract")
        proposal = load_object(args.proposal, "proposal")
        schema(contract, proposal)
        repo = preflight(contract, output)
        if proposal["result"] != "PATCH_PROPOSED":
            payload = {"result": proposal["result"], "reason": "no_patch_applied", "automatic_merge": False,
                       "hardware_used": False, "commands_run": []}
            write_output(output, proposal, payload)
            print(json.dumps(payload, sort_keys=True))
            return 0
        if not contract["source_allowed"]:
            fail("source_not_allowed")
        for command_id in proposal["requested_command_ids"]:
            if command_id not in contract["allowed_command_ids"]:
                fail("command_id_unapproved")
            if command_id != "RUN_HARMLESS_CHECK":
                fail("command_id_invalid")
        expected = parse_patch(proposal["patch"], contract, repo)
        check = git(repo, "apply", "--check", "--whitespace=error-all", "-", input_text=proposal["patch"])
        if check.returncode:
            fail("patch_apply_check_failed")
        applied = git(repo, "apply", "--whitespace=error-all", "-", input_text=proposal["patch"])
        if applied.returncode:
            fail("patch_apply_failed")
        actual = changed_paths(repo)
        if actual != expected:
            fail("post_apply_scope_mismatch")
        for command_id in proposal["requested_command_ids"]:
            run_fixed_check(repo, command_id)
        payload = {"result": "APPLIED", "reason": "none", "automatic_merge": False, "hardware_used": False,
                   "commands_run": proposal["requested_command_ids"], "changed_paths": actual}
        write_output(output, proposal, payload)
        print(json.dumps(payload, sort_keys=True))
        return 0
    except Reject as error:
        payload = {"result": "REJECTED", "reason": error.reason, "automatic_merge": False, "hardware_used": False}
        try:
            if not output.exists() and output.is_absolute() and no_symlink_ancestors(output):
                write_output(output, proposal if proposal is not None else {}, payload)
        except OSError:
            pass
        print(json.dumps(payload, sort_keys=True))
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
