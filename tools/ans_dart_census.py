#!/usr/bin/env python3
"""Read-only census of retained DART page tables before guest entry (ANS005C).

Purpose: replace the assisted descriptor's DART_TABLES=UNOBSERVED marker with
an observation.  ANS on t8103 has no DART of its own (m1n1 src/nvme.c puts
physical addresses in PRPs; only /arm-io/sart-ans filters ASC firmware
buffers), so guest-authorized ANS DMA must never land in another device's
DART tables.  This tool lists every table page that a powered DART points at
when the machine sits at "Running proxy", before m1n1 hv_init and before any
guest, and classifies each page against the planned guest RAM window.

Safety contract (enforced below):
* the only MMIO reads are PMGR PS registers and, for DARTs whose every
  clock-gate device reports ACTUAL==0xf, the DART TTBR block; nothing is
  written, no power state is changed, no unpowered DART register is read;
* t8110 DARTs and unknown compatibles make the census INCOMPLETE (exit 3),
  never "empty";
* --confirm-experiment is required before any proxy access.

The pure functions are exercised offline by tests/test_ans_dart_census.py.
"""
import argparse
import hashlib
import json
import sys
from pathlib import Path

PS_ACTIVE = 0xF
T8020_TTBR_OFF, T8020_TTBR_VALID, T8020_TTBR_ADDR = 0x200, 1 << 31, (1 << 31) - 1
T8020_STREAMS, T8020_TTBRS = 16, 4          # proxyclient m1n1/hw/dart8020.py TTBR map
PTE_VALID, PTE_OFFSET = 1, 0xFFFFFFC000     # m1n1 src/dart.c DART_T8020_PTE_OFFSET
PAGE = 0x4000
L1_ENTRIES = PAGE // 8


def ttbr_addresses(base):
    """(stream, index, mmio address) for every t8020 TTBR register."""
    return [(s, i, base + T8020_TTBR_OFF + 16 * s + 4 * i)
            for s in range(T8020_STREAMS) for i in range(T8020_TTBRS)]


def decode_ttbr(value):
    return (value & T8020_TTBR_ADDR) << 12 if value & T8020_TTBR_VALID else None


def l2_tables(l1_words):
    """Distinct L2 table PAs referenced by valid L1 entries."""
    return sorted({w & PTE_OFFSET for w in l1_words if w & PTE_VALID})


def classify(pa, size, guest_ram, reserved):
    """GUEST_RAM_CONFLICT if any byte lies in the DMA-admissible guest window
    and is not covered by an explicit reservation; else OUTSIDE/RESERVED."""
    gb, gs = guest_ram
    if pa + size <= gb or pa >= gb + gs:
        return 'OUTSIDE_GUEST_RAM'
    for rb, rs in reserved:
        if rb <= pa and pa + size <= rb + rs:
            return 'RESERVED'
    return 'GUEST_RAM_CONFLICT'


def census(darts, read32, read64_block, guest_ram, reserved):
    """darts: [{path, compatible, base, powered}] -> receipt dict.
    read32(addr) and read64_block(pa, count) are the only hardware accessors."""
    out, complete = [], True
    for d in darts:
        rec = dict(path=d['path'], compatible=d['compatible'], base=hex(d['base']),
                   powered=d['powered'], tables=[])
        if d['compatible'] not in ('dart,t8020',):
            rec['status'] = 'UNSUPPORTED_COMPATIBLE'
            complete = False
        elif d['powered'] is None:
            rec['status'] = 'POWER_UNKNOWN'
            complete = False
        elif not d['powered']:
            # A gated DART has lost TTBR state; any later owner programs new
            # tables through m1n1 (bounded heap) or the guest (guest memory).
            rec['status'] = 'UNPOWERED_NOT_READ'
        else:
            rec['status'] = 'READ'
            for s, i, addr in ttbr_addresses(d['base']):
                l1 = decode_ttbr(read32(addr))
                if l1 is None:
                    continue
                pages = [('L1', l1)] + [('L2', pa) for pa in l2_tables(read64_block(l1, L1_ENTRIES))]
                for level, pa in pages:
                    rec['tables'].append(dict(stream=s, ttbr=i, level=level, pa=hex(pa),
                                              verdict=classify(pa, PAGE, guest_ram, reserved)))
        out.append(rec)
    conflicts = sum(t['verdict'] == 'GUEST_RAM_CONFLICT' for r in out for t in r['tables'])
    return dict(schema='ans-dart-census-v1', guest_ram=[hex(x) for x in guest_ram],
                reserved=[[hex(b), hex(s)] for b, s in reserved], darts=out,
                complete=complete, conflicts=conflicts,
                verdict=('INCOMPLETE' if not complete else
                         'CONFLICT' if conflicts else 'NO_RETAINED_TABLE_IN_GUEST_RAM'))


