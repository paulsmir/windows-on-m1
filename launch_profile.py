"""Canonical Windows launch-profile values shared by public host tools."""

from dataclasses import dataclass
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
}
_KNOWN_FLAGS = 0xF


@dataclass(frozen=True)
class LaunchProfile:
    execution: Execution
    display: Display
    debug: Debug

    @property
    def physical_display(self) -> bool:
        return self.display in (Display.PHYSICAL, Display.BOTH)

    @property
    def virtual_display(self) -> bool:
        return self.display in (Display.VIRTUAL, Display.BOTH)

    @property
    def capture_uart(self) -> bool:
        return self.debug in (Debug.UART, Debug.FULL)

    @property
    def telemetry(self) -> bool:
        return self.debug is Debug.FULL

    @property
    def manifest_flags(self) -> int:
        return _DISPLAY_FLAGS[self.display] | _DEBUG_FLAGS[self.debug]


def parse_profile(
    execution: str | Execution = Execution.STANDALONE,
    display: str | Display = Display.PHYSICAL,
    debug: str | Debug = Debug.OFF,
) -> LaunchProfile:
    """Validate strings at the CLI boundary and return a typed profile."""
    return LaunchProfile(Execution(execution), Display(display), Debug(debug))


def profile_from_manifest_flags(flags: int) -> LaunchProfile:
    """Decode the standalone display/debug flag fields and reject ambiguity."""
    if flags < 0 or flags & ~_KNOWN_FLAGS:
        raise ValueError(f"unsupported launch-profile flags {flags:#x}")

    display_bits = flags & 0x3
    debug_bits = flags & 0xC
    display = next(value for value, bits in _DISPLAY_FLAGS.items() if bits == display_bits)
    try:
        debug = next(value for value, bits in _DEBUG_FLAGS.items() if bits == debug_bits)
    except StopIteration as exc:
        raise ValueError(f"unsupported debug flags {debug_bits:#x}") from exc
    return LaunchProfile(Execution.STANDALONE, display, debug)
