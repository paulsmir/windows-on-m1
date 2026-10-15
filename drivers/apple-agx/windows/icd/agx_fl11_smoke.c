/* D3D-H5 hardware checkpoint: the feature level 11_0 stages.
 * The device is created at FL 11_0 (APPLE_AGX_D3D11_DDI=1 and
 * APPLE_AGX_D3D11_FL11=1 in this process); every case runs and reports on
 * its own:
 * - U (cs_5_0): a 2x2 grid of 8x8 groups writes a typed R32_UINT UAV
 *   texture, InterlockedAdd (constant and per-thread values) and
 *   InterlockedMax on a raw UAV, and appends to an append buffer; the
 *   texture, the three words, CopyStructureCount and the appended values
 *   (sorted) equal the expectation (the per-thread add is the subgroup
 *   reduction Asahi's uniform-atomic optimisation emits, EXP1294 STATS);
 * - T (hs_5_0/ds_5_0): one triangle patch, integer partitioning, factor 4,
 *   the domain shader placing uvw-weighted control points and passing uvw as
 *   colour: a pixel inside is covered, one outside is not, the pixel next to
 *   control point 0 is dominated by u (red); culling of the patch with
 *   back-face culling is reported, not judged;
 * - I: DrawInstancedIndirect of a triangle and DispatchIndirect of one group
 *   writing a marker word, from DRAWINDIRECT_ARGS buffers;
 * - B: a 4x4 BC7 texture (one mode 6 block of a solid colour) sampled to
 *   exactly that colour.
 * 8x MSAA quality levels for R8G8B8A8_UNORM are reported (AGX has none).
 * Every line is written through to the output file before the next step.
 * Usage: agx_fl11_smoke.exe <out.txt> [iterations] */
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

static const char cs_uav[] =
    "RWTexture2D<uint> img : register(u0);\n"
    "RWByteAddressBuffer words : register(u1);\n"
    "AppendStructuredBuffer<uint> app : register(u2);\n"
    "[numthreads(8, 8, 1)]\n"
    "void main(uint3 id : SV_DispatchThreadID) {\n"
    "  img[id.xy] = id.x * 16 + id.y + 1;\n"
    "  uint old;\n"
    "  words.InterlockedAdd(0, 1, old);\n"
    "  words.InterlockedMax(4, id.x * 100 + id.y);\n"
    "  words.InterlockedAdd(8, id.x);\n"
    "  if ((id.x + id.y) % 3 == 0) app.Append(id.x * 16 + id.y);\n"
    "}\n";
static const char cs_marker[] =
    "RWByteAddressBuffer words : register(u0);\n"
    "[numthreads(1, 1, 1)]\n"
    "void main(uint3 id : SV_GroupID) { words.Store(0, 0x600d0000 + id.x + id.y + id.z); }\n";
static const char vs_cp[] =
    "struct CP { float2 p : POS; };\n"
    "CP main(uint id : SV_VertexID) {\n"
    "  CP o; o.p = id == 0 ? float2(-0.9, -0.9) : id == 1 ? float2(-0.9, 0.9) : float2(0.9, -0.9);\n"
    "  return o;\n"
    "}\n";
static const char hs_tri[] =
    "struct CP { float2 p : POS; };\n"
    "struct PC { float e[3] : SV_TessFactor; float i : SV_InsideTessFactor; };\n"
    "PC pc(InputPatch<CP, 3> p) { PC o; o.e[0] = o.e[1] = o.e[2] = 4; o.i = 4; return o; }\n"
    "[domain(\"tri\")] [partitioning(\"integer\")] [outputtopology(\"triangle_cw\")]\n"
    "[outputcontrolpoints(3)] [patchconstantfunc(\"pc\")]\n"
    "CP main(InputPatch<CP, 3> p, uint i : SV_OutputControlPointID) { return p[i]; }\n";
static const char ds_tri[] =
    "struct CP { float2 p : POS; };\n"
    "struct PC { float e[3] : SV_TessFactor; float i : SV_InsideTessFactor; };\n"
    "struct V { float4 pos : SV_Position; float3 c : COLOR; };\n"
    "[domain(\"tri\")]\n"
    "V main(PC pc, float3 uvw : SV_DomainLocation, const OutputPatch<CP, 3> p) {\n"
    "  V v; v.pos = float4(p[0].p * uvw.x + p[1].p * uvw.y + p[2].p * uvw.z, 0.5, 1);\n"
    "  v.c = uvw; return v;\n"
    "}\n";
