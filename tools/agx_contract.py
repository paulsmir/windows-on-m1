"""Strict, immutable J313 AGX resource contract."""

from dataclasses import dataclass
import hashlib
import json
from pathlib import Path
import re
from types import MappingProxyType
from typing import Mapping


class ContractError(ValueError):
    """The AGX resource contract is malformed or unsupported."""


@dataclass(frozen=True)
class Region:
    base: int
    size: int


@dataclass(frozen=True)
class Source:
    root_commit: str
    m1n1_commit: str
    mu_commit: str
    adt_identity: str


@dataclass(frozen=True)
class Firmware:
    generation: str
    version: str


@dataclass(frozen=True)
class UatGeometry:
    page_size: int
    num_contexts: int
    address_bits: int


@dataclass(frozen=True)
class AgxContract:
    contract_version: int
    platform: str
    source: Source
    firmware: Firmware
    nodes: tuple[str, ...]
    regions: Mapping[str, Region]
    interrupts: tuple[int, ...]
    dependencies: tuple[str, ...]
    uat: UatGeometry


TOP_LEVEL = {
    "contract_version",
    "platform",
    "source",
    "firmware",
    "nodes",
    "regions",
    "interrupts",
    "dependencies",
    "uat",
}
REGION_KEYS = {"base", "size"}
REGION_NAMES = {
    "sgx_mmio",
    "asc_mmio",
    "rtkit_private",
    "gpu",
    "shared",
    "handoff",
}
SOURCE_KEYS = {"root_commit", "m1n1_commit", "mu_commit", "adt_identity"}
FIRMWARE_KEYS = {"generation", "version"}
UAT_KEYS = {"page_size", "num_contexts", "address_bits"}
COMMIT_RE = re.compile(r"[0-9a-f]{40}\Z")
ALIGNMENT = 0x4000
U64_LIMIT = 1 << 64


def _exact(value, keys, where):
    if not isinstance(value, dict) or set(value) != set(keys):
        raise ContractError(f"{where} keys must be exactly {sorted(keys)}")


def _integer(value, where, *, minimum=0, maximum=U64_LIMIT - 1):
    if isinstance(value, str):
        try:
            value = int(value, 0)
        except ValueError as exc:
            raise ContractError(f"{where} is not an integer") from exc
    if isinstance(value, bool) or not isinstance(value, int):
        raise ContractError(f"{where} is not an integer")
    if not minimum <= value <= maximum:
        raise ContractError(f"{where} is outside {minimum:#x}..{maximum:#x}")
    return value


def _nonempty_string(value, where):
    if not isinstance(value, str) or not value:
        raise ContractError(f"{where} must be a non-empty string")
    return value


def _path_list(value, where):
    if not isinstance(value, list) or not value:
        raise ContractError(f"{where} must be a non-empty list")
    result = tuple(_nonempty_string(item, f"{where} item") for item in value)
    if any(not item.startswith("/") for item in result):
        raise ContractError(f"{where} entries must be absolute ADT paths")
    if len(set(result)) != len(result):
        raise ContractError(f"{where} entries must be unique")
    return result


def _region(value, name):
    _exact(value, REGION_KEYS, f"regions.{name}")
    base = _integer(value["base"], f"regions.{name}.base", minimum=ALIGNMENT)
    size = _integer(value["size"], f"regions.{name}.size", minimum=ALIGNMENT)
    if base % ALIGNMENT or size % ALIGNMENT:
        raise ContractError(f"regions.{name} must be 16 KiB aligned")
    if base + size > U64_LIMIT:
        raise ContractError(f"regions.{name} wraps the 64-bit address space")
    return Region(base, size)


def _check_region_overlap(regions):
    ordered = sorted(regions.items(), key=lambda item: item[1].base)
    for (left_name, left), (right_name, right) in zip(ordered, ordered[1:]):
        if left.base + left.size > right.base:
            raise ContractError(
                f"regions {left_name} and {right_name} overlap"
            )


def _check_region_classes(regions):
    sgx = regions["sgx_mmio"]
    asc = regions["asc_mmio"]
    if asc.base < sgx.base or asc.base + asc.size > sgx.base + sgx.size:
        raise ContractError("regions.asc_mmio must be inside sgx_mmio")
    _check_region_overlap(
        {
            name: regions[name]
            for name in ("rtkit_private", "gpu", "shared", "handoff")
        }
    )


