/* G4-r3 TDR drill (EXP1296 boundary): proves that a TDR on a job whose worker
 * left without its completion ends in this process's device removal, not in
 * bugcheck 0x116.
 * The tool arms the KMD drill for its own process (DxgkDdiEscape payload
 * ADMISSION_TDR_DRILL, render-admission/include/render_gpu_fault.h; accepted
 * only with the device value Wom1TdrDrillAllowed = 1), then submits one short
 * draw. The KMD worker leaves that job with the firmware (dispatch freezes);
 * VidSch's TDR (TdrDelay) calls DxgkDdiResetEngine, whose quiescence poll
 * consumes the job's end and reports it as aborted. Expected: the event query
 * never signals, GetDeviceRemovedReason becomes DXGI_ERROR_DEVICE_HUNG within
 * about TdrDelay + 1 s, DWM survives, Wom1GpuReset records the reset.
 * "baseline" skips the arm (same draw, must complete).
 * Every line is written through, so the log survives a bugcheck.
 * Usage: agx_tdr_drill.exe <out.txt> [drill|baseline] [jobs] */
#define COBJMACROS
#define INITGUID
#include <windows.h>
#include <d3d11.h>
#include <d3dcompiler.h>
#include <dxgi1_2.h>
#include <d3dkmthk.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* render_gpu_fault.h ADMISSION_TDR_DRILL v1 */
#define ADMISSION_TDR_DRILL_MAGIC 0x44524454u
#define ADMISSION_TDR_DRILL_VERSION 1u
typedef struct {
  unsigned int Magic, Version, Bytes, Jobs;
} ADMISSION_TDR_DRILL;

static HANDLE out = INVALID_HANDLE_VALUE;
static double qpc_ms;

static void say(const char *format, ...) {
  char line[512];
  DWORD written;
  va_list args;
  int n;
  va_start(args, format);
  n = vsnprintf(line, sizeof(line) - 2, format, args);
  va_end(args);
  if (n < 0) return;
  if (n > (int)sizeof(line) - 3) n = (int)sizeof(line) - 3;
  line[n++] = '\r';
  line[n++] = '\n';
  fwrite(line, 1, (size_t)n, stdout);
  fflush(stdout);
  if (out != INVALID_HANDLE_VALUE) {
    WriteFile(out, line, (DWORD)n, &written, NULL);
    FlushFileBuffers(out);
  }
}

static double now_ms(void) {
  LARGE_INTEGER t;
  QueryPerformanceCounter(&t);
  return (double)t.QuadPart / qpc_ms;
}

static const char vs_source[] =
    "float4 main(uint id : SV_VertexID) : SV_Position {\n"
    "  float2 uv = float2((id << 1) & 2, id & 2);\n"
    "  return float4(uv * float2(2, -2) + float2(-1, 1), 0, 1);\n"
    "}\n";
static const char ps_source[] =
    "float4 main(float4 p : SV_Position) : SV_Target {\n"
    "  return float4(p.x / 64.0, p.y / 64.0, 0.25, 1);\n"
    "}\n";

static long arm(LUID luid, unsigned int jobs) {
  D3DKMT_OPENADAPTERFROMLUID open;
  D3DKMT_CLOSEADAPTER close;
  D3DKMT_ESCAPE escape;
  ADMISSION_TDR_DRILL drill;
  NTSTATUS status;
  memset(&open, 0, sizeof(open));
  open.AdapterLuid = luid;
  status = D3DKMTOpenAdapterFromLuid(&open);
  if (status < 0) return (long)status;
  memset(&drill, 0, sizeof(drill));
  drill.Magic = ADMISSION_TDR_DRILL_MAGIC;
  drill.Version = ADMISSION_TDR_DRILL_VERSION;
  drill.Bytes = sizeof(drill);
  drill.Jobs = jobs;
  memset(&escape, 0, sizeof(escape));
  escape.hAdapter = open.hAdapter;
  escape.Type = D3DKMT_ESCAPE_DRIVERPRIVATE;
  escape.pPrivateDriverData = &drill;
  escape.PrivateDriverDataSize = sizeof(drill);
  status = D3DKMTEscape(&escape);
  memset(&close, 0, sizeof(close));
  close.hAdapter = open.hAdapter;
  D3DKMTCloseAdapter(&close);
  return (long)status;
}

