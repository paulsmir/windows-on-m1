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
    "interrupt_routes", "runtime", "firmware_lifecycle",
}
RUNTIME_KEYS = {"context_id", "queue_index", "work_timeout_ms"}
FIRMWARE_LIFECYCLE_KEYS = {
    "management_endpoint", "firmware_endpoint", "doorbell_endpoint",
    "iop_boot_request_state", "running_state", "stopped_state",
    "asc_cpu_control_offset", "asc_cpu_status_offset",
    "asc_inbox_control_offset", "asc_outbox_control_offset",
    "asc_inbox0_offset", "asc_inbox1_offset", "asc_outbox0_offset",
    "asc_outbox1_offset", "asc_boot_timeout_ms", "endpoint_timeout_ms",
    "initdata_timeout_ms", "heartbeat_timeout_ms", "stop_timeout_ms",
}
ROUTE_KEYS = {"physical", "guest"}
EXACT_ACPI_MMIO = ("sgx_mmio",)
EXACT_MMIO_SUBREGIONS = ("asc_mmio",)
EXACT_FIRMWARE_REGIONS = ("gpu", "shared", "handoff", "rtkit_private")
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
FIRMWARE_LIFECYCLE_EXACT = {
    "management_endpoint": 0x00,
    "firmware_endpoint": 0x20,
    "doorbell_endpoint": 0x21,
    "iop_boot_request_state": 0x220,
    "running_state": 0x20,
    "stopped_state": 0x10,
    "asc_cpu_control_offset": 0x0044,
    "asc_cpu_status_offset": 0x0048,
    "asc_inbox_control_offset": 0x8110,
    "asc_outbox_control_offset": 0x8114,
    "asc_inbox0_offset": 0x8800,
    "asc_inbox1_offset": 0x8808,
    "asc_outbox0_offset": 0x8830,
    "asc_outbox1_offset": 0x8838,
    "asc_boot_timeout_ms": 3000,
    "endpoint_timeout_ms": 500,
    "initdata_timeout_ms": 500,
    "heartbeat_timeout_ms": 500,
    "stop_timeout_ms": 1000,
}
FIRMWARE_LIFECYCLE_HEADER_NAMES = {
    "management_endpoint": "MANAGEMENT_ENDPOINT",
    "firmware_endpoint": "FIRMWARE_ENDPOINT",
    "doorbell_endpoint": "DOORBELL_ENDPOINT",
    "iop_boot_request_state": "IOP_BOOT_REQUEST_STATE",
    "running_state": "RUNNING_STATE",
    "stopped_state": "STOPPED_STATE",
    "asc_cpu_control_offset": "ASC_CPU_CONTROL_OFFSET",
    "asc_cpu_status_offset": "ASC_CPU_STATUS_OFFSET",
    "asc_inbox_control_offset": "ASC_INBOX_CTRL_OFFSET",
    "asc_outbox_control_offset": "ASC_OUTBOX_CTRL_OFFSET",
    "asc_inbox0_offset": "ASC_INBOX0_OFFSET",
    "asc_inbox1_offset": "ASC_INBOX1_OFFSET",
    "asc_outbox0_offset": "ASC_OUTBOX0_OFFSET",
    "asc_outbox1_offset": "ASC_OUTBOX1_OFFSET",
    "asc_boot_timeout_ms": "ASC_BOOT_TIMEOUT_MS",
    "endpoint_timeout_ms": "ENDPOINT_TIMEOUT_MS",
    "initdata_timeout_ms": "INITDATA_TIMEOUT_MS",
    "heartbeat_timeout_ms": "HEARTBEAT_TIMEOUT_MS",
    "stop_timeout_ms": "STOP_TIMEOUT_MS",
}
UAT_INPUT_ADDRESS_BITS = 39
UAT_PAGE_BITS = 14
UAT_LEVELS = ((36, 8), (25, 2048), (14, 2048))
UAT_FIRMWARE_CONTEXT = 0
UAT_RENDER_CONTEXTS = (1, 62)
UAT_QUALIFICATION_CONTEXT = 63
INITDATA_SIZE = 0xBC
INITDATA_VERSION_WORDS = (0x6BA0, 0x1F28, 0x0601, 0x00B0)
INITDATA_OBJECT_SIZES = (
    ("REGION_A", 0x4000),
    ("REGION_B", 0x6BC0),
    ("REGION_C", 0x12394),
    ("FW_STATUS", 0x80),
)
FWCTL_STATE_SIZE = 0x30
FWCTL_MESSAGE_SIZE = 0x14
FWCTL_RING_ENTRY_COUNT = 0x100
CHANNEL_INFO_SIZE = 0x10
CHANNEL_INFO_COUNT = 0x11
CHANNEL_STATE_STRIDE = 0x30
CMD_QUEUE_CHANNEL_COUNT = 0x0C
CHANNEL_RING_LAYOUT = (
    ("CMD_QUEUE", 0x30, 0x100),
    ("DEVCTRL", 0x30, 0x100),
    ("EVENT", 0x38, 0x100),
    ("FWLOG", 0xD8, 0x100 * 6),
    ("KTRACE", 0x38, 0x200),
    ("STATS", 0x40, 0x100),
)
FWLOG_RING_COUNT = 6
FWLOG_DUMMY_RING_SIZE = 0x150000
REGIONB_OBJECT_SIZES = (
    ("STATS_TA", 0x690),
    ("STATS_3D", 0x748),
    ("STATS_CP", 0x1180),
    ("HWDATA_A", 0x421C),
    ("FAULT_INFO", 0x80),
    ("TIMESTAMP", 0xC0),
    ("HWDATA_B", 0x1884),
    ("BUFFER_MGR_CTL", 0x7F0),
)


