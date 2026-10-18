"""M1: the launcher's side of the preloaded-ramdisk contract.

Only the launcher knows whether it preloaded a boot medium, so it tells both
consumers:
- Mu (PEI, MMU off) reads the header page at the window base: the header from
  `header()` means "reserve the window and register the payload"; anything
  else, including the cleared page from `cleared_header()`, gives the window
  to the OS (mu/.../Include/Library/PreloadedRamdisk.h);
- m1n1 (EL2) receives the window as a launch-descriptor region of kind
  HV_CONTRACT_REGION_RAMDISK and keeps GPU DMA out of it; without that region
  the window is ordinary guest RAM (m1n1_windows/src/hv_launch_contract.h).
"""
from __future__ import annotations

MAGIC = b"ASIRAMDK"
PAYLOAD_OFFSET = 0x1000
# enum hv_contract_region_kind: HV_CONTRACT_REGION_RAMDISK
REGION_RAMDISK = 8


def header(payload_size: int) -> bytes:
    """The header page Mu's PreloadedRamdiskHeaderValid accepts."""
    if payload_size <= 0:
        raise ValueError("a preloaded ramdisk needs a non-empty payload")
    return (MAGIC + payload_size.to_bytes(8, "little")).ljust(PAYLOAD_OFFSET, b"\0")


def cleared_header() -> bytes:
    """The header page for a boot without a preloaded medium."""
    return bytes(PAYLOAD_OFFSET)


def check_fits(payload_size: int, base: int, max_size: int, ram_base: int, ram_end: int) -> None:
    """Raise ValueError unless the payload and the window fit guest RAM."""
    room = max_size - PAYLOAD_OFFSET
    if payload_size <= 0 or payload_size > room:
        raise ValueError(f"image size {payload_size} does not fit in the {room}-byte window "
                         f"(increase PcdPreloadedRamdiskMaxSize and --ramdisk-max-size)")
    if not (ram_base <= base and base + max_size <= ram_end):
        raise ValueError(f"window 0x{base:x}+0x{max_size:x} is outside "
                         f"guest RAM 0x{ram_base:x}..0x{ram_end:x}")


def descriptor_region(base: int, max_size: int) -> tuple[int, int, int, int]:
    """The launch-descriptor region (kind, flags, base, size) for a preloaded medium."""
    return (REGION_RAMDISK, 0, base, max_size)
