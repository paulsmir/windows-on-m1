/* D3D-H2b hardware checkpoint: compute on 10.x hardware (cs_4_x).
 * (cs_4_1 needs FL 10_1; the device is FL 10_0.)
 * - the device reports D3D11_FEATURE_D3D10_X_HARDWARE_OPTIONS
 *   ComputeShaders_Plus_RawAndStructuredBuffers_Via_Shader_4_x;
 * - case S (cs_4_0): four groups of 64 threads read a structured SRV
 *   {id, value}, a raw SRV word and a constant buffer, add up 64 groupshared
 *   words after a group barrier and write a uint4 per thread to a structured
 *   UAV;
 * - case R (cs_4_0): a 4x2 grid of 8x4 groups writes three words per thread
 *   at byte addresses of a raw UAV from a raw SRV;
 * - case C: ClearUnorderedAccessViewUint fills a raw UAV with 0xa5a5a5a5,
 *   one dispatch writes every other word, a second dispatch reads that
 *   buffer through an SRV (D3D11 orders the two dispatches with no API
 *   barrier) and writes word + 3 * index to another raw UAV.
 * Every result is copied to a staging buffer and compared word by word with
 * the expectation computed here; the inputs change every iteration.
 * Every line is written through to the output file before the next step.
 * Usage: agx_compute_smoke.exe <out.txt> [iterations] */
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

static const char cs_structured[] =
    "struct Item { uint id; float value; };\n"
    "StructuredBuffer<Item> items : register(t0);\n"
    "ByteAddressBuffer words : register(t1);\n"
    "RWStructuredBuffer<uint4> result : register(u0);\n"
    "cbuffer params : register(b0) { uint scale; uint bias; };\n"
    "groupshared uint partial[64];\n"
    "[numthreads(64, 1, 1)]\n"
    "void main(uint3 dtid : SV_DispatchThreadID, uint3 gid : SV_GroupID, uint gi : SV_GroupIndex) {\n"
    "  Item it = items[dtid.x];\n"
    "  partial[gi] = it.id * scale + words.Load(dtid.x * 4);\n"
    "  GroupMemoryBarrierWithGroupSync();\n"
    "  uint sum = 0;\n"
    "  for (uint i = 0; i < 64; ++i) sum += partial[i];\n"
    "  result[dtid.x] = uint4(sum, gid.x, asuint(it.value * 2.0f), partial[(gi + 1) & 63] + bias);\n"
    "}\n";
static const char cs_raw[] =
    "ByteAddressBuffer src : register(t0);\n"
    "RWByteAddressBuffer dst : register(u0);\n"
    "[numthreads(8, 4, 1)]\n"
    "void main(uint3 dtid : SV_DispatchThreadID, uint3 gtid : SV_GroupThreadID, uint3 gid : SV_GroupID) {\n"
    "  uint index = dtid.y * 32 + dtid.x;\n"
    "  uint2 v = src.Load2(index * 8);\n"
    "  dst.Store3(index * 12, uint3(v.x ^ v.y, gtid.x + gtid.y * 8, gid.x | (gid.y << 16)));\n"
    "}\n";
static const char cs_even[] =
    "RWByteAddressBuffer dst : register(u0);\n"
    "cbuffer params : register(b0) { uint scale; uint bias; };\n"
    "[numthreads(64, 1, 1)]\n"
    "void main(uint3 dtid : SV_DispatchThreadID) {\n"
    "  if ((dtid.x & 1) == 0) dst.Store(dtid.x * 4, dtid.x * scale + bias);\n"
    "}\n";
static const char cs_chain[] =
    "ByteAddressBuffer first : register(t0);\n"
    "RWByteAddressBuffer second : register(u0);\n"
    "[numthreads(64, 1, 1)]\n"
    "void main(uint3 dtid : SV_DispatchThreadID) {\n"
    "  second.Store(dtid.x * 4, first.Load(dtid.x * 4) + dtid.x * 3);\n"
    "}\n";

#define S_THREADS 256u
#define R_INDICES 256u
#define C_WORDS 256u
#define CLEAR_WORD 0xa5a5a5a5u

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

static ID3D11ComputeShader *create_cs(pD3DCompile fn, const char *src, size_t len, const char *target,
                                      const char *name) {
  ID3D11ComputeShader *cs = NULL;
  ID3DBlob *code = compile(fn, src, len, target);
  HRESULT hr;
  if (!code) return NULL;
  hr = ID3D11Device_CreateComputeShader(device, ID3D10Blob_GetBufferPointer(code),
                                        ID3D10Blob_GetBufferSize(code), NULL, &cs);
  say("CreateComputeShader %s %s 0x%08lx", name, target, (unsigned long)hr);
  ID3D10Blob_Release(code);
  return SUCCEEDED(hr) ? cs : NULL;
}

