#!/usr/bin/env python3
"""Normalize a read-only J313 ADT capture for contract review."""

import argparse
import json
from pathlib import Path


def _number(value):
    return int(value, 0) if isinstance(value, str) else int(value)


def select_parent_irq(interrupts, pin_register, allowed_parents):
    """Resolve the Apple GPIO parent IRQ selected by PIN.GROUP[18:16]."""
    group = (_number(pin_register) >> 16) & 0x7
    if group >= len(interrupts):
        raise ValueError(f"GPIO interrupt group {group} has no parent IRQ")
    parent = _number(interrupts[group])
    if parent not in tuple(_number(value) for value in allowed_parents):
        raise ValueError(f"GPIO parent IRQ {parent} is outside the reviewed group")
    return parent


def select_startup_parent_irq(interrupts, pin_register, allowed_parents):
    """Select the parent used after Apple GPIO IRQ startup programs group 0.

    A pin left in IRQ-OFF mode may retain group 7 in its register.  That is not
    a valid parent index on J313.  The upstream Apple GPIO driver deliberately
    programs GROUP[18:16] to zero before enabling the interrupt, so consumers
    must route parent zero and treat the old group as diagnostic state only.
    """
    old_group = (_number(pin_register) >> 16) & 0x7
    parent = select_parent_irq(interrupts, 0, allowed_parents)
    return parent, 0, old_group


def first_reg(node):
    """Return a node's first translated ADT register range, or ``None``."""
    try:
        base, size = node.get_reg(0)
        return int(base), int(size)
    except (AttributeError, KeyError, TypeError, ValueError, IndexError):
        return None


def nodes_with_reg(nodes, base, size):
    """Find an ADT node by its immutable MMIO identity, not its firmware name."""
    expected = int(base), int(size)
    return [node for node in nodes if first_reg(node) == expected]


def extract_inventory(adt):
    spi = adt["spi"]
    device = adt["device"]
    ap_gpio = adt["ap_gpio"]
    nub_gpio = adt["nub_gpio"]
    parent = _number(device["interrupt_parent_irq"])
    if parent not in range(330, 337):
        raise ValueError("J313 nub GPIO parent IRQ is outside the reviewed group")
    if _number(device["ap_gpio_pin"]) >= _number(ap_gpio["pin_count"]):
        raise ValueError("AP GPIO pin is out of range")
    if _number(device["nub_gpio_pin"]) >= _number(nub_gpio["pin_count"]):
        raise ValueError("nub GPIO pin is out of range")
    return {
        "ap_gpio": {"base": _number(ap_gpio["base"]), "compatible": ap_gpio.get("compatible", ""),
                    "path": ap_gpio["path"], "pin": _number(device["ap_gpio_pin"]),
                    "pin_count": _number(ap_gpio["pin_count"]), "size": _number(ap_gpio["size"])},
        "device": {"bus_hz": _number(device["bus_hz"]), "compatible": device["compatible"],
                   "path": device["path"], "reg": _number(device["reg"])},
        "nub_gpio": {"base": _number(nub_gpio["base"]), "compatible": nub_gpio.get("compatible", ""),
                     "path": nub_gpio["path"], "pin": _number(device["nub_gpio_pin"]),
                     "pin_count": _number(nub_gpio["pin_count"]), "size": _number(nub_gpio["size"])},
        "selected_parent_irq": parent,
        "spi": {"base": _number(spi["base"]), "compatible": spi["compatible"],
                "path": spi["path"], "size": _number(spi["size"]),
                "source_hz": _number(spi["source_hz"])},
    }


def write_inventory(path, inventory):
    Path(path).write_text(json.dumps(inventory, indent=2, sort_keys=True) + "\n")


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("capture", type=Path, help="JSON emitted by the read-only ADT capture step")
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    inventory = extract_inventory(json.loads(args.capture.read_text()))
    write_inventory(args.output, inventory)


if __name__ == "__main__":
    main()
