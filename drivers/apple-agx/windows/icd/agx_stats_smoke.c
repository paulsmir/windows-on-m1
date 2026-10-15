/* D3D-G10d hardware checkpoint: statistics queries.
 * - pipeline statistics: two triangles cover a 64x64 target exactly once;
 *   IAVertices 6, IAPrimitives 2, VSInvocations 1..6, GS counters 0,
 *   CInvocations and CPrimitives 2, PSInvocations 4096;
 * - stream output: eight points pass a geometry shader whose position is
 *   captured into a buffer with room for five; NumPrimitivesWritten 5,
 *   PrimitivesStorageNeeded 8, the stream-overflow predicate TRUE (and FALSE
 *   for a buffer with room for eight).
 * Every line is written through to the output file before the next step.
 * Usage: agx_stats_smoke.exe <out.txt> [iterations] */
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

static const char vs_quad[] =
    "float4 main(uint id : SV_VertexID) : SV_Position {\n"
    "  float2 c[6] = { float2(-1, 1), float2(1, 1), float2(-1, -1),\n"
    "                  float2(1, 1), float2(1, -1), float2(-1, -1) };\n"
    "  return float4(c[id], 0, 1);\n"
    "}\n";
static const char ps_white[] =
    "float4 main(float4 p : SV_Position) : SV_Target { return float4(1, 1, 1, 1); }\n";
static const char vs_points[] =
    "struct V { float4 p : SV_Position; };\n"
    "V main(uint id : SV_VertexID) { V v; v.p = float4(id, 0, 0, 1); return v; }\n";
static const char gs_points[] =
    "struct V { float4 p : SV_Position; };\n"
    "[maxvertexcount(1)]\n"
    "void main(point V i[1], inout PointStream<V> s) { s.Append(i[0]); }\n";

static ID3D11Device *device;
static ID3D11DeviceContext *ctx;

static ID3DBlob *compile(pD3DCompile fn, const char *src, size_t len, const char *target) {
  ID3DBlob *code = NULL, *errors = NULL;
  HRESULT hr = fn(src, len, NULL, NULL, NULL, "main", target, 0, 0, &code, &errors);
  if (FAILED(hr)) say("D3DCompile %s 0x%08lx", target, (unsigned long)hr);
  if (errors) {
    say("%.300s", (const char *)ID3D10Blob_GetBufferPointer(errors));
    ID3D10Blob_Release(errors);
  }
  return SUCCEEDED(hr) ? code : NULL;
}

static HRESULT wait_data(ID3D11Asynchronous *q, void *data, UINT size) {
  HRESULT hr;
  for (int polls = 0; polls < 20000; ++polls) {
    hr = ID3D11DeviceContext_GetData(ctx, q, data, size, 0);
    if (hr != S_FALSE) return hr;
    Sleep(0);
  }
  return S_FALSE;
}

