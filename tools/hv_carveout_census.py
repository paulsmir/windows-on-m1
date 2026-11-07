#!/usr/bin/env python3
"""HVS2ISO002: read-only write-activity census of the iBoot carveouts inside guest RAM.

At a fresh Running proxy (no guest), read every 16 KiB page of the preregistered
carveout ranges three times (pass starts at t0, t0+60 s, t0+180 s) and record a
sha256 and a zero/non-zero flag per page.  A page whose hash changes between passes
proves a live writer.

Safety contract:
  * memory reads only (iface.readmem of DRAM); no write, no MMIO, no power change,
    no m1n1.setup (it writes the PMU panic counter);
  * preconditions, checked before the proxy is opened: no run_uefi.py process alive
    and guest SSH unreachable; after connect: the m1n1 boot banner ("Running proxy")
    was delivered to this connection (proxy fresh);
  * the live ADT must yield exactly the preregistered ranges (in_ram_carveouts from
    m1n1_windows feature/hv-stage2-above-ram >= 23a01cb6), otherwise refuse;
  * every chunk read is bracketed by the m1n1 exception counter (GUARD.SKIP|SILENT);
  * each pass must finish within --pass-budget seconds (default 300).

Exit: 0 STABLE, 2 CHANGED, 3 INCOMPLETE, 4 REFUSED.
"""

import argparse
import hashlib
import io
import json
import subprocess
import sys
import time
from pathlib import Path

PAGE = 0x4000
CHUNK = 0x40000  # 256 KiB per proxy read, split into 16 KiB pages
RANGES = ((0x860500000, 0x8614a0000), (0x864000000, 0x86f518000))
GUEST_RAM = (0x850000000, 0x9df708000)
PASS_STARTS = (0, 60, 180)
PASS_BUDGET = 300
ZERO_PAGE = bytes(PAGE)
EXIT = {"STABLE": 0, "CHANGED": 2, "INCOMPLETE": 3, "REFUSED": 4}
DEFAULT_SSH = "/Users/pavel/public_windows/.local/tandem/airssh"


class Fault(RuntimeError):
    pass


# --------------------------------------------------------------------------- preconditions


def run_uefi_alive(run=subprocess.run):
    r = run(["pgrep", "-f", "run_uefi.py"], capture_output=True, text=True)
    return [pid for pid in r.stdout.split() if pid.strip()]


def guest_ssh_up(ssh, run=subprocess.run, timeout=20):
    try:
        r = run([ssh, "-o", "ConnectTimeout=6", "-o", "BatchMode=yes", "echo up"],
                capture_output=True, text=True, timeout=timeout)
    except subprocess.TimeoutExpired:
        return False
    except FileNotFoundError:
        raise Fault(f"ssh helper {ssh} not found")
    return r.returncode == 0 and "up" in r.stdout


def banner_fresh(text):
    return "Running proxy" in text and "boot_args at" in text


def check_ranges(ranges):
    got = tuple((r.start, r.end) for r in ranges)
    if got != RANGES:
        raise Fault("live ADT carveout ranges differ from the preregistration: "
                    + ", ".join(f"[{a:#x},{b:#x})" for a, b in got))


# --------------------------------------------------------------------------- census core


def pages(ranges=RANGES):
    for start, end in ranges:
        yield from range(start, end, PAGE)


def hash_pass(read_chunk, ranges=RANGES, clock=time.monotonic, budget=PASS_BUDGET):
    """One pass: {page_pa: (sha256, nonzero)}. Raises Fault on fault or budget overrun."""
    t0 = clock()
    out = {}
    for start, end in ranges:
        addr = start
        while addr < end:
            size = min(CHUNK, end - addr)
            data = read_chunk(addr, size)
            if len(data) != size:
                raise Fault(f"short read at {addr:#x}: {len(data):#x} != {size:#x}")
            for off in range(0, size, PAGE):
                page = data[off:off + PAGE]
                out[addr + off] = (hashlib.sha256(page).hexdigest(), page != ZERO_PAGE)
            addr += size
            if clock() - t0 > budget:
                raise Fault(f"pass exceeded {budget} s at {addr:#x}")
    return out, clock() - t0


