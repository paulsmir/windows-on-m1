"""EXP1091 0xD1: the vsync query never touches the escape buffer at DIRQL.

EXP1091 hardware: DRIVER_IRQL_NOT_LESS_OR_EQUAL (P2 IRQL 9, write) in
AdmissionScanoutVsyncSnapshot under KeSynchronizeExecution, called from
AdmissionDdiEscape for AppleAgxVsyncTrace.exe: the snapshot copied the
5,224-byte APPLE_AGX_VSYNC_QUERY straight into the escape's private-data
buffer, which is pageable, and its second page was not resident. Invariants:
- the synchronized snapshot writes only into a nonpaged allocation;
- the caller's buffer is written after DxgkCbSynchronizeExecution returns,
  and only when the snapshot ran;
- the allocation is freed on every path after the call.
"""
from pathlib import Path
import re
import unittest

ROOT = Path(__file__).resolve().parents[1]
SRC = ROOT / 'drivers/apple-agx/render-admission/src/scanout_windows.c'


def body(text, signature):
    start = text.index(signature)
    brace = text.index('{', start); depth = 1; end = brace + 1
    while depth:
        depth += (text[end] == '{') - (text[end] == '}'); end += 1
    return text[start:end]


class VsyncQueryNonpaged(unittest.TestCase):
    def test_snapshot_targets_nonpaged_memory(self):
        text = SRC.read_text()
        query = re.sub(r'\s+', ' ', body(text, 'NTSTATUS AdmissionScanoutQueryTimeline('))
        alloc = query.index('args.Query = ExAllocatePool2( POOL_FLAG_NON_PAGED')
        sync = query.index('DxgkCbSynchronizeExecution(')
        copy = query.index('RtlCopyMemory(Query, args.Query, sizeof(*Query));')
        free = query.index('ExFreePoolWithTag(args.Query, ADMISSION_SCANOUT_TAG);')
        self.assertLess(alloc, sync)
        self.assertLess(sync, copy)
        self.assertLess(copy, free)
        self.assertIn('if (NT_SUCCESS(status) && done) RtlCopyMemory(Query', query)
        self.assertNotIn('args.Query = Query;', query)
        # Every return after the allocation follows the free.
        self.assertEqual(query[alloc:].count('return '), 2)
        self.assertLess(free, query.rindex('return '))


if __name__ == '__main__':
    unittest.main()
