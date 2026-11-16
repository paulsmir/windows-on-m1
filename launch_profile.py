"""Canonical Windows launch-profile values shared by public host tools."""

from __future__ import annotations

from dataclasses import dataclass, replace
from enum import Enum


class Execution(str, Enum):
    STANDALONE = "standalone"
    ASSISTED = "assisted"


class Display(str, Enum):
    NONE = "none"
    PHYSICAL = "physical"
    VIRTUAL = "virtual"
    BOTH = "both"


class Debug(str, Enum):
    OFF = "off"
    UART = "uart"
    FULL = "full"
    MONITOR = "monitor"


_DISPLAY_FLAGS = {
    Display.NONE: 0x0,
    Display.PHYSICAL: 0x1,
    Display.VIRTUAL: 0x2,
    Display.BOTH: 0x3,
}
_DEBUG_FLAGS = {
    Debug.OFF: 0x0,
    Debug.UART: 0x4,
    Debug.FULL: 0x8,
    Debug.MONITOR: 0x10,
}
# Working-profile knobs (m1n1 hv_autonomous_manifest.h, 2026-11-17): each bit is
# the standalone equivalent of one host-side knob of the assisted bench launch.
ANS_DIRECT_FLAG = 0x20        # --storage-mode ANS_NATIVE --ans-direct
AGX_G2_BROKER_FLAG = 0x40     # WOM1_AGX_G2_POWER_BROKER=1
CPPC_FLAG = 0x80              # WOM1_CPPC=1
EXPANDED_RAM_FLAG = 0x100     # --guest-ram-profile expanded
PCPU_PSTATE_SHIFT = 12        # WOM1_PCPU_PSTATE (0 = cpufreq default)
PCPU_PSTATE_MASK = 0xF000
PCPU_PSTATE_MAX = 12          # 13..15 are turbo states (tools/j313_cpufreq.py)
_KNOWN_FLAGS = 0xF1FF


@dataclass(frozen=True)
class LaunchProfile:
    execution: Execution
    display: Display
    debug: Debug
    ans_direct: bool = False
    agx_g2_broker: bool = False
    cppc: bool = False
    expanded_ram: bool = False
    pcpu_pstate: int = 0

    def __post_init__(self) -> None:
        if not isinstance(self.pcpu_pstate, int) or not 0 <= self.pcpu_pstate <= PCPU_PSTATE_MAX:
            raise ValueError(f"P-cluster P-state {self.pcpu_pstate!r} is outside 0..{PCPU_PSTATE_MAX}")

    @property
    def physical_display(self) -> bool:
        return self.display in (Display.PHYSICAL, Display.BOTH)

    @property
    def virtual_display(self) -> bool:
        return self.display in (Display.VIRTUAL, Display.BOTH)

    @property
    def capture_uart(self) -> bool:
        return self.debug in (Debug.UART, Debug.FULL, Debug.MONITOR)

    @property
    def telemetry(self) -> bool:
        # FULL additionally enables verbose synchronous tracing.  MONITOR uses
        # only the bounded lock-free sample stream, but the sole host proxy
        # owner must still register its asynchronous event handler.
        return self.debug in (Debug.FULL, Debug.MONITOR)

    @property
    def proxy_takeover(self) -> bool:
        return self.debug in (Debug.UART, Debug.FULL)

    @property
    def manifest_flags(self) -> int:
        return (
            _DISPLAY_FLAGS[self.display]
            | _DEBUG_FLAGS[self.debug]
            | (ANS_DIRECT_FLAG if self.ans_direct else 0)
            | (AGX_G2_BROKER_FLAG if self.agx_g2_broker else 0)
            | (CPPC_FLAG if self.cppc else 0)
            | (EXPANDED_RAM_FLAG if self.expanded_ram else 0)
            | (self.pcpu_pstate << PCPU_PSTATE_SHIFT)
        )


def parse_profile(
    execution: str | Execution = Execution.STANDALONE,
    display: str | Display = Display.PHYSICAL,
    debug: str | Debug = Debug.OFF,
) -> LaunchProfile:
    """Validate strings at the CLI boundary and return a typed profile."""
    return LaunchProfile(Execution(execution), Display(display), Debug(debug))


def working_profile(profile: LaunchProfile) -> LaunchProfile:
    """Add the bench working-profile knobs (EXP2481 launcher full-owner2481.sh).

    --storage-mode ANS_NATIVE --ans-direct, WOM1_AGX_G2_POWER_BROKER=1, WOM1_CPPC=1,
    --guest-ram-profile expanded and WOM1_PCPU_PSTATE=12; WOM1_DRAMCFG_FOLLOW_PSTATE
    is the default rule in both launch paths and has no bit of its own.
    """
    return replace(profile, ans_direct=True, agx_g2_broker=True, cppc=True,
                   expanded_ram=True, pcpu_pstate=12)


def profile_from_manifest_flags(flags: int) -> LaunchProfile:
    """Decode the standalone manifest flag fields and reject ambiguity."""
    if flags < 0 or flags & ~_KNOWN_FLAGS:
        raise ValueError(f"unsupported launch-profile flags {flags:#x}")
    pstate = (flags & PCPU_PSTATE_MASK) >> PCPU_PSTATE_SHIFT
    if pstate > PCPU_PSTATE_MAX:
        raise ValueError(f"unsupported P-cluster P-state {pstate} in flags {flags:#x}")

    display_bits = flags & 0x3
    debug_bits = flags & 0x1C
    display = next(value for value, bits in _DISPLAY_FLAGS.items() if bits == display_bits)
    try:
        debug = next(value for value, bits in _DEBUG_FLAGS.items() if bits == debug_bits)
    except StopIteration as exc:
        raise ValueError(f"unsupported debug flags {debug_bits:#x}") from exc
    return LaunchProfile(
        Execution.STANDALONE,
        display,
        debug,
        ans_direct=bool(flags & ANS_DIRECT_FLAG),
        agx_g2_broker=bool(flags & AGX_G2_BROKER_FLAG),
        cppc=bool(flags & CPPC_FLAG),
        expanded_ram=bool(flags & EXPANDED_RAM_FLAG),
        pcpu_pstate=pstate,
    )
