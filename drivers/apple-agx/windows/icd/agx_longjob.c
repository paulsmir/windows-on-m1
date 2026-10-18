/* G4-recovery measurement: how long one valid GPU job may run.
 * Each step is one draw: a full-screen triangle into a 256x256 render target
 * whose pixel shader runs a dependent multiply-add loop N times. N comes from
 * a constant buffer, so the compiler keeps a dynamic loop. The tool first
 * doubles N until one draw takes at least 30 ms, then runs one draw per target
 * duration. Each draw is timed from Flush to its event query (QPC), and
 * GetDeviceRemovedReason is read after it. Every line is written through to
 * the output file before the next draw, so the log survives a bugcheck.
 * Calibration takes the fastest of three draws at each loop count, so one
 * draw delayed behind other GPU work does not skew the rate (EXP1276).
 * A final argument "hang" runs one draw whose loop never finishes within
 * TdrDelay: the firmware must report it (G4-r2b), so the device is removed
 * instead of the draw completing silently.
 * Usage: agx_longjob.exe <out.txt> <target-ms>... [hang]  (e.g. 100 250 700 1400) */
#define COBJMACROS
#define INITGUID
#include <windows.h>
#include <d3d11.h>
#include <d3dcompiler.h>
#include <dxgi1_2.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static HANDLE out = INVALID_HANDLE_VALUE;

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

static const char vs_source[] =
    "float4 main(uint id : SV_VertexID) : SV_Position {\n"
    "  float2 uv = float2((id << 1) & 2, id & 2);\n"
    "  return float4(uv * float2(2, -2) + float2(-1, 1), 0, 1);\n"
    "}\n";
static const char ps_source[] =
    "cbuffer C : register(b0) { uint n; uint3 pad; };\n"
    "float4 main(float4 p : SV_Position) : SV_Target {\n"
    "  float a = p.x * 0.001 + p.y * 0.002;\n"
    "  [loop] for (uint i = 0; i < n; ++i) { a = a * 0.9999 + 0.0001; }\n"
    "  return float4(a, a, a, 1);\n"
    "}\n";

static ID3D11Device *device;
static ID3D11DeviceContext *ctx;
static ID3D11Buffer *cb;
static ID3D11Query *query;
static double qpc_ms;

static ID3DBlob *compile(pD3DCompile fn, const char *src, size_t len, const char *target) {
  ID3DBlob *code = NULL, *errors = NULL;
  HRESULT hr = fn(src, len, NULL, NULL, NULL, "main", target, 0, 0, &code, &errors);
  say("D3DCompile %s 0x%08lx", target, (unsigned long)hr);
  if (errors) {
    say("%.300s", (const char *)ID3D10Blob_GetBufferPointer(errors));
    ID3D10Blob_Release(errors);
  }
  return SUCCEEDED(hr) ? code : NULL;
}

/* One draw of n loop iterations; returns Flush-to-completion ms, or a negative
 * value when the query never completed within limit_ms. */
static double run(UINT n, DWORD limit_ms, HRESULT *removed) {
  D3D11_MAPPED_SUBRESOURCE map;
  LARGE_INTEGER t0, t1;
  BOOL done = FALSE;
  HRESULT hr;
  if (FAILED(ID3D11DeviceContext_Map(ctx, (ID3D11Resource *)cb, 0,
                                     D3D11_MAP_WRITE_DISCARD, 0, &map))) {
    *removed = ID3D11Device_GetDeviceRemovedReason(device);
    return -2.0;
  }
  memset(map.pData, 0, 16);
  memcpy(map.pData, &n, sizeof(n));
  ID3D11DeviceContext_Unmap(ctx, (ID3D11Resource *)cb, 0);
  ID3D11DeviceContext_Draw(ctx, 3, 0);
  ID3D11DeviceContext_End(ctx, (ID3D11Asynchronous *)query);
  QueryPerformanceCounter(&t0);
  ID3D11DeviceContext_Flush(ctx);
  for (;;) {
    hr = ID3D11DeviceContext_GetData(ctx, (ID3D11Asynchronous *)query, &done,
                                     sizeof(done), 0);
    QueryPerformanceCounter(&t1);
    if (hr == S_OK && done) break;
    if (FAILED(hr) || (t1.QuadPart - t0.QuadPart) / qpc_ms > limit_ms) {
      *removed = ID3D11Device_GetDeviceRemovedReason(device);
      return FAILED(hr) ? -3.0 : -1.0;
    }
    Sleep(0);
  }
  *removed = ID3D11Device_GetDeviceRemovedReason(device);
  return (double)(t1.QuadPart - t0.QuadPart) / qpc_ms;
}

