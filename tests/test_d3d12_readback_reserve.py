"""rc99: READBACK heaps reach the native readback reserve.

WHAT REAL BUG OR INVARIANT WILL THIS TEST CATCH? EXP2031/EXP2033: three of
GravityMark's four 512 KiB READBACK heaps fell back to private staging
because the native arenas were full when they were created. Every frame then
completed one submission synchronously and downloaded 512 KiB through copy
escapes (4.1 ms of GPU idle before the next job). The arena keeps a reserve
for them (test_umd_native_staging_arena), but only a heap whose CPU page
property is WRITE_BACK may use it, the intent must be set and cleared under
the owner lock (another thread's BO creation must never see it), and the
APPLE_AGX_D3D12_READBACK_RESERVE=0 kill switch must turn the reserve off.
"""
from pathlib import Path
import re
import unittest

ROOT = Path(__file__).resolve().parents[1]
DDI = ROOT / 'drivers/apple-agx/d3d12/agx_d3d12_ddi.cpp'
OWNER = ROOT / 'drivers/apple-agx/mesa/winsys/agx_d3d12_windows.cpp'
SCREEN = ROOT / 'drivers/apple-agx/render-admission/umd/src/umd_win32_screen.c'


def body(text, start):
    text = text[text.index(start):]
    return text[:text.index('\n}\n')]


class ReadbackReserve(unittest.TestCase):
    def test_create_heap_sets_the_intent_only_for_write_back_under_the_owner_lock(self):
        create = body(DDI.read_text(), 'static HRESULT CreateHeap(')
        self.assertIn('arg.CPUPageProperty == D3D12DDI_CPU_PAGE_PROPERTY_WRITE_BACK', create)
        order = ['AgxD3d12OwnerLock(owner);', 'AgxD3d12OwnerSetReadbackIntent(owner, TRUE);',
                 'AgxD3d12OwnerCreateBuffer(owner, heap->bytes, heap->cpu_visible, &buffer)',
                 'AgxD3d12OwnerSetReadbackIntent(owner, FALSE);', 'AgxD3d12OwnerUnlock(owner);']
        at = [create.index(token) for token in order]
        self.assertEqual(at, sorted(at), 'intent must bracket the heap BO creation under the lock')
        # Every path between setting and clearing reaches the clear (no return).
        between = create[at[1]:at[3]]
        self.assertNotIn('return', between)

    def test_owner_sets_the_device_intent_and_reads_the_kill_switch(self):
        owner = OWNER.read_text()
        intent = body(owner, 'extern "C" void AgxD3d12OwnerSetReadbackIntent(')
        self.assertIn('OwnerLock lock(owner);', intent)
        self.assertIn('owner->Runtime.NativeReadbackIntent = readback ? TRUE : FALSE;', intent)
        self.assertRegex(owner, re.compile(
            r'owner->Runtime\.NativeReadbackReserve =\s*GetEnvironmentVariableA\("APPLE_AGX_D3D12_READBACK_RESERVE",'
            r'[^;]*readback_reserve\[0\] == \'0\'\s*\? 0u : ADMISSION_UMD_NATIVE_READBACK_RESERVE;'))

    def test_screen_uses_the_reserve_only_with_the_intent(self):
        reserve = body(SCREEN.read_text(), 'static BOOL AdmissionUmdNativeStagingReserve(')
        self.assertIn('Device->NativeReadbackIntent && Device->NativeReadbackReserve', reserve)
        ordinary = reserve[:reserve.index('Device->NativeReadbackIntent')]
        self.assertIn('Device->NativeReadbackReserve, 0)', ordinary)
        self.assertNotIn('AdmissionUmdNativeSpansReserve(', reserve)


if __name__ == '__main__':
    unittest.main()
