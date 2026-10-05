"""Verify and decode saved EXP859 evidence without connecting to hardware."""
import argparse
import base64
import hashlib
import json
from pathlib import Path
import re
import struct
import subprocess

ROOT = Path(__file__).resolve().parents[3]


def digest(path):
    with path.open("rb") as f:
        return hashlib.file_digest(f, "sha256").hexdigest()


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("experiment", type=Path)
    parser.add_argument("--suite-log", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    saved = args.experiment / "hardware-evidence"
    hashes = {}
    for item in json.loads((saved / "artifact-hashes.json").read_text()):
        path = saved / item["Name"]
        assert path.stat().st_size == item["Bytes"], path
        hashes[item["Name"]] = digest(path)
        assert hashes[item["Name"]] == item["SHA256"].lower(), path
    state = json.loads((saved / "state.json").read_text())
    receipts = state["Receipts"]
    raw = base64.b64decode(receipts["Wom1G3CopyQueryFailure"], validate=True)
    assert len(raw) == 16
    version, size, predicate, status = struct.unpack("<4I", raw)
    assert (version, size, predicate, status) == (1, 16, 53, 0xC000000D)
    reg = (saved / "devnode.reg").read_text(encoding="utf-16")
    value = re.search(r'"Wom1G3CopyQueryFailure"=hex:([^\n]+)', reg).group(1)
    assert bytes.fromhex(value.strip().replace(",", "")) == raw
    counts = struct.unpack("<32Q", base64.b64decode(receipts["Wom1G3UnpublishedGroups"]))
    flush_values = struct.unpack("<6I8Q", base64.b64decode(receipts["Wom1G3FlushInput"]))
    fields = ("version", "bytes", "branch", "root_segment", "resolve_status",
              "broker_status", "process", "root_offset", "resolved_root_ipa",
              "graph_root_ipa", "input_start", "input_end", "flush_start", "flush_end")
    manifest = json.loads((args.experiment / "source-manifest.json").read_text())
    matched = 0
    for item in manifest["files"]:
        assert digest(ROOT / item["path"]) == item["sha256"], item["path"]
        matched += 1
    baseline = json.loads((ROOT / "investigation/evidence/EXP858-query-audit/summary.json").read_text())
    log = args.suite_log.read_text()
    failures = sorted(line for line in log.splitlines() if line.startswith(("ERROR:", "FAIL:")))
    expected = sorted(baseline["host"]["baseline_failure_error_identities"])
    assert failures == expected, {"new": sorted(set(failures)-set(expected)),
                                  "removed": sorted(set(expected)-set(failures))}
    assert re.search(r"Ran 1154 tests", log)
    assert "FAILED (failures=15, errors=38, skipped=2)" in log
    replay_logs = {}
    for profile in ("16", "64"):
        path = Path(__file__).parent / ("replay" + profile + ".log")
        text = path.read_text()
        assert "R145 local copy: PASS" in text and "Traceback" not in text
        replay_logs[profile] = {"sha256": digest(path), "observations": text.splitlines()}
    result = {
        "base_commit": "3e6744a21cdb5367f3ede01c13cc96fc6056f32a",
        "scope": "offline audit; no production fix, package, Air or builder connection",
        "verdict": "STOP_CAUSE_NOT_UNIQUELY_DETERMINED",
        "package_source_commit": manifest["repository_commit"],
        "matched_package_source_files": matched,
        "guest_original_sha256": hashes,
        "query": {"hex": raw.hex(), "sha256": hashlib.sha256(raw).hexdigest(),
                  "version": version, "bytes": size, "predicate": predicate,
                  "ntstatus": hex(status), "va_length_generations_present": False},
        "unpublished_groups": {str(i): n for i, n in enumerate(counts) if n},
        "local_unpublished_groups": counts[2],
        "flush_snapshot_not_correlated_to_query": dict(zip(fields, flush_values)),
        "ranked_candidates": ["bootstrap/parked root at pre-submit QUERY",
                              "missing parent or leaf at requested VA in selected root",
                              "request range/identity differs from populated mapping"],
        "minimum_leading_discriminator": "Graph.RootIpa == BootstrapIpa at guard53 under the same lock",
        "replays": replay_logs,
        "suite": {"command": "CC=/tmp/agx-clang-wrapper python3 -m unittest discover -s tests -v",
                  "tests": 1154, "failures": 15, "errors": 38, "skipped": 2,
                  "new_failure_error_identities": [], "log": str(args.suite_log),
                  "sha256": digest(args.suite_log)},
        "arm64": "not applicable: no production translation unit changed",
        "nested_diffs": {name: hashlib.sha256(subprocess.check_output(
            ["git", "-C", str(ROOT/name), "diff", "--binary", "HEAD"])).hexdigest()
            for name in ("m1n1_windows", "mu")},
        "audit_script_sha256": {name: digest(Path(__file__).parent/name)
                                 for name in ("analyze.py", "replay.py")},
    }
    args.output.write_text(json.dumps(result, indent=2) + "\n")
    print(result["verdict"], matched, "source files;", len(hashes), "guest hashes; baseline suite unchanged")


if __name__ == "__main__":
    main()
