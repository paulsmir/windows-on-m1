/* D3D-G9 hardware checkpoint: the D3D10 sampling instructions beyond plain
 * sample/sample_l/ld. A pixel shader writes one result per pixel of an 8x1
 * R8G8B8A8 target:
 *   x0 SampleLevel(mip 1)         -> green (mip 1 colour)
 *   x1 SampleBias(+2) at lod 0    -> blue  (mip 2)
 *   x2 SampleGrad(2-texel steps)  -> green (lod 1)
 *   x3 GetDimensions(mip 1)       -> (2/4, 2/4, 3/4, 1) = 0x?bf8080
 *   x4 SampleCmp(ref 0.5, texel 0.25, LESS)          -> 0 (alpha too)
 *   x5 SampleCmpLevelZero(ref 0.5, texel 1.0, LESS)  -> 1
 *   x6 Sample at lod 0            -> red   (mip 0)
 * The texture is B8G8R8A8 4x4 with three mips (red, green, blue), the
 * comparison texture R32_FLOAT 2x2 (0.25, 0.5, 0.75, 1.0), point sampling.
 * Implicit-LOD samples (x1, x4, x6) are taken in uniform control flow at a
 * coordinate whose derivatives give LOD 0 (EXP1288: inside the branches the
 * derivatives were undefined); the explicit ones stay in early-returning
 * branches (EXP1287: fragment exports inside control flow).
 * Every line is written through to the output file before the next step.
 * Usage: agx_sample_smoke.exe <out.txt> [iterations] */
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
    "Texture2D<float4> t : register(t0);\n"
    "Texture2D<float> d : register(t1);\n"
    "SamplerState s : register(s0);\n"
    "SamplerComparisonState c : register(s1);\n"
    "float4 main(float4 p : SV_Position) : SV_Target {\n"
    "  int x = (int)p.x;\n"
    "  float2 centre = float2(0.5, 0.5);\n"
    /* EXP1288: implicit-LOD samples run in uniform control flow (D3D leaves
     * derivatives in non-uniform flow undefined); uv moves one texel of
     * mip 0 per pixel in x and y, so the implicit LOD is 0. */
    "  float2 uv = p.xy / 4.0;\n"
    "  float4 biased = t.SampleBias(s, uv, 2.0);\n"
    "  float4 implicit = t.Sample(s, uv);\n"
    "  float4 compared = d.SampleCmp(c, float2(0.25, 0.25), 0.5).xxxx;\n"
    "  if (x == 0) return t.SampleLevel(s, centre, 1);\n"
    "  if (x == 1) return biased;\n"
    "  if (x == 2) return t.SampleGrad(s, centre, float2(0.5, 0), float2(0, 0.5));\n"
    "  if (x == 3) { uint w, h, l; t.GetDimensions(1, w, h, l);\n"
    "                return float4(w / 4.0, h / 4.0, l / 4.0, 1); }\n"
    "  if (x == 4) return compared;\n"
    "  if (x == 5) return d.SampleCmpLevelZero(c, float2(0.75, 0.75), 0.5).xxxx;\n"
    "  if (x == 6) return implicit;\n"
    "  return float4(0, 0, 0, 1);\n"
    "}\n";

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

static int close_channel(unsigned got, unsigned want) {
  for (int c = 0; c < 32; c += 8) {
    int d = (int)((got >> c) & 0xffu) - (int)((want >> c) & 0xffu);
    if (d < -2 || d > 2) return 0;
  }
  return 1;
}

