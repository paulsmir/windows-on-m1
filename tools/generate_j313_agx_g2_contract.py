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
ASL_INCLUDE = (ROOT / "mu" / "Platform" / "MacBookAirMid2020Pkg" /
               "AcpiTables" / "J313AppleAgx.asl.inc")
M1N1_HEADER = ROOT / "m1n1_windows" / "src" / "hv_agx_g2.generated.h"
OUTPUTS = {
    "windows": WINDOWS_HEADER,
    "asl": ASL_INCLUDE,
    "m1n1": M1N1_HEADER,
}

TOP_KEYS = {
    "contract_version", "platform", "acpi_hid", "source_contract_sha256",
    "acpi_mmio_regions", "mmio_subregions", "synthetic_mmio_regions",
    "interrupt_routes", "runtime",
}
RUNTIME_KEYS = {"context_id", "queue_index", "work_timeout_ms"}
ROUTE_KEYS = {"physical", "guest"}
EXACT_ACPI_MMIO = ("sgx_mmio",)
EXACT_MMIO_SUBREGIONS = ("asc_mmio",)
EXACT_GUEST_INTERRUPTS = tuple(range(880, 889))
RESERVED_GUEST_INTERRUPTS = {64, 865}
HEX64_RE = re.compile(r"[0-9a-f]{64}\Z")
POWER_BROKER_BASE = 0x300000000
POWER_BROKER_SIZE = 0x1000
POWER_BROKER_ABI = (
    ("POWER_MAGIC", 0x58504741),
    ("POWER_ABI_VERSION", 1),
    ("POWER_CAP_FIXED_J313_DOMAINS", 1),
    ("POWER_REG_MAGIC", 0x00),
    ("POWER_REG_ABI_VERSION", 0x04),
    ("POWER_REG_CAPABILITIES", 0x08),
    ("POWER_REG_STATE", 0x0C),
    ("POWER_REG_RESULT", 0x10),
    ("POWER_REG_RECEIPT_SEQUENCE", 0x18),
    ("POWER_REG_ACCEPTED_REQUESTS", 0x20),
    ("POWER_REG_REJECTED_REQUESTS", 0x28),
    ("POWER_REG_REQUEST_SEQUENCE", 0x30),
    ("POWER_REG_COMMAND", 0x38),
    ("POWER_CMD_QUERY", 0),
    ("POWER_CMD_ON", 1),
    ("POWER_CMD_OFF", 2),
    ("POWER_STATE_OFF", 0),
    ("POWER_STATE_ON", 3),
    ("POWER_RESULT_OK", 0),
    ("POWER_RESULT_TRANSITION_FAILED", 4),
)


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
    synthetic_mmio: tuple[tuple[str, int, int], ...]
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

    if data["contract_version"] != 2:
        raise G2ContractError("contract_version must be 2")
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

    synthetic = data["synthetic_mmio_regions"]
    _exact(synthetic, {"power_broker"}, "synthetic_mmio_regions")
    broker = synthetic["power_broker"]
    _exact(broker, {"base", "size"}, "power broker")
    broker_base = _integer(broker["base"], "power broker base", 0, (1 << 40) - 1)
    broker_size = _integer(broker["size"], "power broker size", 1, 0x10000)
    if broker_base != POWER_BROKER_BASE or broker_size != POWER_BROKER_SIZE:
        raise G2ContractError("power broker must be the reviewed 0x300000000/0x1000 page")
    synthetic_mmio = (("power_broker", broker_base, broker_size),)

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
        contract_version=2,
        platform="J313",
        acpi_hid="APPL0002",
        source_contract_sha256=source_hash,
        acpi_mmio=acpi_mmio,
        mmio_subregions=mmio_subregions,
        synthetic_mmio=synthetic_mmio,
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
    for name, base, size in (contract.acpi_mmio + contract.mmio_subregions +
                             contract.synthetic_mmio):
        macro = name.upper()
        lines.extend([
            f"#define J313_AGX_G2_{macro}_BASE 0x{base:x}ULL",
            f"#define J313_AGX_G2_{macro}_SIZE 0x{size:x}ULL",
        ])
    lines.append("")
    lines.extend(
        f"#define J313_AGX_G2_{name} 0x{value:x}u"
        for name, value in POWER_BROKER_ABI
    )
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


def _qword_memory(base, size):
    maximum = base + size - 1
    return f"""        QWordMemory (ResourceConsumer, PosDecode, MinFixed, MaxFixed,
            NonCacheable, ReadWrite, 0x0,
            0x{base:016X}, 0x{maximum:016X}, 0x0,
            0x{size:016X}, ,, , AddressRangeMemory, TypeStatic)"""


def _interrupt_resource(guest):
    return f"""        Interrupt (ResourceConsumer, Level, ActiveHigh, Exclusive)
        {{ {guest} }}"""


