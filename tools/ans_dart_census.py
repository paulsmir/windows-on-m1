#!/usr/bin/env python3
"""Read-only census of retained DART page tables before guest entry (v2).

Purpose: replace the assisted descriptor's DART_TABLES=UNOBSERVED marker with
an observation.  t8103 ANS has no DART (m1n1 src/nvme.c puts physical
addresses in PRPs; only /arm-io/sart-ans filters RTKit buffers), so guest-
authorized ANS DMA must never land in another device's retained DART tables.

v1 was REJECTED by ANSDART001-R2: a PMGR PS "active" gate did not make
dart-scaler readable (EL2 data abort at 0x263200200) and an unguarded bogus
TTBR led to a memory read that dropped the proxy link.  v2 rules:

* A DART register block is read only if m1n1 itself reported accessing that
  exact base during *this* boot (its "dart: dart <path> at <base> is a t8020"
  banner, captured from the proxy console at connect time) AND the path is in
  ALLOWED_DARTS AND the base is one of the node's ADT reg instances.  No banner
  -> no MMIO at all, verdict INCOMPLETE.
* Every MMIO read runs with GUARD.SKIP|SILENT and is checked against the m1n1
  exception counter; the first fault ends the whole census (INCOMPLETE).
* Memory is read only for 16 KiB-aligned table PAs that lie entirely inside
  DRAM [ram_base, ram_base + mem_size_actual); anything else ends the census.
* Nothing is written; --confirm-experiment is required before proxy access.
"""
import argparse
import hashlib
import io
import json
import re
import struct
import sys
from pathlib import Path

ALLOWED_DARTS = ('/arm-io/dart-dcp', '/arm-io/dart-disp0',
                 '/arm-io/dart-usb0', '/arm-io/dart-usb1')
T8020_TTBR_OFF, T8020_TTBR_VALID, T8020_TTBR_ADDR = 0x200, 1 << 31, (1 << 31) - 1
T8020_STREAMS, T8020_TTBRS = 16, 4          # proxyclient m1n1/hw/dart8020.py TTBR map
PTE_VALID, PTE_OFFSET = 1, 0xFFFFFFC000     # m1n1 src/dart.c DART_T8020_PTE_OFFSET
PAGE = 0x4000
L1_ENTRIES = PAGE // 8
BANNER = re.compile(r'dart: dart (/arm-io/[\w.-]+) at (0x[0-9a-f]+) is a (t8020|t6000|t8110)( \(locked\))?')
BANNER_L1 = re.compile(r'dart: dart (/arm-io/[\w-]+)\.(\d+)\.(\d+) L1 (\d+) is real-time at (0x[0-9a-f]+)')


class Fault(Exception):
    """A guarded access faulted or a bound was violated; census stops."""


def parse_banner(text):
    """m1n1 boot banner -> ({path: {base, type, locked}}, [(path, sid?, ttbr, pa)])."""
    darts, l1 = {}, []
    for m in BANNER.finditer(text):
        darts[m.group(1)] = dict(base=int(m.group(2), 16), type=m.group(3),
                                 locked=bool(m.group(4)))
    for m in BANNER_L1.finditer(text):
        l1.append(dict(path=m.group(1), ttbr=int(m.group(4)), pa=int(m.group(5), 16)))
    return darts, l1


def select_targets(banner_darts, adt_regs):
    """adt_regs: {path: [base, ...]} -> [(path, base)] allowed to be read."""
    out = []
    for path in ALLOWED_DARTS:
        b = banner_darts.get(path)
        if b and b['type'] == 't8020' and b['base'] in adt_regs.get(path, ()):
            out.append((path, b['base']))
    return out


def ttbr_addresses(base):
    return [(s, i, base + T8020_TTBR_OFF + 16 * s + 4 * i)
            for s in range(T8020_STREAMS) for i in range(T8020_TTBRS)]


def decode_ttbr(value):
    return (value & T8020_TTBR_ADDR) << 12 if value & T8020_TTBR_VALID else None


def l2_tables(l1_words):
    return sorted({w & PTE_OFFSET for w in l1_words if w & PTE_VALID})


def in_dram(pa, dram):
    lo, size = dram
    return pa % PAGE == 0 and lo <= pa and pa + PAGE <= lo + size


def classify(pa, size, guest_ram, reserved):
    gb, gs = guest_ram
    if pa + size <= gb or pa >= gb + gs:
        return 'OUTSIDE_GUEST_RAM'
    for rb, rs in reserved:
        if rb <= pa and pa + size <= rb + rs:
            return 'RESERVED'
    return 'GUEST_RAM_CONFLICT'


def census(targets, read32, read_table, dram, guest_ram, reserved, banner_seen=True):
    """read32/read_table raise Fault on any guarded fault; first Fault stops."""
    out, stop = [], None
    if not banner_seen:
        stop = 'NO_BOOT_BANNER'
    for path, base in targets:
        if stop:
            break
        rec = dict(path=path, base=hex(base), status='READ', tables=[])
        out.append(rec)
        try:
            for s, i, addr in ttbr_addresses(base):
                l1 = decode_ttbr(read32(addr))
                if l1 is None:
                    continue
                if not in_dram(l1, dram):
                    raise Fault(f'TTBR {path}[{s}][{i}] -> {l1:#x} outside DRAM')
                pages = [('L1', l1)]
                for pa in l2_tables(read_table(l1)):
                    if not in_dram(pa, dram):
                        raise Fault(f'L2 {pa:#x} under {l1:#x} outside DRAM')
                    pages.append(('L2', pa))
                for level, pa in pages:
                    rec['tables'].append(dict(stream=s, ttbr=i, level=level, pa=hex(pa),
                                              verdict=classify(pa, PAGE, guest_ram, reserved)))
        except Fault as e:
            rec['status'] = 'FAULT'
            stop = str(e)
    conflicts = sum(t['verdict'] == 'GUEST_RAM_CONFLICT' for r in out for t in r['tables'])
    complete = stop is None and len(out) == len(targets) and bool(targets)
    return dict(schema='ans-dart-census-v2', guest_ram=[hex(x) for x in guest_ram],
                dram=[hex(x) for x in dram], reserved=[[hex(b), hex(s)] for b, s in reserved],
                targets=[[p, hex(b)] for p, b in targets], darts=out, stop=stop,
                complete=complete, conflicts=conflicts,
                verdict=('INCOMPLETE' if not complete else
                         'CONFLICT' if conflicts else 'NO_RETAINED_TABLE_IN_GUEST_RAM'))


