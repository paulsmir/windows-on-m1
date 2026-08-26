#!/usr/bin/env python3
"""Validate and generate the immutable J313 AGX G2 Windows contract."""

import argparse
from dataclasses import dataclass
import hashlib
import json
from pathlib import Path
import re
import tempfile

try:
    from tools.agx_contract import contract_sha256, load_contract
except ModuleNotFoundError:
    from agx_contract import contract_sha256, load_contract


ROOT = Path(__file__).resolve().parents[1]
G1_CONTRACT = ROOT / "config" / "j313-agx.json"
G2_CONTRACT = ROOT / "config" / "j313-agx-g2.json"
WINDOWS_HEADER = (ROOT / "drivers" / "apple-agx" / "shared" / "include" /
                  "j313_agx_g2.generated.h")

TOP_KEYS = {
    "contract_version", "platform", "acpi_hid", "source_contract_sha256",
    "acpi_mmio_regions", "mmio_subregions", "interrupt_routes", "runtime",
}
RUNTIME_KEYS = {"context_id", "queue_index", "work_timeout_ms"}
ROUTE_KEYS = {"physical", "guest"}
EXACT_ACPI_MMIO = ("sgx_mmio",)
EXACT_MMIO_SUBREGIONS = ("asc_mmio",)
EXACT_GUEST_INTERRUPTS = tuple(range(880, 889))
RESERVED_GUEST_INTERRUPTS = {64, 865}
HEX64_RE = re.compile(r"[0-9a-f]{64}\Z")


class G2ContractError(ValueError):
    """The G2 contract is malformed or is not bound to accepted G1R data."""


@dataclass(frozen=True)
class InterruptRoute:
    physical: int
    guest: int


@dataclass(frozen=True)
class G2Contract:
    contract_version: int
    platform: str
    acpi_hid: str
    source_contract_sha256: str
    acpi_mmio: tuple[tuple[str, int, int], ...]
    mmio_subregions: tuple[tuple[str, int, int], ...]
    interrupt_routes: tuple[InterruptRoute, ...]
    context_id: int
    queue_index: int
    work_timeout_ms: int
    page_size: int
    address_bits: int
    firmware_generation: str
    firmware_version: str


def _exact(value, keys, where):
    if not isinstance(value, dict) or set(value) != set(keys):
        raise G2ContractError(f"{where} keys must be exactly {sorted(keys)}")


def _integer(value, where, minimum, maximum):
    if isinstance(value, bool) or not isinstance(value, int):
        raise G2ContractError(f"{where} must be an integer")
    if not minimum <= value <= maximum:
        raise G2ContractError(f"{where} is outside {minimum}..{maximum}")
    return value


def load_g2_contract(g2_path=G2_CONTRACT, g1_path=G1_CONTRACT):
    try:
        data = json.loads(Path(g2_path).read_text())
    except (OSError, json.JSONDecodeError) as exc:
        raise G2ContractError(str(exc)) from exc
    _exact(data, TOP_KEYS, "G2 contract")

    if data["contract_version"] != 1:
        raise G2ContractError("contract_version must be 1")
    if data["platform"] != "J313":
        raise G2ContractError("platform must be J313")
    if data["acpi_hid"] != "APPL0002":
        raise G2ContractError("acpi_hid must be APPL0002")

    source_hash = data["source_contract_sha256"]
    if not isinstance(source_hash, str) or not HEX64_RE.fullmatch(source_hash):
        raise G2ContractError("source contract SHA-256 is malformed")
    g1 = load_contract(Path(g1_path))
    if source_hash != contract_sha256(g1):
        raise G2ContractError("source contract SHA-256 does not match G1R")

    mmio_names = data["acpi_mmio_regions"]
    if (not isinstance(mmio_names, list)
            or tuple(mmio_names) != EXACT_ACPI_MMIO):
        raise G2ContractError(
            "ACPI MMIO regions must be exactly the non-overlapping sgx_mmio aperture"
        )
    acpi_mmio = tuple(
        (name, g1.regions[name].base, g1.regions[name].size)
        for name in mmio_names
    )
    subregion_names = data["mmio_subregions"]
    if (not isinstance(subregion_names, list)
            or tuple(subregion_names) != EXACT_MMIO_SUBREGIONS):
        raise G2ContractError("MMIO subregions must be exactly asc_mmio")
    mmio_subregions = tuple(
        (name, g1.regions[name].base, g1.regions[name].size)
        for name in subregion_names
    )

    routes_data = data["interrupt_routes"]
    if not isinstance(routes_data, list):
        raise G2ContractError("interrupt_routes must be a list")
    routes = []
    for index, item in enumerate(routes_data):
        _exact(item, ROUTE_KEYS, f"interrupt_routes[{index}]")
        routes.append(InterruptRoute(
            physical=_integer(item["physical"], "physical interrupt", 32, 1019),
            guest=_integer(item["guest"], "guest interrupt", 32, 1019),
        ))
    routes = tuple(routes)
    physical = tuple(route.physical for route in routes)
    guest = tuple(route.guest for route in routes)
    if physical != g1.interrupts:
        raise G2ContractError("physical interrupts must exactly match G1R")
    if len(set(guest)) != len(guest):
        raise G2ContractError("guest interrupts must be unique")
    if any(value in RESERVED_GUEST_INTERRUPTS for value in guest):
        raise G2ContractError("reserved guest interrupt cannot be used")
    if guest != EXACT_GUEST_INTERRUPTS:
        raise G2ContractError("guest interrupts must be the reviewed 880..888 range")

    runtime = data["runtime"]
    _exact(runtime, RUNTIME_KEYS, "runtime")
    context_id = _integer(runtime["context_id"], "context_id", 0, 63)
    queue_index = _integer(runtime["queue_index"], "queue_index", 0, 3)
    timeout = _integer(runtime["work_timeout_ms"], "work_timeout_ms", 1, 5000)
    if context_id != 63:
        raise G2ContractError("context_id must be 63")
    if queue_index != 1:
        raise G2ContractError("queue_index must be 1")
    if timeout != 500:
        raise G2ContractError("work_timeout_ms must be 500")

    return G2Contract(
        contract_version=1,
        platform="J313",
        acpi_hid="APPL0002",
        source_contract_sha256=source_hash,
        acpi_mmio=acpi_mmio,
        mmio_subregions=mmio_subregions,
        interrupt_routes=routes,
        context_id=context_id,
        queue_index=queue_index,
        work_timeout_ms=timeout,
        page_size=g1.uat.page_size,
        address_bits=g1.uat.address_bits,
        firmware_generation=g1.firmware.generation,
        firmware_version=g1.firmware.version,
    )


