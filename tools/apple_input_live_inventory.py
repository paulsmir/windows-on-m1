#!/usr/bin/env python3
"""Capture J313 Apple SPI HID ADT metadata without any hardware writes."""

import argparse
import json
import os
from pathlib import Path
import sys


ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))
sys.path.insert(0, str(ROOT / "m1n1_windows" / "proxyclient"))

from tools.apple_input_contract import load_contract  # noqa: E402
from tools.apple_input_inventory import (first_reg, nodes_with_reg,
                                         select_startup_parent_irq)  # noqa: E402


CONTRACT = ROOT / "config" / "j313-apple-input.json"


def ensure_guest_inactive(root):
    """Refuse to attach m1n1.setup while a guest runner owns the proxy."""
    pid_path = Path(root) / "guest.pid"
    try:
        pid = int(pid_path.read_text().strip())
        os.kill(pid, 0)
    except (FileNotFoundError, ProcessLookupError, ValueError):
        return
    except PermissionError as exc:
        raise RuntimeError(f"cannot verify guest runner {pid}") from exc
    raise RuntimeError(f"guest runner {pid} is active; live inventory is unsafe")


def compatible(node):
    value = node.getprop("compatible", [])
    if isinstance(value, str):
        return [value]
    return [str(item) for item in value]


def json_value(value):
    if value is None or isinstance(value, (bool, int, float, str)):
        return value
    if isinstance(value, bytes):
        return {"bytes_hex": value.hex()}
    # m1n1 decodes ADT function-* properties as a Function container.  Iterating
    # that container yields field names, not values, so preserve the binding
    # explicitly; otherwise a capture cannot prove which GPIO/pin it references.
    if all(hasattr(value, field) for field in ("phandle", "name", "args")):
        return {
            "phandle": int(value.phandle),
            "name": str(value.name),
            "args": [int(item) for item in value.args],
        }
    if isinstance(value, (list, tuple)):
        return [json_value(item) for item in value]
    try:
        return [json_value(item) for item in value]
    except TypeError:
        return repr(value)


def node_record(node):
    record = {
        "path": node._path.removeprefix("/device-tree"),
        "compatible": compatible(node),
        "properties": {key: json_value(value) for key, value in sorted(node._properties.items())
                       if key != "name"},
    }
    try:
        record["reg"] = [[int(base), int(size)] for base, size in
                         (node.get_reg(index) for index in range(len(node.reg)))]
    except (AttributeError, KeyError, TypeError, ValueError, IndexError):
        record["reg"] = []
    return record


def capture():
    # Importing m1n1.setup opens and bootstraps the proxy immediately.  Delay it
    # until after the ownership guard so a diagnostic tool cannot interrupt a
    # running Windows guest.
    from m1n1.setup import u  # noqa: E402

    arm_io = u.adt["/arm-io"]
    all_nodes = list(arm_io.walk_tree())
    hid_nodes = [node for node in all_nodes if "hid-transport,spi" in compatible(node)]
    if len(hid_nodes) != 1:
        raise RuntimeError(f"expected one hid-transport,spi node, found {len(hid_nodes)}")
    hid = hid_nodes[0]
    spi = hid._parent
    contract = load_contract(CONTRACT)
    # Apple has changed this node's name across firmware generations.  The
    # reviewed MMIO range is the stable identity and is also what the launch
    # contract ultimately exposes to Windows.
    # J313's reviewed contract uses the canonical ADT path.  ``walk_tree`` in
    # m1n1 intentionally skips some interrupt-controller children, so searching
    # only its result can miss nub-gpio even though direct lookup succeeds.
    nub = u.adt["/arm-io/nub-gpio"]
    if first_reg(nub) != (contract.nub_gpio.base, contract.nub_gpio.size):
        raise RuntimeError("live nub-gpio range does not match the reviewed contract")
    ap_gpio = u.adt["/arm-io/gpio0"]
    if first_reg(ap_gpio) != (contract.ap_gpio.base, contract.ap_gpio.size):
        raise RuntimeError("live AP GPIO range does not match the reviewed contract")
    nub_base, nub_size = nub.get_reg(0)
    interrupts = [int(value) for value in getattr(nub, "interrupts", [])]
    pin_address = int(nub_base) + contract.nub_gpio.pin * 4
    pin_register = int(u.proxy.read32(pin_address))
    parent_irq, startup_group, observed_group = select_startup_parent_irq(
        interrupts, pin_register, contract.interrupt.parent_candidates)
    related = [spi, hid, ap_gpio, nub]
    return {
        "format_version": 1,
        "platform": str(u.adt["/chosen"].getprop("target-type", "unknown")),
        "interrupt_selection": {
            "nub_gpio_pin": contract.nub_gpio.pin,
            "pin_register": pin_register,
            "observed_group_before_startup": observed_group,
            "programmed_startup_group": startup_group,
            "parent_interrupts": interrupts,
            "selected_parent_irq": parent_irq,
            "guest_vintid": contract.interrupt.guest_vintid,
        },
        "nodes": [node_record(node) for node in related],
    }


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    ensure_guest_inactive(ROOT)
    data = capture()
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(data, indent=2, sort_keys=True) + "\n")
    print(args.output)


if __name__ == "__main__":
    main()
