"""Host requests to a running guest, issued from the launcher's interrupt path.

The m1n1 proxy is serviced only while the guest is stopped in the hypervisor,
so a request is delivered as SIGUSR1: the launcher interrupts the guest, runs
every line of the request file through the proxy, prints one HOST_REQUEST
line per request and resumes the guest.  The file is consumed (renamed to
<file>.done) so a repeated signal never re-arms an old request.

Request lines (``#`` starts a comment):

    refuse N   RQ-2 0x110b: the next N owner swaps fail at the A408 stage
    drop N     RQ-2 0x110c: the next N exact latches are treated as lost
    status     RQ-2 0x110d: print the scanout recovery counters
    mse ECAM BUS DEV FN off|on
               BAR-fault reproduction (EXP2613 class): clear or set Memory Space
               Enable (Command bit 1) of one function through the physical ECAM, so
               the guest driver's next forwarded BAR access completes with an error.
               Prints the Command value before and after.  Nothing else is written.
    mmucfg     CACHE-1, read-only: the twelve 32-bit words of the GPU MMU configuration
               block at SGX+0xd08000 (0x204d08000..0x204d0802c; the EXP2235 receipt read
               the same words from EL2).  Nothing is written.  Issue it only while the GPU
               is powered (during a GPU run): the block is in the GPU power domain.
    mccplanes  CACHE-1, read-only: the system-cache state of the four memory-controller
               planes (plane base 0x200000000, stride 0x40000: PLANE_CACHE_ENABLE +0x1c00,
               PLANE_CACHE_STATUS +0x1c04; m1n1 src/mcc.c; base from the device tree as read in
               EXP2242).  Prints enable, status, data ways (bits 14:10) and tag ways (bits 9:5)
               per plane.  Nothing is written; the controller is always powered.
    mccgroups  CACHE-1, read-only: per-dataset-group cache quota / usage / use count of
               ALL four planes (plane base + 0xa000 / 0xa400 / 0xc000 + 4 x group, groups
               0..31; the m1n1 MCCDSID receipt reads plane 0 only).  One line per plane,
               groups with a non-zero usage or count.  Nothing is written.
    apgpio     R3 count-only AP GPIO filter (APGPIO_FILTER images, WOM1_APGPIO_FILTER=1): m1n1 prints
               its access summary ("HV: APGPIO stats ...") and returns the total access count
               (P_HV_APGPIO_STATS 0xc49).  Nothing is written.  An image without the filter answers
               Bad Command; that is reported as unavailable, not raised.
    trackb     audio Track B live facts (TRACK_B_HW_FACTS.md table B, audio a05ce9046): a fixed list of
               32-bit reads, H2 (PMGR PS of MCA0/1/2) first; H4/H5 (MCA cluster 1 and port blocks) only
               when MCA1 reads active (PS bits 7:4 = 0xf) - a gated block may SError from EL2; H1 (ADMAC SRAM
               sizes), H3 (clock mux 1), H6 (NCO 1 CTRL), H7 (ADMAC channel 4, TX_START) always.  Nothing is
               written.
    apgpioprobe PIN [VALUE]
               L3/B3 deny proof (APGPIO_FILTER images, m1n1 ed8f1677 P_HV_APGPIO_PROBE 0xc4c): m1n1 takes
               PIN's config word through the guest write path (stage-2 lookup, hook, filter decision) and
               prints the detail line; it never writes hardware in a deny build.  Host-side guards: never
               the guest's pin 195, never a value with the data bit (bit 0) set, pins 0..211; VALUE defaults
               to 0x2.  The return word is decoded.
    sdzg [clear] SDZ guard images (m1n1 ed8f1677 P_HV_SDZG_REPORT 0xc4b): m1n1 prints the guard report and the
               lock-wait receipt; with "clear" it then clears the guard's since-clear maxima (opens a measurement
               window).  Nothing is written to hardware.  Bad Command (no guard) is reported, not raised.
    dram follow|fast|slow
               GPU-COLD-2 / DRAMFAST-2 0xc31 (m1n1 built with DRAM_POLICY_OP=1, e.g. 1beede27 = standing
               81c450e2 + the op): DRAM configuration policy of the HV CPPC device - follow the P cluster
               (default rule), hold the fast DCS configuration, hold the boot (slow) one.  Prints the previous
               policy; -1 = refused (CPPC not mapped, bad value).  An image without the op answers Bad
               Command; that is reported as unavailable, not raised.
    memperf    GPU-COLD-2, read-only: memory-side state.  PMGR perf-regs blocks from the J313 ADT
               (/arm-io/pmgr perf-regs: reg0+0x34000 and reg1+0x34000, 64 words each; reg0+0x78000 and
               reg0+0x7c000, 2 words each; both PMGR reg blocks are the ones m1n1 pmgr.c / trackb already
               read) and DCS DRAMCFG0/1 (+0xdc4/+0xdbc, m1n1 src/mcc.c) of the 8 channels at 0x200200000
               + n x 0x40000, decoded fast (ADT dramcfg-data 0x133/0x55555340) or boot; the PMGR power-state
               words of MCC, DCS0..7 and PMP; PMP's ASC CPU_CONTROL (running bit 0x10) only when PMP's power
               state reads active; 16 words of DCS channel 0's PMP window (0x200204000).  Nothing is written.
    pmgrdump   GPU-COLD-3 / BAR1, read-only: the PMGR device power-state words of both PMGR blocks
               (ADT /arm-io/pmgr devices + ps-regs: 0x23b700040..0x23b7003fc, 240 words, the reg0 devices
               ECPM .. GFX; 0x23d280040..0x23d2800bc, 32 words, the reg1 NUB devices incl. the word
               0x23d280058 that the virtual vote devices GFX-SGX / GFX-BUSY / SOC-V* / DCS-V* name) and
               64 words of the PMGR scratch page 0x23b738000 (ADT / Asahi pmgr_dcp dcp-bw-scratch; the
               G13 firmware maps it as PMGRScratch).  The doorbell page 0x23bc3c000 is NOT read (a doorbell
               read may have side effects).  Printed as nonzero words per block.  Nothing is written.
"""

