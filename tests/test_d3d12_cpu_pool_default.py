"""WHAT REAL BUG OR INVARIANT WILL THIS TEST CATCH?

With native staging unavailable/disabled, the D3D12 owner must retain canonical CPU mappings by default,
without duplicate private storage or submitting those bytes through the staging
copy dispatcher. Only an exact CPU_POOL_MAP=0 selects private staging. Execute
the real owner configuration, allocation and transfer-dispatch bodies; retain
borrowed/shared and GPU-only behavior independently of environment spelling.
"""
from pathlib import Path
import subprocess
import tempfile
import unittest

import test_umd_cpu_pool_allocation as allocation_fixture
from test_umd_staging_write_watch import function

ROOT = Path(__file__).resolve().parents[1]
DRIVER = ROOT / "drivers/apple-agx"

ENVIRONMENT = r'''
static const char *pool_environment, *staging_environment, *native_environment="0";
static unsigned GetEnvironmentVariableA(const char *name, char *out, size_t capacity) {
 const char *value = !strcmp(name, "APPLE_AGX_D3D12_CPU_POOL_MAP") ? pool_environment :
     !strcmp(name, "APPLE_AGX_D3D12_NATIVE_STAGING") ? native_environment : staging_environment;
 if (!value) return 0;
 size_t length = strlen(value);
 // Win32 returns the required capacity, including NUL, if too small.
 if (length >= capacity) return unsigned(length + 1);
 memcpy(out, value, length + 1);
 return unsigned(length);
}
struct OWNER { ADMISSION_UMD_DEVICE Runtime; };
static void configure(OWNER *owner) {
@@CONFIG@@
}
static unsigned transfers_up, transfers_down, touches;
static int touch_device(ADMISSION_UMD_DEVICE *, unsigned long long) { ++touches; return 1; }
static int transfer_slot(ADMISSION_UMD_DEVICE *, ADMISSION_UMD_SCREEN_BUFFER *, bool down,
                         UINT *, unsigned long long *) {
 // OS copy transport boundary: the production dispatcher must not reach it
 // for a canonical direct allocation, in either direction.
 if (down) ++transfers_down; else ++transfers_up;
 return 1;
}
@@TRANSFER@@
int main() {
 CB cb = {allocate, deallocate, lock_unused, unlock_unused};
 struct Setting { const char *value; bool staged; } settings[] = {
   {nullptr, false}, {"", false}, {"0", true}, {"1", false},
   {"x", false}, {"00", false}, {"0 ", false}, {"10", false},
   {"0123456789012345678901234567890123456789", false}
 };
 for (const auto &setting : settings) {
  pool_environment = setting.value;
  for (unsigned kind = 0; kind < 6; ++kind) {
   OWNER owner = {}; configure(&owner); auto &d = owner.Runtime;
   d.Magic = ADMISSION_UMD_DEVICE_MAGIC; d.KernelCallbacks = &cb;
   // General CPU read/write, Shader, Encoder, borrowed, GPU-only, shareable.
   const UINT cls = kind == 1 ? 2u : kind == 2 ? 3u : 1u;
   const UINT flags = kind == 1 || kind == 2 ? 6u : kind == 4 ? 0x2cu : kind == 5 ? 0x1fu : 0xfu;
   const UINT borrowed = kind == 3 ? 7u : 0u;
   allocs = frees = private_allocs = refusals = 0;
   APPLE_AGX_U64 token = 0;
   const int created = AdmissionUmdScreenCreateClassBufferImpl(&d, cls, 65536, 65536, flags, &token, borrowed);
   // Shareable creation is not in this wire allocation ABI. It must stay
   // refused; imported/shared handles take the separate adoption path.
   if (kind == 5) {
    assert(!created && !token && allocs == 1 && !private_allocs && !frees);
    continue;
   }
   assert(created);
   auto &s = d.ScreenBuffers[0];
   const bool private_copy = setting.staged && kind < 3;
   if (private_allocs != unsigned(private_copy)) {
    fprintf(stderr, "CPU_POOL_MAP=%s kind=%u private_allocs=%u expected=%u\n",
            setting.value ? setting.value : "<unset>", kind, private_allocs, unsigned(private_copy));
    return 1;
   }
   assert(bool(s.PrivateStaging) == private_copy && bool(s.HeapStaging) == private_copy);
   assert(bool(s.SystemDirect) == (!borrowed && !private_copy));
   assert(parsed.CpuVisible == unsigned(!borrowed && !private_copy && kind != 4));
   // The allocation policy feeds the real upload/download dispatcher. Mark
   // CPU visibility and GPU writes so an accidental staging route is exercised.
   s.CopyHeld = TRUE; s.Mapped = kind != 4; s.GpuWritten = TRUE; s.Queried = TRUE;
   s.CanonicalGpuVa = 0x10000;
   transfers_up = transfers_down = touches = 0;
   assert(transfer_held(&d, false) && transfer_held(&d, true));
   const bool copied = borrowed || private_copy;
   assert(transfers_up == unsigned(copied));
   assert(transfers_down == unsigned(copied && (flags & AppleAgxWin32BufferGpuWrite)));
   assert(!touches && !d.ScreenBufferLock);
   free(s.PrivateStaging);
  }
 }
 // The pre-existing HEAP_STAGING=0 override remains direct even when the new
 // default is explicitly disabled; the two settings retain separate meanings.
 pool_environment = staging_environment = "0";
 OWNER owner = {}; configure(&owner); auto &d = owner.Runtime;
 d.Magic = ADMISSION_UMD_DEVICE_MAGIC; d.KernelCallbacks = &cb;
 APPLE_AGX_U64 token = 0; private_allocs = 0;
 assert(AdmissionUmdScreenCreateClassBufferImpl(&d, 1, 65536, 65536, 0xf, &token, 0));
 assert(d.ScreenBuffers[0].SystemDirect && !private_allocs && !d.ScreenBuffers[0].PrivateStaging);
 puts("D3D12 default canonical maps and staging dispatch: PASS");
 for(const auto &setting:settings) {
  native_environment=setting.value;configure(&owner);
  assert(bool(owner.Runtime.NativeStagingDisabled)==setting.staged);
 }
}
'''


