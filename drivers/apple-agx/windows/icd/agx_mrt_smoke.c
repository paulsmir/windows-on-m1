/* D3D-G2b hardware checkpoint: one draw writes several render targets.
 * Each iteration clears four 64x64 R8G8B8A8 targets to black and draws a
 * full-screen triangle whose pixel shader writes SV_Target0..3 a colour that
 * depends on the target index and the iteration (constant buffer). Every
 * target is read back every iteration and checked at its centre and four
 * corners: a target missing from the render (or showing the previous
 * iteration) fails. Four RGBA8 targets are 16 bytes per sample, the tile
 * buffer size the KMD template admits today.
 * Every line is written through to the output file before the next step.
 * Usage: agx_mrt_smoke.exe <out.txt> [targets 2..4] [iterations] */
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

#define SIZE 64
#define MAX_TARGETS 4

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
/* Target t gets ((t+1)*0x20 + i*4) & 0xff in red, (0xff - t*0x30 - i*2) & 0xff
 * in green, t*0x40 in blue, alpha 1 -- distinct per target and iteration. */
static const char ps_source[] =
    "cbuffer C : register(b0) { uint iteration; uint3 pad; };\n"
    "float4 colour(uint t) {\n"
    "  uint r = ((t + 1) * 0x20 + iteration * 4) & 0xff;\n"
    "  uint g = (0xff - t * 0x30 - iteration * 2) & 0xff;\n"
    "  uint b = (t * 0x40) & 0xff;\n"
    "  return float4(r, g, b, 255) / 255.0;\n"
    "}\n"
    "struct O { float4 c0 : SV_Target0; float4 c1 : SV_Target1;\n"
    "           float4 c2 : SV_Target2; float4 c3 : SV_Target3; };\n"
    "O main(float4 p : SV_Position) {\n"
    "  O o; o.c0 = colour(0); o.c1 = colour(1); o.c2 = colour(2); o.c3 = colour(3);\n"
    "  return o;\n"
    "}\n";

static unsigned expected(unsigned t, unsigned i) {
  unsigned r = ((t + 1u) * 0x20u + i * 4u) & 0xffu;
  unsigned g = (0xffu - t * 0x30u - i * 2u) & 0xffu;
  unsigned b = (t * 0x40u) & 0xffu;
  return r | (g << 8) | (b << 16) | 0xff000000u;
}

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

