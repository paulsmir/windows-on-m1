"""Native Mu must map the EL2 ANS broker window in its stage-1 table.

Real bug (ANS001-R5, 2026-11-06): the ExitBootServices handoff gate read
ANS_BROKER_BASE (0x300010000) while no Mu memory-map region covered it, so
DXE took a translation fault inside CoreExitBootServices and recursed.
"""
import json
import re
import unittest
from pathlib import Path

R = Path(__file__).resolve().parents[1]
DSC = R / 'mu/Platform/MacBookAirMid2020Pkg/MacBookAirMid2020.dsc'
MAP = R / 'mu/Silicon/Apple/T810XFamilyPkg/Library/MemoryInitPeiLib/MemoryInitPeiLib.c'
DEFS = R / 'mu/Silicon/Apple/T810XFamilyPkg/Include/Library/T810XFamilyVirtualMemoryMapDefines.h'


def native_pcds(text):
    out = {}
    for block in re.findall(r'!if \$\(J313_ANS_NATIVE_PROFILE\) == TRUE\n(.*?)!endif', text, re.S):
        for name, value in re.findall(r'gAppleSiliconPkgTokenSpaceGuid\.(PcdAnsBrokerWindow\w+)\|(0x[0-9a-fA-F]+)', block):
            out[name] = int(value, 16)
    return out


class MuBrokerWindow(unittest.TestCase):
    def test_native_dsc_maps_exact_broker_window(self):
        broker = json.loads((R / 'config/j313-ans.json').read_text())['broker']
        pcds = native_pcds(DSC.read_text())
        self.assertEqual(pcds.get('PcdAnsBrokerWindowBase'), int(broker['guest_base'], 0))
        self.assertEqual(pcds.get('PcdAnsBrokerWindowSize'), broker['size'])
        # Only the native profile may map it.
        outside = re.sub(r'!if \$\(J313_ANS_NATIVE_PROFILE\) == TRUE\n.*?!endif', '', DSC.read_text(), flags=re.S)
        self.assertNotIn('PcdAnsBrokerWindow', outside)

    def test_memory_map_adds_device_entry_with_capacity(self):
        src = MAP.read_text()
        entry = re.search(r'PcdGet32 \(PcdAnsBrokerWindowSize\) != 0\) \{(.*?)\}', src, re.S)
        self.assertIsNotNone(entry)
        self.assertIn('ARM_MEMORY_REGION_ATTRIBUTE_DEVICE', entry.group(1))
        cap = int(re.search(r'#define MAX_VIRTUAL_MEMORY_MAP_DESCRIPTORS (\d+)', src).group(1))
        used = src.count('VirtualMemoryTable[++Index]') + 1  # + entry 0
        self.assertLessEqual(used, cap)

    def test_broker_window_is_not_inside_an_existing_mmio_range(self):
        # Proves why the explicit entry is required (no implicit coverage).
        defs = DEFS.read_text()
        sizes = {'SIZE_64KB': 1 << 16, 'SIZE_2MB': 1 << 21, 'SIZE_512MB': 1 << 29,
                 'SIZE_1GB': 1 << 30, 'SIZE_2GB': 1 << 31}
        base = 0x300010000
        for name, b in re.findall(r'#define (APPLE_\w+_RANGE_\d+)_BASE (0x[0-9A-Fa-f]+)', defs):
            s = re.search(rf'#define {name}_SIZE (\S+)', defs).group(1)
            size = sizes.get(s) or int(s, 0)
            self.assertFalse(int(b, 16) <= base < int(b, 16) + size, name)


if __name__ == '__main__':
    unittest.main()