/* misc: 0, D3D11_RESOURCE_MISC_BUFFER_ALLOW_RAW_VIEWS or _STRUCTURED */
static ID3D11Buffer *buffer(UINT bytes, UINT bind, UINT misc, UINT stride, const void *data,
                            const char *name) {
  D3D11_BUFFER_DESC bd;
  D3D11_SUBRESOURCE_DATA init;
  ID3D11Buffer *b = NULL;
  HRESULT hr;
  memset(&bd, 0, sizeof(bd));
  bd.ByteWidth = bytes;
  bd.Usage = D3D11_USAGE_DEFAULT;
  bd.BindFlags = bind;
  bd.MiscFlags = misc;
  bd.StructureByteStride = stride;
  init.pSysMem = data;
  init.SysMemPitch = init.SysMemSlicePitch = 0;
  hr = ID3D11Device_CreateBuffer(device, &bd, data ? &init : NULL, &b);
  if (FAILED(hr)) say("CreateBuffer %s 0x%08lx", name, (unsigned long)hr);
  return SUCCEEDED(hr) ? b : NULL;
}

static ID3D11Buffer *staging(UINT bytes) {
  D3D11_BUFFER_DESC bd;
  ID3D11Buffer *b = NULL;
  memset(&bd, 0, sizeof(bd));
  bd.ByteWidth = bytes;
  bd.Usage = D3D11_USAGE_STAGING;
  bd.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
  return SUCCEEDED(ID3D11Device_CreateBuffer(device, &bd, NULL, &b)) ? b : NULL;
}

static ID3D11ShaderResourceView *raw_srv(ID3D11Buffer *b, UINT words) {
  D3D11_SHADER_RESOURCE_VIEW_DESC d;
  ID3D11ShaderResourceView *v = NULL;
  HRESULT hr;
  memset(&d, 0, sizeof(d));
  d.Format = DXGI_FORMAT_R32_TYPELESS;
  d.ViewDimension = D3D11_SRV_DIMENSION_BUFFEREX;
  d.BufferEx.NumElements = words;
  d.BufferEx.Flags = D3D11_BUFFEREX_SRV_FLAG_RAW;
  hr = ID3D11Device_CreateShaderResourceView(device, (ID3D11Resource *)b, &d, &v);
  if (FAILED(hr)) say("CreateShaderResourceView raw 0x%08lx", (unsigned long)hr);
  return v;
}

static ID3D11UnorderedAccessView *raw_uav(ID3D11Buffer *b, UINT words) {
  D3D11_UNORDERED_ACCESS_VIEW_DESC d;
  ID3D11UnorderedAccessView *v = NULL;
  HRESULT hr;
  memset(&d, 0, sizeof(d));
  d.Format = DXGI_FORMAT_R32_TYPELESS;
  d.ViewDimension = D3D11_UAV_DIMENSION_BUFFER;
  d.Buffer.NumElements = words;
  d.Buffer.Flags = D3D11_BUFFER_UAV_FLAG_RAW;
  hr = ID3D11Device_CreateUnorderedAccessView(device, (ID3D11Resource *)b, &d, &v);
  if (FAILED(hr)) say("CreateUnorderedAccessView raw 0x%08lx", (unsigned long)hr);
  return v;
}

/* Copies 'src' to 'stage' and compares 'words' words with 'expect';
 * reports the first three differences.  Returns the number wrong. */
static int compare(ID3D11Buffer *src, ID3D11Buffer *stage, const UINT *expect, UINT words,
                   const char *name, int iteration) {
  D3D11_MAPPED_SUBRESOURCE m;
  HRESULT hr;
  int wrong = 0;
  ID3D11DeviceContext_CopyResource(ctx, (ID3D11Resource *)stage, (ID3D11Resource *)src);
  hr = ID3D11DeviceContext_Map(ctx, (ID3D11Resource *)stage, 0, D3D11_MAP_READ, 0, &m);
  if (FAILED(hr)) {
    say("%s Map 0x%08lx at %d", name, (unsigned long)hr, iteration);
    return (int)words;
  }
  for (UINT i = 0; i < words; ++i) {
    UINT got = ((const UINT *)m.pData)[i];
    if (got != expect[i] && wrong++ < 3)
      say("%s WRONG iteration %d word %u got 0x%08x expect 0x%08x", name, iteration, i, got, expect[i]);
  }
  ID3D11DeviceContext_Unmap(ctx, (ID3D11Resource *)stage, 0);
  return wrong;
}

