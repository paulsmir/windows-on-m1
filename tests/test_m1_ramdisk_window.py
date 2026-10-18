"""M1: the 1 GiB ramdisk window is reserved only on boots that carry a ramdisk.

Owners (docs/superpowers/plans/2026-10-12-m1-ramdisk-window.md):
- the launcher decides: with --ramdisk it writes the header and publishes the
  window to m1n1 as an HV_CONTRACT_REGION_RAMDISK region; without it it clears
  the header page; either way it cleans the page to the point of coherency
  because Mu's PEI reads it with the MMU off;
- Mu owns the OS map: PEI reserves the window only for a valid header and
  publishes its decision in a GUID HOB; DXE registers a RAM disk only from
  that HOB (never from a header PEI did not accept);
- m1n1 owns EL2 DMA: GPU backing pages are denied inside a published ramdisk
  region, and standalone launches (no ramdisk ever) clear a stale header.
These tests pin the byte ABI shared by all three and each owner's decision.
"""
from pathlib import Path
import os
import re
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))
from tools import j313_ramdisk  # noqa: E402

MU = ROOT / "mu/Silicon/Apple"
PRELOADED = MU / "AppleSiliconPkg/Include/Library/PreloadedRamdisk.h"
PEI = MU / "T810XFamilyPkg/Library/MemoryInitPeiLib/MemoryInitPeiLib.c"
DXE = MU / "AppleSiliconPkg/Drivers/BootRamdiskHelperDxe/BootRamdiskHelperDxe.c"
CONTRACT = ROOT / "m1n1_windows/src/hv_launch_contract.h"
STANDALONE = ROOT / "m1n1_windows/src/hv_autonomous_runtime.c"
RUN_UEFI = ROOT / "run_uefi.py"

PREDICATE = r'''
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <Library/PreloadedRamdisk.h>
int main(void) {
  PRELOADED_RAMDISK_HEADER h;
  const UINT64 base = 0x860000000ULL, window = 0x40000000ULL;
  memcpy(h.Magic, "ASIRAMDK", 8); h.Size = 0x1234;
  assert(sizeof(h) == 16);
  assert(PreloadedRamdiskHeaderValid(&h, base, window));
  h.Size = window - PRELOADED_RAMDISK_PAYLOAD_OFFSET;
  assert(PreloadedRamdiskHeaderValid(&h, base, window));
  h.Size = window - PRELOADED_RAMDISK_PAYLOAD_OFFSET + 1;
  assert(!PreloadedRamdiskHeaderValid(&h, base, window));
  h.Size = 0;
  assert(!PreloadedRamdiskHeaderValid(&h, base, window));
  h.Size = 0x1234;
  memset(&h, 0, sizeof(h));          /* the launcher's cleared page */
  assert(!PreloadedRamdiskHeaderValid(&h, base, window));
  memcpy(h.Magic, "ASIRAMDk", 8); h.Size = 0x1234;
  assert(!PreloadedRamdiskHeaderValid(&h, base, window));
  memcpy(h.Magic, "ASIRAMDK", 8);
  assert(!PreloadedRamdiskHeaderValid(NULL, base, window));
  assert(!PreloadedRamdiskHeaderValid(&h, 0, window));
  assert(!PreloadedRamdiskHeaderValid(&h, base, PRELOADED_RAMDISK_PAYLOAD_OFFSET));
  assert(!PreloadedRamdiskHeaderValid(&h, ~0ULL - 0x1000ULL, window));
  puts("PASS");
  return 0;
}
'''

BASE_SHIM = ("#include <stdint.h>\n#include <stddef.h>\n"
             "typedef uint64_t UINT64; typedef uint32_t UINT32; typedef size_t UINTN;\n"
             "typedef char CHAR8; typedef unsigned char BOOLEAN;\n"
             "#define TRUE 1\n#define FALSE 0\n#define IN\n#define CONST const\n"
             "#define STATIC static\n#define MAX_UINT64 UINT64_MAX\n")