import pathlib
import struct

P_DISPLAY_INJECT_REFUSE_SWAPS = 0x110B
P_DISPLAY_INJECT_DROP_LATCHES = 0x110C
P_DISPLAY_SCANOUT_RECOVERY_STATUS = 0x110D
P_HV_APGPIO_STATS = 0xC49
P_HV_APGPIO_PROBE = 0xC4C
P_HV_SDZG_REPORT = 0xC4B
P_HV_CPPC_DRAM_POLICY = 0xC31
DRAM_POLICIES = {"follow": 0, "fast": 1, "slow": 2}
PMGR_PERF_BLOCKS = (("pmgr_perf0", 0x23B734000, 64), ("pmgr_perf1", 0x23D2B4000, 64),
                    ("pmgr_perf2", 0x23B778000, 2), ("pmgr_perf3", 0x23B77C000, 2))
DCS_BASE, DCS_STRIDE, DCS_CHANNELS = 0x200200000, 0x40000, 8
DCS_DRAMCFG0, DCS_DRAMCFG1 = 0xDC4, 0xDBC
DCS_DRAMCFG_FAST = (0x133, 0x55555340)
# PMGR power-state words (ADT /arm-io/pmgr devices: ps-regs[psreg] + 8 x psidx; m1n1 pmgr.c: ACTUAL bits 7:4,
# TARGET 3:0, AUTO 27:24): MCC, DCS0..7, PMP.  PMP's ASC CPU_CONTROL (ADT /arm-io/pmp reg0 0x23bc00000 + 0x44, m1n1
# src/asc.c; bit 0x10 = running) is read only when PMP's power state reads active (ACTUAL = 0xf), as trackb gates MCA1.
MEM_PS = (("MCC", 0x23B7002F8), ("DCS0", 0x23B700300), ("DCS1", 0x23B700310), ("DCS2", 0x23B700308),
          ("DCS3", 0x23B700318), ("DCS4", 0x23B700320), ("DCS5", 0x23B700330), ("DCS6", 0x23B700328),
          ("DCS7", 0x23B700338), ("PMP", 0x23B7003D8))
