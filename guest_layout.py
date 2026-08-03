"""Canonical J313 Windows guest layout and generated build contracts."""

from dataclasses import dataclass, fields
import json
from pathlib import Path
import re


ALIGNMENT = 0x4000
DEFAULT_LAYOUT_PATH = Path(__file__).resolve().parent / "config/j313-guest-layout.json"


@dataclass(frozen=True)
class GuestLayout:
    layout_version: int
    phys_base: int
    ram_end: int
    firmware_base: int
    firmware_max_size: int
    adt_base: int
    adt_max_size: int
    boot_args_base: int
    boot_args_size: int
    ramdisk_base: int
    ramdisk_max_size: int
    low_mem_ipa: int
    low_mem_pa: int
    low_mem_size: int
    virtual_fb_base: int
    virtual_fb_width: int
    virtual_fb_height: int
    virtual_fb_stride: int
    pci_ecam: int
    pci_bar_window: int
    nvme_vintid: int
    xhci_base: int
    cpu_count: int

    @property
    def virtual_fb_size(self) -> int:
        return self.virtual_fb_stride * self.virtual_fb_height

    def validate(self) -> None:
        if self.layout_version != 1:
            raise ValueError(f"unsupported layout version {self.layout_version}")
        if self.cpu_count != 8:
            raise ValueError(f"J313 guest requires 8 CPUs, got {self.cpu_count}")
        if self.phys_base >= self.ram_end:
            raise ValueError("guest RAM range is empty")
        if self.virtual_fb_stride < self.virtual_fb_width * 4:
            raise ValueError("virtual framebuffer stride is too small")

        aligned = {
            "phys_base": self.phys_base,
            "ram_end": self.ram_end,
            "firmware_base": self.firmware_base,
            "adt_base": self.adt_base,
            "boot_args_base": self.boot_args_base,
            "ramdisk_base": self.ramdisk_base,
            "low_mem_pa": self.low_mem_pa,
            "virtual_fb_base": self.virtual_fb_base,
            "pci_ecam": self.pci_ecam,
            "pci_bar_window": self.pci_bar_window,
            "xhci_base": self.xhci_base,
        }
        for name, value in aligned.items():
            if value % ALIGNMENT:
                raise ValueError(f"{name} is not {ALIGNMENT:#x}-aligned")

        ranges = {
            "ADT": (self.adt_base, self.adt_base + self.adt_max_size),
            "firmware": (
                self.firmware_base,
                self.firmware_base + self.firmware_max_size,
            ),
            "boot args": (
                self.boot_args_base,
                self.boot_args_base + self.boot_args_size,
            ),
            "virtual framebuffer": (
                self.virtual_fb_base,
                self.virtual_fb_base + self.virtual_fb_size,
            ),
            "RAM disk": (
                self.ramdisk_base,
                self.ramdisk_base + self.ramdisk_max_size,
            ),
            "low-memory backing": (
                self.low_mem_pa,
                self.low_mem_pa + self.low_mem_size,
            ),
        }
        for name, (start, end) in ranges.items():
            if start < self.phys_base or end > self.ram_end or start >= end:
                raise ValueError(f"{name} is outside guest RAM")

        items = list(ranges.items())
        for index, (left_name, (left_start, left_end)) in enumerate(items):
            for right_name, (right_start, right_end) in items[index + 1 :]:
                if left_start < right_end and right_start < left_end:
                    raise ValueError(f"{left_name} overlap with {right_name}")

        if self.low_mem_ipa < 0x1000:
            raise ValueError("low-memory IPA must preserve a null guard page")


def _decode_int(value: object, name: str) -> int:
    if isinstance(value, bool):
        raise ValueError(f"{name} must be an integer")
    if isinstance(value, int):
        return value
    if isinstance(value, str):
        try:
            return int(value, 0)
        except ValueError as exc:
            raise ValueError(f"{name} is not an integer: {value!r}") from exc
    raise ValueError(f"{name} must be an integer")


def load_layout(path: Path) -> GuestLayout:
    raw = json.loads(path.read_text())
    names = {field.name for field in fields(GuestLayout)}
    missing = names - raw.keys()
    extra = raw.keys() - names
    if missing or extra:
        raise ValueError(
            f"layout keys differ: missing={sorted(missing)}, extra={sorted(extra)}"
        )
    layout = GuestLayout(**{name: _decode_int(raw[name], name) for name in names})
    layout.validate()
    return layout


