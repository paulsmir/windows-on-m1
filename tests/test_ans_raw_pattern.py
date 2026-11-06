"""ANS008: ansraw.exe (C) and the recovery/host reference (Python) must agree byte for byte,
or a correct native write would be judged as corruption (or a wrong one as success)."""
import hashlib
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools'))
import ans_raw_pattern  # noqa: E402

SOURCE = r'''
#include "ansraw_pattern.h"
#include <stdio.h>
#include <stdlib.h>
int main(int argc,char**argv){uint8_t b[ANSRAW_BLOCK];
 for(int i=1;i<argc;i++){ansraw_fill_block(strtoull(argv[i],0,0),b);fwrite(b,1,sizeof b,stdout);}return 0;}
'''


class RawPatternTests(unittest.TestCase):
    def test_c_and_python_blocks_identical(self):
        cc = shutil.which('cc')
        self.assertIsNotNone(cc)
        lbas = ['0', '1', '30912256', '30916351', str(1 << 40), str((1 << 52) - 1)]
        with tempfile.TemporaryDirectory(dir=ROOT / '.local/ans-native') as d:
            src = Path(d) / 'p.c'
            src.write_text(SOURCE)
            exe = Path(d) / 'p'
            subprocess.run([cc, '-std=c11', '-Wall', '-Wextra', '-Werror', '-I',
                            str(ROOT / 'drivers/storage/appleans/tools/ansraw'), str(src), '-o', str(exe)],
                           check=True)
            out = subprocess.run([str(exe), *lbas], check=True, stdout=subprocess.PIPE).stdout
        expected = b''.join(ans_raw_pattern.block(int(v)) for v in lbas)
        self.assertEqual(out, expected)

    def test_blocks_are_lba_unique_and_never_zero(self):
        seen = {ans_raw_pattern.block(lba) for lba in range(30912256, 30912256 + 64)}
        self.assertEqual(len(seen), 64)
        self.assertNotIn(bytes(4096), seen)

    def test_msr_reference_hashes(self):
        # J313 Microsoft Reserved partition: LBA 30912256, 4096 blocks (recovery Get-Partition).
        self.assertEqual(ans_raw_pattern.zero_sha256(4096),
                         '080acf35a507ac9849cfcba47dc2ad83e01b75663a516279c8b9d243b719643e')
        self.assertEqual(ans_raw_pattern.zero_sha256(4096),
                         hashlib.sha256(bytes(4096 * 4096)).hexdigest())


if __name__ == '__main__':
    unittest.main()