int main(int argc, char **argv) {
  IDXGIFactory1 *factory = NULL;
  IDXGIAdapter1 *adapter = NULL, *pick = NULL;
  ID3D11Device *device = NULL;
  ID3D11DeviceContext *ctx = NULL;
  ID3D11Texture2D *target = NULL, *staging = NULL, *colour = NULL, *depthlike = NULL;
  ID3D11RenderTargetView *rtv = NULL;
  ID3D11ShaderResourceView *srv[2] = {0};
  ID3D11SamplerState *samplers[2] = {0};
  ID3D11VertexShader *vs = NULL;
  ID3D11PixelShader *ps = NULL;
  ID3DBlob *vsb, *psb;
  D3D_FEATURE_LEVEL level = 0;
  const D3D_FEATURE_LEVEL want[] = {D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_1,
                                    D3D_FEATURE_LEVEL_10_0};
  /* RGBA8 in memory (little-endian u32): red 0xff0000ff, green 0xff00ff00, blue 0xffff0000. */
  const unsigned want_px[7] = {0xff00ff00u, 0xffff0000u, 0xff00ff00u, 0xffbf8080u,
                               0x00000000u, 0xffffffffu, 0xff0000ffu};
  HMODULE compiler;
  pD3DCompile compile_fn;
  HRESULT hr;
  int iterations, bad = 0;

  if (argc < 2) {
    printf("usage: agx_sample_smoke.exe <out.txt> [iterations]\n");
    return 2;
  }
  iterations = argc > 2 ? atoi(argv[2]) : 10;
  if (iterations < 1) iterations = 1;
  out = CreateFileA(argv[1], GENERIC_WRITE, FILE_SHARE_READ, NULL, CREATE_ALWAYS,
                    FILE_ATTRIBUTE_NORMAL | FILE_FLAG_WRITE_THROUGH, NULL);
  say("SAMPLE iterations %d", iterations);
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
    D3D11_SUBRESOURCE_DATA mips[3], cmp;
    D3D11_SAMPLER_DESC sd;
    static unsigned mip0[16], mip1[4], mip2[1];
    static const float cmp_texels[4] = {0.25f, 0.5f, 0.75f, 1.0f};
    for (int i = 0; i < 16; ++i) mip0[i] = 0xffff0000u; /* B8G8R8A8 red: B 00 G 00 R ff A ff */
    for (int i = 0; i < 4; ++i) mip1[i] = 0xff00ff00u;  /* green */
    mip2[0] = 0xff0000ffu;                              /* blue */
    mips[0].pSysMem = mip0; mips[0].SysMemPitch = 16; mips[0].SysMemSlicePitch = 0;
    mips[1].pSysMem = mip1; mips[1].SysMemPitch = 8; mips[1].SysMemSlicePitch = 0;
    mips[2].pSysMem = mip2; mips[2].SysMemPitch = 4; mips[2].SysMemSlicePitch = 0;
    memset(&td, 0, sizeof(td));
    td.Width = 8; td.Height = 1; td.MipLevels = td.ArraySize = 1;
    td.Format = DXGI_FORMAT_R8G8B8A8_UNORM; td.SampleDesc.Count = 1;
    td.Usage = D3D11_USAGE_DEFAULT; td.BindFlags = D3D11_BIND_RENDER_TARGET;
    hr = ID3D11Device_CreateTexture2D(device, &td, NULL, &target);
    if (SUCCEEDED(hr)) hr = ID3D11Device_CreateRenderTargetView(device, (ID3D11Resource *)target, NULL, &rtv);
    td.Usage = D3D11_USAGE_STAGING; td.BindFlags = 0; td.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    if (SUCCEEDED(hr)) hr = ID3D11Device_CreateTexture2D(device, &td, NULL, &staging);
    say("TARGET 0x%08lx", (unsigned long)hr);
    if (FAILED(hr)) return 1;
    td.Width = td.Height = 4; td.MipLevels = 3; td.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    td.Usage = D3D11_USAGE_DEFAULT; td.BindFlags = D3D11_BIND_SHADER_RESOURCE; td.CPUAccessFlags = 0;
    hr = ID3D11Device_CreateTexture2D(device, &td, mips, &colour);
    if (SUCCEEDED(hr)) hr = ID3D11Device_CreateShaderResourceView(device, (ID3D11Resource *)colour, NULL, &srv[0]);
    say("COLOUR_TEXTURE 0x%08lx", (unsigned long)hr);
    if (FAILED(hr)) return 1;
    td.Width = td.Height = 2; td.MipLevels = 1; td.Format = DXGI_FORMAT_R32_FLOAT;
    cmp.pSysMem = cmp_texels; cmp.SysMemPitch = 8; cmp.SysMemSlicePitch = 0;
    hr = ID3D11Device_CreateTexture2D(device, &td, &cmp, &depthlike);
    if (SUCCEEDED(hr)) hr = ID3D11Device_CreateShaderResourceView(device, (ID3D11Resource *)depthlike, NULL, &srv[1]);
    say("COMPARISON_TEXTURE 0x%08lx", (unsigned long)hr);
    if (FAILED(hr)) return 1;
    memset(&sd, 0, sizeof(sd));
    sd.Filter = D3D11_FILTER_MIN_MAG_MIP_POINT;
    sd.AddressU = sd.AddressV = sd.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
    sd.MaxLOD = D3D11_FLOAT32_MAX; sd.ComparisonFunc = D3D11_COMPARISON_NEVER;
    hr = ID3D11Device_CreateSamplerState(device, &sd, &samplers[0]);
    sd.Filter = D3D11_FILTER_COMPARISON_MIN_MAG_MIP_POINT; sd.ComparisonFunc = D3D11_COMPARISON_LESS;
    if (SUCCEEDED(hr)) hr = ID3D11Device_CreateSamplerState(device, &sd, &samplers[1]);
    say("SAMPLERS 0x%08lx", (unsigned long)hr);
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
    vp.Width = 8.0f; vp.Height = 1.0f; vp.MaxDepth = 1.0f;
    ID3D11DeviceContext_RSSetViewports(ctx, 1, &vp);
  }
  ID3D11DeviceContext_OMSetRenderTargets(ctx, 1, &rtv, NULL);
  ID3D11DeviceContext_IASetPrimitiveTopology(ctx, D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
  ID3D11DeviceContext_VSSetShader(ctx, vs, NULL, 0);
  ID3D11DeviceContext_PSSetShader(ctx, ps, NULL, 0);
  ID3D11DeviceContext_PSSetShaderResources(ctx, 0, 2, srv);
  ID3D11DeviceContext_PSSetSamplers(ctx, 0, 2, samplers);
  for (int i = 0; i < iterations; ++i) {
    const float black[4] = {0.0f, 0.0f, 0.0f, 1.0f};
    D3D11_MAPPED_SUBRESOURCE map;
    unsigned got[7];
    ID3D11DeviceContext_ClearRenderTargetView(ctx, rtv, black);
    ID3D11DeviceContext_Draw(ctx, 3, 0);
    ID3D11DeviceContext_CopyResource(ctx, (ID3D11Resource *)staging, (ID3D11Resource *)target);
    hr = ID3D11DeviceContext_Map(ctx, (ID3D11Resource *)staging, 0, D3D11_MAP_READ, 0, &map);
    if (FAILED(hr)) {
      say("READBACK_MAP 0x%08lx removed 0x%08lx", (unsigned long)hr,
          (unsigned long)ID3D11Device_GetDeviceRemovedReason(device));
      return 1;
    }
    memcpy(got, map.pData, sizeof(got));
    ID3D11DeviceContext_Unmap(ctx, (ID3D11Resource *)staging, 0);
    for (int k = 0; k < 7; ++k)
      if (!close_channel(got[k], want_px[k])) {
        if (bad < 14) say("WRONG iteration %d op %d got 0x%08x want 0x%08x", i, k, got[k], want_px[k]);
        ++bad;
      }
    if (i == 0 || i == iterations - 1)
      say("ITERATION %d bad %d pixels %08x %08x %08x %08x %08x %08x %08x removed 0x%08lx", i, bad,
          got[0], got[1], got[2], got[3], got[4], got[5], got[6],
          (unsigned long)ID3D11Device_GetDeviceRemovedReason(device));
  }
  say("RESULT iterations %d bad %d removed 0x%08lx", iterations, bad,
      (unsigned long)ID3D11Device_GetDeviceRemovedReason(device));
  say(bad ? "AGX_SAMPLE_SMOKE: FAIL" : "AGX_SAMPLE_SMOKE: PASS");
  return bad ? 1 : 0;
}
