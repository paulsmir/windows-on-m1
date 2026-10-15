/* D3D-G8 hardware checkpoint: general stream output layouts.
 * A geometry shader emits one point per input point with SV_Position,
 * TEXCOORD0 (float2) and TEXCOORD1 (float4), all derived from the vertex id
 * and a per-iteration base; stream output captures them and the buffers are
 * read back and compared exactly:
 * - mode "i": one buffer, position.xyzw + TEXCOORD0.xy + TEXCOORD1.zw
 *   interleaved (32 bytes a vertex);
 * - mode "h": one buffer, position.xyzw, a two-component hole, TEXCOORD0.xy
 *   (the hole is not checked);
 * - mode "m": three buffers, position / TEXCOORD0.xy / TEXCOORD1.xyzw;
 * - mode "v" (D3D-G10e): the "i" layout written by the vertex shader, the
 *   stream output created from the vertex shader's bytecode (no GS).
 * A 64x64 render target stays bound (rasterized stream 0).
 * Every line is written through to the output file before the next step.
 * Usage: agx_so_smoke.exe <out.txt> [i|h|m|v] [iterations] */
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

#define POINTS 8
#define BUFFERS 3

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
    "struct VI { uint id : ID; };\n"
    "VI main(uint id : SV_VertexID) { VI o; o.id = id; return o; }\n";
/* D3D-G10e mode "v": the vertex shader writes the outputs itself and the
 * stream output is created from its bytecode (no geometry shader). */
static const char vs_so_source[] =
    "cbuffer C : register(b0) { float base; float3 pad; };\n"
    "struct V { float4 pos : SV_Position; float2 a : TEXCOORD0; float4 b : TEXCOORD1; };\n"
    "V main(uint id : SV_VertexID) {\n"
    "  V v; float f = (float)id + base;\n"
    "  v.pos = float4(f, f * 2, 0.5, 1); v.a = float2(f + 0.25, f + 0.5);\n"
    "  v.b = float4(f * 10, f * 10 + 1, f * 10 + 2, f * 10 + 3);\n"
    "  return v;\n"
    "}\n";
static const char gs_source[] =
    "cbuffer C : register(b0) { float base; float3 pad; };\n"
    "struct VI { uint id : ID; };\n"
    "struct V { float4 pos : SV_Position; float2 a : TEXCOORD0; float4 b : TEXCOORD1; };\n"
    "[maxvertexcount(1)] void main(point VI i[1], inout PointStream<V> s) {\n"
    "  V v; float f = (float)i[0].id + base;\n"
    "  v.pos = float4(f, f * 2, 0.5, 1); v.a = float2(f + 0.25, f + 0.5);\n"
    "  v.b = float4(f * 10, f * 10 + 1, f * 10 + 2, f * 10 + 3);\n"
    "  s.Append(v);\n"
    "}\n";
static const char ps_source[] =
    "float4 main(float4 p : SV_Position) : SV_Target { return float4(1, 1, 1, 1); }\n";

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

static int close_to(float got, float want) {
  float d = got - want;
  return d > -0.0001f && d < 0.0001f;
}

