"""Kernel-base search and VA read used to capture a halted native bugcheck (ANS010)."""
import unittest
from tools.ans_snapshot import find_kernel_base, parse, read_virtual, snapshot_kernel_read

BASE = 0xfffff802bb800000
OFF = 0x7f0000000000  # fake VA->PA offset


class Fake:
    def __init__(self):
        self.mem = {}
        self.mapped = set()
        self.map(BASE, 0x1000000)
        self.put(BASE, b'MZ')
        self.put(BASE - 0x200000, b'\0\0')           # mapped, no header below the image
    def map(self, va, size):
        for p in range(va & ~0xfff, va + size, 0x1000): self.mapped.add(p)
    def put(self, va, data):
        for i, b in enumerate(data): self.mem[va + i - OFF] = b
    def translate(self, va):
        return (va - OFF) if (va & ~0xfff) in self.mapped else 0
    def readmem(self, pa, n):
        return bytes(self.mem.get(pa + i, 0) for i in range(n))


class SnapshotRead(unittest.TestCase):
    def test_base_from_pc_inside_text_and_data(self):
        f = Fake()
        # The ANS010-R2 PCs: cpu0 xHalHaltSystem, cpu1-7 KiCheckStall (offsets from R1/R2).
        for pc in (BASE + 0x4825e0, BASE + 0x50bfb4):
            self.assertEqual(find_kernel_base(f.translate, f.readmem, pc), BASE)
        self.assertIsNone(find_kernel_base(f.translate, f.readmem, 0x1234))   # not a kernel VA

    def test_bugcheck_words_across_page_boundary(self):
        f = Fake()
        rva = 0xdbc9c0
        f.map(BASE + rva, 0x2000)
        words = [0x7b, 0xffffa00011112222, 0xffffffffc0000034, 0, 0]
        f.put(BASE + rva, b''.join(w.to_bytes(8, 'little') for w in words))
        line = snapshot_kernel_read(f.translate, f.readmem, BASE + 0x4825e0, rva, 40)
        self.assertIn(f'base=0x{BASE:x}', line)
        self.assertIn('words=0x7b 0xffffa00011112222 0xffffffffc0000034 0x0 0x0', line)
        f2 = Fake(); f2.map(BASE + 0xffc, 0x4); f2.mapped.discard((BASE + 0x1000))
        with self.assertRaises(ValueError):
            read_virtual(f2.translate, f2.readmem, BASE + 0xffc + 0x1000, 16)

    def test_spec_bounds(self):
        self.assertEqual(parse('0xdbc9c0:40'), (0xdbc9c0, 40))
        for bad in ('0:8', '0x10:0', '0x10:0x401', 'x'):
            with self.assertRaises(ValueError): parse(bad)


if __name__ == '__main__':
    unittest.main()
