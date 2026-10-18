"""Guard the PEI MMU descriptor array, including its terminator."""

from pathlib import Path
import re
import unittest


SOURCE = (Path(__file__).resolve().parents[1] / "mu/Silicon/Apple/T810XFamilyPkg/Library/MemoryInitPeiLib/MemoryInitPeiLib.c")


def descriptor_capacity(source: str) -> tuple[int, int]:
    limit = int(re.search(r"#define MAX_VIRTUAL_MEMORY_MAP_DESCRIPTORS\s+(\d+)", source).group(1))
    body = source.split("VOID BuildVirtualMemoryMap(OUT ARM_MEMORY_REGION_DESCRIPTOR **VirtualMemoryMap)\n{", 1)[1]
    writes = re.findall(r"VirtualMemoryTable\[(\+\+Index|Index)\]\.PhysicalBase\s*=\s*([^;]+);", body)
    assert writes and writes[0][0] == "Index" and writes[-1] == ("++Index", "0")
    assert sum(index == "Index" for index, _ in writes) == 1
    return 1 + sum(index == "++Index" for index, _ in writes), limit


class VirtualMemoryTableCapacityTest(unittest.TestCase):
    def test_enabled_low_window_and_terminator_fit(self):
        used, limit = descriptor_capacity(SOURCE.read_text())
        self.assertEqual(used, 19)
        self.assertEqual(limit, 20)
        self.assertLessEqual(used, limit)

    def test_capacity_guard_detects_overflow(self):
        source = SOURCE.read_text().replace(
            "#define MAX_VIRTUAL_MEMORY_MAP_DESCRIPTORS 20",
            "#define MAX_VIRTUAL_MEMORY_MAP_DESCRIPTORS 18",
        )
        used, limit = descriptor_capacity(source)
        self.assertGreater(used, limit)


if __name__ == "__main__":
    unittest.main()
