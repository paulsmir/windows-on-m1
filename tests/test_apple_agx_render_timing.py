"""Check the production fixed-panel timing projection against EXP495 live data."""
import os
import re
import subprocess
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


class PanelTimingTests(unittest.TestCase):
    def test_target_initialization_preserves_allocator_minimum_refresh_sentinel(self):
        source = (ROOT / 'drivers/apple-agx/render-admission/src/display.c').read_text()
        helper = re.search(r'static VOID AdmissionFillPanelSignalInfo\(.*?^}',
                           source, re.S | re.M)
        init = re.search(
            r'status = targetSetInterface->pfnCreateNewModeInfo\(targetSet,\s*'
            r'&targetMode\);(.*?)status = targetSetInterface->pfnAddMode',
            source, re.S)
        self.assertIsNotNone(helper)
        self.assertIsNotNone(init)
        shim = r'''
#include <stdint.h>
#include <string.h>
#include <assert.h>
#define VOID void
#define RtlZeroMemory(p,n) memset((p),0,(n))
#define NT_SUCCESS(s) ((s)>=0)
#define D3DKMDT_VSS_OTHER 255u
#define D3DDDI_VSSLO_PROGRESSIVE 1u
#define D3DKMDT_MP_PREFERRED 1u
typedef struct {uint32_t Numerator,Denominator;} RATIONAL;
typedef struct {uint32_t cx,cy;} SIZE2;
typedef struct {uint32_t VideoStandard; SIZE2 TotalSize,ActiveSize;
 RATIONAL VSyncFreq,HSyncFreq; uint64_t PixelRate; uint32_t ScanLineOrdering;}
 D3DKMDT_VIDEO_SIGNAL_INFO;
typedef struct {uint32_t Id; D3DKMDT_VIDEO_SIGNAL_INFO VideoSignalInfo;
 uint32_t Preference; RATIONAL MinimumVSyncFreq;} D3DKMDT_VIDPN_TARGET_MODE;
'''
        body = ('\nstatic void initialize(D3DKMDT_VIDPN_TARGET_MODE *targetMode) {'
                '\nint status=0;\n' + init.group(1) + '\nExit:;\n}\n')
        cases = r'''
int main(void) {
 D3DKMDT_VIDPN_TARGET_MODE mode = {0};
 mode.Id=37;
 mode.MinimumVSyncFreq.Numerator=UINT32_MAX;
 mode.MinimumVSyncFreq.Denominator=UINT32_MAX;
 initialize(&mode);
 assert(mode.MinimumVSyncFreq.Numerator==UINT32_MAX);
 assert(mode.MinimumVSyncFreq.Denominator==UINT32_MAX);
 assert(mode.Id==37);
 assert(mode.Preference==D3DKMDT_MP_PREFERRED);
 assert(mode.VideoSignalInfo.PixelRate==266630000ULL);
 assert(mode.VideoSignalInfo.TotalSize.cx==2642);
 return 0;
}
'''
        with tempfile.TemporaryDirectory() as tmp:
            program = Path(tmp) / 'target.c'
            program.write_text(shim + helper.group(0) + body + cases)
            binary = Path(tmp) / 'target'
            subprocess.run([os.environ.get('CC','clang'),'-std=c11','-Wall','-Wextra',
                            '-Werror','-fsanitize=address,undefined',str(program),
                            '-o',str(binary)],check=True)
            subprocess.run([str(binary)],check=True)

    def test_actual_signal_tuple_has_native_totals_and_consistent_frequencies(self):
        source = (ROOT / 'drivers/apple-agx/render-admission/src/display.c').read_text()
        match = re.search(r'static VOID AdmissionFillPanelSignalInfo\(.*?^}',
                          source, re.S | re.M)
        self.assertIsNotNone(match, 'Native signal-info projection is missing')
        shim = r'''
#include <stdint.h>
#include <string.h>
#include <assert.h>
#define VOID void
#define RtlZeroMemory(p,n) memset((p),0,(n))
#define D3DKMDT_VSS_OTHER 255u
#define D3DDDI_VSSLO_PROGRESSIVE 1u
typedef struct {uint32_t Numerator, Denominator;} RATIONAL;
typedef struct {uint32_t cx,cy;} SIZE2;
typedef struct {uint32_t VideoStandard; SIZE2 TotalSize,ActiveSize;
 RATIONAL VSyncFreq,HSyncFreq; uint64_t PixelRate; uint32_t ScanLineOrdering;}
 D3DKMDT_VIDEO_SIGNAL_INFO;
'''
        cases = r'''
int main(void) {
 D3DKMDT_VIDEO_SIGNAL_INFO signal;
 memset(&signal,0xa5,sizeof(signal));
 AdmissionFillPanelSignalInfo(&signal);
 assert(signal.ActiveSize.cx==2560 && signal.ActiveSize.cy==1600);
 assert(signal.TotalSize.cx==2642 && signal.TotalSize.cy==1682);
 assert(signal.PixelRate==266630000ULL);
 assert(signal.HSyncFreq.Numerator!=0 && signal.HSyncFreq.Denominator!=0);
 assert(signal.VSyncFreq.Numerator!=0 && signal.VSyncFreq.Denominator!=0);
 assert((uint64_t)signal.HSyncFreq.Numerator*2642 ==
        signal.PixelRate*signal.HSyncFreq.Denominator);
 assert((uint64_t)signal.VSyncFreq.Numerator*2642*1682 ==
        signal.PixelRate*signal.VSyncFreq.Denominator);
 assert(signal.ScanLineOrdering==D3DDDI_VSSLO_PROGRESSIVE);
 assert(signal.VideoStandard==D3DKMDT_VSS_OTHER);
 return 0;
}
'''
        with tempfile.TemporaryDirectory() as tmp:
            program = Path(tmp) / 'timing.c'
            program.write_text(shim + match.group(0) + cases)
            binary = Path(tmp) / 'timing'
            subprocess.run([os.environ.get('CC','clang'),'-std=c11','-Wall','-Wextra',
                            '-Werror','-fsanitize=address,undefined',str(program),
                            '-o',str(binary)],check=True)
            subprocess.run([str(binary)],check=True)


if __name__ == '__main__':
    unittest.main()
