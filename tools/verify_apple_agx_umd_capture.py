#!/usr/bin/env python3
"""Classify a bounded DBWIN trace for one exact AppleAgx probe process."""

from __future__ import annotations

import argparse
import json
import re
from pathlib import Path


RECORD = re.compile(r"^timestamp_ms=\d+ pid=(\d+) message=(.*)$")
SUMMARY = re.compile(r"^capture=summary ready=([01]) records=(\d+) lost=(\d+)$")


def classify_capture(text: str, expected_pid: int, token: str) -> dict[str, object]:
    records: list[tuple[int, str]] = []
    summary: tuple[bool, int, int] | None = None
    for raw_line in text.splitlines():
        line = raw_line.rstrip("\r")
        match = RECORD.fullmatch(line)
        if match:
            records.append((int(match.group(1)), match.group(2)))
            continue
        match = SUMMARY.fullmatch(line)
        if match:
            summary = (match.group(1) == "1", int(match.group(2)), int(match.group(3)))

    ready, recorded_count, lost = summary if summary is not None else (False, 0, 0)
    start_message = f"AppleAgxProbe: START token={token}"
    end_message = f"AppleAgxProbe: END token={token}"
    start_count = sum(
        1 for pid, message in records if pid == expected_pid and message == start_message
    )
    end_count = sum(
        1 for pid, message in records if pid == expected_pid and message == end_message
    )

    reasons: list[str] = []
    if not ready:
        reasons.append("listener-not-ready")
    if start_count == 0:
        reasons.append("start-marker-missing")
    elif start_count != 1:
        reasons.append("start-marker-ambiguous")
    if end_count == 0:
        reasons.append("end-marker-missing")
    elif end_count != 1:
        reasons.append("end-marker-ambiguous")
    if lost != 0:
        reasons.append("records-lost")
    if recorded_count != len(records):
        reasons.append("record-count-mismatch")

    return {
        "complete": not reasons,
        "end_count": end_count,
        "expected_pid": expected_pid,
        "lost": lost,
        "ready": ready,
        "reasons": reasons,
        "records": recorded_count,
        "start_count": start_count,
        "token": token,
    }


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--trace", required=True, type=Path)
    parser.add_argument("--expected-pid", required=True, type=int)
    parser.add_argument("--token", required=True)
    args = parser.parse_args()
    if args.expected_pid <= 0 or not args.token:
        parser.error("expected PID and token must be nonzero")
    result = classify_capture(
        args.trace.read_text(encoding="utf-8"), args.expected_pid, args.token
    )
    print(json.dumps(result, separators=(",", ":"), sort_keys=True))
    return 0 if result["complete"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