def render_asl_include(contract):
    if len(contract.acpi_mmio) != 1:
        raise G2ContractError("ASL requires exactly one ACPI MMIO aperture")
    _, base, size = contract.acpi_mmio[0]
    if len(contract.synthetic_mmio) != 1:
        raise G2ContractError("ASL requires exactly one synthetic power broker page")
    _, broker_base, broker_size = contract.synthetic_mmio[0]
    interrupts = "\n".join(
        _interrupt_resource(route.guest)
        for route in contract.interrupt_routes
    )
    return f"""// Generated by tools/generate_j313_agx_g2_contract.py; do not edit.
Device (AGX0)
{{
    Name (_HID, \"{contract.acpi_hid}\")
    Name (_UID, Zero)
    Name (_CCA, One)
    Name (_STA, 0x0F)
    Name (_CRS, ResourceTemplate ()
    {{
{_qword_memory(base, size)}
{_qword_memory(broker_base, broker_size)}
{interrupts}
    }})
    Name (_DSD, Package ()
    {{
        ToUUID (\"daffd814-6eba-4d8c-8a91-bc9bbf4aa301\"),
        Package ()
        {{
            Package () {{ \"agx-contract-version\", 0x{contract.contract_version:02X} }},
            Package () {{ \"agx-source-contract-sha256\", \"{contract.source_contract_sha256}\" }},
            Package () {{ \"agx-firmware-generation\", \"{contract.firmware_generation}\" }},
            Package () {{ \"agx-firmware-version\", \"{contract.firmware_version}\" }}
        }}
    }})
}}
"""


def render_m1n1_header(contract):
    if len(contract.acpi_mmio) != 1:
        raise G2ContractError("m1n1 policy requires exactly one ACPI MMIO aperture")
    _, base, size = contract.acpi_mmio[0]
    if len(contract.synthetic_mmio) != 1:
        raise G2ContractError("m1n1 policy requires one synthetic power broker page")
    _, broker_base, broker_size = contract.synthetic_mmio[0]
    route_names = [
        f"HV_AGX_G2_INTERRUPT_ROUTE_{index}"
        for index in range(len(contract.interrupt_routes))
    ]
    lines = [
        "/* Generated by tools/generate_j313_agx_g2_contract.py; do not edit. */",
        "/* clang-format off */",
        "#ifndef HV_AGX_G2_GENERATED_H",
        "#define HV_AGX_G2_GENERATED_H",
        "",
        '#define HV_AGX_G2_PROFILE_IDENTITY "agx-g2"',
        "#define HV_AGX_G2_SOURCE_CONTRACT_SHA256 \\",
        f'  "{contract.source_contract_sha256}"',
        f"#define HV_AGX_G2_CONTRACT_VERSION {contract.contract_version}u",
        f"#define HV_AGX_G2_SGX_MMIO_BASE 0x{base:x}ULL",
        f"#define HV_AGX_G2_SGX_MMIO_SIZE 0x{size:x}ULL",
        f"#define HV_AGX_G2_POWER_BROKER_BASE 0x{broker_base:x}ULL",
        f"#define HV_AGX_G2_POWER_BROKER_SIZE 0x{broker_size:x}ULL",
        *(
            f"#define HV_AGX_G2_{name} 0x{value:x}u"
            for name, value in POWER_BROKER_ABI
        ),
        "#define HV_AGX_G2_INTERRUPT_LEVEL 1u",
        "#define HV_AGX_G2_INTERRUPT_ACTIVE_HIGH 1u",
        "#define HV_AGX_G2_INTERRUPT_EXCLUSIVE 1u",
        "",
        "struct hv_agx_g2_interrupt_route {",
        "  unsigned int physical_intid;",
        "  unsigned int guest_intid;",
        "};",
        ("#define HV_AGX_G2_INTERRUPT_ROUTE_COUNT "
         f"{len(contract.interrupt_routes)}u"),
    ]
    for index, route in enumerate(contract.interrupt_routes):
        lines.append(
            f"#define {route_names[index]} "
            f"{{{route.physical}u, {route.guest}u}}"
        )
    lines.extend([
        "#define HV_AGX_G2_INTERRUPT_ROUTE_VALUES "
        "{" + ", ".join(route_names) + "}",
        "",
        "/* clang-format on */",
        "#endif /* HV_AGX_G2_GENERATED_H */",
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
    contract = load_g2_contract()
    rendered = {
        "windows": render_windows_header(contract),
        "asl": render_asl_include(contract),
        "m1n1": render_m1n1_header(contract),
    }
    stale = [
        name for name, path in OUTPUTS.items()
        if not path.exists() or path.read_text() != rendered[name]
    ]
    if args.check:
        if stale:
            parser.error(
                "generated J313 AGX G2 contract is stale: " + ", ".join(stale)
            )
        return
    for name in stale:
        _atomic_write(OUTPUTS[name], rendered[name])


if __name__ == "__main__":
    main()
