#!/usr/bin/env python3
"""Generate immutable J313 Hwdata bytes using the production native serializer.

Run with proxyenv/bin/python and the exact raw ADT captured for the profile.
The generated header carries the original native MIT notice because its bytes
derive substantially from the native constructors and performance defaults.
"""
import argparse
from dataclasses import dataclass
import hashlib
from pathlib import Path
import struct
import sys

ROOT = Path(__file__).resolve().parents[1]
NATIVE = ROOT / "m1n1_windows"
sys.path.insert(0, str(NATIVE / "proxyclient"))

SCALARS = (
    "gpu-avg-power-filter-tc-ms", "gpu-avg-power-ki-only",
    "gpu-avg-power-kp", "gpu-avg-power-min-duty-cycle",
    "gpu-avg-power-target-filter-tc", "gpu-fast-die0-integral-gain",
    "gpu-fast-die0-prop-tgt-delta", "gpu-fast-die0-proportional-gain",
    "gpu-fast-die0-release-temp", "gpu-perf-boost-ce-step",
    "gpu-perf-boost-min-util", "gpu-perf-filter-drop-threshold",
    "gpu-perf-filter-time-constant", "gpu-perf-filter-time-constant2",
    "gpu-perf-integral-gain", "gpu-perf-integral-gain2",
    "gpu-perf-integral-min-clamp", "gpu-perf-proportional-gain",
    "gpu-perf-proportional-gain2", "gpu-perf-reset-iters",
    "gpu-perf-tgt-utilization", "gpu-ppm-filter-time-constant-ms",
    "gpu-ppm-ki", "gpu-ppm-kp", "gpu-pwr-filter-time-constant",
    "gpu-pwr-integral-gain", "gpu-pwr-integral-min-clamp",
    "gpu-pwr-min-duty-cycle", "gpu-pwr-proportional-gain",
    "gpu-pwr-sample-period-aic-clks", "gpu-idle-off-delay-ms",
    "gpu-fender-idle-off-delay-ms", "gpu-fw-early-wake-timeout-ms",
)
GEOMETRY = (
    "perf-state-count", "perf-state-table-count", "perf-states",
    "gpu-perf-base-pstate", "gpu-num-perf-states", "gpu-power-sample-period",
    "gpu-region-base",
)
SE = (
    "gpu-se-inactive-threshold", "gpu-se-engagement-criteria", "gpu-se-reset-criteria",
    "gpu-se-filter-time-constant", "gpu-se-filter-time-constant-1",
    "gpu-se-ki", "gpu-se-ki-1", "gpu-se-kp", "gpu-se-kp-1",
)
ZONES = tuple(f"gpu-power-zone-{kind}-{i}" for i in range(5)
              for kind in ("target", "target-offset", "filter-tc"))
INPUT_NAMES = SCALARS + GEOMETRY + SE + ZONES + ("cs-perf-states", "afr-perf-states")
SOURCE_PATHS = (
    "m1n1_windows/proxyclient/m1n1/agx/initdata.py",
    "m1n1_windows/proxyclient/m1n1/fw/agx/initdata.py",
    "m1n1_windows/proxyclient/m1n1/constructutils.py",
    "m1n1_windows/proxyclient/m1n1/adt.py",
    "tools/generate_agx_hwdata_profile.py",
)


@dataclass(frozen=True)
class Profile:
    a: bytes
    b: bytes
    inputs: tuple
    sources: dict
    identity: bytes
    adt_sha256: str


def raw_properties(adt_bytes, path):
    """Parse raw ADT values directly: never round-trip float32 through Python."""
    from m1n1.adt import ADTNodeStruct
    node = ADTNodeStruct.parse(adt_bytes)
    for part in path.strip("/").split("/"):
        matches = [child for child in node.children if any(
            p.name == "name" and p.value.rstrip(b"\0") == part.encode("ascii")
            for p in child.properties)]
        if len(matches) != 1:
            raise ValueError(f"Missing or duplicate ADT node: {path}")
        node = matches[0]
    props = {}
    for prop in node.properties:
        if prop.name in props:
            raise ValueError(f"Duplicate raw ADT property: {path}/{prop.name}")
        props[prop.name] = bytes(prop.value)
    return props


def canonical_inputs(inputs):
    data = bytearray(struct.pack("<I", len(inputs)))
    for name, value in inputs:
        encoded = name.encode("ascii")
        data += struct.pack("<I", len(encoded)) + encoded
        data += struct.pack("<I", 0xffffffff if value is None else len(value))
        if value is not None:
            data += value
    return bytes(data)


def profile_identity(inputs, sources, a, b):
    digest = hashlib.sha256(b"AGX-HWDATA-PROFILE\0v1\0T8103\0J313\0G13\0V13_5\0")
    digest.update(canonical_inputs(inputs))
    digest.update(struct.pack("<I", len(sources)))
    for name, identity in sorted(sources.items()):
        encoded = name.encode("ascii")
        digest.update(struct.pack("<I", len(encoded)) + encoded + bytes.fromhex(identity))
    for blob in (a, b):
        digest.update(struct.pack("<I", len(blob)) + blob)
    return digest.digest()


