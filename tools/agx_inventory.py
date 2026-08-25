"""Pure conversion of a captured ADT inventory into an AGX contract."""

from tools.agx_contract import ContractError, validate_contract


RAW_KEYS = {
    "format_version",
    "platform",
    "adt_identity",
    "firmware",
    "uat",
    "nodes",
    "dependencies",
}
NODE_KEYS = {"reg", "interrupts", "properties"}
REQUIRED_PATHS = ("/arm-io/sgx", "/arm-io/gfx-asc")
SGX_PROPERTIES = {
    "rtkit_private": (
        "rtkit-private-vm-region-base",
        "rtkit-private-vm-region-size",
    ),
    "gpu": ("gpu-region-base", "gpu-region-size"),
    "shared": ("gfx-shared-region-base", "gfx-shared-region-size"),
    "handoff": ("gfx-handoff-base", "gfx-handoff-size"),
}


def required_paths() -> tuple[str, ...]:
    """Return the ADT paths required to construct the AGX contract."""

    return REQUIRED_PATHS


def _exact_dict(value, keys, where):
    if not isinstance(value, dict) or set(value) != set(keys):
        raise ContractError(f"{where} keys must be exactly {sorted(keys)}")


def _node(nodes, path):
    if path not in nodes:
        raise ContractError(f"required ADT node {path} is missing")
    node = nodes[path]
    _exact_dict(node, NODE_KEYS, f"node {path}")
    return node


def _single_reg(node, path):
    registers = node["reg"]
    if (
        not isinstance(registers, list)
        or len(registers) != 1
        or not isinstance(registers[0], list)
        or len(registers[0]) != 2
    ):
        raise ContractError(f"node {path} must contain exactly one reg tuple")
    return {"base": registers[0][0], "size": registers[0][1]}


def _property(properties, name):
    if name not in properties:
        raise ContractError(f"required ADT property {name} is missing")
    return properties[name]


def extract_contract(raw: dict, source: dict):
    """Validate and convert an untrusted raw ADT record without hardware I/O."""

    _exact_dict(raw, RAW_KEYS, "raw inventory")
    if raw["format_version"] != 1:
        raise ContractError("raw inventory format_version must be 1")
    if not isinstance(raw["nodes"], dict) or not raw["nodes"]:
        raise ContractError("raw inventory nodes must be a non-empty mapping")

    nodes = raw["nodes"]
    sgx = _node(nodes, "/arm-io/sgx")
    asc = _node(nodes, "/arm-io/gfx-asc")
    if not isinstance(sgx["properties"], dict):
        raise ContractError("node /arm-io/sgx properties must be a mapping")

    regions = {
        "sgx_mmio": _single_reg(sgx, "/arm-io/sgx"),
        "asc_mmio": _single_reg(asc, "/arm-io/gfx-asc"),
    }
    for region_name, (base_name, size_name) in SGX_PROPERTIES.items():
        regions[region_name] = {
            "base": _property(sgx["properties"], base_name),
            "size": _property(sgx["properties"], size_name),
        }

    contract = {
        "contract_version": 1,
        "platform": raw["platform"],
        "source": {
            **source,
            "adt_identity": raw["adt_identity"],
        },
        "firmware": raw["firmware"],
        "nodes": list(nodes),
        "regions": regions,
        "interrupts": sgx["interrupts"],
        "dependencies": raw["dependencies"],
        "uat": raw["uat"],
    }
    return validate_contract(contract)