def pmgr_ps_address(pmgr_regs, ps_regs, device):
    """m1n1 src/pmgr.c pmgr_device_get_addr() for die 0."""
    reg = ps_regs[device['psreg']]
    return pmgr_regs[reg['reg']] + reg['offset'] + (device['psidx'] << 3)


def powered(gate_ids, devices_by_id, ps_addr, read32):
    if not gate_ids:
        return None
    for gid in gate_ids:
        dev = devices_by_id.get(gid)
        if dev is None:
            return None
        if ((read32(ps_addr(dev)) >> 4) & 0xF) != PS_ACTIVE:
            return False
    return True


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
    if a.output.exists():
        ap.error('refusing to overwrite existing evidence')
    sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'm1n1_windows/proxyclient'))
    # Not m1n1.setup: it writes the PMU panic counter.  Same bootstrap as
    # run_uefi.py without cpufreq_init(); ProxyUtils only reads heapblock(0).
    from m1n1.proxy import UartInterface, M1N1Proxy  # noqa: E402
    from m1n1.proxyutils import ProxyUtils, bootstrap_port  # noqa: E402
    iface = UartInterface()
    p = M1N1Proxy(iface, debug=False)
    bootstrap_port(iface, p)
    u = ProxyUtils(p)
    adt = u.adt
    pmgr = adt['/arm-io/pmgr']
    pmgr_regs = [pmgr.get_reg(i)[0] for i in range(len(pmgr.reg))]
    ps_regs = [dict(reg=r.reg, offset=r.offset) for r in pmgr.ps_regs]
    u8id = pmgr.devices[0].id1 != pmgr.devices[1].id1
    by_id = {(d.id1 if u8id else d.id2): dict(psreg=d.psreg, psidx=d.psidx) for d in pmgr.devices}
    darts = []
    for node in adt['/arm-io']:
        compat = getattr(node, 'compatible', [''])[0]
        if not compat.startswith('dart,'):
            continue
        gates = list(getattr(node, 'clock_gates', []) or [])
        for inst in range(len(node.reg)):
            darts.append(dict(path=f'{node._path}#{inst}', compatible=compat,
                              base=node.get_reg(inst)[0],
                              powered=powered(gates, by_id,
                                              lambda d: pmgr_ps_address(pmgr_regs, ps_regs, d),
                                              p.read32)))
    def block(pa, n):  # one bulk memory read per table, no MMIO
        import struct
        return list(struct.unpack(f'<{n}Q', u.iface.readmem(pa, 8 * n)))
    mm = adt['chosen']['memory-map']
    reserved = [tuple(getattr(mm, k)) for k in ('DeviceTree', 'TrustCache', 'SEPFW', 'BootArgs')
                if hasattr(mm, k)]
    end = a.guest_ram_end or (u.ba.phys_base + u.ba.mem_size)
    receipt = census(darts, p.read32, block, (a.guest_ram_base, end - a.guest_ram_base), reserved)
    receipt['experiment'] = a.confirm_experiment
    receipt['boot_args'] = dict(phys_base=hex(u.ba.phys_base), mem_size=hex(u.ba.mem_size),
                                top_of_kernel_data=hex(u.ba.top_of_kernel_data))
    receipt['adt_sha256'] = hashlib.sha256(adt.build()).hexdigest()
    sart = adt['/arm-io/sart-ans']
    receipt['sart_ans'] = dict(base=hex(sart.get_reg(0)[0]),
                               version=repr(getattr(sart, 'sart_version', None)))  # ADT only
    data = (json.dumps(receipt, indent=2) + '\n').encode()
    a.output.parent.mkdir(parents=True, exist_ok=True)
    with a.output.open('xb') as f:
        f.write(data)
    print(f"{receipt['verdict']} sha256={hashlib.sha256(data).hexdigest()} -> {a.output}")
    return {'NO_RETAINED_TABLE_IN_GUEST_RAM': 0, 'CONFLICT': 2}.get(receipt['verdict'], 3)


if __name__ == '__main__':
    sys.exit(main())