def generate_profile(adt_bytes):
    import construct
    from m1n1.adt import load_adt
    from m1n1.agx.initdata import CHIP_INFO, populate_hwdata
    from m1n1.constructutils import Ver
    from m1n1.fw.agx.initdata import AGXHWDataA, AGXHWDataB, IOMapping

    adt = load_adt(adt_bytes)
    chosen = adt["/chosen"]
    if chosen.chip_id != 0x8103 or chosen.board_id != 0x26:
        raise ValueError("Profile requires J313 board0x26/T8103")
    firmware = chosen.getprop("firmware-version", b"")
    if isinstance(firmware, str):
        firmware = firmware.encode("ascii")
    if firmware.rstrip(b"\0") != b"iBoot-8422.141.2":
        raise ValueError("Profile requires the captured V13_5 firmware ABI")
    Ver.set_version_key("V", "V13_5")
    Ver.set_version_key("G", "G13")
    sgx = adt["/arm-io/sgx"]
    raw = raw_properties(adt_bytes, "/arm-io/sgx")
    inputs = tuple((name, raw.get(name)) for name in INPUT_NAMES)
    chip_info = CHIP_INFO[0x8103]
    a, b = AGXHWDataA(sgx, chip_info), AGXHWDataB(sgx, chip_info)
    populate_hwdata(sgx, chip_info, a, b)
    b.io_mappings = [IOMapping() for _ in range(25)]
    b.timestamp_region_base = 0
    b.sgx_sram_ptr = 0
    a_bytes, b_bytes = a.build(), b.build()
    if len(a_bytes) != 0x421c or len(b_bytes) != 0x1884:
        raise ValueError("Native Hwdata layout no longer matches the J313 profile ABI")
    sources = {name: hashlib.sha256((ROOT / name).read_bytes()).hexdigest()
               for name in SOURCE_PATHS}
    sources["python-package/construct-version"] = hashlib.sha256(
        construct.__version__.encode("ascii")).hexdigest()
    identity = profile_identity(inputs, sources, a_bytes, b_bytes)
    return Profile(a_bytes, b_bytes, inputs, sources, identity,
                   hashlib.sha256(adt_bytes).hexdigest())


def byte_array(name, data):
    # One stored zero for a present-empty property keeps it distinct from NULL.
    stored = data or b"\0"
    rows = ["    " + ", ".join(f"0x{b:02x}" for b in stored[i:i + 16]) + ","
            for i in range(0, len(stored), 16)]
    return f"static const unsigned char {name}[{len(stored)}] = {{\n" + "\n".join(rows) + "\n};"


def render_header(profile):
    notice = (NATIVE / "LICENSE").read_text().strip()
    lines = ["/* Generated by tools/generate_agx_hwdata_profile.py; do not edit.",
             " * Native-derived profile bytes are distributed under the following license:"]
    lines += [" * " + line if line else " *" for line in notice.splitlines()]
    lines += [" */", "#ifndef APPLE_AGX_HWDATA_PROFILE_GENERATED_H",
              "#define APPLE_AGX_HWDATA_PROFILE_GENERATED_H", "",
              f"/* Raw ADT SHA-256: {profile.adt_sha256}",
              f" * Profile SHA-256: {profile.identity.hex()}"]
    lines += [f" * Source {name}: {identity}" for name, identity in sorted(profile.sources.items())]
    lines += [" */", "#define AGX_HWDATA_A_BYTES 0x421cu", "#define AGX_HWDATA_B_BYTES 0x1884u",
              f"#define AGX_HWDATA_INPUT_COUNT {len(profile.inputs)}u", "",
              "typedef struct _AGX_HWDATA_INPUT {",
              "    const char *Name;", "    unsigned int Length;",
              "    const unsigned char *Bytes;", "} AGX_HWDATA_INPUT;", "",
              byte_array("AgxHwdataAProfile", profile.a), "",
              byte_array("AgxHwdataBProfile", profile.b), "",
              byte_array("AgxHwdataProfileId", profile.identity), ""]
    for i, (_, raw) in enumerate(profile.inputs):
        if raw is not None:
            lines += [byte_array(f"AgxHwdataInputBytes{i}", raw)]
    lines += ["", "/* 0xffffffff means absent; length zero means present-empty. */",
              "static const AGX_HWDATA_INPUT AgxHwdataInputs[AGX_HWDATA_INPUT_COUNT] = {"]
    for i, (name, raw) in enumerate(profile.inputs):
        value = "0xffffffffu, 0" if raw is None else f"{len(raw)}u, AgxHwdataInputBytes{i}"
        lines += [f'    {{"{name}", {value}}},']
    lines += ["};", "", "#endif", ""]
    return "\n".join(lines)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--adt", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    profile = generate_profile(args.adt.read_bytes())
    args.output.write_text(render_header(profile))


if __name__ == "__main__":
    main()
