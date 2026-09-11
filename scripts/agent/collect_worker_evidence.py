#!/usr/bin/env python3
"""Copy raw evidence and publish a compact, machine-readable worker summary."""
import argparse
import json
import shutil
from pathlib import Path


def main():
    p = argparse.ArgumentParser()
    p.add_argument("--task-id", required=True)
    p.add_argument("--input-commit", required=True)
    p.add_argument("--worker", required=True)
    p.add_argument("--result", required=True, choices=("PASS", "FAIL", "BLOCKED", "INCONCLUSIVE"))
    p.add_argument("--raw-log", action="append", default=[], type=Path)
    p.add_argument("--output", required=True, type=Path)
    p.add_argument("--first-failure", default="NONE")
    a = p.parse_args()
    if a.output.exists():
        raise SystemExit("refuse to overwrite evidence output")
    raw = a.output / "raw"
    raw.mkdir(parents=True)
    copied = []
    for source in a.raw_log:
        if not source.is_file():
            raise SystemExit(f"raw log does not exist: {source}")
        target = raw / source.name
        if target.exists():
            raise SystemExit(f"duplicate raw log basename: {source.name}")
        shutil.copy2(source, target)
        copied.append(str(target))
    payload = {"task_id": a.task_id, "input_commit": a.input_commit,
               "worker": a.worker, "result": a.result,
               "first_failure": a.first_failure, "raw_logs": copied,
               "source_changed": None, "hardware_used": False,
               "architectural_recommendation": "NOT_REQUESTED"}
    (a.output / "summary.json").write_text(json.dumps(payload, indent=2, sort_keys=True) + "\n")
    print(json.dumps(payload, sort_keys=True))


if __name__ == "__main__":
    main()