PMP_ASC_CPU_CONTROL = 0x23BC00044
# GPU-COLD-3 / BAR1 pmgrdump: (tag, base, words); all inside blocks m1n1 pmgr.c / memperf already read.
PMGR_DUMP_BLOCKS = (("ps0", 0x23B700040, 240), ("ps1", 0x23D280040, 32), ("scratch", 0x23B738000, 64))
# DCS channel 0 window that ADT /arm-io/pmp lists among PMP's own registers (reg 0x200204000, 0x4000): 16 words.
DCS0_PMP_WINDOW, DCS0_PMP_WORDS = 0x200204000, 16
APGPIO_GUEST_PIN, APGPIO_PINS = 195, 212
APGPIO_PROBE_ACTIONS = ("called-hook", "would-forward", "not-hooked", "refused-pin", "refused-guest-pin",
                        "refused-value", "not-installed")
APGPIO_PROBE_KINDS = ("unmapped", "filter-hook", "other-hook", "hardware", "other-sw")
TRACKB_H2 = (("MCA1", 0x23B7002C0), ("MCA0", 0x23B7002B8), ("MCA2", 0x23B7002C8))
TRACKB_ALWAYS = (("H1", (0x238200094, 0x238200098)), ("H3", (0x23B0400D8,)), ("H6", (0x23B048000,)),
                 ("H7", (0x238208800, 0x238208850, 0x238200000)))
TRACKB_CLUSTER1 = (("H4", (0x238400600, 0x238400604, 0x238400608, 0x238404600, 0x238404604, 0x238404608)),
                   ("H5", (0x238404000, 0x238404100, 0x238404300)))
SGX_MMUCFG_BASE = 0x204D08000
SGX_MMUCFG_WORDS = 12
MCC_PLANE_BASE = 0x200000000
MCC_PLANE_STRIDE = 0x40000
MCC_PLANES = 4
MCC_PLANE_CACHE_ENABLE = 0x1C00
MCC_PLANE_CACHE_STATUS = 0x1C04
MCC_GROUP_QUOTA, MCC_GROUP_USAGE, MCC_GROUP_COUNT, MCC_GROUPS = 0xA000, 0xA400, 0xC000, 32
STATUS_SIZE = 40
STATUS_FIELDS = ("latch_deadline_expiries", "latch_poll_errors", "late_latches_ignored",
                 "injected_refusals", "dropped_latches", "pending_refuse_swaps",
                 "pending_drop_latches", "abandoned_swap_id")
MAX_COUNT = 64
PCI_COMMAND = 0x04
PCI_COMMAND_MSE = 0x2


