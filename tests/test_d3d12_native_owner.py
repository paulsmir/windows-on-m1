"""D3D12 C4b native owner (mesa/winsys/agx_d3d12_windows.cpp).

Invariants caught (investigation/analysis/D3D12-C4-copy.md):
- the owner's UMD device storage borrows a context the owner itself creates
  and destroys (never the D3D10 initializer, which needs D3D10 device args);
- CopyBufferRegion is a GPU copy (the context's resource_copy_region, whose
  Asahi buffer path runs libagx copy kernels), never the winsys CPU memcpy
  helper AgxWin32AsahiBufferCopy;
- the copy validates both byte ranges before recording;
- submission waits for the context to retire and reports faults;
- teardown order: context, screen, UMD device storage, kernel context.
"""
from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[1]
OWNER = ROOT / 'drivers/apple-agx/mesa/winsys/agx_d3d12_windows.cpp'
PROJECT = ROOT / 'drivers/apple-agx/render-admission/umd/AppleAgxRenderAdmissionUmd.vcxproj'


def body(text, name):
    start = text.index(name)
    return text[start:text.index('\n}\n', start) + 3]


class NativeOwner(unittest.TestCase):
    def test_context_and_initializer(self):
        text = OWNER.read_text()
        create = body(text, 'extern "C" HRESULT AgxD3d12OwnerCreate(')
        self.assertIn('pfnCreateContextVirtualCb(owner->RuntimeDevice, &context)', create)
        self.assertIn('AdmissionUmdRuntimeDeviceInitializeForQueue(', create)
        self.assertNotIn('AdmissionUmdRuntimeDeviceInitialize(&', create)
        self.assertLess(create.index('pfnCreateContextVirtualCb'), create.index('AdmissionUmdRuntimeDeviceInitializeForQueue('))
        self.assertLess(create.index('AgxWin32AsahiScreenCreateForWindows'), create.index('AgxWin32AsahiContextCreate'))
        # EXP1365: the KMD binds one Win32 generation per kernel device
        self.assertIn('contract.Generation = args->Generation;', create)
        self.assertIn('owner->KernelContext, args->Generation);', create)

    def test_gpu_copy(self):
        text = OWNER.read_text()
        copy = body(text, 'extern "C" HRESULT AgxD3d12OwnerCopy(')
        self.assertIn('->resource_copy_region(', copy)
        self.assertNotIn('AgxWin32AsahiBufferCopy', text)
        self.assertIn('bytes > dst->Bytes - dst_offset', copy)
        self.assertIn('bytes > src->Bytes - src_offset', copy)
        submit = body(text, 'extern "C" HRESULT AgxD3d12OwnerSubmitAndWait(')
        self.assertLess(submit.index('->flush('), submit.index('AgxWin32AsahiContextRetire('))
        self.assertIn('AgxWin32AsahiContextFaulted(', submit)

    def test_teardown_order(self):
        text = OWNER.read_text()
        destroy = body(text, 'extern "C" HRESULT AgxD3d12OwnerDestroy(')
        order = [destroy.index(k) for k in ('AgxWin32AsahiContextDestroy', 'AgxWin32AsahiScreenDestroy',
                                            'AdmissionUmdRuntimeDeviceFinalize', 'destroy_kernel_context(owner)')]
        self.assertEqual(order, sorted(order))

    def test_built_only_with_the_native_frontend(self):
        project = PROJECT.read_text()
        group = project[project.index("<ItemGroup Condition=\"'$(EnableNativeFrontend)' == 'true'\">"):]
        group = group[:group.index('</ItemGroup>')]
        self.assertIn('agx_d3d12_windows.cpp', group)
        self.assertIn('APPLE_AGX_D3D12_NATIVE_OWNER=1', project)


if __name__ == '__main__':
    unittest.main()