static const char ps_colour[] =
    "struct V { float4 pos : SV_Position; float3 c : COLOR; };\n"
    "float4 main(V v) : SV_Target { return float4(v.c, 1); }\n";
static const char vs_tri[] =
    "struct V { float4 pos : SV_Position; float3 c : COLOR; };\n"
    "V main(uint id : SV_VertexID) {\n"
    "  V v; v.pos = float4(id == 2 ? 0.9 : -0.9, id == 1 ? 0.9 : -0.9, 0.5, 1);\n"
    "  v.c = float3(0, 1, 0); return v;\n"
    "}\n";
static const char vs_full[] =
    "float4 main(uint id : SV_VertexID) : SV_Position {\n"
    "  return float4(id == 1 ? 3.0 : -1.0, id == 2 ? -3.0 : 1.0, 0, 1);\n"
    "}\n";
static const char ps_bc7[] =
    "Texture2D<float4> t : register(t0);\n"
    "SamplerState s : register(s0);\n"
    "float4 main(float4 p : SV_Position) : SV_Target { return t.Sample(s, float2(0.5, 0.5)); }\n";

#define SIZE 64u

static ID3D11Device *device;
static ID3D11DeviceContext *ctx;
static pD3DCompile compile_fn;

static ID3DBlob *compile(const char *src, const char *target) {
  ID3DBlob *code = NULL, *errors = NULL;
  HRESULT hr = compile_fn(src, strlen(src), NULL, NULL, NULL, "main", target, 0, 0, &code, &errors);
  if (FAILED(hr)) say("D3DCompile %s 0x%08lx", target, (unsigned long)hr);
  if (errors) {
    say("%.300s", (const char *)ID3D10Blob_GetBufferPointer(errors));
    ID3D10Blob_Release(errors);
  }
  return SUCCEEDED(hr) ? code : NULL;
}

#define SHADER(kind, Kind, src, target, outvar)                                              \
  do {                                                                                       \
    ID3DBlob *b_ = compile(src, target);                                                     \
    HRESULT h_ = b_ ? ID3D11Device_Create##Kind##Shader(device, ID3D10Blob_GetBufferPointer(b_), \
                         ID3D10Blob_GetBufferSize(b_), NULL, &(outvar)) : E_FAIL;          \
    say("Create" #Kind "Shader %s 0x%08lx", target, (unsigned long)h_);                     \
    if (b_) ID3D10Blob_Release(b_);                                                          \
  } while (0)

static ID3D11Buffer *buffer(UINT bytes, UINT bind, UINT misc, UINT stride, const void *data, UINT usage,
                            UINT cpu) {
  D3D11_BUFFER_DESC bd;
  D3D11_SUBRESOURCE_DATA init;
  ID3D11Buffer *b = NULL;
  memset(&bd, 0, sizeof(bd));
  bd.ByteWidth = bytes;
  bd.Usage = (D3D11_USAGE)usage;
  bd.BindFlags = bind;
  bd.MiscFlags = misc;
  bd.StructureByteStride = stride;
  bd.CPUAccessFlags = cpu;
  init.pSysMem = data;
  init.SysMemPitch = init.SysMemSlicePitch = 0;
  if (FAILED(ID3D11Device_CreateBuffer(device, &bd, data ? &init : NULL, &b))) return NULL;
  return b;
}

/* Copies 'src' to a new staging resource and maps it. */
static int read_buffer(ID3D11Buffer *src, UINT bytes, void *dst) {
  ID3D11Buffer *stage = buffer(bytes, 0, 0, 0, NULL, D3D11_USAGE_STAGING, D3D11_CPU_ACCESS_READ);
  D3D11_MAPPED_SUBRESOURCE m;
  if (!stage) return 0;
  ID3D11DeviceContext_CopyResource(ctx, (ID3D11Resource *)stage, (ID3D11Resource *)src);
  if (FAILED(ID3D11DeviceContext_Map(ctx, (ID3D11Resource *)stage, 0, D3D11_MAP_READ, 0, &m))) {
    ID3D11Buffer_Release(stage);
    return 0;
  }
  memcpy(dst, m.pData, bytes);
  ID3D11DeviceContext_Unmap(ctx, (ID3D11Resource *)stage, 0);
  ID3D11Buffer_Release(stage);
  return 1;
}