int main(int argc, char **argv) {
  IDXGIFactory1 *factory = NULL;
  IDXGIAdapter1 *adapter = NULL, *pick = NULL;
  D3D11_FEATURE_DATA_D3D10_X_HARDWARE_OPTIONS options;
  ID3D11ComputeShader *cs_s, *cs_r, *cs_e, *cs_c;
  ID3D11Buffer *items, *words, *params, *result_s, *src_r, *dst_r, *first, *second;
  ID3D11Buffer *stage_s, *stage_r, *stage_c;
  ID3D11ShaderResourceView *items_v, *words_v, *src_v, *first_v;
  ID3D11UnorderedAccessView *result_u, *dst_u, *first_u, *second_u;
  D3D_FEATURE_LEVEL level = 0;
  const D3D_FEATURE_LEVEL want[] = {D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_1,
                                    D3D_FEATURE_LEVEL_10_0};
  static UINT item_data[S_THREADS * 2], word_data[S_THREADS], src_data[R_INDICES * 2];
  static UINT expect_s[S_THREADS * 4], expect_r[R_INDICES * 3], expect_c[C_WORDS];
  HMODULE compiler;
  pD3DCompile compile_fn;
  HRESULT hr;
  int iterations, bad = 0;

  if (argc < 2) {
    printf("usage: agx_compute_smoke.exe <out.txt> [iterations]\n");
    return 2;
  }
  iterations = argc > 2 ? atoi(argv[2]) : 10;
  if (iterations < 1) iterations = 1;
  out = CreateFileA(argv[1], GENERIC_WRITE, FILE_SHARE_READ, NULL, CREATE_ALWAYS,
                    FILE_ATTRIBUTE_NORMAL | FILE_FLAG_WRITE_THROUGH, NULL);
  say("COMPUTE iterations %d", iterations);
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
  memset(&options, 0, sizeof(options));
  hr = ID3D11Device_CheckFeatureSupport(device, D3D11_FEATURE_D3D10_X_HARDWARE_OPTIONS, &options,
                                        sizeof(options));
  say("D3D10_X_HARDWARE_OPTIONS 0x%08lx compute_raw_structured %d", (unsigned long)hr,
      options.ComputeShaders_Plus_RawAndStructuredBuffers_Via_Shader_4_x);
  if (FAILED(hr) || !options.ComputeShaders_Plus_RawAndStructuredBuffers_Via_Shader_4_x) {
    say("AGX_COMPUTE_SMOKE: FAIL (no compute on this device)");
    return 1;
  }
  cs_s = create_cs(compile_fn, cs_structured, sizeof(cs_structured) - 1, "cs_4_0", "S");
  cs_r = create_cs(compile_fn, cs_raw, sizeof(cs_raw) - 1, "cs_4_0", "R");
  cs_e = create_cs(compile_fn, cs_even, sizeof(cs_even) - 1, "cs_4_0", "C1");
  cs_c = create_cs(compile_fn, cs_chain, sizeof(cs_chain) - 1, "cs_4_0", "C2");
  if (!cs_s || !cs_r || !cs_e || !cs_c) { say("AGX_COMPUTE_SMOKE: FAIL (shaders)"); return 1; }

  for (UINT i = 0; i < S_THREADS; ++i) {
    float value = (float)i * 0.5f;
    item_data[2 * i] = i * 3u + 1u;
    memcpy(&item_data[2 * i + 1], &value, 4);
    word_data[i] = i * 11u;
  }
  for (UINT i = 0; i < R_INDICES; ++i) {
    src_data[2 * i] = i * 5u + 1u;
    src_data[2 * i + 1] = i ^ 0x55u;
  }
  items = buffer(sizeof(item_data), D3D11_BIND_SHADER_RESOURCE, D3D11_RESOURCE_MISC_BUFFER_STRUCTURED, 8,
                 item_data, "items");
  words = buffer(sizeof(word_data), D3D11_BIND_SHADER_RESOURCE, D3D11_RESOURCE_MISC_BUFFER_ALLOW_RAW_VIEWS,
                 0, word_data, "words");
  params = buffer(16, D3D11_BIND_CONSTANT_BUFFER, 0, 0, NULL, "params");
  result_s = buffer(S_THREADS * 16, D3D11_BIND_UNORDERED_ACCESS, D3D11_RESOURCE_MISC_BUFFER_STRUCTURED,
                    16, NULL, "result");
  src_r = buffer(sizeof(src_data), D3D11_BIND_SHADER_RESOURCE, D3D11_RESOURCE_MISC_BUFFER_ALLOW_RAW_VIEWS,
                 0, src_data, "src");
  dst_r = buffer(R_INDICES * 12, D3D11_BIND_UNORDERED_ACCESS, D3D11_RESOURCE_MISC_BUFFER_ALLOW_RAW_VIEWS,
                 0, NULL, "dst");
  first = buffer(C_WORDS * 4, D3D11_BIND_UNORDERED_ACCESS | D3D11_BIND_SHADER_RESOURCE,
                 D3D11_RESOURCE_MISC_BUFFER_ALLOW_RAW_VIEWS, 0, NULL, "first");
  second = buffer(C_WORDS * 4, D3D11_BIND_UNORDERED_ACCESS, D3D11_RESOURCE_MISC_BUFFER_ALLOW_RAW_VIEWS,
                  0, NULL, "second");
  stage_s = staging(S_THREADS * 16);
  stage_r = staging(R_INDICES * 12);
  stage_c = staging(C_WORDS * 4);
  if (!items || !words || !params || !result_s || !src_r || !dst_r || !first || !second || !stage_s ||
      !stage_r || !stage_c) {
    say("AGX_COMPUTE_SMOKE: FAIL (buffers)");
    return 1;
  }
  {
    D3D11_SHADER_RESOURCE_VIEW_DESC sd;
    D3D11_UNORDERED_ACCESS_VIEW_DESC ud;
    memset(&sd, 0, sizeof(sd));
    sd.Format = DXGI_FORMAT_UNKNOWN;
    sd.ViewDimension = D3D11_SRV_DIMENSION_BUFFER;
    sd.Buffer.NumElements = S_THREADS;
    items_v = NULL;
    hr = ID3D11Device_CreateShaderResourceView(device, (ID3D11Resource *)items, &sd, &items_v);
    if (FAILED(hr)) say("CreateShaderResourceView structured 0x%08lx", (unsigned long)hr);
    memset(&ud, 0, sizeof(ud));
    ud.Format = DXGI_FORMAT_UNKNOWN;
    ud.ViewDimension = D3D11_UAV_DIMENSION_BUFFER;
    ud.Buffer.NumElements = S_THREADS;
    result_u = NULL;
    hr = ID3D11Device_CreateUnorderedAccessView(device, (ID3D11Resource *)result_s, &ud, &result_u);
    if (FAILED(hr)) say("CreateUnorderedAccessView structured 0x%08lx", (unsigned long)hr);
  }
  words_v = raw_srv(words, S_THREADS);
  src_v = raw_srv(src_r, R_INDICES * 2);
  first_v = raw_srv(first, C_WORDS);
  dst_u = raw_uav(dst_r, R_INDICES * 3);
  first_u = raw_uav(first, C_WORDS);
  second_u = raw_uav(second, C_WORDS);
  say("VIEWS %d", items_v && result_u && words_v && src_v && first_v && dst_u && first_u && second_u);
  if (!items_v || !result_u || !words_v || !src_v || !first_v || !dst_u || !first_u || !second_u) {
    say("AGX_COMPUTE_SMOKE: FAIL (views)");
    return 1;
  }

  for (int it = 0; it < iterations; ++it) {
    UINT scale = 2u + (UINT)it, bias = 5u + 7u * (UINT)it, cb[4] = {scale, bias, 0, 0};
    const UINT clear[4] = {CLEAR_WORD, 0, 0, 0};
    ID3D11ShaderResourceView *srvs[2], *no_srvs[2] = {NULL, NULL};
    ID3D11UnorderedAccessView *no_uav = NULL;
    int wrong_s, wrong_r, wrong_c;

    ID3D11DeviceContext_UpdateSubresource(ctx, (ID3D11Resource *)params, 0, NULL, cb, 0, 0);
    /* case S */
    for (UINT g = 0; g < S_THREADS / 64u; ++g) {
      UINT sum = 0;
      for (UINT t = 0; t < 64u; ++t) sum += item_data[2 * (g * 64 + t)] * scale + word_data[g * 64 + t];
      for (UINT t = 0; t < 64u; ++t) {
        UINT i = g * 64u + t, next = g * 64u + ((t + 1u) & 63u);
        float twice = (float)i;
        expect_s[4 * i] = sum;
        expect_s[4 * i + 1] = g;
        memcpy(&expect_s[4 * i + 2], &twice, 4);
        expect_s[4 * i + 3] = item_data[2 * next] * scale + word_data[next] + bias;
      }
    }
    srvs[0] = items_v; srvs[1] = words_v;
    ID3D11DeviceContext_CSSetShader(ctx, cs_s, NULL, 0);
    ID3D11DeviceContext_CSSetShaderResources(ctx, 0, 2, srvs);
    ID3D11DeviceContext_CSSetConstantBuffers(ctx, 0, 1, &params);
    ID3D11DeviceContext_CSSetUnorderedAccessViews(ctx, 0, 1, &result_u, NULL);
    ID3D11DeviceContext_Dispatch(ctx, S_THREADS / 64u, 1, 1);
    ID3D11DeviceContext_CSSetUnorderedAccessViews(ctx, 0, 1, &no_uav, NULL);
    ID3D11DeviceContext_CSSetShaderResources(ctx, 0, 2, no_srvs);
    wrong_s = compare(result_s, stage_s, expect_s, S_THREADS * 4, "S", it);
    /* case R */
    for (UINT y = 0; y < 8u; ++y)
      for (UINT x = 0; x < 32u; ++x) {
        UINT i = y * 32u + x;
        expect_r[3 * i] = src_data[2 * i] ^ src_data[2 * i + 1];
        expect_r[3 * i + 1] = (x % 8u) + (y % 4u) * 8u;
        expect_r[3 * i + 2] = (x / 8u) | ((y / 4u) << 16);
      }
    ID3D11DeviceContext_CSSetShader(ctx, cs_r, NULL, 0);
    ID3D11DeviceContext_CSSetShaderResources(ctx, 0, 1, &src_v);
    ID3D11DeviceContext_CSSetUnorderedAccessViews(ctx, 0, 1, &dst_u, NULL);
    ID3D11DeviceContext_Dispatch(ctx, 4, 2, 1);
    ID3D11DeviceContext_CSSetUnorderedAccessViews(ctx, 0, 1, &no_uav, NULL);
    ID3D11DeviceContext_CSSetShaderResources(ctx, 0, 1, no_srvs);
    wrong_r = compare(dst_r, stage_r, expect_r, R_INDICES * 3, "R", it);
    /* case C: clear, write even words, chain through an SRV */
    for (UINT i = 0; i < C_WORDS; ++i)
      expect_c[i] = ((i & 1u) ? CLEAR_WORD : i * scale + bias) + i * 3u;
    ID3D11DeviceContext_ClearUnorderedAccessViewUint(ctx, first_u, clear);
    ID3D11DeviceContext_CSSetShader(ctx, cs_e, NULL, 0);
    ID3D11DeviceContext_CSSetUnorderedAccessViews(ctx, 0, 1, &first_u, NULL);
    ID3D11DeviceContext_Dispatch(ctx, C_WORDS / 64u, 1, 1);
    ID3D11DeviceContext_CSSetUnorderedAccessViews(ctx, 0, 1, &no_uav, NULL);
    ID3D11DeviceContext_CSSetShader(ctx, cs_c, NULL, 0);
    ID3D11DeviceContext_CSSetShaderResources(ctx, 0, 1, &first_v);
    ID3D11DeviceContext_CSSetUnorderedAccessViews(ctx, 0, 1, &second_u, NULL);
    ID3D11DeviceContext_Dispatch(ctx, C_WORDS / 64u, 1, 1);
    ID3D11DeviceContext_CSSetUnorderedAccessViews(ctx, 0, 1, &no_uav, NULL);
    ID3D11DeviceContext_CSSetShaderResources(ctx, 0, 1, no_srvs);
    wrong_c = compare(second, stage_c, expect_c, C_WORDS, "C", it);

    if (wrong_s || wrong_r || wrong_c || it == 0)
      say("ITERATION %d %s S %d R %d C %d removed 0x%08lx", it,
          (wrong_s || wrong_r || wrong_c) ? "WRONG" : "ok", wrong_s, wrong_r, wrong_c,
          (unsigned long)ID3D11Device_GetDeviceRemovedReason(device));
    bad += (wrong_s || wrong_r || wrong_c);
    if (ID3D11Device_GetDeviceRemovedReason(device) != S_OK) break;
  }
  say("RESULT iterations %d bad %d removed 0x%08lx", iterations, bad,
      (unsigned long)ID3D11Device_GetDeviceRemovedReason(device));
  say("AGX_COMPUTE_SMOKE: %s", bad ? "FAIL" : "PASS");
  return bad ? 1 : 0;
}