def parse(text):
    """Return [(verb, count)]; raise ValueError on any malformed line."""
    requests = []
    for number, raw in enumerate(text.splitlines(), 1):
        line = raw.split("#", 1)[0].strip()
        if not line:
            continue
        words = line.split()
        verb = words[0].lower()
        if verb == "status" and len(words) == 1:
            requests.append(("status", 0))
            continue
        if verb == "mse" and len(words) == 6 and words[5].lower() in ("off", "on"):
            try:
                ecam, bus, dev, fn = (int(w, 0) for w in words[1:5])
            except ValueError:
                raise ValueError(f"line {number}: cannot parse {raw!r}") from None
            if ecam & 0xfffff or not (1 <= bus <= 255 and 0 <= dev <= 31 and 0 <= fn <= 7):
                raise ValueError(f"line {number}: bad ECAM/BDF in {raw!r}")
            requests.append(("mse", (ecam, bus, dev, fn, words[5].lower() == "on")))
            continue
        if verb == "mccgroups" and len(words) == 1:
            requests.append(("mccgroups", 0))
            continue
        if verb == "mccplanes" and len(words) == 1:
            requests.append(("mccplanes", 0))
            continue
        if verb == "sdzg" and (len(words) == 1 or (len(words) == 2 and words[1].lower() == "clear")):
            requests.append(("sdzg", int(len(words) == 2)))
            continue
        if verb == "apgpioprobe" and len(words) in (2, 3):
            try:
                pin = int(words[1], 0)
                value = int(words[2], 0) if len(words) == 3 else 0x2
            except ValueError:
                raise ValueError(f"line {number}: cannot parse {raw!r}") from None
            if not 0 <= pin < APGPIO_PINS or pin == APGPIO_GUEST_PIN or value & 1 or not 0 <= value <= 0xFFFFFFFF:
                raise ValueError(f"line {number}: refused probe {raw!r} (guest pin, data bit or range)")
            requests.append(("apgpioprobe", (pin, value)))
            continue
        if verb == "trackb" and len(words) == 1:
            requests.append(("trackb", 0))
            continue
        if verb == "apgpio" and len(words) == 1:
            requests.append(("apgpio", 0))
            continue
        if verb == "mmucfg" and len(words) == 1:
            requests.append(("mmucfg", 0))
            continue
        if verb == "dram" and len(words) == 2 and words[1].lower() in DRAM_POLICIES:
            requests.append(("dram", DRAM_POLICIES[words[1].lower()]))
            continue
        if verb == "memperf" and len(words) == 1:
            requests.append(("memperf", 0))
            continue
        if verb == "pmgrdump" and len(words) == 1:
            requests.append(("pmgrdump", 0))
            continue
        if verb in ("refuse", "drop") and len(words) == 2 and words[1].isdigit():
            count = int(words[1])
            if count > MAX_COUNT:
                raise ValueError(f"line {number}: count {count} above {MAX_COUNT}")
            requests.append((verb, count))
            continue
        raise ValueError(f"line {number}: cannot parse {raw!r}")
    return requests


def decode_status(data):
    return dict(zip(STATUS_FIELDS, struct.unpack("<QQIIIIII", data)))


