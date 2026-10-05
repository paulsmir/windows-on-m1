from pathlib import Path
import os
import subprocess
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[1]
RENDER = ROOT / "drivers/apple-agx/render-admission"


class BltProbeTests(unittest.TestCase):
    def test_probe_records_every_executed_copy_and_reports_overflow(self):
        frontend = (RENDER / "src/blt_probe_windows.c").read_text()
        frontend = frontend.replace('#include "render_admission.h"', '')
        shim = r'''
#include <assert.h>
#include <stdint.h>
#include <string.h>
#include "render_qualification.h"
#define _Use_decl_annotations_
#define APPLE_AGX_BLT_PROBE_QUALIFICATION 1
#define APPLE_AGX_VERSION_BUILD 809u
#define STATUS_SUCCESS 0
#define STATUS_INVALID_PARAMETER (-1)
#define RtlZeroMemory(p,n) memset((p),0,(n))
#define KeMemoryBarrier() __sync_synchronize()
typedef int NTSTATUS;
typedef int LONG;
typedef unsigned int ULONG;
typedef uintptr_t ULONG_PTR;
typedef void VOID;
typedef struct _ADMISSION_CONTEXT {
  int Started;
  unsigned int Win32BootGeneration;
  ADMISSION_BLT_PROBE BltProbe;
} ADMISSION_CONTEXT;
static LONG InterlockedIncrement(volatile LONG *p){return __atomic_add_fetch(p,1,__ATOMIC_SEQ_CST);}
static LONG InterlockedCompareExchange(volatile LONG *p,LONG v,LONG c){__atomic_compare_exchange_n(p,&c,v,0,__ATOMIC_SEQ_CST,__ATOMIC_SEQ_CST);return c;}
static LONG InterlockedExchange(volatile LONG *p,LONG v){return __atomic_exchange_n(p,v,__ATOMIC_SEQ_CST);}
'''
        test = r'''
int main(void) {
  ADMISSION_CONTEXT context = {0};
  ADMISSION_BLT_PROBE query = {0};
  ADMISSION_BLT_EXECUTION event = {0};
  unsigned int i;
  context.Started = 1;
  context.Win32BootGeneration = 0x1234u;
  query.Magic = ADMISSION_BLT_PROBE_MAGIC;
  query.Version = ADMISSION_BLT_PROBE_VERSION;
  query.Bytes = sizeof(query);
  assert(AdmissionBltProbeQueryWindows(&context, &query) == STATUS_SUCCESS);
  assert(query.CpuBltExecutions == 0u && query.EventCount == 0u);
  assert(query.BootGeneration == 0x1234u);
  assert(query.AdapterToken == (uintptr_t)&context);
  event.SourceSegment = 1u;
  event.DestinationSegment = 2u;
  event.SourceAddress = 0x10000ULL;
  event.DestinationAddress = 0x1500030000ULL;
  event.DestinationHostPa = 0x9bc020000ULL;
  event.BytesCopied = 4096ULL;
  for (i = 0; i <= ADMISSION_BLT_PROBE_CAPACITY; ++i)
    AdmissionBltProbeRecordWindows(&context, &event);
  query.Magic = ADMISSION_BLT_PROBE_MAGIC;
  query.Version = ADMISSION_BLT_PROBE_VERSION;
  query.Bytes = sizeof(query);
  assert(AdmissionBltProbeQueryWindows(&context, &query) == STATUS_SUCCESS);
  assert(query.CpuBltExecutions == ADMISSION_BLT_PROBE_CAPACITY + 1u);
  assert(query.EventCount == ADMISSION_BLT_PROBE_CAPACITY);
  assert(query.Overflow == 1u);
  assert(query.Events[0].Valid == 1u && query.Events[0].Sequence == 1u);
  assert(query.Events[0].DestinationHostPa == 0x9bc020000ULL);
  assert(query.Events[0].BytesCopied == 4096ULL);
  query.Version++;
  assert(AdmissionBltProbeQueryWindows(&context, &query) == STATUS_INVALID_PARAMETER);
  return 0;
}
'''
        with tempfile.TemporaryDirectory() as tmp:
            source = Path(tmp) / "probe.c"
            binary = Path(tmp) / "probe"
            source.write_text(shim + frontend + test)
            subprocess.run([
                os.environ.get("CC", "clang"), "-std=c11", "-Wall", "-Wextra",
                "-Werror", "-fsanitize=address,undefined", "-I", str(RENDER / "include"),
                str(source), "-o", str(binary),
            ], check=True, cwd=ROOT)
            subprocess.run([str(binary)], check=True, cwd=ROOT)

    def test_route_and_copy_point_are_instrumented(self):
        callbacks = (RENDER / "src/callbacks.c").read_text()
        virtual = (RENDER / "src/gpuva_g3_windows.c").read_text()
        submit = (RENDER / "src/paging_windows.c").read_text()
        execute = (RENDER / "src/memory_runtime_windows.c").read_text()
        self.assertIn("AdmissionBltProbeQueryWindows", callbacks)
        self.assertIn("BltProbe.PresentCalls", callbacks)
        self.assertIn("BltProbe.PresentBltCalls", callbacks)
        self.assertIn("BltProbe.VirtualSubmitCalls", virtual)
        self.assertIn("BltProbe.PhysicalPresentSubmits", submit)
        self.assertIn("AdmissionBltProbeRecordWindows", execute)
        client = (ROOT / "drivers/apple-agx/windows/one-shot/apple_agx_blt_probe.c").read_text()
        self.assertIn("D3DKMTEnumAdapters2", client)
        self.assertIn("D3DKMTCloseAdapter", client)
        self.assertIn("BLT_ADAPTER index=", client)
        self.assertIn("D3DKMTEscape", client)
        self.assertIn("BLT_PROBE", client)
        project = (RENDER / "AppleAgxRenderAdmission.vcxproj").read_text()
        builder = (RENDER / "scripts/build-driver.ps1").read_text()
        self.assertIn("APPLE_AGX_BLT_PROBE_QUALIFICATION=1", project)
        self.assertIn("AppleAgxBltProbeQualification=$bltProbeQualificationValue", builder)


if __name__ == "__main__":
    unittest.main()
