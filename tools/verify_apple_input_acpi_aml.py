#!/usr/bin/env python3
"""Reject Mu builds whose compiled DSDT lost the J313 Apple input node."""

from __future__ import annotations

import argparse
from pathlib import Path


class AmlContractError(ValueError):
    """The compiled AML does not contain one exact Apple input contract."""


def _require_once(aml: bytes, value: bytes, description: str) -> None:
    count = aml.count(value)
    if count != 1:
        raise AmlContractError(
            f"compiled DSDT must contain {description} exactly once; found {count}"
        )


def verify_aml(aml: bytes) -> None:
    """Verify the generated J313 node survived ASL preprocessing and iasl."""
    _require_once(aml, b"AINP", "Device(AINP)")
    _require_once(aml, b"APPL0001", "APPL0001 _HID")
    for address in (0x23510C000, 0x23C100000, 0x23D1F0000):
        _require_once(
            aml,
            address.to_bytes(8, "little"),
            f"64-bit MMIO base {address:#x}",
        )
    _require_once(aml, (0x361).to_bytes(4, "little"), "guest INTID 865")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("aml", type=Path, help="compiled DSDT.aml")
    args = parser.parse_args()
    try:
        verify_aml(args.aml.read_bytes())
    except (OSError, AmlContractError) as exc:
        parser.error(str(exc))
    print(f"validated Apple input ACPI contract: {args.aml}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