class LauncherTest(unittest.TestCase):
    def test_header_bytes_match_mu(self):
        header = PRELOADED.read_text()
        magic = re.search(r'#define PRELOADED_RAMDISK_MAGIC\s+"(\w+)"', header).group(1)
        offset = int(re.search(r"#define PRELOADED_RAMDISK_PAYLOAD_OFFSET\s+(0x[0-9a-fA-F]+)",
                               header).group(1), 16)
        self.assertEqual(j313_ramdisk.MAGIC, magic.encode())
        self.assertEqual(j313_ramdisk.PAYLOAD_OFFSET, offset)
        self.assertRegex(header, r"CHAR8\s+Magic\[8\];\s+UINT64\s+Size;")
        page = j313_ramdisk.header(0x1234)
        self.assertEqual(len(page), offset)
        self.assertEqual(page[:8], b"ASIRAMDK")
        self.assertEqual(int.from_bytes(page[8:16], "little"), 0x1234)
        self.assertEqual(page[16:], bytes(offset - 16))
        self.assertEqual(j313_ramdisk.cleared_header(), bytes(offset))
        with self.assertRaises(ValueError):
            j313_ramdisk.header(0)

    def test_region_kind_matches_m1n1(self):
        enum = CONTRACT.read_text().split("enum hv_contract_region_kind {", 1)[1].split("};", 1)[0]
        kinds = re.findall(r"^\s*(HV_CONTRACT_REGION_\w+),", enum, re.M)
        self.assertEqual(kinds.index("HV_CONTRACT_REGION_RAMDISK"), j313_ramdisk.REGION_RAMDISK)
        self.assertEqual(j313_ramdisk.descriptor_region(0x860000000, 0x40000000),
                         (j313_ramdisk.REGION_RAMDISK, 0, 0x860000000, 0x40000000))

    def test_fit_checks(self):
        ram = (0x850000000, 0x9e0000000)
        j313_ramdisk.check_fits(0x40000000 - 0x1000, 0x860000000, 0x40000000, *ram)
        for size, base in ((0x40000000 - 0xfff, 0x860000000), (0, 0x860000000),
                           (0x1000, 0x9c0000000), (0x1000, 0x840000000)):
            with self.assertRaises(ValueError):
                j313_ramdisk.check_fits(size, base, 0x40000000, *ram)

    def test_run_uefi_decides_before_publishing(self):
        source = RUN_UEFI.read_text()
        self.assertLess(source.index("j313_ramdisk.check_fits("),
                        source.index("p.hv_launch_publish(descriptor)"))
        self.assertIn("j313_ramdisk.descriptor_region(args.ramdisk_base, args.ramdisk_max_size)",
                      source)
        self.assertIn(") + ramdisk_regions,", source)
        self.assertIn("iface.writemem(args.ramdisk_base, j313_ramdisk.cleared_header())", source)
        self.assertIn("iface.writemem(args.ramdisk_base, j313_ramdisk.header(len(ramdisk_blob)))",
                      source)
        self.assertIn("p.dc_cvac(args.ramdisk_base, j313_ramdisk.PAYLOAD_OFFSET)", source)
        self.assertNotIn('b"ASIRAMDK"', source)


class MuTest(unittest.TestCase):
    def test_predicate(self):
        with tempfile.TemporaryDirectory() as tmp:
            (Path(tmp) / "Base.h").write_text(BASE_SHIM)
            library = Path(tmp) / "Library"
            library.mkdir()
            (library / "PreloadedRamdisk.h").write_text(PRELOADED.read_text())
            program = Path(tmp) / "predicate.c"
            program.write_text(PREDICATE)
            binary = Path(tmp) / "predicate"
            build = subprocess.run([os.environ.get("CC", "cc"), "-std=c11", "-Wall", "-Wextra",
                                    "-Werror", "-I", tmp, str(program), "-o", str(binary)],
                                   capture_output=True, text=True)
            self.assertEqual(build.returncode, 0, build.stderr)
            result = subprocess.run([str(binary)], capture_output=True, text=True)
            self.assertEqual(result.stdout.strip(), "PASS", result.stderr)

    def test_pei_reserves_only_a_valid_header_and_publishes_the_decision(self):
        source = PEI.read_text()
        block = source.split("if ((PcdGet64 (PcdPreloadedRamdiskBase) != 0) && "
                             "(PcdGet32 (PcdPreloadedRamdiskMaxSize) != 0)) {", 1)[1]
        block = block.split("\n  }\n", 1)[0]
        guarded = block.split("PreloadedRamdiskHeaderValid (", 1)
        self.assertEqual(len(guarded), 2)
        self.assertNotIn("ReserveMemoryRegion", guarded[0])
        self.assertIn("ReserveMemoryRegion", guarded[1].split("} else {", 1)[0])
        self.assertIn("BuildGuidDataHob (&gAppleSiliconPkgPreloadedRamdiskDecisionGuid", block)
        # The AGX local reserve keeps the window out unconditionally (layout invariant).
        self.assertIn("LocalExcluded[LocalExcludedCount++] = (AGX_LOCAL_RANGE){PcdGet64 "
                      "(PcdPreloadedRamdiskBase)", source)

    def test_dxe_registers_only_from_the_decision(self):
        source = DXE.read_text()
        self.assertIn("GetFirstGuidHob(&gAppleSiliconPkgPreloadedRamdiskDecisionGuid)", source)
        self.assertIn("RamdiskProtocol->Register(Decision->PayloadBase, Decision->PayloadSize", source)
        self.assertNotIn("Header->Magic", source)


class M1n1Test(unittest.TestCase):
    def test_standalone_clears_a_stale_header(self):
        source = STANDALONE.read_text()
        self.assertIn("memset((void *)layout->ramdisk_base, 0, 0x1000);", source)
        self.assertIn("dc_civac_range((void *)layout->ramdisk_base, 0x1000);", source)


if __name__ == "__main__":
    unittest.main()