static ID3D11Texture2D *texture(UINT w, UINT h, DXGI_FORMAT format, UINT bind, UINT usage, UINT cpu,
                                const D3D11_SUBRESOURCE_DATA *data) {
  D3D11_TEXTURE2D_DESC td;
  ID3D11Texture2D *t = NULL;
  memset(&td, 0, sizeof(td));
  td.Width = w; td.Height = h; td.MipLevels = td.ArraySize = 1;
  td.Format = format; td.SampleDesc.Count = 1;
  td.Usage = (D3D11_USAGE)usage; td.BindFlags = bind; td.CPUAccessFlags = cpu;
  if (FAILED(ID3D11Device_CreateTexture2D(device, &td, data, &t))) return NULL;
  return t;
}

/* One texel (RGBA8 or R32) of a 2D texture through a staging copy. */
static int read_texels(ID3D11Texture2D *src, UINT w, UINT h, DXGI_FORMAT format, UINT *texels) {
  ID3D11Texture2D *stage = texture(w, h, format, 0, D3D11_USAGE_STAGING, D3D11_CPU_ACCESS_READ, NULL);
  D3D11_MAPPED_SUBRESOURCE m;
  if (!stage) return 0;
  ID3D11DeviceContext_CopyResource(ctx, (ID3D11Resource *)stage, (ID3D11Resource *)src);
  if (FAILED(ID3D11DeviceContext_Map(ctx, (ID3D11Resource *)stage, 0, D3D11_MAP_READ, 0, &m))) {
    ID3D11Texture2D_Release(stage);
    return 0;
  }
  for (UINT y = 0; y < h; ++y)
    memcpy(texels + y * w, (const unsigned char *)m.pData + (size_t)y * m.RowPitch, w * 4u);
  ID3D11DeviceContext_Unmap(ctx, (ID3D11Resource *)stage, 0);
  ID3D11Texture2D_Release(stage);
  return 1;
}

static int compare_uint(const void *a, const void *b) {
  UINT x = *(const UINT *)a, y = *(const UINT *)b;
  return x < y ? -1 : x > y;
}