def execute(proxy, requests):
    """Run parsed requests through `proxy`; return the HOST_REQUEST lines."""
    lines = []
    for verb, count in requests:
        if verb == "status":
            address = proxy.heap.malloc(STATUS_SIZE)
            try:
                ok = proxy.request(P_DISPLAY_SCANOUT_RECOVERY_STATUS, address, STATUS_SIZE)
                status = decode_status(proxy.iface.readmem(address, STATUS_SIZE)) if ok else None
            finally:
                proxy.heap.free(address)
            body = " ".join(f"{k}={v}" for k, v in status.items()) if status else "unavailable"
            lines.append(f"HOST_REQUEST status {body}")
            continue
        if verb == "mse":
            ecam, bus, dev, fn, on = count
            reg = ecam + (bus << 20) + (dev << 15) + (fn << 12) + PCI_COMMAND
            before = proxy.read16(reg)
            after = (before | PCI_COMMAND_MSE) if on else (before & ~PCI_COMMAND_MSE)
            proxy.write16(reg, after)
            lines.append(f"HOST_REQUEST mse {bus:02x}:{dev:02x}.{fn} {'on' if on else 'off'} "
                         f"command 0x{before:04x} -> 0x{proxy.read16(reg):04x}")
            continue
        if verb == "mccgroups":
            for plane in range(MCC_PLANES):
                base = MCC_PLANE_BASE + plane * MCC_PLANE_STRIDE
                parts = []
                for group in range(MCC_GROUPS):
                    quota = proxy.read32(base + MCC_GROUP_QUOTA + 4 * group)
                    usage = proxy.read32(base + MCC_GROUP_USAGE + 4 * group)
                    count = proxy.read32(base + MCC_GROUP_COUNT + 4 * group)
                    if usage or count:
                        parts.append(f"{group:x}:{quota:x}/{usage:x}/{count:x}")
                lines.append(f"HOST_REQUEST mccgroups p{plane} grp:quota/usage/cnt " + (" ".join(parts) or "none"))
            continue
        if verb == "mccplanes":
            parts = []
            for plane in range(MCC_PLANES):
                base = MCC_PLANE_BASE + plane * MCC_PLANE_STRIDE
                enable = proxy.read32(base + MCC_PLANE_CACHE_ENABLE)
                status = proxy.read32(base + MCC_PLANE_CACHE_STATUS)
                parts.append(f"p{plane}:enable=0x{enable:x},status=0x{status:x},"
                             f"data_ways={(status >> 10) & 0x1f},tag_ways={(status >> 5) & 0x1f}")
            lines.append("HOST_REQUEST mccplanes " + " ".join(parts))
            continue
        if verb == "sdzg":
            try:
                proxy.request(P_HV_SDZG_REPORT, count)
            except Exception as error:
                lines.append(f"HOST_REQUEST sdzg unavailable {type(error).__name__}: {error}")
                continue
            lines.append(f"HOST_REQUEST sdzg report (clear={count}) printed")
            continue
        if verb == "apgpioprobe":
            pin, value = count
            r = int(proxy.request(P_HV_APGPIO_PROBE, pin, value))
            act, kind = r & 0xFF, (r >> 8) & 0xFF
            an = APGPIO_PROBE_ACTIONS[act] if act < len(APGPIO_PROBE_ACTIONS) else f"action{act}"
            kn = APGPIO_PROBE_KINDS[kind] if kind < len(APGPIO_PROBE_KINDS) else f"kind{kind}"
            lines.append(f"HOST_REQUEST apgpioprobe pin {pin} value 0x{value:x} action={an} stage2={kn} "
                         f"refused_delta={(r >> 16) & 0xFF} pin_write_delta={(r >> 24) & 0xFF} "
                         f"(detail line: HV: APGPIO probe ...)")
            continue
        if verb == "trackb":
            h2 = [(name, addr, proxy.read32(addr)) for name, addr in TRACKB_H2]
            active = ((h2[0][2] >> 4) & 0xF) == 0xF
            lines.append("HOST_REQUEST trackb H2 " + " ".join(f"{n} 0x{a:x}=0x{v:08x}" for n, a, v in h2)
                         + f" mca1_active={int(active)}")
            for tag, addrs in TRACKB_ALWAYS[:2] + (TRACKB_CLUSTER1 if active else ()) + TRACKB_ALWAYS[2:]:
                for a in addrs:
                    lines.append(f"HOST_REQUEST trackb {tag} 0x{a:x}=0x{proxy.read32(a):08x}")
            if not active:
                lines.extend(f"HOST_REQUEST trackb {tag} skipped (MCA1 not active)" for tag, _ in TRACKB_CLUSTER1)
            continue
        if verb == "apgpio":
            try:
                total = proxy.request(P_HV_APGPIO_STATS)
            except Exception as error:  # Bad Command on images built without APGPIO_FILTER
                lines.append(f"HOST_REQUEST apgpio unavailable {type(error).__name__}: {error}")
                continue
            lines.append(f"HOST_REQUEST apgpio total={total} (summary line: HV: APGPIO stats ...)")
            continue
        if verb == "mmucfg":
            values = [proxy.read32(SGX_MMUCFG_BASE + 4 * i) for i in range(SGX_MMUCFG_WORDS)]
            lines.append("HOST_REQUEST mmucfg " + " ".join(f"{v:08x}" for v in values))
            continue
        if verb == "dram":
            name = next(k for k, v in DRAM_POLICIES.items() if v == count)
            try:
                previous = proxy.request(P_HV_CPPC_DRAM_POLICY, count, signed=True)
            except Exception as error:  # Bad Command on images built without DRAM_POLICY_OP
                lines.append(f"HOST_REQUEST dram {name} unavailable {type(error).__name__}: {error}")
                continue
            lines.append(f"HOST_REQUEST dram {name} previous_policy={previous}"
                         + (" REFUSED" if previous < 0 else ""))
            continue
        if verb == "memperf":
            for tag, base, words in PMGR_PERF_BLOCKS:
                values = [proxy.read32(base + 4 * i) for i in range(words)]
                nonzero = " ".join(f"+{4 * i:03x}={v:08x}" for i, v in enumerate(values) if v)
                lines.append(f"HOST_REQUEST memperf {tag} 0x{base:x} {nonzero or 'all-zero'} "
                             f"nonzero={sum(1 for v in values if v)}")
            parts, fast = [], 0
            for ch in range(DCS_CHANNELS):
                base = DCS_BASE + ch * DCS_STRIDE
                cfg = (proxy.read32(base + DCS_DRAMCFG0), proxy.read32(base + DCS_DRAMCFG1))
                is_fast = cfg == DCS_DRAMCFG_FAST
                fast += is_fast
                parts.append(f"ch{ch}={cfg[0]:08x}/{cfg[1]:08x}:{'fast' if is_fast else 'boot'}")
            lines.append("HOST_REQUEST memperf dcs " + " ".join(parts) + f" fast_channels={fast}/{DCS_CHANNELS}")
            ps = [(name, proxy.read32(addr)) for name, addr in MEM_PS]
            lines.append("HOST_REQUEST memperf ps " + " ".join(f"{n}={v:08x}:a{(v >> 4) & 0xF:x}t{v & 0xF:x}" for n, v in ps))
            pmp_active = ((ps[-1][1] >> 4) & 0xF) == 0xF
            if pmp_active:
                ctl = proxy.read32(PMP_ASC_CPU_CONTROL)
                lines.append(f"HOST_REQUEST memperf pmp cpu_control=0x{ctl:08x} running={int(bool(ctl & 0x10))}")
            else:
                lines.append("HOST_REQUEST memperf pmp skipped (PMP power state not active)")
            values = [proxy.read32(DCS0_PMP_WINDOW + 4 * i) for i in range(DCS0_PMP_WORDS)]
            lines.append("HOST_REQUEST memperf dcs0_pmp_window " + " ".join(f"{v:08x}" for v in values))
            continue
        if verb == "pmgrdump":
            for tag, base, words in PMGR_DUMP_BLOCKS:
                values = [proxy.read32(base + 4 * i) for i in range(words)]
                nonzero = " ".join(f"{base + 4 * i:x}={v:08x}" for i, v in enumerate(values) if v)
                lines.append(f"HOST_REQUEST pmgrdump {tag} 0x{base:x} words={words} "
                             f"nonzero={sum(1 for v in values if v)} {nonzero or 'all-zero'}")
            continue
        op = P_DISPLAY_INJECT_REFUSE_SWAPS if verb == "refuse" else P_DISPLAY_INJECT_DROP_LATCHES
        previous = proxy.request(op, count, signed=True)
        lines.append(f"HOST_REQUEST {verb} count={count} previous_pending={previous}")
    return lines


def consume(path, proxy):
    """Read, execute and retire the request file; return the printed lines."""
    path = pathlib.Path(path)
    if not path.exists():
        return ["HOST_REQUEST none (no request file)"]
    text = path.read_text()
    path.rename(path.with_name(path.name + ".done"))
    try:
        requests = parse(text)
    except ValueError as error:
        return [f"HOST_REQUEST rejected {error}"]
    return execute(proxy, requests)