class G2ContractError(ValueError):
    """The G2 contract is malformed or is not bound to accepted G1R data."""


@dataclass(frozen=True)
class InterruptRoute:
    physical: int
    guest: int


@dataclass(frozen=True)
class FirmwareLifecycle:
    management_endpoint: int
    firmware_endpoint: int
    doorbell_endpoint: int
    iop_boot_request_state: int
    running_state: int
    stopped_state: int
    asc_cpu_control_offset: int
    asc_cpu_status_offset: int
    asc_inbox_control_offset: int
    asc_outbox_control_offset: int
    asc_inbox0_offset: int
    asc_inbox1_offset: int
    asc_outbox0_offset: int
    asc_outbox1_offset: int
    asc_boot_timeout_ms: int
    endpoint_timeout_ms: int
    initdata_timeout_ms: int
    heartbeat_timeout_ms: int
    stop_timeout_ms: int


@dataclass(frozen=True)
class G2Contract:
    contract_version: int
    platform: str
    acpi_hid: str
    source_contract_sha256: str
    acpi_mmio: tuple[tuple[str, int, int], ...]
    mmio_subregions: tuple[tuple[str, int, int], ...]
    synthetic_mmio: tuple[tuple[str, int, int], ...]
    firmware_regions: tuple[tuple[str, int, int], ...]
    interrupt_routes: tuple[InterruptRoute, ...]
    context_id: int
    queue_index: int
    work_timeout_ms: int
    page_size: int
    address_bits: int
    num_contexts: int
    firmware_generation: str
    firmware_version: str
    firmware_lifecycle: FirmwareLifecycle


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
    firmware_regions = tuple(
        (name, g1.regions[name].base, g1.regions[name].size)
        for name in EXACT_FIRMWARE_REGIONS
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

    lifecycle_data = data["firmware_lifecycle"]
    _exact(lifecycle_data, FIRMWARE_LIFECYCLE_KEYS, "firmware_lifecycle")
    lifecycle_values = {}
    for key, exact in FIRMWARE_LIFECYCLE_EXACT.items():
        value = _integer(lifecycle_data[key], key, 0, 0xFFFFFFFF)
        if value != exact:
            raise G2ContractError(f"{key} must be {exact}")
        lifecycle_values[key] = value

    endpoint_values = (
        lifecycle_values["management_endpoint"],
        lifecycle_values["firmware_endpoint"],
        lifecycle_values["doorbell_endpoint"],
    )
    if len(set(endpoint_values)) != len(endpoint_values):
        raise G2ContractError("firmware lifecycle endpoints must be unique")

    register_widths = {
        "asc_cpu_control_offset": 4,
        "asc_cpu_status_offset": 4,
        "asc_inbox_control_offset": 4,
        "asc_outbox_control_offset": 4,
        "asc_inbox0_offset": 8,
        "asc_inbox1_offset": 8,
        "asc_outbox0_offset": 8,
        "asc_outbox1_offset": 8,
    }
    asc_size = dict(
        (name, size) for name, _, size in mmio_subregions
    )["asc_mmio"]
    offsets = []
    for key, width in register_widths.items():
        offset = lifecycle_values[key]
        if offset + width > asc_size:
            raise G2ContractError(f"{key} is outside asc_mmio")
        offsets.append(offset)
    if len(set(offsets)) != len(offsets):
        raise G2ContractError("ASC lifecycle register offsets must be unique")

    return G2Contract(
        contract_version=2,
        platform="J313",
        acpi_hid="APPL0002",
        source_contract_sha256=source_hash,
        acpi_mmio=acpi_mmio,
        mmio_subregions=mmio_subregions,
        synthetic_mmio=synthetic_mmio,
        firmware_regions=firmware_regions,
        interrupt_routes=routes,
        context_id=context_id,
        queue_index=queue_index,
        work_timeout_ms=timeout,
        page_size=g1.uat.page_size,
        address_bits=g1.uat.address_bits,
        num_contexts=g1.uat.num_contexts,
        firmware_generation=g1.firmware.generation,
        firmware_version=g1.firmware.version,
        firmware_lifecycle=FirmwareLifecycle(**lifecycle_values),
    )


def _validate_windows_binary_contract(contract):
    if 1 << UAT_PAGE_BITS != contract.page_size:
        raise G2ContractError("UAT page bits do not match the G1 page size")
    if contract.address_bits != 40:
        raise G2ContractError("UAT output address bits must be 40")
    if contract.num_contexts != 64:
        raise G2ContractError("UAT context count must be 64")
    if contract.firmware_generation != "G13":
        raise G2ContractError("initdata firmware generation must be G13")
    if contract.firmware_version != "V13_5":
        raise G2ContractError("initdata firmware version must be V13_5")

    contexts = {UAT_FIRMWARE_CONTEXT, UAT_QUALIFICATION_CONTEXT}
    contexts.update(range(UAT_RENDER_CONTEXTS[0], UAT_RENDER_CONTEXTS[1] + 1))
    if contexts != set(range(contract.num_contexts)):
        raise G2ContractError("UAT context classes must cover exactly 0..63")
    if any(count <= 0 or count & (count - 1) for _, count in UAT_LEVELS):
        raise G2ContractError("UAT level entry counts must be powers of two")
    if (tuple(name for name, _, _ in contract.firmware_regions) !=
            EXACT_FIRMWARE_REGIONS):
        raise G2ContractError("firmware regions must preserve the accepted G1R order")
    for name, base, size in contract.firmware_regions:
        if size == 0 or (base | size) & (contract.page_size - 1):
            raise G2ContractError(f"firmware region {name} must be page aligned")
    regions = {name: (base, size)
               for name, base, size in contract.firmware_regions}
    rtkit_base, rtkit_size = regions["rtkit_private"]
    if rtkit_size > (1 << 64) - rtkit_base:
        raise G2ContractError("rtkit_private end overflows the VA space")
    if any(size <= 0 for _, size in INITDATA_OBJECT_SIZES):
        raise G2ContractError("initdata object sizes must be positive")
    if (FWCTL_STATE_SIZE <= 0 or FWCTL_MESSAGE_SIZE <= 0 or
            FWCTL_RING_ENTRY_COUNT <= 0):
        raise G2ContractError("firmware-control sizes must be positive")
    if (CHANNEL_INFO_SIZE <= 0 or CHANNEL_INFO_COUNT <= 0 or
            CHANNEL_STATE_STRIDE <= 0):
        raise G2ContractError("channel-info geometry must be positive")
    if not 0 < CMD_QUEUE_CHANNEL_COUNT <= CHANNEL_INFO_COUNT:
        raise G2ContractError("command-channel count is invalid")
    if FWLOG_RING_COUNT <= 0:
        raise G2ContractError("firmware-log ring count must be positive")
    if any(message_size <= 0 or entry_count <= 0
           for _, message_size, entry_count in CHANNEL_RING_LAYOUT):
        raise G2ContractError("channel ring geometry must be positive")
    if any(size <= 0 for _, size in REGIONB_OBJECT_SIZES):
        raise G2ContractError("RegionB object sizes must be positive")


def render_windows_header(contract):
    _validate_windows_binary_contract(contract)
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
        f"#define J313_AGX_G2_UAT_INPUT_ADDRESS_BITS {UAT_INPUT_ADDRESS_BITS}u",
        f"#define J313_AGX_G2_UAT_OUTPUT_ADDRESS_BITS {contract.address_bits}u",
        f"#define J313_AGX_G2_UAT_PAGE_BITS {UAT_PAGE_BITS}u",
        f"#define J313_AGX_G2_UAT_LEVEL_COUNT {len(UAT_LEVELS)}u",
        *(f"#define J313_AGX_G2_UAT_LEVEL{index}_SHIFT {shift}u"
          for index, (shift, _) in enumerate(UAT_LEVELS)),
        *(f"#define J313_AGX_G2_UAT_LEVEL{index}_ENTRIES {entries}u"
          for index, (_, entries) in enumerate(UAT_LEVELS)),
        f"#define J313_AGX_G2_UAT_CONTEXT_COUNT {contract.num_contexts}u",
        f"#define J313_AGX_G2_UAT_FIRMWARE_CONTEXT {UAT_FIRMWARE_CONTEXT}u",
        f"#define J313_AGX_G2_UAT_RENDER_CONTEXT_MIN {UAT_RENDER_CONTEXTS[0]}u",
        f"#define J313_AGX_G2_UAT_RENDER_CONTEXT_MAX {UAT_RENDER_CONTEXTS[1]}u",
        ("#define J313_AGX_G2_UAT_QUALIFICATION_CONTEXT "
         f"{UAT_QUALIFICATION_CONTEXT}u"),
        f"#define J313_AGX_G2_INITDATA_SIZE 0x{INITDATA_SIZE:x}u",
        *(f"#define J313_AGX_G2_INITDATA_VERSION_WORD{index} 0x{word:x}u"
          for index, word in enumerate(INITDATA_VERSION_WORDS)),
        *(f"#define J313_AGX_G2_INITDATA_{name}_SIZE 0x{size:x}u"
          for name, size in INITDATA_OBJECT_SIZES),
        f"#define J313_AGX_G2_FWCTL_STATE_SIZE 0x{FWCTL_STATE_SIZE:x}u",
        f"#define J313_AGX_G2_FWCTL_MESSAGE_SIZE 0x{FWCTL_MESSAGE_SIZE:x}u",
        ("#define J313_AGX_G2_FWCTL_RING_ENTRY_COUNT "
         f"0x{FWCTL_RING_ENTRY_COUNT:x}u"),
        ("#define J313_AGX_G2_FWCTL_RING_SIZE "
         f"0x{FWCTL_MESSAGE_SIZE * FWCTL_RING_ENTRY_COUNT:x}u"),
        f"#define J313_AGX_G2_CHANNEL_INFO_SIZE 0x{CHANNEL_INFO_SIZE:x}u",
        f"#define J313_AGX_G2_CHANNEL_INFO_COUNT 0x{CHANNEL_INFO_COUNT:x}u",
        ("#define J313_AGX_G2_CHANNEL_INFO_SET_SIZE "
         f"0x{CHANNEL_INFO_SIZE * CHANNEL_INFO_COUNT:x}u"),
        ("#define J313_AGX_G2_CHANNEL_STATE_STRIDE "
         f"0x{CHANNEL_STATE_STRIDE:x}u"),
        ("#define J313_AGX_G2_CMD_QUEUE_CHANNEL_COUNT "
         f"0x{CMD_QUEUE_CHANNEL_COUNT:x}u"),
        *(f"#define J313_AGX_G2_{name}_RING_SIZE "
          f"0x{message_size * entry_count:x}u"
          for name, message_size, entry_count in CHANNEL_RING_LAYOUT),
        f"#define J313_AGX_G2_FWLOG_RING_COUNT 0x{FWLOG_RING_COUNT:x}u",
        ("#define J313_AGX_G2_FWLOG_STATE_SIZE "
         f"0x{CHANNEL_STATE_STRIDE * FWLOG_RING_COUNT:x}u"),
        ("#define J313_AGX_G2_FWLOG_DUMMY_RING_SIZE "
         f"0x{FWLOG_DUMMY_RING_SIZE:x}u"),
        *(f"#define J313_AGX_G2_REGIONB_{name}_SIZE 0x{size:x}u"
          for name, size in REGIONB_OBJECT_SIZES),
        "",
    ]
    for name, base, size in (contract.acpi_mmio + contract.mmio_subregions +
                             contract.synthetic_mmio +
                             contract.firmware_regions):
        macro = name.upper()
        lines.extend([
            f"#define J313_AGX_G2_{macro}_BASE 0x{base:x}ULL",
            f"#define J313_AGX_G2_{macro}_SIZE 0x{size:x}ULL",
        ])
    firmware_regions = {
        name: (base, size) for name, base, size in contract.firmware_regions
    }
    rtkit_base, rtkit_size = firmware_regions["rtkit_private"]
    lines.append(
        f"#define J313_AGX_G2_KERNEL_VA_BASE 0x{rtkit_base + rtkit_size:x}ULL"
    )
    lines.append("")
    lifecycle = contract.firmware_lifecycle
    lines.extend(
        f"#define J313_AGX_G2_{FIRMWARE_LIFECYCLE_HEADER_NAMES[key]} "
        f"0x{getattr(lifecycle, key):x}u"
        if not key.endswith("_timeout_ms") else
        f"#define J313_AGX_G2_{FIRMWARE_LIFECYCLE_HEADER_NAMES[key]} "
        f"{getattr(lifecycle, key)}u"
        for key in FIRMWARE_LIFECYCLE_EXACT
    )
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