def validate_contract(data: dict) -> AgxContract:
    """Validate an untrusted dictionary and return its immutable form."""

    _exact(data, TOP_LEVEL, "contract")
    version = _integer(data["contract_version"], "contract_version", minimum=1,
                       maximum=1)
    if data["platform"] != "J313":
        raise ContractError("platform must be J313")

    source_data = data["source"]
    _exact(source_data, SOURCE_KEYS, "source")
    commits = {}
    for key in ("root_commit", "m1n1_commit", "mu_commit"):
        value = _nonempty_string(source_data[key], f"source.{key}")
        if not COMMIT_RE.fullmatch(value):
            raise ContractError(f"source.{key} must be 40 lowercase hex digits")
        commits[key] = value
    source = Source(**commits,
                    adt_identity=_nonempty_string(source_data["adt_identity"],
                                                  "source.adt_identity"))

    firmware_data = data["firmware"]
    _exact(firmware_data, FIRMWARE_KEYS, "firmware")
    firmware = Firmware(
        _nonempty_string(firmware_data["generation"], "firmware.generation"),
        _nonempty_string(firmware_data["version"], "firmware.version"),
    )

    region_data = data["regions"]
    _exact(region_data, REGION_NAMES, "regions")
    regions = {name: _region(region_data[name], name) for name in REGION_NAMES}
    _check_region_classes(regions)

    irq_data = data["interrupts"]
    if not isinstance(irq_data, list) or not irq_data:
        raise ContractError("interrupts must be a non-empty list")
    interrupts = tuple(
        _integer(value, "interrupt", minimum=32, maximum=1019)
        for value in irq_data
    )
    if len(set(interrupts)) != len(interrupts):
        raise ContractError("interrupts must be unique")

    uat_data = data["uat"]
    _exact(uat_data, UAT_KEYS, "uat")
    page_size = _integer(uat_data["page_size"], "uat.page_size",
                         minimum=ALIGNMENT, maximum=ALIGNMENT)
    uat = UatGeometry(
        page_size=page_size,
        num_contexts=_integer(uat_data["num_contexts"], "uat.num_contexts",
                              minimum=1, maximum=256),
        address_bits=_integer(uat_data["address_bits"], "uat.address_bits",
                              minimum=36, maximum=48),
    )

    return AgxContract(
        contract_version=version,
        platform="J313",
        source=source,
        firmware=firmware,
        nodes=_path_list(data["nodes"], "nodes"),
        regions=MappingProxyType(regions),
        interrupts=interrupts,
        dependencies=_path_list(data["dependencies"], "dependencies"),
        uat=uat,
    )


def contract_dict(contract: AgxContract) -> dict:
    """Return the JSON representation of a validated contract."""

    return {
        "contract_version": contract.contract_version,
        "platform": contract.platform,
        "source": {
            "root_commit": contract.source.root_commit,
            "m1n1_commit": contract.source.m1n1_commit,
            "mu_commit": contract.source.mu_commit,
            "adt_identity": contract.source.adt_identity,
        },
        "firmware": {
            "generation": contract.firmware.generation,
            "version": contract.firmware.version,
        },
        "nodes": list(contract.nodes),
        "regions": {
            name: {"base": region.base, "size": region.size}
            for name, region in sorted(contract.regions.items())
        },
        "interrupts": list(contract.interrupts),
        "dependencies": list(contract.dependencies),
        "uat": {
            "page_size": contract.uat.page_size,
            "num_contexts": contract.uat.num_contexts,
            "address_bits": contract.uat.address_bits,
        },
    }


def canonical_bytes(contract: AgxContract) -> bytes:
    return (json.dumps(contract_dict(contract), indent=2, sort_keys=True) + "\n").encode()


def contract_sha256(contract: AgxContract) -> str:
    return hashlib.sha256(canonical_bytes(contract)).hexdigest()


def load_contract(path: Path) -> AgxContract:
    try:
        data = json.loads(Path(path).read_text())
    except (OSError, json.JSONDecodeError) as exc:
        raise ContractError(str(exc)) from exc
    return validate_contract(data)
