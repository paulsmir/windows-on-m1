"""Installed native boot contract (ANS010): the Mu ESP pin and the EL2 write window.

Invariants this catches:
- the firmware boot gate admits a different ESP than the one the window and the launcher use;
- the write window grows into macOS/APFS/Apple recovery or the second ESP, or leaves out a
  Windows-mounted volume (ANS009-R1..R3: any writable volume outside the window wedges Windows);
- the qualification profile seals a window other than the recorded one.
"""
import json
import re
import unittest
import uuid
from pathlib import Path

R = Path(__file__).resolve().parents[1]
CFG = json.loads((R / 'config/j313-ans-installed-boot.json').read_text())
DSC = (R / 'mu/Platform/MacBookAirMid2020Pkg/MacBookAirMid2020.dsc').read_text()
B = 4096


def installed_pcds():
    m = re.search(r'!if \$\(J313_ANS_INSTALLED_BOOT\) == TRUE\n(.*?)!endif', DSC, re.S)
    assert m, 'installed PCD block missing'
    block = m.group(1)
    start = int(re.search(r'PcdAnsInstalledEspStartLba\|(0x[0-9A-Fa-f]+)', block).group(1), 16)
    blocks = int(re.search(r'PcdAnsInstalledEspBlocks\|(0x[0-9A-Fa-f]+)', block).group(1), 16)
    guid = bytes(int(x, 16) for x in re.search(r'PcdAnsInstalledEspGuid\|\{([^}]*)\}', block).group(1).split(','))
    return start, blocks, guid


class InstalledBoot(unittest.TestCase):
    def test_dsc_pins_the_recorded_windows_esp(self):
        start, blocks, guid = installed_pcds()
        esp = CFG['esp']
        part = next(p for p in CFG['partitions'] if p['index'] == esp['index'])
        self.assertEqual(part['gpt_type'], 'c12a7328-f81f-11d2-ba4b-00a0c93ec93b')
        self.assertEqual((start, blocks), (esp['start_lba'], esp['blocks']))
        self.assertEqual((start * B, blocks * B), (part['offset'], part['size']))
        self.assertEqual(guid, uuid.UUID(esp['guid']).bytes_le)

    def test_installed_pcds_only_inside_native_profile(self):
        outside = re.sub(r'!if \$\(J313_ANS_NATIVE_PROFILE\) == TRUE\n.*?!endif\n!endif', '', DSC, flags=re.S)
        self.assertNotIn('PcdAnsInstalledEsp', outside)

    def test_window_is_exactly_the_windows_partitions(self):
        w = CFG['write_window']
        lo, hi = w['first_lba'] * B, (w['first_lba'] + w['count']) * B
        self.assertLessEqual(w['first_lba'] + w['count'], CFG['namespace_blocks'])
        covered = [p for p in CFG['partitions'] if p['index'] in w['covers_indices']]
        self.assertEqual(lo, min(p['offset'] for p in covered))
        self.assertEqual(hi, max(p['offset'] + p['size'] for p in covered))
        for p in CFG['partitions']:
            inside = lo <= p['offset'] and p['offset'] + p['size'] <= hi
            overlap = p['offset'] < hi and lo < p['offset'] + p['size']
            if p['index'] in w['covers_indices']:
                self.assertTrue(inside, p)
                self.assertIn(p['gpt_type'], ('c12a7328-f81f-11d2-ba4b-00a0c93ec93b',
                                              'e3c9e316-0b5c-4db8-817d-f92df00215ae',
                                              'ebd0a0a2-b9e5-4433-87c0-68b6b72699c7'), p)
            else:
                self.assertFalse(overlap, p)  # APFS, iBoot, Apple recovery, second ESP
        self.assertEqual(w['first_lba'], CFG['esp']['start_lba'])

    def test_qualification_profile_seals_the_recorded_window(self):
        import sys
        sys.path.insert(0, str(R))
        from tools.ans_qualification import PROFILES
        p = PROFILES['ANS010']
        w = CFG['write_window']
        self.assertEqual(p['write_window'], (w['first_lba'], w['count']))
        self.assertEqual(p['max_write_blocks'], w['count'])
        self.assertTrue(p['installed_boot'])


if __name__ == '__main__':
    unittest.main()
