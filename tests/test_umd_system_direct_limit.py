"""EXP1111/C75: unshared Mesa buffers up to the KMD class limit are direct.

EXP1110: Notepad downloaded its 2.4 MiB mapped buffer through ~1500 64-KiB
copy escapes per second, because only buffers up to 1 MiB were one
CPU-visible allocation mapped by both the CPU and the GPU. EXP1111 raised
the threshold to the KMD's then 16 MiB class limit. EXP1269 raised the KMD's
General-class limit to the segment-3 size, but the UMD kept 16 MiB: in
EXP1899N Night Raid uploaded a 25 MiB buffer 416 times and a 64 MiB buffer
72 times through copy escapes, and GravityMark D3D11 (EXP1908) downloaded
89/221 MiB buffers the same way.
Invariant: the direct decision compares against the class limit the KMD
reports (AGX_WIN32_BUFFER_CLASS_INFO.MaximumBytes), never a UMD constant,
and that limit for General BOs is the segment-sized AdmissionGeneralMaximumBytes.
"""
from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[1]


class SystemDirectLimit(unittest.TestCase):
    def test_direct_threshold_is_the_reported_class_limit(self):
        umd = (ROOT / 'drivers/apple-agx/render-admission/umd/src/umd_win32_screen.c').read_text()
        self.assertIn('systemDirect = !BorrowedStaging && Bytes <= classInfo->MaximumBytes;', umd)
        self.assertNotIn('ADMISSION_UMD_SYSTEM_DIRECT_BYTES', umd)
        internal = (ROOT / 'drivers/apple-agx/render-admission/umd/src/umd_internal.h').read_text()
        self.assertNotIn('ADMISSION_UMD_SYSTEM_DIRECT_BYTES', internal)

    def test_general_class_limit_is_the_segment(self):
        kmd = (ROOT / 'drivers/apple-agx/render-admission/src/lifecycle.c').read_text()
        self.assertIn('info->Classes[0].MaximumBytes = AdmissionGeneralMaximumBytes(context);', kmd)


if __name__ == '__main__':
    unittest.main()
