from pathlib import Path
import os
import subprocess
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[1]
RENDER = ROOT / "drivers/apple-agx/render-admission"


class StandardPresentTraceTests(unittest.TestCase):
    def test_windows_trace_is_bounded_rearmable_and_boot_scoped(self):
        frontend = (RENDER / "src/standard_present_windows.c").read_text()
        frontend = frontend.replace('#include "render_admission.h"', '')
        shim = r'''
#include <stdint.h>
#include <string.h>
#include "render_qualification.h"
#define _Use_decl_annotations_
#define APPLE_AGX_SUBMIT_QUALIFICATION 1
#define APPLE_AGX_VERSION_BUILD 641u
#define TRUE 1
#define FALSE 0
#define STATUS_SUCCESS 0
#define STATUS_INVALID_PARAMETER (-1)
#define STATUS_DEVICE_NOT_READY (-2)
#define RtlZeroMemory(p,n) memset((p),0,(n))
#define RtlCopyMemory(p,q,n) memcpy((p),(q),(n))
#define KeMemoryBarrier() __sync_synchronize()
typedef int NTSTATUS;
typedef int LONG;
typedef unsigned int ULONG;
typedef void VOID;
typedef struct { unsigned int BootGeneration; } TEST_CORRELATION;
typedef struct _ADMISSION_CONTEXT {
  int Started;
  TEST_CORRELATION RenderCorrelation;
  volatile LONG StandardPresentTraceArmed;
  volatile LONG StandardPresentTraceNext;
  volatile LONG StandardPresentTraceOverflow;
  ADMISSION_STANDARD_PRESENT_TRACE StandardPresentTrace;
} ADMISSION_CONTEXT;
static LONG InterlockedExchange(volatile LONG *p,LONG v){return __atomic_exchange_n(p,v,__ATOMIC_SEQ_CST);}
static LONG InterlockedCompareExchange(volatile LONG *p,LONG v,LONG c){__atomic_compare_exchange_n(p,&c,v,0,__ATOMIC_SEQ_CST,__ATOMIC_SEQ_CST);return c;}
static LONG InterlockedIncrement(volatile LONG *p){return __atomic_add_fetch(p,1,__ATOMIC_SEQ_CST);}
'''
        test = (RENDER / "tests/standard_present_windows_test.c").read_text()
        with tempfile.TemporaryDirectory() as tmp:
            program = Path(tmp) / "standard_present_windows.c"
            binary = Path(tmp) / "standard_present_windows"
            program.write_text(shim + frontend + test)
            subprocess.run([
                os.environ.get("CC", "clang"), "-std=c11", "-Wall",
                "-Wextra", "-Werror", "-fsanitize=address,undefined",
                "-I", str(RENDER / "include"), str(program),
                str(RENDER / "src/render_qualification.c"),
                "-o", str(binary),
            ], check=True, cwd=ROOT)
            subprocess.run([str(binary)], check=True, cwd=ROOT)

    def test_kmd_wires_trace_without_enabling_private_presentation(self):
        callbacks = (RENDER / "src/callbacks.c").read_text()
        display = (RENDER / "src/display.c").read_text()
        scanout = (RENDER / "src/scanout_windows.c").read_text()
        project = (RENDER / "AppleAgxRenderAdmission.vcxproj").read_text()

        present = callbacks[
            callbacks.index("AdmissionDdiPresent("):
            callbacks.index("AdmissionDdiStopCapture(")
        ]
        source = display[
            display.index("AdmissionDdiSetVidPnSourceAddress("):
            display.index("AdmissionDdiStopDeviceAndReleasePostDisplayOwnership(")
        ]
        escape = callbacks[
            callbacks.index("AdmissionDdiEscape("):
            callbacks.index("AdmissionDdiCreateContext(")
        ]
        self.assertGreaterEqual(
            present.count("AdmissionStandardPresentTraceRecordWindows"), 2
        )
        blt = present[
            present.index("Present->Flags.Value == 1u"):
            present.index("Present->pDmaBuffer != NULL")
        ]
        self.assertIn("traceEvent.AllocationToken", blt)
        self.assertIn("source->Allocation", blt)
        self.assertIn("Present->pAllocationList[DXGK_PRESENT_SOURCE_INDEX]", blt)
        self.assertNotIn("Present->pAllocationInfo[DXGK_PRESENT_SOURCE_INDEX]", blt)
        self.assertGreaterEqual(
            source.count("AdmissionStandardPresentTraceRecordWindows"), 2
        )
        self.assertIn("AdmissionStandardPresentTraceQueryWindows", escape)
        self.assertIn(r"src\standard_present_windows.c", project)

        queue = scanout[
            scanout.index("AdmissionScanoutQueuePresent("):
            scanout.index("AdmissionScanoutInterrupt(")
        ]
        self.assertIn("#if defined(APPLE_AGX_VISIBLE_AGX_QUALIFICATION)", queue)
        self.assertLess(
            queue.index("#if defined(APPLE_AGX_VISIBLE_AGX_QUALIFICATION)"),
            queue.index("ADMISSION_DISPLAY_OUTPUT_LEASE fallbackCandidate"),
        )

    def test_windowed_blt_producer_uses_interactive_dwm_route(self):
        producer = (
            ROOT / "drivers/apple-agx/windows/one-shot/apple_agx_d3dkmt_render.c"
        ).read_text()
        self.assertIn('L"--standard-blt-present-hold"', producer)
        self.assertIn("WTSGetActiveConsoleSessionId", producer)
        self.assertIn("ProcessIdToSessionId", producer)
        self.assertIn("present.Flags.Blt = 1u", producer)
        self.assertIn("present.Flags.SrcRectValid = 1u", producer)
        self.assertIn("present.Flags.DstRectValid = 1u", producer)
        self.assertIn("AdmissionStandardPresentTraceAcceptPresent", producer)
        self.assertIn("PHASE STANDARD_BLT_HOLD_PASS", producer)


if __name__ == "__main__":
    unittest.main()