def census(read_chunk, clock=time.monotonic, sleep=time.sleep,
           starts=PASS_STARTS, budget=PASS_BUDGET, ranges=RANGES):
    t0 = clock()
    passes, stop = [], None
    for i, start in enumerate(starts):
        wait = t0 + start - clock()
        if wait > 0:
            sleep(wait)
        begin = clock() - t0
        try:
            result, elapsed = hash_pass(read_chunk, ranges, clock, budget)
        except Fault as e:
            stop = f"pass {i}: {e}"
            break
        passes.append({"index": i, "start_s": round(begin, 3), "elapsed_s": round(elapsed, 3),
                       "pages": result})
    expected = sum((b - a) // PAGE for a, b in ranges)
    complete = stop is None and len(passes) == len(starts) and all(
        len(p["pages"]) == expected for p in passes)
    changed = []
    if passes:
        base = passes[0]["pages"]
        for p in passes[1:]:
            for pa, (h, _) in p["pages"].items():
                if base[pa][0] != h:
                    changed.append({"pa": hex(pa), "pass": p["index"]})
    verdict = "CHANGED" if changed else ("STABLE" if complete else "INCOMPLETE")
    nonzero = sum(1 for _, nz in passes[0]["pages"].values() if nz) if passes else 0
    return {
        "verdict": verdict,
        "stop": stop,
        "complete": complete,
        "ranges": [[hex(a), hex(b)] for a, b in ranges],
        "page_count": expected,
        "nonzero_pages_pass0": nonzero,
        "changed_pages": changed,
        "passes": [
            {"index": p["index"], "start_s": p["start_s"], "elapsed_s": p["elapsed_s"],
             "pages": {hex(pa): [h, nz] for pa, (h, nz) in sorted(p["pages"].items())}}
            for p in passes
        ],
    }


def guarded_chunk_reader(p, iface, guard_mode):
    p.set_exc_guard(guard_mode)

    def read_chunk(addr, size):
        if not all(a <= addr and addr + size <= b for a, b in [GUEST_RAM]):
            raise Fault(f"read {addr:#x}+{size:#x} outside guest RAM window")
        before = p.get_exc_count()
        data = iface.readmem(addr, size)
        if p.get_exc_count() != before:
            raise Fault(f"memory fault reading {addr:#x}")
        return data
    return read_chunk


# --------------------------------------------------------------------------- live wrapper


class _Tee(io.TextIOBase):
    def __init__(self, a):
        self.a, self.buf = a, io.StringIO()

    def write(self, s):
        self.buf.write(s)
        return self.a.write(s)

    def flush(self):
        self.a.flush()


def write_receipt(path, receipt):
    data = (json.dumps(receipt, indent=1) + "\n").encode()
    with path.open("xb") as f:
        f.write(data)
    return hashlib.sha256(data).hexdigest()


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--confirm-experiment", required=True)
    ap.add_argument("--output", type=Path, required=True)
    ap.add_argument("--ssh", default=DEFAULT_SSH)
    ap.add_argument("--pass-budget", type=int, default=PASS_BUDGET)
    a = ap.parse_args(argv)
    if a.confirm_experiment != "HVS2ISO002":
        ap.error("--confirm-experiment HVS2ISO002 is required")
    evid = a.output.parent
    if a.output.exists() or (evid / "banner.txt").exists():
        ap.error("refusing to overwrite existing evidence")
    evid.mkdir(parents=True, exist_ok=True)

    def refuse(reason):
        sha = write_receipt(a.output, {"experiment": a.confirm_experiment,
                                       "verdict": "REFUSED", "stop": reason})
        print(f"REFUSED {reason} sha256={sha} -> {a.output}")
        return EXIT["REFUSED"]

    pids = run_uefi_alive()
    if pids:
        return refuse(f"run_uefi.py alive: pids {' '.join(pids)}")
    if guest_ssh_up(a.ssh):
        return refuse("guest SSH reachable")

    root = Path(__file__).resolve().parents[1]
    sys.path.insert(0, str(root / "m1n1_windows/proxyclient"))
    try:
        from m1n1.hv.stage2_policy import in_ram_carveouts  # noqa: E402
    except ImportError:
        return refuse("m1n1_windows lacks hv/stage2_policy.in_ram_carveouts (need >= 23a01cb6)")
    from m1n1.proxy import UartInterface, M1N1Proxy, GUARD  # noqa: E402
    from m1n1.proxyutils import ProxyUtils, bootstrap_port  # noqa: E402

    tee, real = _Tee(sys.stdout), sys.stdout
    sys.stdout = tee  # proxy.py prints the buffered m1n1 console as "TTY> ..."
    try:
        iface = UartInterface()
        p = M1N1Proxy(iface, debug=False)
        bootstrap_port(iface, p)
        u = ProxyUtils(p)
        adt_blob = u.adt.build()
    finally:
        sys.stdout = real
    banner = tee.buf.getvalue()
    (evid / "banner.txt").open("x").write(banner)
    if not banner_fresh(banner):
        return refuse("NO_BOOT_BANNER: proxy not fresh (another tool connected first)")
    try:
        check_ranges(in_ram_carveouts(u.adt, *GUEST_RAM))
    except Exception as e:  # noqa: BLE001 - any ADT mismatch refuses
        return refuse(str(e))

    read_chunk = guarded_chunk_reader(p, iface, GUARD.SKIP | GUARD.SILENT)
    try:
        receipt = census(read_chunk, budget=a.pass_budget)
    finally:
        p.set_exc_guard(GUARD.OFF)
    receipt.update(
        experiment=a.confirm_experiment,
        boot_args=dict(phys_base=hex(u.ba.phys_base), mem_size=hex(u.ba.mem_size),
                       mem_size_actual=hex(u.ba.mem_size_actual)),
        adt_sha256=hashlib.sha256(adt_blob).hexdigest(),
        tool_sha256=hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),
    )
    sha = write_receipt(a.output, receipt)
    print(f"{receipt['verdict']} stop={receipt['stop']} changed={len(receipt['changed_pages'])} "
          f"nonzero={receipt['nonzero_pages_pass0']}/{receipt['page_count']} "
          f"sha256={sha} -> {a.output}")
    return EXIT[receipt["verdict"]]


if __name__ == "__main__":
    sys.exit(main())
