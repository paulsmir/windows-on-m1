#!/usr/bin/env python3
"""Fixed, non-worker helper for the bounded patch-proposal demonstration."""
import argparse
import sys
from pathlib import Path


DEMO_PATH = Path("investigation/agent_tasks/demo.txt")
RUNNER_MARKER = "runner-marker\n"


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--repo", required=True, type=Path)
    args = parser.parse_args()
    target = args.repo / DEMO_PATH
    try:
        content = target.read_text(encoding="utf-8")
    except (OSError, UnicodeError):
        raise SystemExit(1)
    raise SystemExit(0 if content == RUNNER_MARKER else 1)


if __name__ == "__main__":
    main()