int main(int argc, char **argv) {
  IDXGIFactory1 *factory = NULL;
  IDXGIAdapter1 *adapter = NULL, *pick = NULL;
  DXGI_ADAPTER_DESC1 desc;
  ID3D11Device *device = NULL;
  ID3D11DeviceContext *ctx = NULL;
  ID3D11Texture2D *target = NULL;
  ID3D11RenderTargetView *rtv = NULL;
  ID3D11VertexShader *vs = NULL;
  ID3D11PixelShader *ps = NULL;
  ID3D11Query *query = NULL;
  ID3DBlob *vsb = NULL, *psb = NULL;
  D3D_FEATURE_LEVEL level = 0;
  const D3D_FEATURE_LEVEL want[] = {D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_1,
                                    D3D_FEATURE_LEVEL_10_0};
  HMODULE compiler;
  pD3DCompile compile_fn;
  LARGE_INTEGER freq;
  HRESULT hr, removed = S_OK;
  int drill = 1;
  unsigned int jobs = 1u;
  double start, waited = 0.0;
  BOOL signaled = FALSE;

  if (argc < 2) {
    printf("usage: agx_tdr_drill.exe <out.txt> [drill|baseline] [jobs]\n");
    return 2;
  }
  if (argc > 2) drill = strcmp(argv[2], "baseline") != 0;
  if (argc > 3) jobs = (unsigned int)strtoul(argv[3], NULL, 10);
  out = CreateFileA(argv[1], GENERIC_WRITE, FILE_SHARE_READ, NULL, CREATE_ALWAYS,
                    FILE_ATTRIBUTE_NORMAL | FILE_FLAG_WRITE_THROUGH, NULL);
  QueryPerformanceFrequency(&freq);
  qpc_ms = (double)freq.QuadPart / 1000.0;
  say("TDR_DRILL mode %s jobs %u pid %lu", drill ? "drill" : "baseline", jobs,
      GetCurrentProcessId());
  compiler = LoadLibraryA("d3dcompiler_47.dll");
  compile_fn = compiler ? (pD3DCompile)(void *)GetProcAddress(compiler, "D3DCompile") : NULL;
  if (!compile_fn) { say("NO_D3DCOMPILER %lu", GetLastError()); return 1; }
  hr = CreateDXGIFactory1(&IID_IDXGIFactory1, (void **)&factory);
  if (FAILED(hr)) { say("CreateDXGIFactory1 0x%08lx", (unsigned long)hr); return 1; }
  for (UINT a = 0; IDXGIFactory1_EnumAdapters1(factory, a, &adapter) != DXGI_ERROR_NOT_FOUND; ++a) {
    DXGI_ADAPTER_DESC1 d;
    IDXGIAdapter1_GetDesc1(adapter, &d);
    say("ADAPTER %u vendor %04x flags %x %ls", a, d.VendorId, d.Flags, d.Description);
    if (!pick && !(d.Flags & DXGI_ADAPTER_FLAG_SOFTWARE)) { pick = adapter; desc = d; continue; }
    IDXGIAdapter1_Release(adapter);
  }
  if (!pick) { say("NO_HARDWARE_ADAPTER"); return 1; }
  hr = D3D11CreateDevice((IDXGIAdapter *)pick, D3D_DRIVER_TYPE_UNKNOWN, NULL, 0, want, 3,
                         D3D11_SDK_VERSION, &device, &level, &ctx);
  say("D3D11CreateDevice 0x%08lx FEATURE_LEVEL 0x%x", (unsigned long)hr, level);
  if (FAILED(hr)) return 1;
  {
    D3D11_TEXTURE2D_DESC td;
    D3D11_QUERY_DESC qd;
    D3D11_VIEWPORT vp;
    memset(&td, 0, sizeof(td));
    td.Width = td.Height = 64; td.MipLevels = td.ArraySize = 1;
    td.Format = DXGI_FORMAT_R8G8B8A8_UNORM; td.SampleDesc.Count = 1;
    td.Usage = D3D11_USAGE_DEFAULT; td.BindFlags = D3D11_BIND_RENDER_TARGET;
    hr = ID3D11Device_CreateTexture2D(device, &td, NULL, &target);
    if (SUCCEEDED(hr)) hr = ID3D11Device_CreateRenderTargetView(device, (ID3D11Resource *)target, NULL, &rtv);
    memset(&qd, 0, sizeof(qd));
    qd.Query = D3D11_QUERY_EVENT;
    if (SUCCEEDED(hr)) hr = ID3D11Device_CreateQuery(device, &qd, &query);
    if (SUCCEEDED(hr)) hr = compile_fn(vs_source, sizeof(vs_source) - 1, "vs", NULL, NULL, "main",
                                       "vs_4_0", 0, 0, &vsb, NULL);
    if (SUCCEEDED(hr)) hr = compile_fn(ps_source, sizeof(ps_source) - 1, "ps", NULL, NULL, "main",
                                       "ps_4_0", 0, 0, &psb, NULL);
    if (SUCCEEDED(hr)) hr = ID3D11Device_CreateVertexShader(device, ID3D10Blob_GetBufferPointer(vsb),
                                                           ID3D10Blob_GetBufferSize(vsb), NULL, &vs);
    if (SUCCEEDED(hr)) hr = ID3D11Device_CreatePixelShader(device, ID3D10Blob_GetBufferPointer(psb),
                                                          ID3D10Blob_GetBufferSize(psb), NULL, &ps);
    say("RESOURCES 0x%08lx", (unsigned long)hr);
    if (FAILED(hr)) return 1;
    memset(&vp, 0, sizeof(vp));
    vp.Width = vp.Height = 64.0f; vp.MaxDepth = 1.0f;
    ID3D11DeviceContext_OMSetRenderTargets(ctx, 1, &rtv, NULL);
    ID3D11DeviceContext_RSSetViewports(ctx, 1, &vp);
    ID3D11DeviceContext_IASetPrimitiveTopology(ctx, D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    ID3D11DeviceContext_VSSetShader(ctx, vs, NULL, 0);
    ID3D11DeviceContext_PSSetShader(ctx, ps, NULL, 0);
  }
  /* A first draw that must complete: the process, context and its GPU VA
   * exist before the arm, so the drilled job is an ordinary render. */
  ID3D11DeviceContext_Draw(ctx, 3, 0);
  ID3D11DeviceContext_End(ctx, (ID3D11Asynchronous *)query);
  ID3D11DeviceContext_Flush(ctx);
  start = now_ms();
  while (ID3D11DeviceContext_GetData(ctx, (ID3D11Asynchronous *)query, &signaled, sizeof(signaled), 0) != S_OK &&
         now_ms() - start < 5000.0)
    Sleep(1);
  say("WARMUP signaled %d ms %.2f removed 0x%08lx", signaled, now_ms() - start,
      (unsigned long)ID3D11Device_GetDeviceRemovedReason(device));
  if (!signaled) { say("WARMUP_FAILED"); return 1; }
  if (drill) {
    long status = arm(desc.AdapterLuid, jobs);
    say("ARM status 0x%08lx", (unsigned long)status);
    if (status < 0) { say("ARM_REFUSED"); return 1; }
  }
  signaled = FALSE;
  ID3D11DeviceContext_Draw(ctx, 3, 0);
  ID3D11DeviceContext_End(ctx, (ID3D11Asynchronous *)query);
  ID3D11DeviceContext_Flush(ctx);
  start = now_ms();
  for (;;) {
    hr = ID3D11DeviceContext_GetData(ctx, (ID3D11Asynchronous *)query, &signaled, sizeof(signaled), 0);
    removed = ID3D11Device_GetDeviceRemovedReason(device);
    waited = now_ms() - start;
    if (hr == S_OK || removed != S_OK || waited > 20000.0) break;
    Sleep(5);
  }
  say("DRAW query 0x%08lx signaled %d removed 0x%08lx ms %.2f", (unsigned long)hr, signaled,
      (unsigned long)removed, waited);
  if (drill)
    say(removed == DXGI_ERROR_DEVICE_HUNG ? "TDR_DRILL_DONE device hung" :
        removed != S_OK ? "TDR_DRILL_DONE device removed (not hung)" :
        "TDR_DRILL_NOT_REMOVED");
  else
    say(hr == S_OK && removed == S_OK ? "TDR_DRILL_BASELINE_DONE" : "TDR_DRILL_BASELINE_FAIL");
  return 0;
}