int main(int argc, char **argv) {
  IDXGIFactory1 *factory = NULL;
  IDXGIAdapter1 *adapter = NULL, *pick = NULL;
  ID3D11Device *device = NULL;
  ID3D11DeviceContext *ctx = NULL;
  ID3D11Texture2D *target = NULL;
  ID3D11RenderTargetView *rtv = NULL;
  ID3D11Buffer *cb = NULL, *so[BUFFERS] = {0}, *staging[BUFFERS] = {0};
  ID3D11VertexShader *vs = NULL;
  ID3D11GeometryShader *gs = NULL;
  ID3D11PixelShader *ps = NULL;
  ID3DBlob *vsb, *gsb, *psb;
  D3D_FEATURE_LEVEL level = 0;
  const D3D_FEATURE_LEVEL want[] = {D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_1,
                                    D3D_FEATURE_LEVEL_10_0};
  HMODULE compiler;
  pD3DCompile compile_fn;
  HRESULT hr;
  const char *mode = argc > 2 ? argv[2] : "i";
  int vertex_so = !strcmp(mode, "v");
  D3D11_SO_DECLARATION_ENTRY decl[3];
  UINT strides[BUFFERS] = {0}, buffers = 1, entries = 3;
  int iterations, bad = 0;

  if (argc < 2) {
    printf("usage: agx_so_smoke.exe <out.txt> [i|h|m|v] [iterations]\n");
    return 2;
  }
  iterations = argc > 3 ? atoi(argv[3]) : 30;
  if (iterations < 2) iterations = 2;
  memset(decl, 0, sizeof(decl));
  decl[0].SemanticName = "SV_Position"; decl[0].ComponentCount = 4;
  if (!strcmp(mode, "m")) {
    decl[1].SemanticName = "TEXCOORD"; decl[1].ComponentCount = 2; decl[1].OutputSlot = 1;
    decl[2].SemanticName = "TEXCOORD"; decl[2].SemanticIndex = 1; decl[2].ComponentCount = 4;
    decl[2].OutputSlot = 2;
    strides[0] = 16; strides[1] = 8; strides[2] = 16; buffers = 3;
  } else if (!strcmp(mode, "h")) {
    decl[1].SemanticName = NULL; decl[1].ComponentCount = 2;
    decl[2].SemanticName = "TEXCOORD"; decl[2].ComponentCount = 2;
    strides[0] = 32;
  } else {
    mode = vertex_so ? "v" : "i";
    decl[1].SemanticName = "TEXCOORD"; decl[1].ComponentCount = 2;
    decl[2].SemanticName = "TEXCOORD"; decl[2].SemanticIndex = 1;
    decl[2].StartComponent = 2; decl[2].ComponentCount = 2;
    strides[0] = 32;
  }
  out = CreateFileA(argv[1], GENERIC_WRITE, FILE_SHARE_READ, NULL, CREATE_ALWAYS,
                    FILE_ATTRIBUTE_NORMAL | FILE_FLAG_WRITE_THROUGH, NULL);
  say("SO mode %s buffers %u iterations %d", mode, buffers, iterations);
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
    td.Width = td.Height = 64; td.MipLevels = td.ArraySize = 1;
    td.Format = DXGI_FORMAT_R8G8B8A8_UNORM; td.SampleDesc.Count = 1;
    td.Usage = D3D11_USAGE_DEFAULT; td.BindFlags = D3D11_BIND_RENDER_TARGET;
    {
      /* EXP1284: a render target created with initial data. */
      static unsigned black_pixels[64 * 64];
      D3D11_SUBRESOURCE_DATA init = {black_pixels, 64 * 4, 0};
      for (int p = 0; p < 64 * 64; ++p) black_pixels[p] = 0xff000000u;
      hr = ID3D11Device_CreateTexture2D(device, &td, &init, &target);
      say("CreateTexture2D render target with data 0x%08lx", (unsigned long)hr);
    }
    if (SUCCEEDED(hr))
      hr = ID3D11Device_CreateRenderTargetView(device, (ID3D11Resource *)target, NULL, &rtv);
    for (UINT b = 0; b < buffers && SUCCEEDED(hr); ++b) {
      memset(&bd, 0, sizeof(bd));
      bd.ByteWidth = strides[b] * POINTS; bd.Usage = D3D11_USAGE_DEFAULT;
      bd.BindFlags = D3D11_BIND_STREAM_OUTPUT;
      hr = ID3D11Device_CreateBuffer(device, &bd, NULL, &so[b]);
      bd.Usage = D3D11_USAGE_STAGING; bd.BindFlags = 0; bd.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
      if (SUCCEEDED(hr)) hr = ID3D11Device_CreateBuffer(device, &bd, NULL, &staging[b]);
    }
    memset(&bd, 0, sizeof(bd));
    bd.ByteWidth = 16; bd.Usage = D3D11_USAGE_DYNAMIC;
    bd.BindFlags = D3D11_BIND_CONSTANT_BUFFER; bd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    if (SUCCEEDED(hr)) hr = ID3D11Device_CreateBuffer(device, &bd, NULL, &cb);
    say("RESOURCES 0x%08lx", (unsigned long)hr);
    if (FAILED(hr)) return 1;
  }
  vsb = vertex_so ? compile(compile_fn, vs_so_source, sizeof(vs_so_source) - 1, "vs_4_0")
                  : compile(compile_fn, vs_source, sizeof(vs_source) - 1, "vs_4_0");
  gsb = compile(compile_fn, gs_source, sizeof(gs_source) - 1, "gs_4_0");
  psb = compile(compile_fn, ps_source, sizeof(ps_source) - 1, "ps_4_0");
  if (!vsb || !gsb || !psb) return 1;
  hr = ID3D11Device_CreateVertexShader(device, ID3D10Blob_GetBufferPointer(vsb),
                                       ID3D10Blob_GetBufferSize(vsb), NULL, &vs);
  if (SUCCEEDED(hr)) hr = ID3D11Device_CreatePixelShader(device, ID3D10Blob_GetBufferPointer(psb),
                                       ID3D10Blob_GetBufferSize(psb), NULL, &ps);
  if (SUCCEEDED(hr))
    hr = ID3D11Device_CreateGeometryShaderWithStreamOutput(device,
        ID3D10Blob_GetBufferPointer(vertex_so ? vsb : gsb),
        ID3D10Blob_GetBufferSize(vertex_so ? vsb : gsb), decl, entries, strides, buffers, 0, NULL, &gs);
  say("SHADERS 0x%08lx", (unsigned long)hr);
  if (FAILED(hr)) return 1;
  {
    D3D11_VIEWPORT vp;
    memset(&vp, 0, sizeof(vp));
    vp.Width = vp.Height = 64.0f; vp.MaxDepth = 1.0f;
    ID3D11DeviceContext_RSSetViewports(ctx, 1, &vp);
  }
  ID3D11DeviceContext_OMSetRenderTargets(ctx, 1, &rtv, NULL);
  ID3D11DeviceContext_IASetPrimitiveTopology(ctx, D3D11_PRIMITIVE_TOPOLOGY_POINTLIST);
  ID3D11DeviceContext_VSSetShader(ctx, vs, NULL, 0);
  ID3D11DeviceContext_GSSetShader(ctx, gs, NULL, 0);
  ID3D11DeviceContext_PSSetShader(ctx, ps, NULL, 0);
  ID3D11DeviceContext_GSSetConstantBuffers(ctx, 0, 1, &cb);
  ID3D11DeviceContext_VSSetConstantBuffers(ctx, 0, 1, &cb); /* mode v */
  for (int i = 0; i < iterations; ++i) {
    const float black[4] = {0.0f, 0.0f, 0.0f, 1.0f};
    const UINT zero[BUFFERS] = {0, 0, 0};
    D3D11_MAPPED_SUBRESOURCE map;
    float base = (float)(i * 16);
    hr = ID3D11DeviceContext_Map(ctx, (ID3D11Resource *)cb, 0, D3D11_MAP_WRITE_DISCARD, 0, &map);
    if (FAILED(hr)) { say("CB_MAP 0x%08lx", (unsigned long)hr); return 1; }
    memset(map.pData, 0, 16);
    *(float *)map.pData = base;
    ID3D11DeviceContext_Unmap(ctx, (ID3D11Resource *)cb, 0);
    ID3D11DeviceContext_ClearRenderTargetView(ctx, rtv, black);
    ID3D11DeviceContext_SOSetTargets(ctx, buffers, so, zero);
    ID3D11DeviceContext_Draw(ctx, POINTS, 0);
    ID3D11DeviceContext_SOSetTargets(ctx, 0, NULL, NULL);
    for (UINT b = 0; b < buffers; ++b)
      ID3D11DeviceContext_CopyResource(ctx, (ID3D11Resource *)staging[b], (ID3D11Resource *)so[b]);
    for (UINT b = 0; b < buffers; ++b) {
      const float *f;
      hr = ID3D11DeviceContext_Map(ctx, (ID3D11Resource *)staging[b], 0, D3D11_MAP_READ, 0, &map);
      if (FAILED(hr)) {
        say("SO_MAP 0x%08lx buffer %u at %d removed 0x%08lx", (unsigned long)hr, b, i,
            (unsigned long)ID3D11Device_GetDeviceRemovedReason(device));
        return 1;
      }
      f = (const float *)map.pData;
      for (int k = 0; k < POINTS; ++k) {
        float v = (float)k + base, want_v[8];
        int n = 0, skip_lo = -1, skip_hi = -1;
        const float *row = f + (size_t)k * (strides[b] / 4u);
        const float pos[4] = {v, v * 2.0f, 0.5f, 1.0f}, a[2] = {v + 0.25f, v + 0.5f};
        const float c[4] = {v * 10.0f, v * 10.0f + 1.0f, v * 10.0f + 2.0f, v * 10.0f + 3.0f};
        if (!strcmp(mode, "m")) {
          const float *src = b == 0 ? pos : b == 1 ? a : c;
          n = b == 1 ? 2 : 4;
          memcpy(want_v, src, (size_t)n * sizeof(float));
        } else {
          memcpy(want_v, pos, sizeof(pos));
          if (!strcmp(mode, "h")) { skip_lo = 4; skip_hi = 5; memcpy(want_v + 6, a, sizeof(a)); }
          else { memcpy(want_v + 4, a, sizeof(a)); want_v[6] = c[2]; want_v[7] = c[3]; }
          n = 8;
        }
        for (int e = 0; e < n; ++e) {
          if (e >= skip_lo && e <= skip_hi) continue;
          if (!close_to(row[e], want_v[e])) {
            if (bad < 8)
              say("WRONG iteration %d buffer %u vertex %d component %d got %.4f want %.4f", i, b, k,
                  e, row[e], want_v[e]);
            ++bad;
          }
        }
      }
      ID3D11DeviceContext_Unmap(ctx, (ID3D11Resource *)staging[b], 0);
    }
    if (i < 2 || i == iterations - 1)
      say("ITERATION %d bad %d removed 0x%08lx", i, bad,
          (unsigned long)ID3D11Device_GetDeviceRemovedReason(device));
  }
  say("RESULT mode %s iterations %d bad %d removed 0x%08lx", mode, iterations, bad,
      (unsigned long)ID3D11Device_GetDeviceRemovedReason(device));
  say(bad ? "AGX_SO_SMOKE: FAIL" : "AGX_SO_SMOKE: PASS");
  return bad ? 1 : 0;
}