def validate_fdf(layout: GuestLayout, source: str) -> None:
    base_match = re.search(r"^BaseAddress\s*=\s*(0x[0-9a-fA-F]+)\|", source, re.MULTILINE)
    size_match = re.search(r"^Size\s*=\s*(0x[0-9a-fA-F]+)\|", source, re.MULTILINE)
    if base_match is None or size_match is None:
        raise ValueError("FDF firmware base or size declaration is missing")
    base = int(base_match.group(1), 0)
    size = int(size_match.group(1), 0)
    if base != layout.firmware_base:
        raise ValueError(
            f"FDF firmware base {base:#x} differs from layout {layout.firmware_base:#x}"
        )
    if size != layout.firmware_max_size:
        raise ValueError(
            f"FDF firmware size {size:#x} differs from layout {layout.firmware_max_size:#x}"
        )


def render_c(layout: GuestLayout) -> str:
    values = [
        ("layout_version", "u32"),
        ("cpu_count", "u32"),
        ("phys_base", "u64"),
        ("ram_end", "u64"),
        ("firmware_base", "u64"),
        ("firmware_max_size", "u64"),
        ("adt_base", "u64"),
        ("adt_max_size", "u64"),
        ("boot_args_base", "u64"),
        ("boot_args_size", "u64"),
        ("ramdisk_base", "u64"),
        ("ramdisk_max_size", "u64"),
        ("low_mem_ipa", "u64"),
        ("low_mem_pa", "u64"),
        ("low_mem_size", "u64"),
        ("virtual_fb_base", "u64"),
        ("virtual_fb_width", "u32"),
        ("virtual_fb_height", "u32"),
        ("virtual_fb_stride", "u32"),
        ("pci_ecam", "u64"),
        ("pci_bar_window", "u64"),
        ("nvme_vintid", "u32"),
        ("xhci_base", "u64"),
    ]
    members = "\n".join(f"    {kind} {name};" for name, kind in values)
    initializers = "\n".join(
        f"    .{name} = 0x{getattr(layout, name):x}{'u' if kind == 'u32' else 'ull'},"
        for name, kind in values
    )
    return f"""/* Generated by tools/generate_guest_layout.py. Do not edit. */
#ifndef HV_AUTONOMOUS_LAYOUT_GENERATED_H
#define HV_AUTONOMOUS_LAYOUT_GENERATED_H

#include \"types.h\"

struct hv_autonomous_layout {{
{members}
}};

static const struct hv_autonomous_layout J313_AUTONOMOUS_LAYOUT = {{
{initializers}
}};

#endif
"""


def render_dsc(layout: GuestLayout) -> str:
    return f"""# Generated by tools/generate_guest_layout.py. Do not edit.
[PcdsFixedAtBuild.common]
  # Mu is built for the pinned guest layout shared by the assisted and autonomous
  # hypervisor paths. Moving one of these regions independently makes early PrePi
  # dereference an unmapped or unrelated guest address.
  gAppleSiliconPkgTokenSpaceGuid.PcdBootArgsPointer|0x{layout.boot_args_base:x}
  gAppleSiliconPkgTokenSpaceGuid.PcdAdtPointer|0x{layout.adt_base:x}
  # A preloaded development RAM disk is carved out before DXE allocates guest RAM.
  gAppleSiliconPkgTokenSpaceGuid.PcdPreloadedRamdiskBase|0x{layout.ramdisk_base:x}
  gAppleSiliconPkgTokenSpaceGuid.PcdPreloadedRamdiskMaxSize|0x{layout.ramdisk_max_size:x}
  # Windows Boot Manager allocates low physical pages although Apple DRAM starts at
  # 0x800000000. m1n1 aliases this IPA window to the high backing range below.
  gAppleSiliconPkgTokenSpaceGuid.PcdLowMemoryWindowBase|0x{layout.low_mem_ipa:x}
  gAppleSiliconPkgTokenSpaceGuid.PcdLowMemoryWindowSize|0x{layout.low_mem_size:x}
  gAppleSiliconPkgTokenSpaceGuid.PcdLowMemoryWindowBackingBase|0x{layout.low_mem_pa:x}
"""


DEFAULT_LAYOUT = load_layout(DEFAULT_LAYOUT_PATH)