int main(int argc, char **argv) {
  IDXGIFactory1 *factory = NULL;
  IDXGIAdapter1 *adapter = NULL, *pick = NULL;
  ID3D11Texture2D *target = NULL;
  ID3D11RenderTargetView *rtv = NULL;
  ID3D11VertexShader *vsq = NULL, *vsp = NULL;
  ID3D11PixelShader *ps = NULL;
  ID3D11GeometryShader *gs = NULL;
  ID3D11Buffer *so_small = NULL, *so_large = NULL;
  ID3D11Query *stats = NULL, *so_stats = NULL, *overflow = NULL;
  ID3DBlob *b1, *b2, *b3, *b4;
  D3D_FEATURE_LEVEL level = 0;
  const D3D_FEATURE_LEVEL want[] = {D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_1,
                                    D3D_FEATURE_LEVEL_10_0};
  HMODULE compiler;
  pD3DCompile compile_fn;
  HRESULT hr;
  int iterations, bad = 0;

  if (argc < 2) {
    printf("usage: agx_stats_smoke.exe <out.txt> [iterations]\n");
    return 2;
  }
  iterations = argc > 2 ? atoi(argv[2]) : 20;
  if (iterations < 1) iterations = 1;
  out = CreateFileA(argv[1], GENERIC_WRITE, FILE_SHARE_READ, NULL, CREATE_ALWAYS,
                    FILE_ATTRIBUTE_NORMAL | FILE_FLAG_WRITE_THROUGH, NULL);
  say("STATS iterations %d", iterations);
  compiler = LoadLibraryA("d3dcompiler_47.dll");
  compile_fn = compiler ? (pD3DCompile)(void *)GetProcAddress(compiler, "D3DCompile") : NULL;
  if (!compile_fn) { say("NO_D3DCOMPILER %lu", GetLastError()); return 1; }
  hr = CreateDXGIFactory1(&IID_IDXGIFactory1, (void **)&factory);
  if (FAILED(hr)) { say("CreateDXGIFactory1 0x%08lx", (unsigned long)hr); return 1; }
  for (UINT a = 0; IDXGIFactory1_EnumAdapters1(factory, a, &adapter) != DXGI_ERROR_NOT_FOUND; ++a) {
    DXGI_ADAPTER_DESC1 d;
    IDXGIAdapter1_GetDesc1(adapter, &d);
    if (!pick && !(d.Flags & DXGI_ADAPTER_FLAG_SOFTWARE)) { pick = adapter; continue; }
    IDXGIAdapter1_Release(adapter);
  }
  if (!pick) { say("NO_HARDWARE_ADAPTER"); return 1; }
  hr = D3D11CreateDevice((IDXGIAdapter *)pick, D3D_DRIVER_TYPE_UNKNOWN, NULL, 0, want, 3,
                         D3D11_SDK_VERSION, &device, &level, &ctx);
  say("D3D11CreateDevice 0x%08lx FEATURE_LEVEL 0x%x", (unsigned long)hr, level);
  if (FAILED(hr)) return 1;
  b1 = compile(compile_fn, vs_quad, sizeof(vs_quad) - 1, "vs_4_0");
  b2 = compile(compile_fn, ps_white, sizeof(ps_white) - 1, "ps_4_0");
  b3 = compile(compile_fn, vs_points, sizeof(vs_points) - 1, "vs_4_0");
  b4 = compile(compile_fn, gs_points, sizeof(gs_points) - 1, "gs_4_0");
  if (!b1 || !b2 || !b3 || !b4) return 1;
  {
    D3D11_SO_DECLARATION_ENTRY decl = {0, "SV_Position", 0, 0, 4, 0};
    UINT stride = 16;
    hr = ID3D11Device_CreateVertexShader(device, ID3D10Blob_GetBufferPointer(b1), ID3D10Blob_GetBufferSize(b1), NULL, &vsq);
    if (SUCCEEDED(hr)) hr = ID3D11Device_CreatePixelShader(device, ID3D10Blob_GetBufferPointer(b2), ID3D10Blob_GetBufferSize(b2), NULL, &ps);
    if (SUCCEEDED(hr)) hr = ID3D11Device_CreateVertexShader(device, ID3D10Blob_GetBufferPointer(b3), ID3D10Blob_GetBufferSize(b3), NULL, &vsp);
    if (SUCCEEDED(hr)) hr = ID3D11Device_CreateGeometryShaderWithStreamOutput(device,
        ID3D10Blob_GetBufferPointer(b4), ID3D10Blob_GetBufferSize(b4), &decl, 1, &stride, 1,
        0 /* FL 10_x requires stream 0 rasterized (EXP1292: NO_RASTERIZED_STREAM -> E_INVALIDARG) */,
        NULL, &gs);
    say("SHADERS 0x%08lx", (unsigned long)hr);
    if (FAILED(hr)) return 1;
  }
  {
    D3D11_TEXTURE2D_DESC td;
    D3D11_BUFFER_DESC bd;
    D3D11_QUERY_DESC qd;
    memset(&td, 0, sizeof(td));
    td.Width = td.Height = 64; td.MipLevels = td.ArraySize = 1; td.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    td.SampleDesc.Count = 1; td.Usage = D3D11_USAGE_DEFAULT; td.BindFlags = D3D11_BIND_RENDER_TARGET;
    hr = ID3D11Device_CreateTexture2D(device, &td, NULL, &target);
    if (SUCCEEDED(hr)) hr = ID3D11Device_CreateRenderTargetView(device, (ID3D11Resource *)target, NULL, &rtv);
    memset(&bd, 0, sizeof(bd));
    bd.Usage = D3D11_USAGE_DEFAULT; bd.BindFlags = D3D11_BIND_STREAM_OUTPUT;
    bd.ByteWidth = 5 * 16;
    if (SUCCEEDED(hr)) hr = ID3D11Device_CreateBuffer(device, &bd, NULL, &so_small);
    bd.ByteWidth = 8 * 16;
    if (SUCCEEDED(hr)) hr = ID3D11Device_CreateBuffer(device, &bd, NULL, &so_large);
    say("RESOURCES 0x%08lx", (unsigned long)hr);
    if (FAILED(hr)) return 1;
    memset(&qd, 0, sizeof(qd));
    qd.Query = D3D11_QUERY_PIPELINE_STATISTICS;
    hr = ID3D11Device_CreateQuery(device, &qd, &stats);
    say("CreateQuery PIPELINE_STATISTICS 0x%08lx", (unsigned long)hr);
    qd.Query = D3D11_QUERY_SO_STATISTICS;
    hr = ID3D11Device_CreateQuery(device, &qd, &so_stats);
    say("CreateQuery SO_STATISTICS 0x%08lx", (unsigned long)hr);
    qd.Query = D3D11_QUERY_SO_OVERFLOW_PREDICATE;
    hr = ID3D11Device_CreateQuery(device, &qd, &overflow);
    say("CreateQuery SO_OVERFLOW_PREDICATE 0x%08lx", (unsigned long)hr);
    if (!stats || !so_stats || !overflow) { say("AGX_STATS_SMOKE: FAIL (queries)"); return 1; }
  }
  for (int i = 0; i < iterations; ++i) {
    D3D11_QUERY_DATA_PIPELINE_STATISTICS ps_data;
    D3D11_QUERY_DATA_SO_STATISTICS so_data;
    BOOL over_small = FALSE, over_large = TRUE;
    D3D11_VIEWPORT vp;
    UINT zero = 0;
    int wrong = 0;
    memset(&vp, 0, sizeof(vp)); vp.Width = vp.Height = 64.0f; vp.MaxDepth = 1.0f;
    ID3D11DeviceContext_RSSetViewports(ctx, 1, &vp);
    /* pipeline statistics */
    ID3D11DeviceContext_OMSetRenderTargets(ctx, 1, &rtv, NULL);
    ID3D11DeviceContext_IASetPrimitiveTopology(ctx, D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    ID3D11DeviceContext_VSSetShader(ctx, vsq, NULL, 0);
    ID3D11DeviceContext_GSSetShader(ctx, NULL, NULL, 0);
    ID3D11DeviceContext_PSSetShader(ctx, ps, NULL, 0);
    ID3D11DeviceContext_Begin(ctx, (ID3D11Asynchronous *)stats);
    ID3D11DeviceContext_Draw(ctx, 6, 0);
    ID3D11DeviceContext_End(ctx, (ID3D11Asynchronous *)stats);
    hr = wait_data((ID3D11Asynchronous *)stats, &ps_data, sizeof(ps_data));
    if (hr != S_OK) { say("STATS GetData 0x%08lx at %d", (unsigned long)hr, i); ++bad; break; }
    if (ps_data.IAVertices != 6 || ps_data.IAPrimitives != 2 || ps_data.VSInvocations < 1 ||
        ps_data.VSInvocations > 6 || ps_data.GSInvocations || ps_data.GSPrimitives ||
        ps_data.CInvocations != 2 || ps_data.CPrimitives != 2 || ps_data.PSInvocations != 4096)
      ++wrong;
    if (wrong || i == 0)
      say("PIPELINE %s IA %llu/%llu VS %llu GS %llu/%llu C %llu/%llu PS %llu", wrong ? "WRONG" : "ok",
          ps_data.IAVertices, ps_data.IAPrimitives, ps_data.VSInvocations, ps_data.GSInvocations,
          ps_data.GSPrimitives, ps_data.CInvocations, ps_data.CPrimitives, ps_data.PSInvocations);
    /* stream output: room for five of eight, then for all eight */
    ID3D11DeviceContext_OMSetRenderTargets(ctx, 0, NULL, NULL);
    ID3D11DeviceContext_IASetPrimitiveTopology(ctx, D3D11_PRIMITIVE_TOPOLOGY_POINTLIST);
    ID3D11DeviceContext_VSSetShader(ctx, vsp, NULL, 0);
    ID3D11DeviceContext_GSSetShader(ctx, gs, NULL, 0);
    ID3D11DeviceContext_PSSetShader(ctx, NULL, NULL, 0);
    ID3D11DeviceContext_SOSetTargets(ctx, 1, &so_small, &zero);
    ID3D11DeviceContext_Begin(ctx, (ID3D11Asynchronous *)so_stats);
    ID3D11DeviceContext_Begin(ctx, (ID3D11Asynchronous *)overflow);
    ID3D11DeviceContext_Draw(ctx, 8, 0);
    ID3D11DeviceContext_End(ctx, (ID3D11Asynchronous *)overflow);
    ID3D11DeviceContext_End(ctx, (ID3D11Asynchronous *)so_stats);
    hr = wait_data((ID3D11Asynchronous *)so_stats, &so_data, sizeof(so_data));
    if (hr == S_OK) hr = wait_data((ID3D11Asynchronous *)overflow, &over_small, sizeof(over_small));
    ID3D11DeviceContext_SOSetTargets(ctx, 1, &so_large, &zero);
    ID3D11DeviceContext_Begin(ctx, (ID3D11Asynchronous *)overflow);
    ID3D11DeviceContext_Draw(ctx, 8, 0);
    ID3D11DeviceContext_End(ctx, (ID3D11Asynchronous *)overflow);
    if (hr == S_OK) hr = wait_data((ID3D11Asynchronous *)overflow, &over_large, sizeof(over_large));
    ID3D11DeviceContext_SOSetTargets(ctx, 0, NULL, NULL);
    if (hr != S_OK) { say("SO GetData 0x%08lx at %d", (unsigned long)hr, i); ++bad; break; }
    {
      int so_wrong = so_data.NumPrimitivesWritten != 5 || so_data.PrimitivesStorageNeeded != 8 ||
                     !over_small || over_large;
      if (so_wrong || i == 0)
        say("SO %s written %llu needed %llu overflow %d/%d", so_wrong ? "WRONG" : "ok",
            so_data.NumPrimitivesWritten, so_data.PrimitivesStorageNeeded, over_small, over_large);
      wrong += so_wrong;
    }
    bad += wrong != 0;
    if (wrong) say("ITERATION %d bad removed 0x%08lx", i, (unsigned long)ID3D11Device_GetDeviceRemovedReason(device));
  }
  say("RESULT iterations %d bad %d removed 0x%08lx", iterations, bad,
      (unsigned long)ID3D11Device_GetDeviceRemovedReason(device));
  say("AGX_STATS_SMOKE: %s", bad ? "FAIL" : "PASS");
  return bad ? 1 : 0;
}
