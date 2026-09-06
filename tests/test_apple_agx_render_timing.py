"""Check the production fixed-panel timing projection against EXP495 live data."""
import os
import re
import subprocess
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


class PanelTimingTests(unittest.TestCase):
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
