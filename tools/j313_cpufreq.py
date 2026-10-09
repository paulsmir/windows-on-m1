"""J313 (T8103) P-cluster operating point for a guest without DVFS.

Windows has no Apple SoC CPU frequency driver, so the cores stay at the
P-states m1n1's cpufreq_init leaves (E-cluster 5 = 2064 MHz, its maximum;
P-cluster 7 = 1956 MHz). EXP1083 cpubench: every core ran a dependent madd
chain at ~1.5 ns per iteration (3 cycles at ~2 GHz), and DWM's composition
thread was CPU-bound (~77 % running). Asahi's t8103 P-cluster OPP table goes
to level 12 (2988 MHz); levels 13-15 are turbo-mode, "not available until CPU
deep sleep is implemented", so they are refused here.

The command register encoding follows m1n1 cpufreq.c set_pstate for T8103
(and Asahi apple-soc-cpufreq.c): DESIRED1 bits 4:0 and DESIRED2 bits 15:12
both carry the P-state, SET (bit 25) requests the transition, BUSY (bit 31)
clears when it is done.
"""

PCPU_PSTATE_REG = 0x211E00000 + 0x20020   # t8103_clusters PCPU base + CLUSTER_PSTATE
PSTATE_BUSY = 1 << 31
PSTATE_SET = 1 << 25
PSTATE_DESIRED2 = 0xF << 12
PSTATE_DESIRED1 = 0x1F
PCPU_MAX_NON_TURBO = 12
PCPU_MHZ = {1: 600, 2: 828, 3: 1056, 4: 1284, 5: 1500, 6: 1728, 7: 1956,
            8: 2184, 9: 2388, 10: 2592, 11: 2772, 12: 2988}


def pcpu_pstate_command(current, pstate):
    """Return the command register value that requests `pstate`."""
    if not isinstance(pstate, int) or not 1 <= pstate <= PCPU_MAX_NON_TURBO:
        raise ValueError(f"P-cluster P-state {pstate!r} is outside 1..{PCPU_MAX_NON_TURBO}")
    value = current & ~(PSTATE_DESIRED1 | PSTATE_DESIRED2 | PSTATE_BUSY)
    return value | PSTATE_SET | (pstate << 12) | pstate


def requested_pstate(value):
    return value & PSTATE_DESIRED1


def set_pcpu_pstate(proxy, pstate, polls=4000):
    """Request `pstate` through the m1n1 proxy and wait for BUSY to clear.
    Returns (before, after) register values."""
    before = proxy.read64(PCPU_PSTATE_REG)
    proxy.write64(PCPU_PSTATE_REG, pcpu_pstate_command(before, pstate))
    for _ in range(polls):
        after = proxy.read64(PCPU_PSTATE_REG)
        if not after & PSTATE_BUSY:
            break
    else:
        raise RuntimeError(f"P-cluster P-state switch to {pstate} timed out: 0x{after:x}")
    if requested_pstate(after) != pstate:
        raise RuntimeError(f"P-cluster P-state request not retained: 0x{after:x}")
    return before, after
