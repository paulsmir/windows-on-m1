"""Run the real UMD deallocation boundary against a controlled runtime callback.

Break caught: an E_INVALIDARG from the Windows callback is indistinguishable
from a local guard failure, or a 64-bit runtime resource handle is truncated.
"""
from pathlib import Path
import os
import subprocess
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / "drivers/apple-agx/render-admission/umd/src/umd_runtime_device.c"


class UmdDeallocateFailureTests(unittest.TestCase):
    def test_failure_receipt_classifies_guard_and_callback_with_full_handle(self):
        source = SOURCE.read_text()
        start = source.index("static HRESULT APIENTRY AdmissionUmdDeallocateResource(")
        end = source.index("\n}\n", start) + 2
        function = source[start:end]
        program = r'''
#include <assert.h>
#include <stdint.h>
#include <string.h>
typedef void *HANDLE;
typedef unsigned int UINT;
typedef uintptr_t ULONG_PTR;
typedef uint64_t ULONGLONG;
typedef int32_t HRESULT;
#define APIENTRY
#define E_INVALIDARG ((HRESULT)0x80070057u)
#define S_OK ((HRESULT)0)
#define FAILED(x) ((x) < 0)
#define ZeroMemory(p,n) memset((p),0,(n))
#define ARRAYSIZE(a) (sizeof(a)/sizeof((a)[0]))
typedef struct { HANDLE hResource; UINT NumAllocations; HANDLE *HandleList; } D3DDDICB_DEALLOCATE;
typedef HRESULT (*DEALLOCATE_CALLBACK)(HANDLE,const D3DDDICB_DEALLOCATE *);
typedef struct { DEALLOCATE_CALLBACK pfnDeallocateCb; } CALLBACKS;
typedef struct { CALLBACKS *KernelCallbacks; struct { HANDLE handle; } RuntimeDevice; } ADMISSION_UMD_DEVICE;
static UINT receipt[3], receipt_count, callback_count;
static HRESULT receipt_status, callback_result;
static HANDLE callback_resource;
static __attribute__((used)) void AdmissionUmdDiagnostic(const char *name,HRESULT result,const UINT *values,UINT count) {
  assert(strcmp(name,"umd-deallocate-failure")==0);
  assert(count==3);
  ++receipt_count; receipt_status=result;
  memcpy(receipt,values,sizeof(receipt));
}
static HRESULT callback(HANDLE device,const D3DDDICB_DEALLOCATE *request) {
  assert(device==(HANDLE)(uintptr_t)0x55);
  assert(request->NumAllocations==0 && request->HandleList==0);
  ++callback_count; callback_resource=request->hResource;
  return callback_result;
}
''' + function + r'''
int main(void) {
  CALLBACKS callbacks={callback};
  ADMISSION_UMD_DEVICE device={&callbacks,{(HANDLE)(uintptr_t)0x55}};
  HANDLE wide=(HANDLE)(uintptr_t)UINT64_C(0x12345678abcdef00);
  callback_result=E_INVALIDARG;
  assert(AdmissionUmdDeallocateResource(&device,wide)==E_INVALIDARG);
  assert(callback_count==1 && callback_resource==wide);
  assert(receipt_count==1 && receipt_status==E_INVALIDARG);
  assert(receipt[0]==2 && receipt[1]==0xabcdef00u && receipt[2]==0x12345678u);
  receipt_count=0;callback_count=0;
  assert(AdmissionUmdDeallocateResource(&device,0)==E_INVALIDARG);
  assert(callback_count==0 && receipt_count==1);
  assert(receipt[0]==1 && receipt[1]==0 && receipt[2]==0);
  receipt_count=0;callback_result=S_OK;
  assert(AdmissionUmdDeallocateResource(&device,wide)==S_OK);
  assert(callback_count==1 && receipt_count==0);
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