int main(int argc, char **argv) {
  IDXGIFactory1 *factory = NULL;
  IDXGIAdapter1 *adapter = NULL, *pick = NULL;
  ID3D11Texture2D *target = NULL;
  ID3D11RenderTargetView *rtv = NULL;
  ID3D11VertexShader *vs = NULL;
  ID3D11PixelShader *ps = NULL;
  ID3DBlob *vsb, *psb;
  D3D_FEATURE_LEVEL level = 0;
  const D3D_FEATURE_LEVEL want[] = {D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_1,
                                    D3D_FEATURE_LEVEL_10_0};
  HMODULE compiler;
  pD3DCompile compile_fn;
  LARGE_INTEGER freq;
  HRESULT hr, removed = S_OK;
  UINT n = 256u;
  double ms = 0.0, per_iteration;
  int i;

  if (argc < 3) {
    printf("usage: agx_longjob.exe <out.txt> <target-ms>...\n");
    return 2;
  }
  out = CreateFileA(argv[1], GENERIC_WRITE, FILE_SHARE_READ, NULL, CREATE_ALWAYS,
                    FILE_ATTRIBUTE_NORMAL | FILE_FLAG_WRITE_THROUGH, NULL);
  QueryPerformanceFrequency(&freq);
  qpc_ms = (double)freq.QuadPart / 1000.0;
  compiler = LoadLibraryA("d3dcompiler_47.dll");
  compile_fn = compiler ? (pD3DCompile)(void *)GetProcAddress(compiler, "D3DCompile") : NULL;
  if (!compile_fn) { say("NO_D3DCOMPILER %lu", GetLastError()); return 1; }
  hr = CreateDXGIFactory1(&IID_IDXGIFactory1, (void **)&factory);
  if (FAILED(hr)) { say("CreateDXGIFactory1 0x%08lx", (unsigned long)hr); return 1; }
  for (UINT a = 0; IDXGIFactory1_EnumAdapters1(factory, a, &adapter) != DXGI_ERROR_NOT_FOUND; ++a) {
    DXGI_ADAPTER_DESC1 d;
    IDXGIAdapter1_GetDesc1(adapter, &d);
    say("ADAPTER %u vendor %04x flags %x %ls", a, d.VendorId, d.Flags, d.Description);
    if (!pick && !(d.Flags & DXGI_ADAPTER_FLAG_SOFTWARE)) { pick = adapter; continue; }
    IDXGIAdapter1_Release(adapter);
  }
  if (!pick) { say("NO_HARDWARE_ADAPTER"); return 1; }
  hr = D3D11CreateDevice((IDXGIAdapter *)pick, D3D_DRIVER_TYPE_UNKNOWN, NULL, 0, want, 3,
                         D3D11_SDK_VERSION, &device, &level, &ctx);
  say("D3D11CreateDevice 0x%08lx FEATURE_LEVEL 0x%x", (unsigned long)hr, level);
  if (FAILED(hr)) return 1;
  {
    D3D11_TEXTURE2D_DESC td;
    D3D11_BUFFER_DESC bd;
    D3D11_QUERY_DESC qd;
    D3D11_VIEWPORT vp;
    memset(&td, 0, sizeof(td));
    td.Width = td.Height = 256; td.MipLevels = td.ArraySize = 1;
    td.Format = DXGI_FORMAT_R8G8B8A8_UNORM; td.SampleDesc.Count = 1;
    td.Usage = D3D11_USAGE_DEFAULT; td.BindFlags = D3D11_BIND_RENDER_TARGET;
    hr = ID3D11Device_CreateTexture2D(device, &td, NULL, &target);
    if (SUCCEEDED(hr)) hr = ID3D11Device_CreateRenderTargetView(device, (ID3D11Resource *)target, NULL, &rtv);
    memset(&bd, 0, sizeof(bd));
    bd.ByteWidth = 16; bd.Usage = D3D11_USAGE_DYNAMIC;
    bd.BindFlags = D3D11_BIND_CONSTANT_BUFFER; bd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    if (SUCCEEDED(hr)) hr = ID3D11Device_CreateBuffer(device, &bd, NULL, &cb);
    memset(&qd, 0, sizeof(qd));
    qd.Query = D3D11_QUERY_EVENT;
    if (SUCCEEDED(hr)) hr = ID3D11Device_CreateQuery(device, &qd, &query);
    say("RESOURCES 0x%08lx", (unsigned long)hr);
    if (FAILED(hr)) return 1;
    vsb = compile(compile_fn, vs_source, sizeof(vs_source) - 1, "vs_4_0");
    psb = compile(compile_fn, ps_source, sizeof(ps_source) - 1, "ps_4_0");
    if (!vsb || !psb) return 1;
    hr = ID3D11Device_CreateVertexShader(device, ID3D10Blob_GetBufferPointer(vsb),
                                         ID3D10Blob_GetBufferSize(vsb), NULL, &vs);
    if (SUCCEEDED(hr)) hr = ID3D11Device_CreatePixelShader(device, ID3D10Blob_GetBufferPointer(psb),
                                         ID3D10Blob_GetBufferSize(psb), NULL, &ps);
    say("SHADERS 0x%08lx", (unsigned long)hr);
    if (FAILED(hr)) return 1;
    memset(&vp, 0, sizeof(vp));
    vp.Width = vp.Height = 256.0f; vp.MaxDepth = 1.0f;
    ID3D11DeviceContext_RSSetViewports(ctx, 1, &vp);
    ID3D11DeviceContext_OMSetRenderTargets(ctx, 1, &rtv, NULL);
    ID3D11DeviceContext_IASetPrimitiveTopology(ctx, D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    ID3D11DeviceContext_VSSetShader(ctx, vs, NULL, 0);
    ID3D11DeviceContext_PSSetShader(ctx, ps, NULL, 0);
    ID3D11DeviceContext_PSSetConstantBuffers(ctx, 0, 1, &cb);
  }
  /* Calibration: the first draw also compiles the pipeline. */
  (void)run(n, 10000u, &removed);
  for (;;) {
    double best = -1.0;
    for (int k = 0; k < 3; ++k) {
      double sample = run(n, 10000u, &removed);
      if (sample < 0.0 || removed != S_OK) { best = sample; break; }
      if (best < 0.0 || sample < best) best = sample;
    }
    ms = best;
    say("CALIBRATE n %u ms %.2f removed 0x%08lx", n, ms, (unsigned long)removed);
    if (ms < 0.0 || removed != S_OK) { say("CALIBRATION_FAILED"); return 1; }
    if (ms >= 30.0 || n >= (1u << 28)) break;
    n *= 2u;
  }
  per_iteration = ms / (double)n;
  say("RATE %.9f ms per iteration", per_iteration);
  for (i = 2; i < argc; ++i) {
    double want_ms, iterations;
    if (!strcmp(argv[i], "hang")) {
      /* ~0x7fffffff iterations: far beyond TdrDelay at any measured rate. */
      say("HANG_START n %u", 0x7fffffffu);
      ms = run(0x7fffffffu, 15000u, &removed);
      say("HANG ms %.2f removed 0x%08lx", ms, (unsigned long)removed);
      say("HANG_AFTER removed 0x%08lx", (unsigned long)ID3D11Device_GetDeviceRemovedReason(device));
      continue;
    }
    want_ms = atof(argv[i]);
    iterations = want_ms / per_iteration;
    UINT step = iterations >= 4294967295.0 ? 0xffffffffu : (UINT)iterations;
    say("STEP_START target %.0f n %u", want_ms, step);
    ms = run(step, (DWORD)(want_ms * 4.0 + 10000.0), &removed);
    say("STEP target %.0f n %u ms %.2f removed 0x%08lx", want_ms, step, ms,
        (unsigned long)removed);
    if (ms < 0.0 || removed != S_OK) { say("STOPPED_AT target %.0f", want_ms); return 1; }
  }
  say("LONGJOB_DONE");
  return 0;
}