class D3d12CpuPoolDefault(unittest.TestCase):
    def test_owner_default_drives_actual_allocation_and_transfer_dispatch(self):
        owner = (DRIVER / "mesa/winsys/agx_d3d12_windows.cpp").read_text()
        start = owner.index("    char heap_staging[2]")
        config = owner[start:owner.index("#endif", start)]
        screen = (DRIVER / "render-admission/umd/src/umd_win32_screen.c").read_text()
        gpuva = (DRIVER / "render-admission/umd/src/umd_gpuva_windows.c").read_text()
        prefix = allocation_fixture.HARNESS.split("int main() {")[0]
        prefix = prefix.replace("#include <cassert>", "#include <cassert>\n#include <cstdio>\n#include <cstdint>")
        prefix = prefix.replace("using UINT=unsigned;", "using ULONGLONG=unsigned long long;\nusing UINT=unsigned;")
        prefix = prefix.replace("BOOL ScreenClosing,DrawTerminal,HeapStaging,DirectCpuPool;",
                                "BOOL ScreenClosing,DrawTerminal,HeapStaging,DirectCpuPool,NativeStagingDisabled;")
        prefix = prefix.replace("struct CB {", "static int lock_unused() {return 1;}\nstatic int unlock_unused() {return 1;}\nstruct CB {")
        prefix = prefix.replace("HRESULT (*pfnDeallocateCb)(void*,D3DDDICB_DEALLOCATE*);",
                                "HRESULT (*pfnDeallocateCb)(void*,D3DDDICB_DEALLOCATE*); int (*pfnLockCb)(); int (*pfnUnlockCb)();")
        prefix = prefix.replace("BOOL Active,Transition,Borrowed,WriteWatch,HeapStaging,SystemDirect;",
                                "BOOL Active,Transition,Borrowed,WriteWatch,HeapStaging,SystemDirect;\n"
                                " BOOL Direct,Mapped,CopyHeld,GpuWritten,Queried;\n"
                                " unsigned long long CanonicalGpuVa; void *NativeBo; int (*NativeMapRelease)();\n"
                                " struct {bool Valid;} Sync;")
        prefix = prefix.replace("@@FUNCTION@@", function(screen, "AdmissionUmdScreenCreateClassBufferImpl"))
        prefix = prefix.replace(
            "assert(AdmissionWin32AllocationCreateValidateEx(r->pAllocationInfo->pPrivateDriverData,\n"
            "     r->pAllocationInfo->PrivateDriverDataSize,&parsed,&cls,&flags,256ull<<20)==AdmissionWin32TransportSuccess);",
            "if (AdmissionWin32AllocationCreateValidateEx(r->pAllocationInfo->pPrivateDriverData,\n"
            "     r->pAllocationInfo->PrivateDriverDataSize,&parsed,&cls,&flags,256ull<<20)!=AdmissionWin32TransportSuccess) return E_FAIL;")
        transfer = "\n".join(function(gpuva, name) for name in
                             ("direct_shadow_slot", "cpu_quiet", "download_due", "transfer_held"))
        code = prefix + ENVIRONMENT.replace("@@CONFIG@@", config).replace("@@TRANSFER@@", transfer)
        with tempfile.TemporaryDirectory(prefix="rc45-cpu-pool-") as tmp:
            src, exe = Path(tmp) / "default.cpp", Path(tmp) / "default"
            src.write_text(code)
            includes = ["-I", str(DRIVER / "shared/include"), "-I", str(DRIVER / "render-admission/include")]
            flags = ["-include", str(ROOT / "tests/d3d12_wait_disabled_fixture.h"), "-fsanitize=address,undefined"]
            objects = []
            for unit in (DRIVER / "render-admission/src/render_win32_transport.c",
                         DRIVER / "render-admission/src/render_allocation.c",
                         DRIVER / "shared/src/apple_agx_win32_abi.c"):
                obj = Path(tmp) / (unit.stem + ".o")
                built = subprocess.run(["clang", "-std=c11", *flags, *includes, "-c", str(unit), "-o", str(obj)],
                                       capture_output=True, text=True)
                self.assertEqual(built.returncode, 0, built.stdout + built.stderr)
                objects.append(str(obj))
            built = subprocess.run(["clang++", "-std=c++17", *flags, *includes, str(src), *objects, "-o", str(exe)],
                                   capture_output=True, text=True)
            self.assertEqual(built.returncode, 0, built.stdout + built.stderr)
            ran = subprocess.run([str(exe)], capture_output=True, text=True)
            self.assertEqual(ran.returncode, 0, ran.stdout + ran.stderr)
            self.assertIn("D3D12 default canonical maps and staging dispatch: PASS", ran.stdout)


if __name__ == "__main__":
    unittest.main()
