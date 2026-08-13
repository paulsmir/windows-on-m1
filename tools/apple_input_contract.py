"""Strict, immutable J313 Apple SPI input resource contract."""

from dataclasses import dataclass
import json
from pathlib import Path


class ContractError(ValueError):
    pass


@dataclass(frozen=True)
class SpiResource:
    base: int
    size: int
    source_hz: int
    bus_hz: int


@dataclass(frozen=True)
class GpioResource:
    base: int
    size: int
    pin: int


@dataclass(frozen=True)
class InterruptResource:
    active_low: bool
    parent_candidates: tuple[int, ...]
    startup_group: int
    startup_parent: int
    guest_vintid: int


@dataclass(frozen=True)
class TimingResource:
    reset_high: int
    reset_low: int
    boot_wait: int
    cs_setup: int
    cs_hold: int
    cs_inactive: int
    transfer_timeout: int


@dataclass(frozen=True)
class AppleInputContract:
    contract_version: int
    acpi_hid: str
    spi: SpiResource
    ap_gpio: GpioResource
    nub_gpio: GpioResource
    interrupt: InterruptResource
    timings_us: TimingResource


def _exact(obj, keys, where):
    if not isinstance(obj, dict) or set(obj) != set(keys):
        raise ContractError(f"{where} keys must be exactly {sorted(keys)}")


def _integer(value, where, *, minimum=0, maximum=(1 << 64) - 1):
    if isinstance(value, str):
        try:
            value = int(value, 0)
        except ValueError as exc:
            raise ContractError(f"{where} is not an integer") from exc
    if isinstance(value, bool) or not isinstance(value, int) or not minimum <= value <= maximum:
        raise ContractError(f"{where} is outside {minimum:#x}..{maximum:#x}")
    return value


def _region(obj, where, *, with_pin=False):
    keys = {"base", "size", "pin"} if with_pin else {"base", "size", "source_hz", "bus_hz"}
    _exact(obj, keys, where)
    base = _integer(obj["base"], f"{where}.base", minimum=0x200000000, maximum=0x2FFFFFFFF)
    size = _integer(obj["size"], f"{where}.size", minimum=0x1000, maximum=0x1000000)
    if base + size > 0x300000000 or base & 0xFFF or size & 0xFFF:
        raise ContractError(f"{where} must be page aligned inside arm-io")
    if with_pin:
        return GpioResource(base, size, _integer(obj["pin"], f"{where}.pin", maximum=1023))
    return SpiResource(base, size,
                       _integer(obj["source_hz"], f"{where}.source_hz", minimum=1),
                       _integer(obj["bus_hz"], f"{where}.bus_hz", minimum=1))


def load_contract(path: Path) -> AppleInputContract:
    try:
        data = json.loads(Path(path).read_text())
    except (OSError, json.JSONDecodeError) as exc:
        raise ContractError(str(exc)) from exc
    _exact(data, {"contract_version", "acpi_hid", "spi", "ap_gpio", "nub_gpio",
                  "interrupt", "timings_us"}, "contract")

    spi = _region(data["spi"], "spi")
    ap_gpio = _region(data["ap_gpio"], "ap_gpio", with_pin=True)
    nub_gpio = _region(data["nub_gpio"], "nub_gpio", with_pin=True)
    if spi.bus_hz > spi.source_hz:
        raise ContractError("SPI bus clock exceeds source clock")
    if ap_gpio.pin >= 212 or nub_gpio.pin >= 32:
        raise ContractError("GPIO pin exceeds the supported J313 controller")

    irq = data["interrupt"]
    _exact(irq, {"active_low", "parent_candidates", "startup_group",
                 "startup_parent", "guest_vintid"}, "interrupt")
    if type(irq["active_low"]) is not bool:
        raise ContractError("interrupt.active_low must be boolean")
    parents = tuple(_integer(v, "interrupt.parent", minimum=32, maximum=1019)
                    for v in irq["parent_candidates"])
    if len(set(parents)) != len(parents) or not parents:
        raise ContractError("interrupt parent candidates must be non-empty and unique")
    startup_group = _integer(irq["startup_group"], "interrupt.startup_group",
                             maximum=len(parents) - 1)
    startup_parent = _integer(irq["startup_parent"], "interrupt.startup_parent",
                              minimum=32, maximum=1019)
    if parents[startup_group] != startup_parent:
        raise ContractError("interrupt startup group does not select startup parent")
    guest = _integer(irq["guest_vintid"], "interrupt.guest_vintid", minimum=32, maximum=1019)

    timing = data["timings_us"]
    timing_keys = {"reset_high", "reset_low", "boot_wait", "cs_setup", "cs_hold",
                   "cs_inactive", "transfer_timeout"}
    _exact(timing, timing_keys, "timings_us")
    values = {key: _integer(timing[key], f"timings_us.{key}", minimum=1, maximum=1_000_000)
              for key in timing_keys}

    version = _integer(data["contract_version"], "contract_version", minimum=1, maximum=1)
    if data["acpi_hid"] != "APPL0001":
        raise ContractError("unsupported ACPI HID")
    return AppleInputContract(version, data["acpi_hid"], spi, ap_gpio, nub_gpio,
                              InterruptResource(irq["active_low"], parents, startup_group,
                                                startup_parent, guest),
                              TimingResource(**values))
