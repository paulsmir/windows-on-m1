"""Rejected GPUVA batch must reach the real frontend Flush error callback.

Reuse the residency replay's boundary objects and production BatchFinish,
GpuvaSubmit/parser/rollback. Extract the actual generated frontend Flush and
Windows status function; no substitute status policy is implemented here.
"""
from pathlib import Path
import os
import re
import subprocess
import tempfile
import unittest
from g3_vidmm_replay import body

ROOT = Path(__file__).resolve().parents[1]


class SubmitError(unittest.TestCase):
    def test_rejected_batch_reports_error_and_releases_rollback(self):
        batch = (ROOT / "drivers/apple-agx/mesa/winsys/agx_win32_gpuva_batch.c").read_text()
        windows = (ROOT / "drivers/apple-agx/mesa/winsys/agx_d3d10_windows.cpp").read_text()
        generator = (ROOT / "drivers/apple-agx/mesa/scripts/build-native-asahi-state.py").read_text()
        flush = re.search(r"replace_function_body\('src/gallium/frontends/d3d10umd/Device.cpp','Flush','''(.*?)'''\)", generator, re.S).group(1)
        source = (ROOT / "tests/g4_mesa_pool_residency_replay.c").read_text()
        declarations = r'''
typedef int32_t HRESULT;
typedef unsigned UINT;
#define AdmissionUmdMeasureNativeFlushStage 5u
static void AgxD3d10WindowsPresentMeasure(unsigned kind,HRESULT status,const unsigned *values,unsigned count) {
  (void)kind;(void)status;(void)values;(void)count;
}
#define S_OK 0
#define E_FAIL ((HRESULT)0x80004005u)
#define E_INVALIDARG ((HRESULT)0x80070057u)
#define FAILED(x) ((x)<0)
#define SUCCEEDED(x) ((x)>=0)
#define AgxD3d10DeviceReady 1
typedef struct { struct { HRESULT RenderStatus,PostStatus; } Submission; } ADMISSION_UMD_ASAHI_BATCH;
typedef struct {
  int Stage;
  AGX_WIN32_ASAHI_BACKEND Backend;
  struct { void *NativeBatchTransaction; unsigned DrawTerminal; HRESULT LastScreenError; } Runtime;
} AGX_D3D10_WINDOWS_DEVICE;
static AGX_D3D10_WINDOWS_DEVICE device;
#define owner device.Backend
static void AgxD3d10WindowsDiagnosticState(AGX_D3D10_WINDOWS_DEVICE *d,const char *s) {(void)d;(void)s;}
static unsigned frontend_errors;
static HRESULT frontend_error;
struct Pipe { void (*flush)(struct Pipe *,void *,unsigned); };
typedef struct { AGX_D3D10_WINDOWS_DEVICE *windows; struct Pipe *pipe; } Device;
static Device *CastDevice(Device *d) { return d; }
static void SetError(Device *d,HRESULT h) {(void)d;++frontend_errors;frontend_error=h;}
static HRESULT AgxD3d10WindowsQueryCollect(AGX_D3D10_WINDOWS_DEVICE *d) {(void)d;return S_OK;}
static HRESULT AgxD3d10WindowsFlushDeferredResources(AGX_D3D10_WINDOWS_DEVICE *d) {(void)d;return S_OK;}
static struct agx_batch *active_batch;
static struct drm_asahi_cmd_render *active_render;
static int finish_result;
'''
        source = source.replace("static AGX_WIN32_ASAHI_BACKEND owner;", declarations)
        status = body(windows, "AgxD3d10WindowsFlushStatus")
        source = source.replace('#include "g4_mesa_pool_functions.inc"',
            '#include "g4_mesa_pool_functions.inc"\n' + status +
            '''
static void native_flush(struct Pipe *p,void *f,unsigned flags) {
  (void)p;(void)f;(void)flags;
  finish_result=AgxWin32AsahiBatchFinish(active_batch,active_render);
}
static void FrontendFlush(Device *hDevice) {
''' + flush + '\n}\n')
        source = source.replace("int result=AgxWin32AsahiBatchFinish(&batch,&native);", """
  active_batch=&batch;active_render=&native;finish_result=0;
  device.Stage=AgxD3d10DeviceReady;
  frontend_errors=0;frontend_error=S_OK;
  struct Pipe pipe={native_flush};Device frontend={&device,&pipe};
  FrontendFlush(&frontend);
  int result=finish_result;
  if(!result) {
    fprintf(stderr,"R149 rejected=%u terminal=%u frontend_errors=%u status=%x\\n",
      capsule(&batch)->Rejected,owner.Gpuva.Terminal,frontend_errors,(unsigned)frontend_error);
    assert(frontend_errors==1 && FAILED(frontend_error));
    assert(AgxWin32AsahiBatchPoll(&batch,0));
  } else { assert(!frontend_errors); }
""")
        names = ("batch_refuse", "batch_has_render_work", "gpuva_color_format_supported", "gpuva_color_class", "add_bo", "append_native", "append_attachments", "prepare_process_buffers",
                 "AgxWin32AsahiBatchFinish", "AgxWin32AsahiBatchPoll",
                 "AgxWin32AsahiBatchAbort", "AgxWin32AsahiBatchRelease")
        with tempfile.TemporaryDirectory(prefix="r149-submit-error-") as directory:
            tmp = Path(directory)
            (tmp / "g4_mesa_pool_functions.inc").write_text("void (*AgxWin32BatchRefusalHook)(unsigned, unsigned, unsigned, unsigned);\n#define AGX_WIN32_ASAHI_FAIL(b, f) ((b)->Failed = 1)\n" + "\n".join(body(batch, n) for n in names))
            (tmp / "replay.c").write_text(source)
            binary = tmp / "replay"
            subprocess.run([os.environ.get("CC", "clang"), "-std=c11", "-Wall", "-Wextra",
                "-Werror", "-fsanitize=address,undefined", "-I", str(tmp),
                "-I", str(ROOT / "drivers/apple-agx/shared/include"),
                "-I", str(ROOT / "drivers/apple-agx/mesa/winsys"), str(tmp / "replay.c"),
                str(ROOT / "drivers/apple-agx/mesa/winsys/agx_win32_gpuva.c"),
                str(ROOT / "drivers/apple-agx/shared/src/apple_agx_g4_submit.c"),
                "-o", str(binary)], check=True)
            subprocess.run([str(binary)], check=True, timeout=10)