/* case U */
static int case_uav(int iteration) {
  static ID3D11ComputeShader *cs;
  static ID3D11Texture2D *img;
  static ID3D11Buffer *words, *app, *count;
  static ID3D11UnorderedAccessView *uavs[3];
  static int ready = -1;
  UINT texels[256], w[3], appended[256], n = 0, expect_n = 0, expect_vals[256], bad = 0;
  UINT zero_words[3] = {0, 0, 0}, initial[3] = {~0u, ~0u, 0};
  ID3D11UnorderedAccessView *none[3] = {NULL, NULL, NULL};
  if (ready < 0) {
    D3D11_UNORDERED_ACCESS_VIEW_DESC d;
    HRESULT hr = S_OK;
    ready = 0;
    SHADER(compute, Compute, cs_uav, "cs_5_0", cs);
    img = texture(16, 16, DXGI_FORMAT_R32_UINT, D3D11_BIND_UNORDERED_ACCESS, D3D11_USAGE_DEFAULT, 0, NULL);
    words = buffer(12, D3D11_BIND_UNORDERED_ACCESS, D3D11_RESOURCE_MISC_BUFFER_ALLOW_RAW_VIEWS, 0, NULL,
                   D3D11_USAGE_DEFAULT, 0);
    app = buffer(256 * 4, D3D11_BIND_UNORDERED_ACCESS, D3D11_RESOURCE_MISC_BUFFER_STRUCTURED, 4, NULL,
                 D3D11_USAGE_DEFAULT, 0);
    count = buffer(16, 0, 0, 0, NULL, D3D11_USAGE_DEFAULT, 0);
    if (!cs || !img || !words || !app || !count) { say("U SETUP missing"); return 1; }
    hr = ID3D11Device_CreateUnorderedAccessView(device, (ID3D11Resource *)img, NULL, &uavs[0]);
    memset(&d, 0, sizeof(d));
    d.Format = DXGI_FORMAT_R32_TYPELESS; d.ViewDimension = D3D11_UAV_DIMENSION_BUFFER;
    d.Buffer.NumElements = 3; d.Buffer.Flags = D3D11_BUFFER_UAV_FLAG_RAW;
    if (SUCCEEDED(hr)) hr = ID3D11Device_CreateUnorderedAccessView(device, (ID3D11Resource *)words, &d, &uavs[1]);
    memset(&d, 0, sizeof(d));
    d.Format = DXGI_FORMAT_UNKNOWN; d.ViewDimension = D3D11_UAV_DIMENSION_BUFFER;
    d.Buffer.NumElements = 256; d.Buffer.Flags = D3D11_BUFFER_UAV_FLAG_APPEND;
    if (SUCCEEDED(hr)) hr = ID3D11Device_CreateUnorderedAccessView(device, (ID3D11Resource *)app, &d, &uavs[2]);
    say("U VIEWS 0x%08lx", (unsigned long)hr);
    if (FAILED(hr)) return 1;
    ready = 1;
  }
  if (!ready) return 1;
  ID3D11DeviceContext_UpdateSubresource(ctx, (ID3D11Resource *)words, 0, NULL, zero_words, 0, 0);
  ID3D11DeviceContext_CSSetShader(ctx, cs, NULL, 0);
  ID3D11DeviceContext_CSSetUnorderedAccessViews(ctx, 0, 3, uavs, initial);
  ID3D11DeviceContext_Dispatch(ctx, 2, 2, 1);
  ID3D11DeviceContext_CSSetUnorderedAccessViews(ctx, 0, 3, none, NULL);
  ID3D11DeviceContext_CopyStructureCount(ctx, count, 0, uavs[2]);
  if (!read_texels(img, 16, 16, DXGI_FORMAT_R32_UINT, texels) || !read_buffer(words, 12, w) ||
      !read_buffer(count, 4, &n) || !read_buffer(app, sizeof(appended), appended)) {
    say("U READBACK failed at %d", iteration);
    return 1;
  }
  for (UINT y = 0; y < 16; ++y)
    for (UINT x = 0; x < 16; ++x) {
      if (texels[y * 16 + x] != x * 16 + y + 1 && bad++ < 2)
        say("U WRONG texel %u,%u got %u", x, y, texels[y * 16 + x]);
      if ((x + y) % 3 == 0) expect_vals[expect_n++] = x * 16 + y;
    }
  if (w[0] != 256 || w[1] != 1515 || w[2] != 1920) {
    say("U WRONG atomics add %u max %u add-x %u (expect 256 1515 1920)", w[0], w[1], w[2]);
    ++bad;
  }
  if (n != expect_n) { say("U WRONG append count %u expect %u", n, expect_n); ++bad; }
  else {
    qsort(appended, n, 4, compare_uint);
    qsort(expect_vals, n, 4, compare_uint);
    if (memcmp(appended, expect_vals, n * 4)) { say("U WRONG appended values"); ++bad; }
  }
  if (bad || iteration == 0)
    say("U %s iteration %d atomics %u/%u/%u append %u", bad ? "WRONG" : "ok", iteration, w[0], w[1],
        w[2], n);
  return bad != 0;
}

