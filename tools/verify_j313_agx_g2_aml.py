#!/usr/bin/env python3
"""Verify the complete disassembled J313 AGX G2 SSDT contract."""

from __future__ import annotations

import argparse
from pathlib import Path
import re

try:
    from tools.generate_j313_agx_g2_contract import load_g2_contract
except ModuleNotFoundError:
    from generate_j313_agx_g2_contract import load_g2_contract


class AmlContractError(ValueError):
    """The disassembled AML is not one exact J313 AGX G2 device."""


def _without_comments(text):
    text = re.sub(r"/\*.*?\*/", "", text, flags=re.DOTALL)
    return re.sub(r"//.*?$", "", text, flags=re.MULTILINE)


def _balanced(text, opening, open_char="{", close_char="}"):
    start = opening.end() - 1
    depth = 0
    quoted = False
    escaped = False
    for index in range(start, len(text)):
        char = text[index]
        if quoted:
            if escaped:
                escaped = False
            elif char == "\\":
                escaped = True
            elif char == '"':
                quoted = False
            continue
        if char == '"':
            quoted = True
        elif char == open_char:
            depth += 1
        elif char == close_char:
            depth -= 1
            if depth == 0:
                return text[start + 1:index]
    raise AmlContractError("unbalanced AML disassembly")


def _one_match(pattern, text, description, flags=0):
    matches = list(re.finditer(pattern, text, flags))
    if len(matches) != 1:
        raise AmlContractError(
            f"{description} must appear exactly once; found {len(matches)}"
        )
    return matches[0]


def _integer(value):
    names = {"Zero": 0, "One": 1, "Ones": -1}
    value = value.strip()
    if value in names:
        return names[value]
    try:
        return int(value, 0)
    except ValueError as exc:
        raise AmlContractError(f"invalid AML integer {value!r}") from exc


def _name_string(body, name, description):
    match = _one_match(
        rf"\bName\s*\(\s*{re.escape(name)}\s*,\s*\"([^\"]*)\"\s*\)",
        body,
        description,
    )
    return match.group(1)


def _name_integer(body, name, description):
    match = _one_match(
        rf"\bName\s*\(\s*{re.escape(name)}\s*,\s*([A-Za-z]+|0[xX][0-9A-Fa-f]+|[0-9]+)\s*\)",
        body,
        description,
    )
    return _integer(match.group(1))


def _property(body, name, description):
    match = _one_match(
        rf"\bPackage\s*\(\s*\)\s*\{{\s*\"{re.escape(name)}\"\s*,\s*"
        r"(?P<value>\"[^\"]*\"|[A-Za-z]+|0[xX][0-9A-Fa-f]+|[0-9]+)\s*\}",
        body,
        description,
    )
    value = match.group("value")
    return value[1:-1] if value.startswith('"') else _integer(value)


def verify_dsl(dsl, contract=None):
    """Accept only one exact AGX0 device in an iasl ASL/DSL representation."""
    contract = contract or load_g2_contract()
    text = _without_comments(dsl)

    device_match = _one_match(
        r"\bDevice\s*\(\s*AGX0\s*\)\s*\{",
        text,
        "Device (AGX0)",
    )
    device = _balanced(text, device_match)

    if _name_string(device, "_HID", "_HID") != contract.acpi_hid:
        raise AmlContractError("_HID does not match APPL0002")
    if _name_integer(device, "_UID", "_UID") != 0:
        raise AmlContractError("_UID must be zero")
    if _name_integer(device, "_CCA", "_CCA") != 1:
        raise AmlContractError("_CCA must be one")
    if _name_integer(device, "_STA", "_STA") != 0x0F:
        raise AmlContractError("_STA must be 0x0F")

    crs_match = _one_match(
        r"\bName\s*\(\s*_CRS\s*,\s*ResourceTemplate\s*\(\s*\)\s*\{",
        device,
        "_CRS ResourceTemplate",
    )
    crs = _balanced(device, crs_match)

    qwords = list(re.finditer(r"\bQWordMemory\s*\(", crs))
    if len(qwords) != 1:
        raise AmlContractError(
            f"_CRS must contain exactly one QWordMemory; found {len(qwords)}"
        )
    qword = _balanced(crs, qwords[0], "(", ")")
    qword_flags = [part.strip() for part in qword.split(",")[:6]]
    if qword_flags != [
        "ResourceConsumer", "PosDecode", "MinFixed", "MaxFixed",
        "NonCacheable", "ReadWrite",
    ]:
        raise AmlContractError("QWordMemory flags do not match the G2 contract")
    numbers = [_integer(value) for value in re.findall(r"0[xX][0-9A-Fa-f]+|\b[0-9]+\b", qword)]
    _, base, size = contract.acpi_mmio[0]
    expected_mmio = [0, base, base + size - 1, 0, size]
    if numbers != expected_mmio:
        raise AmlContractError(
            f"MMIO descriptor {numbers!r} does not match {expected_mmio!r}"
        )

    interrupt_matches = list(re.finditer(r"\bInterrupt\s*\(", crs))
    if len(interrupt_matches) != len(contract.interrupt_routes):
        raise AmlContractError(
            "interrupt descriptor count does not match the G2 contract"
        )
    observed_interrupts = []
    for match in interrupt_matches:
        header = _balanced(crs, match, "(", ")")
        flags = [part.strip() for part in header.split(",")[:4]]
        if flags != ["ResourceConsumer", "Level", "ActiveHigh", "Exclusive"]:
            raise AmlContractError("interrupt flags do not match the G2 contract")
        close = crs.find(")", match.end())
        body_match = re.search(r"\{", crs[close + 1:])
        if body_match is None:
            raise AmlContractError("interrupt descriptor has no value list")
        absolute = close + 1 + body_match.start()
        synthetic = re.match(r"\{", crs[absolute:])
        values = _balanced(crs[absolute:], synthetic)
        parsed = [_integer(value) for value in re.findall(
            r"0[xX][0-9A-Fa-f]+|\b[0-9]+\b", values
        )]
        if len(parsed) != 1:
            raise AmlContractError("interrupt descriptor must contain one GSIV")
        observed_interrupts.append(parsed[0])
    expected_interrupts = [route.guest for route in contract.interrupt_routes]
    if observed_interrupts != expected_interrupts:
        raise AmlContractError(
            "interrupt order or value does not match the G2 contract"
        )

    dsd_match = _one_match(
        r"\bName\s*\(\s*_DSD\s*,\s*Package\s*\(\s*\)\s*\{",
        device,
        "_DSD package",
    )
    dsd = _balanced(device, dsd_match)
    if _property(dsd, "agx-contract-version", "contract version") != contract.contract_version:
        raise AmlContractError("contract version does not match")
    if _property(dsd, "agx-source-contract-sha256", "source contract hash") != contract.source_contract_sha256:
        raise AmlContractError("source contract hash does not match")
    if _property(dsd, "agx-firmware-generation", "firmware generation") != contract.firmware_generation:
        raise AmlContractError("firmware generation does not match")
    if _property(dsd, "agx-firmware-version", "firmware version") != contract.firmware_version:
        raise AmlContractError("firmware version does not match")


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("dsl", type=Path, help="iasl-disassembled AGX SSDT DSL")
    args = parser.parse_args()
    try:
        verify_dsl(args.dsl.read_text())
    except (OSError, UnicodeError, AmlContractError) as exc:
        parser.error(str(exc))
    print(f"validated J313 AGX G2 AML contract: {args.dsl}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
