#!/usr/bin/env python3
"""Validate the exact enabled CPU list in the J313 static MADT source."""

from __future__ import annotations

import argparse
import re
import sys
from pathlib import Path


CALL = "EFI_ACPI_6_3_GICC_STRUCTURE_INIT"
ENABLED = "EFI_ACPI_6_3_GIC_ENABLED"


def _without_comments(source: str) -> str:
    source = re.sub(r"/\*.*?\*/", "", source, flags=re.DOTALL)
    return re.sub(r"//[^\n]*", "", source)


def _calls(source: str):
    cursor = 0
    while True:
        marker = source.find(CALL, cursor)
        if marker < 0:
            return
        opening = source.find("(", marker + len(CALL))
        if opening < 0:
            raise ValueError(f"unterminated {CALL} at offset {marker}")

        depth = 1
        end = opening + 1
        while end < len(source) and depth:
            if source[end] == "(":
                depth += 1
            elif source[end] == ")":
                depth -= 1
            end += 1
        if depth:
            raise ValueError(f"unterminated {CALL} at offset {marker}")

        yield source[opening + 1 : end - 1]
        cursor = end


def _arguments(call: str) -> list[str]:
    arguments: list[str] = []
    start = 0
    depth = 0
    for index, character in enumerate(call):
        if character == "(":
            depth += 1
        elif character == ")":
            depth -= 1
        elif character == "," and depth == 0:
            arguments.append(call[start:index].strip())
            start = index + 1
    arguments.append(call[start:].strip())
    return arguments


def enabled_gicc_uids(source: str) -> list[int]:
    """Return enabled GICC ACPI UIDs in source order."""
    enabled: list[int] = []
    for call in _calls(_without_comments(source)):
        arguments = _arguments(call)
        if len(arguments) < 4:
            raise ValueError(f"GICC initializer has only {len(arguments)} arguments")
        if re.search(rf"\b{ENABLED}\b", arguments[3]):
            enabled.append(int(arguments[1], 0))
    return enabled


def gicc_efficiency_classes(source: str) -> dict[int, int]:
    """Return the MADT Processor Power Efficiency Class keyed by ACPI UID."""
    classes: dict[int, int] = {}
    for call in _calls(_without_comments(source)):
        arguments = _arguments(call)
        if len(arguments) != 12:
            raise ValueError(f"GICC initializer has {len(arguments)} arguments, expected 12")
        classes[int(arguments[1], 0)] = int(arguments[10], 0)
    return classes


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("source", type=Path)
    parser.add_argument("--expect", required=True, help="comma-separated enabled ACPI UIDs")
    parser.add_argument(
        "--expect-efficiency",
        help="comma-separated ACPI_UID:EFFICIENCY_CLASS pairs",
    )
    args = parser.parse_args(argv)

    expected = [int(value, 0) for value in args.expect.split(",") if value]
    observed = enabled_gicc_uids(args.source.read_text())
    print(f"observed enabled GICC UIDs: {observed}")
    if observed != expected:
        print(f"expected enabled GICC UIDs: {expected}", file=sys.stderr)
        return 1
    if args.expect_efficiency:
        expected_classes = {
            int(uid, 0): int(efficiency, 0)
            for pair in args.expect_efficiency.split(",")
            for uid, efficiency in [pair.split(":", 1)]
        }
        observed_classes = gicc_efficiency_classes(args.source.read_text())
        print(f"observed GICC efficiency classes: {observed_classes}")
        if observed_classes != expected_classes:
            print(
                f"expected GICC efficiency classes: {expected_classes}",
                file=sys.stderr,
            )
            return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