/* case T */
static int case_tess(int iteration) {
  static ID3D11VertexShader *vs;
  static ID3D11HullShader *hs;
  static ID3D11DomainShader *ds;
  static ID3D11PixelShader *ps;
  static ID3D11Texture2D *target;
  static ID3D11RenderTargetView *rtv;
  static ID3D11RasterizerState *none, *back;
  static int ready = -1;
  const float clear[4] = {0, 0, 0, 0};
  D3D11_VIEWPORT vp;
  UINT texels[SIZE * SIZE], culled_texels[SIZE * SIZE];
  int bad = 0;
  if (ready < 0) {
    D3D11_RASTERIZER_DESC rd;
    HRESULT hr;
    ready = 0;
    SHADER(vertex, Vertex, vs_cp, "vs_5_0", vs);
    SHADER(hull, Hull, hs_tri, "hs_5_0", hs);
    SHADER(domain, Domain, ds_tri, "ds_5_0", ds);
    SHADER(pixel, Pixel, ps_colour, "ps_5_0", ps);
    target = texture(SIZE, SIZE, DXGI_FORMAT_R8G8B8A8_UNORM, D3D11_BIND_RENDER_TARGET,
                     D3D11_USAGE_DEFAULT, 0, NULL);
    if (!vs || !hs || !ds || !ps || !target) { say("T SETUP missing"); return 1; }
    hr = ID3D11Device_CreateRenderTargetView(device, (ID3D11Resource *)target, NULL, &rtv);
    memset(&rd, 0, sizeof(rd));
    rd.FillMode = D3D11_FILL_SOLID; rd.CullMode = D3D11_CULL_NONE; rd.DepthClipEnable = TRUE;
    if (SUCCEEDED(hr)) hr = ID3D11Device_CreateRasterizerState(device, &rd, &none);
    rd.CullMode = D3D11_CULL_BACK;
    if (SUCCEEDED(hr)) hr = ID3D11Device_CreateRasterizerState(device, &rd, &back);
    say("T STATE 0x%08lx", (unsigned long)hr);
    if (FAILED(hr)) return 1;
    ready = 1;
  }
  if (!ready) return 1;
  memset(&vp, 0, sizeof(vp));
  vp.Width = vp.Height = (float)SIZE; vp.MaxDepth = 1.0f;
  ID3D11DeviceContext_RSSetViewports(ctx, 1, &vp);
  ID3D11DeviceContext_OMSetRenderTargets(ctx, 1, &rtv, NULL);
  ID3D11DeviceContext_IASetPrimitiveTopology(ctx, D3D11_PRIMITIVE_TOPOLOGY_3_CONTROL_POINT_PATCHLIST);
  ID3D11DeviceContext_VSSetShader(ctx, vs, NULL, 0);
  ID3D11DeviceContext_HSSetShader(ctx, hs, NULL, 0);
  ID3D11DeviceContext_DSSetShader(ctx, ds, NULL, 0);
  ID3D11DeviceContext_GSSetShader(ctx, NULL, NULL, 0);
  ID3D11DeviceContext_PSSetShader(ctx, ps, NULL, 0);
  for (int pass = 0; pass < 2; ++pass) {
    ID3D11DeviceContext_ClearRenderTargetView(ctx, rtv, clear);
    ID3D11DeviceContext_RSSetState(ctx, pass ? back : none);
    ID3D11DeviceContext_Draw(ctx, 3, 0);
    if (!read_texels(target, SIZE, SIZE, DXGI_FORMAT_R8G8B8A8_UNORM, pass ? culled_texels : texels)) {
      say("T READBACK failed at %d", iteration);
      return 1;
    }
  }
  ID3D11DeviceContext_HSSetShader(ctx, NULL, NULL, 0);
  ID3D11DeviceContext_DSSetShader(ctx, NULL, NULL, 0);
  ID3D11DeviceContext_RSSetState(ctx, NULL);
  {
    /* control point 0 at (-0.9,-0.9): pixel (4, 59); inside (16, 40);
     * outside, above the hypotenuse (56, 8) */
    UINT near0 = texels[59 * SIZE + 4], inside = texels[40 * SIZE + 16], outside = texels[8 * SIZE + 56];
    UINT r = near0 & 0xffu, g = (near0 >> 8) & 0xffu, b = (near0 >> 16) & 0xffu;
    UINT covered = 0, covered_back = 0;
    for (UINT i = 0; i < SIZE * SIZE; ++i) {
      covered += (texels[i] >> 24) != 0;
      covered_back += (culled_texels[i] >> 24) != 0;
    }
    if (!(inside >> 24) || (outside >> 24) || !(near0 >> 24) || r <= g || r <= b) ++bad;
    if (bad || iteration == 0)
      say("T %s iteration %d near0 0x%08x inside 0x%08x outside 0x%08x covered %u cull-back covered %u",
          bad ? "WRONG" : "ok", iteration, near0, inside, outside, covered, covered_back);
  }
  return bad;
}