def render_windows_header(contract):
    lines = [
        "/* Generated by tools/generate_j313_agx_g2_contract.py; do not edit. */",
        "#ifndef J313_AGX_G2_GENERATED_H",
        "#define J313_AGX_G2_GENERATED_H",
        "",
        f'#define J313_AGX_G2_ACPI_HID "{contract.acpi_hid}"',
        ("#define J313_AGX_G2_SOURCE_CONTRACT_SHA256 "
         f'"{contract.source_contract_sha256}"'),
        f'#define J313_AGX_G2_FIRMWARE_GENERATION "{contract.firmware_generation}"',
        f'#define J313_AGX_G2_FIRMWARE_VERSION "{contract.firmware_version}"',
        f"#define J313_AGX_G2_CONTRACT_VERSION {contract.contract_version}u",
        f"#define J313_AGX_G2_CONTEXT_ID {contract.context_id}u",
        f"#define J313_AGX_G2_QUEUE_INDEX {contract.queue_index}u",
        f"#define J313_AGX_G2_WORK_TIMEOUT_MS {contract.work_timeout_ms}u",
        f"#define J313_AGX_G2_PAGE_SIZE 0x{contract.page_size:x}ULL",
        f"#define J313_AGX_G2_ADDRESS_BITS {contract.address_bits}u",
        "",
    ]
    for name, base, size in contract.acpi_mmio + contract.mmio_subregions:
        macro = name.upper()
        lines.extend([
            f"#define J313_AGX_G2_{macro}_BASE 0x{base:x}ULL",
            f"#define J313_AGX_G2_{macro}_SIZE 0x{size:x}ULL",
        ])
    route_values = ", ".join(
        f"{{ {route.physical}u, {route.guest}u }}"
        for route in contract.interrupt_routes
    )
    lines.extend([
        "",
        "typedef struct _J313_AGX_G2_INTERRUPT_ROUTE {",
        "    unsigned long PhysicalIntId;",
        "    unsigned long GuestIntId;",
        "} J313_AGX_G2_INTERRUPT_ROUTE;",
        f"#define J313_AGX_G2_INTERRUPT_ROUTE_COUNT {len(contract.interrupt_routes)}u",
        f"#define J313_AGX_G2_INTERRUPT_ROUTE_VALUES {{ {route_values} }}",
        "",
        "#endif /* J313_AGX_G2_GENERATED_H */",
        "",
    ])
    return "\n".join(lines)


def _atomic_write(path, content):
    path.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.NamedTemporaryFile("w", dir=path.parent, delete=False) as handle:
        handle.write(content)
        temporary = Path(handle.name)
    temporary.replace(path)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--check", action="store_true")
    args = parser.parse_args()
    rendered = render_windows_header(load_g2_contract())
    stale = not WINDOWS_HEADER.exists() or WINDOWS_HEADER.read_text() != rendered
    if args.check:
        if stale:
            parser.error("generated J313 AGX G2 contract is stale")
        return
    if stale:
        _atomic_write(WINDOWS_HEADER, rendered)


if __name__ == "__main__":
    main()
