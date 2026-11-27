# SPDX-License-Identifier: MIT
"""B20 stage 1 host side: opt-in /chosen placeholders for the m1n1 hardware RTC anchor and their refresh into the
guest ADT after pcie_init. Enabled only by M1N1_RTC_PLACEHOLDERS=1; otherwise every function is a no-op, so the
launch artefacts are byte-identical to a launch without this module (tests/test_rtc_anchor_host.py)."""
import os
from construct import Int64ul

RTC_PROPS = ("m1n1-rtc-utc-ms", "m1n1-rtc-utc-cntpct", "m1n1-rtc-utc-cntfrq")


def enabled(env=None):
    return (os.environ if env is None else env).get("M1N1_RTC_PLACEHOLDERS", "0") == "1"


def _set_u64(chosen, name, value):
    chosen._properties[name] = value
    chosen._types[name] = (Int64ul, False)


def add_placeholders(chosen, env=None):
    """chainload.py: create the three 8-byte zero properties m1n1 (RTC_ANCHOR) fills; m1n1 cannot add properties."""
    if not enabled(env):
        return False
    for n in RTC_PROPS:
        _set_u64(chosen, n, 0)
    return True


def refresh(guest_adt, live_adt, env=None):
    """HV.init after the wireless-profile pcie_init: the guest ADT was fetched before m1n1 filled the anchor, so copy
    the three values from the live ADT. Returns the copied values (None when disabled or absent)."""
    if not enabled(env):
        return None
    src = live_adt["chosen"]._properties
    if not all(n in src for n in RTC_PROPS):
        print("RTC: no placeholders in the live ADT; guest ADT unchanged")
        return None
    vals = {n: int(src[n]) for n in RTC_PROPS}
    for n, v in vals.items():
        _set_u64(guest_adt["chosen"], n, v)
    print("RTC: guest /chosen refreshed after pcie_init: " + " ".join(f"{n}={v:#x}" for n, v in vals.items()))
    return vals
