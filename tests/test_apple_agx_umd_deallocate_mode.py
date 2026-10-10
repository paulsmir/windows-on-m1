"""Use an allocation handle when a non-shared resource has no KM resource.

EXP963 observed that resource-handle deallocation for this exact class returned
E_INVALIDARG even though pfnAllocateCb supplied a valid allocation handle.
"""

from pathlib import Path
import os
import subprocess
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / "drivers/apple-agx/render-admission/umd/src/umd_runtime_device.c"


class UmdDeallocateModeTests(unittest.TestCase):
    def test_nonshared_without_km_resource_uses_allocation_list(self):
        source = SOURCE.read_text()
        start = source.index("static HRESULT APIENTRY AdmissionUmdDeallocateResource(")
        end = source.index("\n}\n", start) + 2
        function = source[start:end]
        program = r'''
#include <assert.h>
#include <stdint.h>
#include <string.h>
typedef void *HANDLE;
typedef unsigned UINT;
typedef UINT D3DKMT_HANDLE;
typedef uintptr_t ULONG_PTR;
typedef uint64_t ULONGLONG;
typedef int32_t HRESULT;
typedef int BOOL;
#define APIENTRY
#define E_INVALIDARG ((HRESULT)0x80070057u)
#define S_OK ((HRESULT)0)
#define FAILED(x) ((x)<0)
#define ZeroMemory(p,n) memset((p),0,(n))
#define ARRAYSIZE(a) (sizeof(a)/sizeof((a)[0]))
typedef struct { HANDLE hResource; UINT NumAllocations; const UINT *HandleList; } D3DDDICB_DEALLOCATE;
typedef HRESULT (*CALLBACK)(HANDLE,const D3DDDICB_DEALLOCATE *);
typedef struct { CALLBACK pfnDeallocateCb; } CALLBACKS;
typedef struct { HANDLE RuntimeResource; UINT KernelResource,KernelAllocation,Origin; BOOL Primary,Shared; } ADMISSION_UMD_RETIREMENT;
typedef struct { CALLBACKS *KernelCallbacks; struct { HANDLE handle; } RuntimeDevice; } ADMISSION_UMD_DEVICE;
static UINT calls, allocation_seen, receipt_stage;
static HANDLE resource_seen;
static HRESULT result_seen;
static void AdmissionUmdDiagnostic(const char *name,HRESULT result,const UINT *values,UINT count) {
  assert(strcmp(name,"umd-deallocate-failure")==0 && count==3);
  receipt_stage=values[0];result_seen=result;
}
/* EXP1131: a successful deallocation makes the screen slots forget the handle. */
static unsigned forget_calls;
static __attribute__((used)) void AdmissionUmdScreenForgetAllocation(ADMISSION_UMD_DEVICE *device,UINT allocation) {
  (void)device;(void)allocation;++forget_calls;
}
static HRESULT callback(HANDLE device,const D3DDDICB_DEALLOCATE *request) {
  assert(device==(HANDLE)(uintptr_t)0x55);
  ++calls;resource_seen=request->hResource;
  if(request->hResource) {
    assert(request->NumAllocations==0 && request->HandleList==0);
    return E_INVALIDARG;
  }
  assert(request->NumAllocations==1 && request->HandleList);
  allocation_seen=request->HandleList[0];
  return S_OK;
}
''' + function + r'''
int main(void) {
  CALLBACKS callbacks={callback};
  ADMISSION_UMD_DEVICE device={&callbacks,{(HANDLE)(uintptr_t)0x55}};
  HANDLE wide=(HANDLE)(uintptr_t)UINT64_C(0x12345678abcdef00);
  ADMISSION_UMD_RETIREMENT r={wide,0,0x80001140u,1,0,0};
  assert(AdmissionUmdDeallocateResource(&device,&r)==S_OK);
  assert(calls==1 && resource_seen==0 && allocation_seen==0x80001140u);
  assert(receipt_stage==0);
  r.Shared=1;calls=0;resource_seen=0;allocation_seen=0;
  assert(AdmissionUmdDeallocateResource(&device,&r)==E_INVALIDARG);
  assert(calls==1 && resource_seen==wide && allocation_seen==0);
  assert(receipt_stage==2 && result_seen==E_INVALIDARG);
  r.Shared=0;r.KernelResource=0x44;calls=0;receipt_stage=0;
  assert(AdmissionUmdDeallocateResource(&device,&r)==E_INVALIDARG);
  assert(calls==1 && resource_seen==wide && receipt_stage==2);
  return 0;
}
'''
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp) / "test.c"
            binary = Path(tmp) / "test"
            path.write_text(program)
            subprocess.run([
                os.environ.get("CC", "clang"), "-std=c11", "-Wall", "-Wextra",
                "-Werror", "-fsanitize=address,undefined", str(path), "-o",
                str(binary),
            ], check=True, cwd=ROOT)
            subprocess.run([str(binary)], check=True, cwd=ROOT)


if __name__ == "__main__":
    unittest.main()
