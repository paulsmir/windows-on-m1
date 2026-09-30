from pathlib import Path
import os
import subprocess
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[1]
RENDER = ROOT / "drivers/apple-agx/render-admission"


class DwmDdiProbeTests(unittest.TestCase):
    def test_registered_routes_and_read_only_client(self):
        driver = (RENDER / "src/driver.c").read_text()
        callbacks = (RENDER / "src/callbacks.c").read_text()
        display = (RENDER / "src/display.c").read_text()
        virtual = (RENDER / "src/gpuva_g3_windows.c").read_text()
        client = (ROOT / "drivers/apple-agx/windows/one-shot/apple_agx_blt_probe.c").read_text()
        self.assertIn("initialization.DxgkDdiPresent = AdmissionDdiPresent", driver)
        self.assertIn("AdmissionDwmRecordPresent(adapter, Context, Present", callbacks)
        self.assertIn("AdmissionDwmDdiProbeQueryWindows", callbacks)
        self.assertIn("event.Kind = AdmissionDwmDdiSourceAddress", display)
        self.assertIn("AdmissionDwmRecordCommit(context, DdiId, Args, status)", display)
        self.assertIn("AdmissionDdiSubmitCommandVirtualInner(Adapter, Args)", virtual)
        self.assertIn("event.Kind = Args != NULL && Args->Flags.Present", virtual)
        self.assertIn("DWM_DDI_ENTRY kind=", client)
        self.assertNotIn("DxgkDdiPresentDisplayOnly =", driver)
        self.assertNotIn("DxgkDdiSetVidPnSourceAddressWithMultiPlaneOverlay =", driver)

    def test_probe_compiles_in_the_gpuva_g3_package_profile(self):
        header = (RENDER / "include/render_admission.h").read_text()
        probe = (RENDER / "src/dwm_ddi_probe_windows.c").read_text()
        callbacks = (RENDER / "src/callbacks.c").read_text()
        display = (RENDER / "src/display.c").read_text()
        virtual = (RENDER / "src/gpuva_g3_windows.c").read_text()
        for source in (header, probe, callbacks, display, virtual):
            self.assertIn("defined(APPLE_AGX_GPUVA_G3_QUALIFICATION)", source)
        self.assertIn("defined(APPLE_AGX_GPUVA_G3_QUALIFICATION)",
                      callbacks[callbacks.index("AdmissionDdiEscape("):])

    def test_counter_and_last_value_replay(self):
        source = (RENDER / "src/dwm_ddi_probe_windows.c").read_text()
        source = source.replace('#include "render_admission.h"', '')
        shim = r'''
#include <assert.h>
#include <stdint.h>
#include <string.h>
#include "render_qualification.h"
#define _Use_decl_annotations_
#define APPLE_AGX_SUBMIT_QUALIFICATION 1
#define APPLE_AGX_VERSION_BUILD 896u
#define STATUS_SUCCESS 0
#define STATUS_INVALID_PARAMETER (-1)
#define RtlZeroMemory(p,n) memset((p),0,(n))
#define KeMemoryBarrier() __sync_synchronize()
typedef int NTSTATUS;
typedef long LONG;
typedef unsigned int ULONG;
typedef uintptr_t ULONG_PTR;
typedef void VOID;
typedef struct _ADMISSION_CONTEXT {
  int Started;
  unsigned int Win32BootGeneration;
  ADMISSION_DWM_DDI_PROBE DwmDdiProbe;
} ADMISSION_CONTEXT;
static LONG InterlockedIncrement(volatile LONG *p){return __atomic_add_fetch(p,1,__ATOMIC_SEQ_CST);}
static LONG InterlockedCompareExchange(volatile LONG *p,LONG v,LONG c){__atomic_compare_exchange_n(p,&c,v,0,__ATOMIC_SEQ_CST,__ATOMIC_SEQ_CST);return c;}
static LONG InterlockedExchange(volatile LONG *p,LONG v){return __atomic_exchange_n(p,v,__ATOMIC_SEQ_CST);}
void AdmissionDwmDdiProbeRecordWindows(ADMISSION_CONTEXT *, const ADMISSION_DWM_DDI_EVENT *);
NTSTATUS AdmissionDwmDdiProbeQueryWindows(ADMISSION_CONTEXT *, ADMISSION_DWM_DDI_PROBE *);
'''
        test = r'''
int main(void) {
  ADMISSION_CONTEXT context = {0};
  ADMISSION_DWM_DDI_PROBE query = {0};
  ADMISSION_DWM_DDI_EVENT event = {0};
  context.Started = 1;
  context.Win32BootGeneration = 42;
  event.Kind = AdmissionDwmDdiSourceAddress;
  event.Allocation = 0x12340000ULL;
  event.Address = 0x1050000ULL;
  event.Segment = 2;
  event.Status = 0xc000000dU;
  AdmissionDwmDdiProbeRecordWindows(&context, &event);
  event.Address = 0x1060000ULL;
  event.Status = 0;
  AdmissionDwmDdiProbeRecordWindows(&context, &event);
  event.Kind = AdmissionDwmDdiPresentBlt;
  event.Address = 0;
  AdmissionDwmDdiProbeRecordWindows(&context, &event);
  query.Magic = ADMISSION_DWM_DDI_PROBE_MAGIC;
  query.Version = ADMISSION_DWM_DDI_PROBE_VERSION;
  query.Bytes = sizeof(query);
  assert(AdmissionDwmDdiProbeQueryWindows(&context, &query) == STATUS_SUCCESS);
  assert(query.CandidateBuild == 896 && query.BootGeneration == 42);
  assert(query.Entries[AdmissionDwmDdiSourceAddress].Count == 2);
  assert(query.Entries[AdmissionDwmDdiSourceAddress].Last.Address == 0x1060000ULL);
  assert(query.Entries[AdmissionDwmDdiSourceAddress].Last.Allocation == 0x12340000ULL);
  assert(query.Entries[AdmissionDwmDdiSourceAddress].Last.Status == 0);
  assert(query.Entries[AdmissionDwmDdiPresentBlt].Count == 1);
  assert(query.Entries[AdmissionDwmDdiMpo].Count == 0);
  assert(query.Entries[AdmissionDwmDdiDisplayOnly].Count == 0);
  query.Version++;
  assert(AdmissionDwmDdiProbeQueryWindows(&context, &query) == STATUS_INVALID_PARAMETER);
  return 0;
}
'''
        with tempfile.TemporaryDirectory() as temp:
            path = Path(temp) / "probe.c"
            executable = Path(temp) / "probe"
            path.write_text(shim + source + test)
            subprocess.run([
                os.environ.get("CC", "clang"), "-std=c11", "-Wall", "-Wextra",
                "-Werror", "-fsanitize=address,undefined", "-I", str(RENDER / "include"),
                str(path), "-o", str(executable),
            ], check=True, cwd=ROOT)
            subprocess.run([str(executable)], check=True, cwd=ROOT)


if __name__ == "__main__":
    unittest.main()
