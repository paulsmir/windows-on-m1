"""Read-only guest kernel memory read for the runner's SIGINT diagnostic snapshot.

ANS010-R1/R2 halted in a bugcheck (cpu0 xHalHaltSystem, others KiCheckStall) before any
crash dump could be written to the native disk. The stop code lives in ntoskrnl's
KiBugCheckData (ULONG_PTR[5]); this finds the kernel image base from any kernel PC by
looking for the PE 'MZ' header on 2 MiB boundaries below it, then reads base+RVA through
the guest's own stage-1/stage-2 translation. No guest state is modified.
"""

PAGE = 0x1000
ALIGN = 0x200000


def parse(spec):
    rva, length = (int(x, 0) for x in spec.split(':'))
    if rva <= 0 or not 0 < length <= 0x400:
        raise ValueError('snapshot read must be RVA:LEN with 0 < LEN <= 0x400')
    return rva, length


def find_kernel_base(translate, readmem, pc, span=0x2000000):
    """First 2 MiB-aligned VA at or below pc whose page starts with 'MZ'."""
    if pc < (1 << 63):
        return None
    start = pc & ~(ALIGN - 1)
    for va in range(start, max(start - span, 0), -ALIGN):
        pa = translate(va)
        if pa and readmem(pa, 2) == b'MZ':
            return va
    return None


def read_virtual(translate, readmem, va, length):
    out = b''
    while len(out) < length:
        cur = va + len(out)
        n = min(length - len(out), PAGE - (cur & (PAGE - 1)))
        pa = translate(cur)
        if not pa:
            raise ValueError(f'unmapped guest VA 0x{cur:x}')
        out += readmem(pa, n)
    return out


def snapshot_kernel_read(translate, readmem, pc, rva, length):
    base = find_kernel_base(translate, readmem, pc)
    if base is None:
        return f'SNAPSHOT_KERNEL no MZ base below pc=0x{pc:x}'
    data = read_virtual(translate, readmem, base + rva, length)
    words = ' '.join(f'0x{int.from_bytes(data[i:i + 8], "little"):x}' for i in range(0, len(data) - 7, 8))
    return f'SNAPSHOT_KERNEL pc=0x{pc:x} base=0x{base:x} rva=0x{rva:x} words={words}'


def parse_devnodes(spec):
    """'ROOT_RVA,child,sibling,instance,service,state,problem,problem_status' (hex/dec)."""
    v = [int(x, 0) for x in spec.split(',')]
    if len(v) != 8 or v[0] <= 0:
        raise ValueError('snapshot devnodes needs 8 comma-separated values')
    return dict(zip(('root_rva', 'child', 'sibling', 'instance', 'service', 'state', 'problem', 'pstatus'), v))


def _u64(translate, readmem, va):
    return int.from_bytes(read_virtual(translate, readmem, va, 8), 'little')


def _unicode(translate, readmem, va, limit=512):
    length = int.from_bytes(read_virtual(translate, readmem, va, 2), 'little')
    buf = _u64(translate, readmem, va + 8)
    if not length or not buf:
        return ''
    return read_virtual(translate, readmem, buf, min(length, limit)).decode('utf-16-le', 'replace')


def snapshot_devnodes(translate, readmem, pc, o, max_nodes=400):
    """DFS of the kernel PnP tree from *IopRootDeviceNode; one line per devnode."""
    base = find_kernel_base(translate, readmem, pc)
    if base is None:
        return [f'SNAPSHOT_DEVNODE no MZ base below pc=0x{pc:x}']
    lines, stack, seen = [], [_u64(translate, readmem, base + o['root_rva'])], set()
    while stack and len(lines) < max_nodes:
        node = stack.pop()
        if not node or node in seen:
            continue
        seen.add(node)
        try:
            inst = _unicode(translate, readmem, node + o['instance'])
            svc = _unicode(translate, readmem, node + o['service'])
            st = int.from_bytes(read_virtual(translate, readmem, node + o['state'], 4), 'little')
            pr = int.from_bytes(read_virtual(translate, readmem, node + o['problem'], 4), 'little')
            ps = int.from_bytes(read_virtual(translate, readmem, node + o['pstatus'], 4), 'little')
            lines.append(f'SNAPSHOT_DEVNODE state=0x{st:x} problem={pr} pstatus=0x{ps:08x} svc={svc} inst={inst}')
            stack.append(_u64(translate, readmem, node + o['sibling']))
            stack.append(_u64(translate, readmem, node + o['child']))
        except ValueError as error:
            lines.append(f'SNAPSHOT_DEVNODE unreadable node=0x{node:x}: {error}')
    lines.append(f'SNAPSHOT_DEVNODE_COUNT {len(lines)}')
    return lines