/* case I */
static int case_indirect(int iteration) {
  static ID3D11VertexShader *vs;
  static ID3D11PixelShader *ps;
  static ID3D11ComputeShader *cs;
  static ID3D11Buffer *draw_args, *dispatch_args, *marker;
  static ID3D11UnorderedAccessView *marker_uav;
  static ID3D11Texture2D *target;
  static ID3D11RenderTargetView *rtv;
  static int ready = -1;
  const float clear[4] = {0, 0, 0, 0};
  UINT texels[SIZE * SIZE], word = 0, zero = 0;
  int bad = 0;
  if (ready < 0) {
    const UINT draw[4] = {3, 1, 0, 0}, dispatch[3] = {1, 1, 1};
    D3D11_UNORDERED_ACCESS_VIEW_DESC d;
    HRESULT hr;
    ready = 0;
    SHADER(vertex, Vertex, vs_tri, "vs_5_0", vs);
    SHADER(pixel, Pixel, ps_colour, "ps_5_0", ps);
    SHADER(compute, Compute, cs_marker, "cs_5_0", cs);
    draw_args = buffer(16, 0, D3D11_RESOURCE_MISC_DRAWINDIRECT_ARGS, 0, draw, D3D11_USAGE_DEFAULT, 0);
    dispatch_args = buffer(12, 0, D3D11_RESOURCE_MISC_DRAWINDIRECT_ARGS, 0, dispatch, D3D11_USAGE_DEFAULT, 0);
    marker = buffer(4, D3D11_BIND_UNORDERED_ACCESS, D3D11_RESOURCE_MISC_BUFFER_ALLOW_RAW_VIEWS, 0, NULL,
                    D3D11_USAGE_DEFAULT, 0);
    target = texture(SIZE, SIZE, DXGI_FORMAT_R8G8B8A8_UNORM, D3D11_BIND_RENDER_TARGET,
                     D3D11_USAGE_DEFAULT, 0, NULL);
    if (!vs || !ps || !cs || !draw_args || !dispatch_args || !marker || !target) {
      say("I SETUP missing");
      return 1;
    }
    hr = ID3D11Device_CreateRenderTargetView(device, (ID3D11Resource *)target, NULL, &rtv);
    memset(&d, 0, sizeof(d));
    d.Format = DXGI_FORMAT_R32_TYPELESS; d.ViewDimension = D3D11_UAV_DIMENSION_BUFFER;
    d.Buffer.NumElements = 1; d.Buffer.Flags = D3D11_BUFFER_UAV_FLAG_RAW;
    if (SUCCEEDED(hr)) hr = ID3D11Device_CreateUnorderedAccessView(device, (ID3D11Resource *)marker, &d, &marker_uav);
    say("I VIEWS 0x%08lx", (unsigned long)hr);
    if (FAILED(hr)) return 1;
    ready = 1;
  }
  if (!ready) return 1;
  {
    D3D11_VIEWPORT vp;
    ID3D11UnorderedAccessView *none = NULL;
    memset(&vp, 0, sizeof(vp));
    vp.Width = vp.Height = (float)SIZE; vp.MaxDepth = 1.0f;
    ID3D11DeviceContext_RSSetViewports(ctx, 1, &vp);
    ID3D11DeviceContext_OMSetRenderTargets(ctx, 1, &rtv, NULL);
    ID3D11DeviceContext_ClearRenderTargetView(ctx, rtv, clear);
    ID3D11DeviceContext_IASetPrimitiveTopology(ctx, D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    ID3D11DeviceContext_VSSetShader(ctx, vs, NULL, 0);
    ID3D11DeviceContext_PSSetShader(ctx, ps, NULL, 0);
    ID3D11DeviceContext_DrawInstancedIndirect(ctx, draw_args, 0);
    ID3D11DeviceContext_UpdateSubresource(ctx, (ID3D11Resource *)marker, 0, NULL, &zero, 0, 0);
    ID3D11DeviceContext_CSSetShader(ctx, cs, NULL, 0);
    ID3D11DeviceContext_CSSetUnorderedAccessViews(ctx, 0, 1, &marker_uav, NULL);
    ID3D11DeviceContext_DispatchIndirect(ctx, dispatch_args, 0);
    ID3D11DeviceContext_CSSetUnorderedAccessViews(ctx, 0, 1, &none, NULL);
  }
  if (!read_texels(target, SIZE, SIZE, DXGI_FORMAT_R8G8B8A8_UNORM, texels) || !read_buffer(marker, 4, &word)) {
    say("I READBACK failed at %d", iteration);
    return 1;
  }
  /* the clockwise triangle (-0.9,-0.9) (-0.9,0.9) (0.9,-0.9): lower-left half */
  if (texels[48 * SIZE + 16] != 0xff00ff00u || texels[8 * SIZE + 56] != 0u || word != 0x600d0000u) ++bad;
  if (bad || iteration == 0)
    say("I %s iteration %d inside 0x%08x outside 0x%08x marker 0x%08x", bad ? "WRONG" : "ok", iteration,
        texels[48 * SIZE + 16], texels[8 * SIZE + 56], word);
  return bad;
}

/* A BC7 mode 6 block of one colour: endpoints equal, p-bits 0, indices 0;
 * each channel decodes to its 7-bit endpoint << 1. */
static void bc7_solid(unsigned char block[16], UINT r7, UINT g7, UINT b7, UINT a7) {
  unsigned bit = 0;
  const UINT fields[] = {r7, r7, g7, g7, b7, b7, a7, a7};
  memset(block, 0, 16);
  block[0] = 0x40; /* mode 6: bit 6 */
  bit = 7;
  for (unsigned f = 0; f < 8; ++f)
    for (unsigned i = 0; i < 7; ++i, ++bit)
      if (fields[f] & (1u << i)) block[bit / 8] |= (unsigned char)(1u << (bit % 8));
}

/* case B */
static int case_bc7(int iteration) {
  static ID3D11VertexShader *vs;
  static ID3D11PixelShader *ps;
  static ID3D11Texture2D *bc7, *target;
  static ID3D11ShaderResourceView *srv;
  static ID3D11SamplerState *sampler;
  static ID3D11RenderTargetView *rtv;
  static int ready = -1;
  UINT texels[SIZE * SIZE];
  const UINT expect = 0xfe204080u; /* r 0x80 g 0x40 b 0x20 a 0xfe */
  int bad = 0;
  if (ready < 0) {
    unsigned char block[16];
    D3D11_SUBRESOURCE_DATA init;
    D3D11_SAMPLER_DESC sd;
    HRESULT hr;
    ready = 0;
    bc7_solid(block, 0x40, 0x20, 0x10, 0x7f);
    init.pSysMem = block; init.SysMemPitch = 16; init.SysMemSlicePitch = 0;
    SHADER(vertex, Vertex, vs_full, "vs_5_0", vs);
    SHADER(pixel, Pixel, ps_bc7, "ps_5_0", ps);
    bc7 = texture(4, 4, DXGI_FORMAT_BC7_UNORM, D3D11_BIND_SHADER_RESOURCE, D3D11_USAGE_DEFAULT, 0, &init);
    target = texture(SIZE, SIZE, DXGI_FORMAT_R8G8B8A8_UNORM, D3D11_BIND_RENDER_TARGET,
                     D3D11_USAGE_DEFAULT, 0, NULL);
    say("B TEXTURE %d", bc7 != NULL);
    if (!vs || !ps || !bc7 || !target) return 1;
    hr = ID3D11Device_CreateShaderResourceView(device, (ID3D11Resource *)bc7, NULL, &srv);
    if (SUCCEEDED(hr)) hr = ID3D11Device_CreateRenderTargetView(device, (ID3D11Resource *)target, NULL, &rtv);
    memset(&sd, 0, sizeof(sd));
    sd.Filter = D3D11_FILTER_MIN_MAG_MIP_POINT;
    sd.AddressU = sd.AddressV = sd.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
    sd.MaxLOD = D3D11_FLOAT32_MAX;
    if (SUCCEEDED(hr)) hr = ID3D11Device_CreateSamplerState(device, &sd, &sampler);
    say("B VIEWS 0x%08lx", (unsigned long)hr);
    if (FAILED(hr)) return 1;
    ready = 1;
  }
  if (!ready) return 1;
  {
    D3D11_VIEWPORT vp;
    memset(&vp, 0, sizeof(vp));
    vp.Width = vp.Height = (float)SIZE; vp.MaxDepth = 1.0f;
    ID3D11DeviceContext_RSSetViewports(ctx, 1, &vp);
    ID3D11DeviceContext_OMSetRenderTargets(ctx, 1, &rtv, NULL);
    ID3D11DeviceContext_IASetPrimitiveTopology(ctx, D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    ID3D11DeviceContext_VSSetShader(ctx, vs, NULL, 0);
    ID3D11DeviceContext_PSSetShader(ctx, ps, NULL, 0);
    ID3D11DeviceContext_PSSetShaderResources(ctx, 0, 1, &srv);
    ID3D11DeviceContext_PSSetSamplers(ctx, 0, 1, &sampler);
    ID3D11DeviceContext_Draw(ctx, 3, 0);
  }
  if (!read_texels(target, SIZE, SIZE, DXGI_FORMAT_R8G8B8A8_UNORM, texels)) {
    say("B READBACK failed at %d", iteration);
    return 1;
  }
  if (texels[32 * SIZE + 32] != expect) ++bad;
  if (bad || iteration == 0)
    say("B %s iteration %d texel 0x%08x expect 0x%08x", bad ? "WRONG" : "ok", iteration,
        texels[32 * SIZE + 32], expect);
  return bad;
}

int main(int argc, char **argv) {
  IDXGIFactory1 *factory = NULL;
  IDXGIAdapter1 *adapter = NULL, *pick = NULL;
  D3D_FEATURE_LEVEL level = 0;
  const D3D_FEATURE_LEVEL want[] = {D3D_FEATURE_LEVEL_11_0};
  HMODULE compiler;
  HRESULT hr;
  UINT quality8 = 0;
  int iterations, bad[4] = {0, 0, 0, 0};

  if (argc < 2) {
    printf("usage: agx_fl11_smoke.exe <out.txt> [iterations]\n");
    return 2;
  }
  iterations = argc > 2 ? atoi(argv[2]) : 10;
  if (iterations < 1) iterations = 1;
  out = CreateFileA(argv[1], GENERIC_WRITE, FILE_SHARE_READ, NULL, CREATE_ALWAYS,
                    FILE_ATTRIBUTE_NORMAL | FILE_FLAG_WRITE_THROUGH, NULL);
  say("FL11 iterations %d", iterations);
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
  hr = D3D11CreateDevice((IDXGIAdapter *)pick, D3D_DRIVER_TYPE_UNKNOWN, NULL, 0, want, 1,
                         D3D11_SDK_VERSION, &device, &level, &ctx);
  say("D3D11CreateDevice 0x%08lx FEATURE_LEVEL 0x%x", (unsigned long)hr, level);
  if (FAILED(hr)) { say("AGX_FL11_SMOKE: FAIL (no FL 11_0 device)"); return 1; }
  ID3D11Device_CheckMultisampleQualityLevels(device, DXGI_FORMAT_R8G8B8A8_UNORM, 8, &quality8);
  say("MSAA8 quality %u", quality8);
  for (int i = 0; i < iterations; ++i) {
    bad[0] += case_uav(i);
    bad[1] += case_tess(i);
    bad[2] += case_indirect(i);
    bad[3] += case_bc7(i);
    if (ID3D11Device_GetDeviceRemovedReason(device) != S_OK) {
      say("REMOVED 0x%08lx at %d", (unsigned long)ID3D11Device_GetDeviceRemovedReason(device), i);
      break;
    }
  }
  say("RESULT iterations %d U %d T %d I %d B %d removed 0x%08lx", iterations, bad[0], bad[1], bad[2],
      bad[3], (unsigned long)ID3D11Device_GetDeviceRemovedReason(device));
  say("AGX_FL11_SMOKE: %s", (bad[0] || bad[1] || bad[2] || bad[3]) ? "FAIL" : "PASS");
  return (bad[0] || bad[1] || bad[2] || bad[3]) ? 1 : 0;
}
