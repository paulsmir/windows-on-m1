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
