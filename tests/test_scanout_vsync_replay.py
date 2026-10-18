"""Execute the actual KMD ISR, timer/control/stop bodies with the real client.

Invariant: repeated vertical periods cannot require another Present, publish a
pending address, or fault on stale latch. Timer callbacks must drain before free.
"""
from pathlib import Path
import tempfile,subprocess,unittest,struct
ROOT=Path(__file__).resolve().parents[1]

def function(source, signature):
    start=source.index(signature);brace=source.index('{',start);end=brace+1;depth=1
    while depth:
        depth+=(source[end]=='{')-(source[end]=='}');end+=1
    return source[start:end]

def compile_and_run(scenarios):
    s=(ROOT/'drivers/apple-agx/render-admission/src/scanout_windows.c').read_text()
    struct=s[s.index('typedef struct _ADMISSION_SCANOUT_RUNTIME'):s.index('} ADMISSION_SCANOUT_RUNTIME;')+len('} ADMISSION_SCANOUT_RUNTIME;')]
    shim=(ROOT/'tests/fixtures/vsync_replay_shim.inc').read_text()
    a=shim.index('static ADMISSION_SCANOUT_RUNTIME *AdmissionScanoutGet')
    code=shim[:a]+struct+shim[a:]+s[s.index('/* All timeline and receipt writes'):]
    code+='\n'+function(s,'_Use_decl_annotations_ NTSTATUS AdmissionScanoutStop(')
    scheduler=(ROOT/'drivers/apple-agx/render-admission/src/scheduler_windows.c').read_text()
    code+='\n'+function(scheduler,'_Use_decl_annotations_ NTSTATUS AdmissionDdiResetFromTimeout(')
    code+='\n'+function(scheduler,'_Use_decl_annotations_ NTSTATUS AdmissionDdiRestartFromTimeout(')
    code+='\n'+function((ROOT/'drivers/apple-agx/render-admission/src/interrupt.c').read_text(),'VOID AdmissionDdiDpcRoutine(')
    start=function(s,'_Use_decl_annotations_ NTSTATUS AdmissionScanoutStart(')
    publication='  Context->ScanoutRuntime = runtime;'+start.rsplit('  Context->ScanoutRuntime = runtime;',1)[1]
    code+='\nstatic NTSTATUS finish_start(ADMISSION_CONTEXT *Context,ADMISSION_SCANOUT_RUNTIME *runtime) {\n'+publication+'\n'
    code+=scenarios
    with tempfile.TemporaryDirectory() as tmp:
        p=Path(tmp)/'replay.c';p.write_text(code);binary=Path(tmp)/'replay';shared=ROOT/'drivers/apple-agx/shared'
        subprocess.run(['clang','-std=c11','-Wall','-Wextra','-Werror','-fsanitize=address,undefined','-DAPPLE_AGX_GPUVA_G3_QUALIFICATION','-I',str(shared/'include'),str(p),str(shared/'src/apple_agx_scanout.c'),str(shared/'src/apple_agx_fixed_panel.c'),'-o',str(binary)],check=True)
        subprocess.run([str(binary)],check=True)

class ScanoutVsyncReplayTests(unittest.TestCase):
    def test_real_kmd_vsync_state_and_timer_replay(self):
        compile_and_run((ROOT/'tests/fixtures/vsync_replay_scenarios.c').read_text())

    def test_timeline_rate_is_exact_observed_panel_mode(self):
        # Catch replacement of the actual fixed16:16 panel rate by rounded60Hz.
        words=struct.unpack('<20I',(ROOT/'tests/fixtures/j313-timing2-element.bin').read_bytes())
        with tempfile.TemporaryDirectory() as tmp:
            p=Path(tmp)/'mode.c';p.write_text('#include <stdio.h>\n#include "apple_agx_vsync.h"\nint main(void){printf("%u %u %u %u %u",APPLE_AGX_VSYNC_RATE_NUMERATOR,APPLE_AGX_VSYNC_RATE_DENOMINATOR,APPLE_AGX_SCANOUT_J313_WIDTH,APPLE_AGX_SCANOUT_J313_HEIGHT,APPLE_AGX_SCANOUT_J313_STRIDE);}')
            out=Path(tmp)/'mode'
            subprocess.run(['clang','-std=c11','-Wall','-Wextra','-Werror','-I',str(ROOT/'drivers/apple-agx/shared/include'),str(p),'-o',str(out)],check=True)
            rate,den,w,h,stride=map(int,subprocess.check_output([str(out)],text=True).split())
            self.assertEqual((rate,den,w,h,stride),(words[14],65536,words[2],words[10],words[2]*4))
            self.assertEqual((words[1],words[9],words[17]),(2642,1682,266630000))