int main(int argc, char **argv) {
  IDXGIFactory1 *factory = NULL;
  IDXGIAdapter1 *adapter = NULL, *pick = NULL;
  ID3D11Device *device = NULL;
  ID3D11DeviceContext *ctx = NULL;
  ID3D11Texture2D *target[MAX_TARGETS] = {0}, *staging[MAX_TARGETS] = {0};
  ID3D11RenderTargetView *rtv[MAX_TARGETS] = {0};
  ID3D11Buffer *cb = NULL;
  ID3D11VertexShader *vs = NULL;
  ID3D11PixelShader *ps = NULL;
  ID3DBlob *vsb, *psb;
  D3D_FEATURE_LEVEL level = 0;
  const D3D_FEATURE_LEVEL want[] = {D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_1,
                                    D3D_FEATURE_LEVEL_10_0};
  HMODULE compiler;
  pD3DCompile compile_fn;
  HRESULT hr;
  int targets, iterations, bad = 0;

  if (argc < 2) {
    printf("usage: agx_mrt_smoke.exe <out.txt> [targets 2..4] [iterations]\n");
    return 2;
  }
  targets = argc > 2 ? atoi(argv[2]) : 4;
  if (targets < 2) targets = 2;
  if (targets > MAX_TARGETS) targets = MAX_TARGETS;
  iterations = argc > 3 ? atoi(argv[3]) : 30;
  if (iterations < 2) iterations = 2;
  out = CreateFileA(argv[1], GENERIC_WRITE, FILE_SHARE_READ, NULL, CREATE_ALWAYS,
                    FILE_ATTRIBUTE_NORMAL | FILE_FLAG_WRITE_THROUGH, NULL);
  say("TARGETS %d iterations %d", targets, iterations);
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
    memset(&td, 0, sizeof(td));
    td.Width = td.Height = SIZE; td.MipLevels = td.ArraySize = 1;
    td.Format = DXGI_FORMAT_R8G8B8A8_UNORM; td.SampleDesc.Count = 1;
    for (int t = 0; t < targets && SUCCEEDED(hr); ++t) {
      td.Usage = D3D11_USAGE_DEFAULT; td.BindFlags = D3D11_BIND_RENDER_TARGET;
      td.CPUAccessFlags = 0;
      hr = ID3D11Device_CreateTexture2D(device, &td, NULL, &target[t]);
      if (SUCCEEDED(hr))
        hr = ID3D11Device_CreateRenderTargetView(device, (ID3D11Resource *)target[t], NULL, &rtv[t]);
      td.Usage = D3D11_USAGE_STAGING; td.BindFlags = 0; td.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
      if (SUCCEEDED(hr)) hr = ID3D11Device_CreateTexture2D(device, &td, NULL, &staging[t]);
    }
    memset(&bd, 0, sizeof(bd));
    bd.ByteWidth = 16; bd.Usage = D3D11_USAGE_DYNAMIC;
    bd.BindFlags = D3D11_BIND_CONSTANT_BUFFER; bd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    if (SUCCEEDED(hr)) hr = ID3D11Device_CreateBuffer(device, &bd, NULL, &cb);
    say("RESOURCES 0x%08lx", (unsigned long)hr);
    if (FAILED(hr)) return 1;
  }
  vsb = compile(compile_fn, vs_source, sizeof(vs_source) - 1, "vs_4_0");
  psb = compile(compile_fn, ps_source, sizeof(ps_source) - 1, "ps_4_0");
  if (!vsb || !psb) return 1;
  hr = ID3D11Device_CreateVertexShader(device, ID3D10Blob_GetBufferPointer(vsb),
                                       ID3D10Blob_GetBufferSize(vsb), NULL, &vs);
  if (SUCCEEDED(hr)) hr = ID3D11Device_CreatePixelShader(device, ID3D10Blob_GetBufferPointer(psb),
                                       ID3D10Blob_GetBufferSize(psb), NULL, &ps);
  say("SHADERS 0x%08lx", (unsigned long)hr);
  if (FAILED(hr)) return 1;
  {
    D3D11_VIEWPORT vp;
    memset(&vp, 0, sizeof(vp));
    vp.Width = vp.Height = (float)SIZE; vp.MaxDepth = 1.0f;
    ID3D11DeviceContext_RSSetViewports(ctx, 1, &vp);
  }
  ID3D11DeviceContext_OMSetRenderTargets(ctx, (UINT)targets, rtv, NULL);
  ID3D11DeviceContext_IASetPrimitiveTopology(ctx, D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
  ID3D11DeviceContext_VSSetShader(ctx, vs, NULL, 0);
  ID3D11DeviceContext_PSSetShader(ctx, ps, NULL, 0);
  ID3D11DeviceContext_PSSetConstantBuffers(ctx, 0, 1, &cb);
  for (int i = 0; i < iterations; ++i) {
    static const int points[5][2] = {{32, 32}, {1, 1}, {62, 1}, {1, 62}, {62, 62}};
    const float black[4] = {0.0f, 0.0f, 0.0f, 1.0f};
    D3D11_MAPPED_SUBRESOURCE map;
    hr = ID3D11DeviceContext_Map(ctx, (ID3D11Resource *)cb, 0, D3D11_MAP_WRITE_DISCARD, 0, &map);
    if (FAILED(hr)) { say("CB_MAP 0x%08lx at %d", (unsigned long)hr, i); return 1; }
    memset(map.pData, 0, 16);
    *(unsigned *)map.pData = (unsigned)i;
    ID3D11DeviceContext_Unmap(ctx, (ID3D11Resource *)cb, 0);
    for (int t = 0; t < targets; ++t)
      ID3D11DeviceContext_ClearRenderTargetView(ctx, rtv[t], black);
    ID3D11DeviceContext_Draw(ctx, 3, 0);
    for (int t = 0; t < targets; ++t)
      ID3D11DeviceContext_CopyResource(ctx, (ID3D11Resource *)staging[t],
                                       (ID3D11Resource *)target[t]);
    for (int t = 0; t < targets; ++t) {
      hr = ID3D11DeviceContext_Map(ctx, (ID3D11Resource *)staging[t], 0, D3D11_MAP_READ, 0, &map);
      if (FAILED(hr)) {
        say("READBACK_MAP 0x%08lx target %d at %d removed 0x%08lx", (unsigned long)hr, t, i,
            (unsigned long)ID3D11Device_GetDeviceRemovedReason(device));
        return 1;
      }
      for (int k = 0; k < 5; ++k) {
        unsigned got = *(const unsigned *)((const unsigned char *)map.pData +
            (size_t)points[k][1] * map.RowPitch + (size_t)points[k][0] * 4u);
        unsigned want_px = expected((unsigned)t, (unsigned)i);
        /* UNORM rounding: allow one step per channel. */
        int ok = 1;
        for (int c = 0; c < 32; c += 8) {
          int d = (int)((got >> c) & 0xffu) - (int)((want_px >> c) & 0xffu);
          if (d < -1 || d > 1) ok = 0;
        }
        if (!ok) {
          if (bad < 6)
            say("WRONG iteration %d target %d pixel %d,%d got 0x%08x want 0x%08x", i, t,
                points[k][0], points[k][1], got, want_px);
          ++bad;
        }
      }
      ID3D11DeviceContext_Unmap(ctx, (ID3D11Resource *)staging[t], 0);
    }
    if (i < 2 || i == iterations - 1)
      say("ITERATION %d bad %d removed 0x%08lx", i, bad,
          (unsigned long)ID3D11Device_GetDeviceRemovedReason(device));
  }
  say("RESULT targets %d iterations %d bad %d removed 0x%08lx", targets, iterations, bad,
      (unsigned long)ID3D11Device_GetDeviceRemovedReason(device));
  say(bad ? "AGX_MRT_SMOKE: FAIL" : "AGX_MRT_SMOKE: PASS");
  return bad ? 1 : 0;
}