def guarded_reader(p, guard_mode):
    """Wrap proxy reads: guard on, compare exception counter around each read."""
    p.set_exc_guard(guard_mode)

    def read32(addr):
        before = p.get_exc_count()
        v = p.read32(addr)
        if p.get_exc_count() != before:
            raise Fault(f'MMIO fault at {addr:#x}')
        return v
    return read32


class _Tee(io.TextIOBase):
    def __init__(self, a):
        self.a, self.buf = a, io.StringIO()

    def write(self, s):
        self.buf.write(s)
        return self.a.write(s)

    def flush(self):
        self.a.flush()


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument('--confirm-experiment', required=True)
    ap.add_argument('--guest-ram-base', type=lambda x: int(x, 0), required=True)
    ap.add_argument('--guest-ram-end', type=lambda x: int(x, 0),
                    help='default: boot_args phys_base + mem_size (run_uefi.py mem_top)')
    ap.add_argument('--output', type=Path, required=True)
    a = ap.parse_args(argv)
    if not a.confirm_experiment.startswith('ANS'):
        ap.error('an ANS experiment id is required')
    evid = a.output.parent
    if a.output.exists() or (evid / 'adt.bin').exists() or (evid / 'banner.txt').exists():
        ap.error('refusing to overwrite existing evidence')
    sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'm1n1_windows/proxyclient'))
    from m1n1.proxy import UartInterface, M1N1Proxy, GUARD  # noqa: E402
    from m1n1.proxyutils import ProxyUtils, bootstrap_port  # noqa: E402
    evid.mkdir(parents=True, exist_ok=True)
    tee, real = _Tee(sys.stdout), sys.stdout
    sys.stdout = tee           # proxy.py prints the buffered m1n1 console as "TTY> ..."
    try:
        iface = UartInterface()
        p = M1N1Proxy(iface, debug=False)
        bootstrap_port(iface, p)   # same transport bootstrap as run_uefi.py, no cpufreq
        u = ProxyUtils(p)          # not m1n1.setup: that writes the PMU panic counter
        adt = u.adt
        adt_blob = adt.build()
    finally:
        sys.stdout = real
    banner_text = tee.buf.getvalue()
    (evid / 'banner.txt').open('x').write(banner_text)
    with (evid / 'adt.bin').open('xb') as f:
        f.write(adt_blob)
    bdarts, bl1 = parse_banner(banner_text)
    adt_regs = {}
    for path in ALLOWED_DARTS:
        try:
            node = adt[path]
            adt_regs[path] = [node.get_reg(i)[0] for i in range(len(node.reg))]
        except Exception:  # absent node: never a target
            adt_regs[path] = []
    targets = select_targets(bdarts, adt_regs)
    dram = (0x800000000, u.ba.mem_size_actual)  # m1n1 banner "MMU: RAM base: 0x800000000"

    read32 = guarded_reader(p, GUARD.SKIP | GUARD.SILENT)

    def read_table(pa):
        if not in_dram(pa, dram):
            raise Fault(f'table {pa:#x} outside DRAM')
        before = p.get_exc_count()
        words = list(struct.unpack(f'<{L1_ENTRIES}Q', u.iface.readmem(pa, PAGE)))
        if p.get_exc_count() != before:
            raise Fault(f'memory fault reading {pa:#x}')
        return words
    mm = adt['chosen']['memory-map']
    reserved = [tuple(getattr(mm, k)) for k in ('DeviceTree', 'TrustCache', 'SEPFW', 'BootArgs')
                if hasattr(mm, k)]
    end = a.guest_ram_end or (u.ba.phys_base + u.ba.mem_size)
    try:
        receipt = census(targets, read32, read_table, dram,
                         (a.guest_ram_base, end - a.guest_ram_base), reserved,
                         banner_seen=bool(bdarts))
    finally:
        p.set_exc_guard(GUARD.OFF)
    receipt.update(experiment=a.confirm_experiment, banner_darts={k: dict(v, base=hex(v['base']))
                                                                  for k, v in bdarts.items()},
                   banner_l1=[dict(x, pa=hex(x['pa'])) for x in bl1],
                   boot_args=dict(phys_base=hex(u.ba.phys_base), mem_size=hex(u.ba.mem_size),
                                  mem_size_actual=hex(u.ba.mem_size_actual),
                                  top_of_kernel_data=hex(u.ba.top_of_kernel_data)),
                   adt_sha256=hashlib.sha256(adt_blob).hexdigest())
    data = (json.dumps(receipt, indent=2) + '\n').encode()
    with a.output.open('xb') as f:
        f.write(data)
    print(f"{receipt['verdict']} stop={receipt['stop']} sha256={hashlib.sha256(data).hexdigest()} -> {a.output}")
    return {'NO_RETAINED_TABLE_IN_GUEST_RAM': 0, 'CONFLICT': 2}.get(receipt['verdict'], 3)


if __name__ == '__main__':
    sys.exit(main())
