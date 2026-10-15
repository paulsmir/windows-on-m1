/* D3D-G1 hardware checkpoint: a geometry shader runs as compute work inside
 * the render batch, and the render waits for it.
 * Each iteration draws four points; the geometry shader expands each into a
 * 16x16 pixel quad of its own colour in a 64x64 target. Iterations alternate
 * two layouts 4 pixels apart, and every iteration is read back and checked
 * at pixels inside each quad and just outside it, where the other layout's
 * quad lies: a render that ran before (or without) this iteration's compute
 * output shows the previous layout or nothing.
 * Mode "so" also captures the geometry shader's positions with stream output
 * (one 16-byte position per vertex, the shape the UMD admits) and checks that
 * the two triangles of each quad name exactly its four corners.
 * Mode "adj" (D3D-G10c) draws the four points as TRIANGLELIST_ADJ
 * primitives (six vertices each): the triangle vertices 0, 2 and 4 average
 * to the point, the adjacency vertices 1, 3 and 5 lie 0.5 away, so a
 * geometry shader fed the wrong vertices draws its quad off target.
 * Every line is written through to the output file before the next step, so
 * the log survives a bugcheck.
 * Usage: agx_gs_smoke.exe <out.txt> [gs|so|adj] [iterations] */
#define COBJMACROS
#define INITGUID
#include <windows.h>
#include <d3d11.h>
#include <d3dcompiler.h>
#include <dxgi1_2.h>
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define SIZE 64
#define HALF 8 /* quad half-size in pixels: 0.25 in NDC */

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
    "cbuffer C : register(b0) { float4 offset; };\n"
    "struct V { float4 p : SV_Position; float4 c : COLOR0; };\n"
    "V main(uint id : SV_VertexID) {\n"
    "  V v;\n"
    "  float2 centre = float2((id & 1) ? 0.5 : -0.5, (id & 2) ? -0.5 : 0.5);\n"
    "  v.p = float4(centre + offset.xy, 0, 1);\n"
    "  v.c = float4((id == 0 || id == 3) ? 1 : 0, (id == 1 || id == 3) ? 1 : 0,\n"
    "               id == 2 ? 1 : 0, 1);\n"
    "  return v;\n"
    "}\n";
static const char gs_source[] =
    "struct V { float4 p : SV_Position; float4 c : COLOR0; };\n"
    "[maxvertexcount(4)]\n"
    "void main(point V input[1], inout TriangleStream<V> s) {\n"
    "  const float h = 0.25;\n"
    "  V o; o.c = input[0].c;\n"
    "  o.p = input[0].p + float4(-h,  h, 0, 0); s.Append(o);\n"
    "  o.p = input[0].p + float4( h,  h, 0, 0); s.Append(o);\n"
    "  o.p = input[0].p + float4(-h, -h, 0, 0); s.Append(o);\n"
    "  o.p = input[0].p + float4( h, -h, 0, 0); s.Append(o);\n"
    "}\n";
static const char vs_adj_source[] =
    "cbuffer C : register(b0) { float4 offset; };\n"
    "struct V { float4 p : SV_Position; float4 c : COLOR0; };\n"
    "V main(uint vertex : SV_VertexID) {\n"
    "  V v; uint id = vertex / 6, j = vertex % 6;\n"
    "  float2 centre = float2((id & 1) ? 0.5 : -0.5, (id & 2) ? -0.5 : 0.5);\n"
    "  float2 d = j == 0 ? float2(0.03, 0) : j == 2 ? float2(-0.03, 0) :\n"
    "             j == 4 ? float2(0, 0) : float2(0.5, 0.5);\n"
    "  v.p = float4(centre + d + offset.xy, 0, 1);\n"
    "  v.c = float4((id == 0 || id == 3) ? 1 : 0, (id == 1 || id == 3) ? 1 : 0,\n"
    "               id == 2 ? 1 : 0, 1);\n"
    "  return v;\n"
    "}\n";
static const char gs_adj_source[] =
    "struct V { float4 p : SV_Position; float4 c : COLOR0; };\n"
    "[maxvertexcount(4)]\n"
    "void main(triangleadj V input[6], inout TriangleStream<V> s) {\n"
    "  const float h = 0.25;\n"
    "  float4 centre = (input[0].p + input[2].p + input[4].p) / 3;\n"
    "  V o; o.c = input[0].c;\n"
    "  o.p = centre + float4(-h,  h, 0, 0); s.Append(o);\n"
    "  o.p = centre + float4( h,  h, 0, 0); s.Append(o);\n"
    "  o.p = centre + float4(-h, -h, 0, 0); s.Append(o);\n"
    "  o.p = centre + float4( h, -h, 0, 0); s.Append(o);\n"
    "}\n";
static const char ps_source[] =
    "float4 main(float4 p : SV_Position, float4 c : COLOR0) : SV_Target { return c; }\n";

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

/* R8G8B8A8 of point id (the vertex shader's colours). */
static unsigned colour_of(int id) {
  unsigned r = (id == 0 || id == 3) ? 0xffu : 0u, g = (id == 1 || id == 3) ? 0xffu : 0u;
  unsigned b = id == 2 ? 0xffu : 0u;
  return r | (g << 8) | (b << 16) | 0xff000000u;
}

/* The centre of quad id in pixels for a layout shifted by (shift, shift). */
static void centre_of(int id, int shift, int *x, int *y) {
  *x = ((id & 1) ? 48 : 16) + shift;
  *y = ((id & 2) ? 48 : 16) + shift;
}

/* Checks one read-back frame; returns the number of wrong samples. */
static int check_frame(const unsigned char *data, UINT pitch, int shift, int iteration) {
  static const int inside[4][2] = {{-(HALF - 1), 0}, {HALF - 1, 0}, {0, -(HALF - 1)}, {0, HALF - 1}};
  static const int outside[4][2] = {{-(HALF + 1), 0}, {HALF + 1, 0}, {0, -(HALF + 1)}, {0, HALF + 1}};
  int wrong = 0;
  for (int id = 0; id < 4; ++id) {
    int cx, cy;
    centre_of(id, shift, &cx, &cy);
    for (int k = 0; k < 9; ++k) {
      int x = cx, y = cy;
      unsigned want = colour_of(id);
      if (k < 4) { x += inside[k][0]; y += inside[k][1]; }
      else if (k < 8) { x += outside[k - 4][0]; y += outside[k - 4][1]; want = 0xff000000u; }
      unsigned got = *(const unsigned *)(data + (size_t)y * pitch + (size_t)x * 4u);
      if (got != want) {
        if (wrong < 4)
          say("WRONG iteration %d shift %d quad %d pixel %d,%d got 0x%08x want 0x%08x",
              iteration, shift, id, x, y, got, want);
        ++wrong;
      }
    }
  }
  return wrong;
}

/* Checks the captured triangles: quad q (point q) is triangles 2q and 2q+1,
 * whose six positions are its corners, all four of them. */
static int check_stream(const float *v, int shift, int iteration) {
  int wrong = 0;
  for (int q = 0; q < 4; ++q) {
    int cx, cy, seen = 0;
    centre_of(q, shift, &cx, &cy);
    for (int k = 0; k < 6; ++k) {
      const float *p = v + (size_t)(q * 6 + k) * 4u;
      /* NDC back to pixels: x = (p.x + 1) * 32, y = (1 - p.y) * 32. */
      int px = (int)floorf((p[0] + 1.0f) * (SIZE / 2) + 0.5f);
      int py = (int)floorf((1.0f - p[1]) * (SIZE / 2) + 0.5f);
      int corner = -1;
      if (px == cx - HALF && py == cy - HALF) corner = 0;
      else if (px == cx + HALF && py == cy - HALF) corner = 1;
      else if (px == cx - HALF && py == cy + HALF) corner = 2;
      else if (px == cx + HALF && py == cy + HALF) corner = 3;
      if (corner < 0 || p[2] != 0.0f || p[3] != 1.0f) {
        if (wrong < 4)
          say("SO_WRONG iteration %d quad %d vertex %d (%.4f %.4f %.4f %.4f)", iteration,
              q, k, p[0], p[1], p[2], p[3]);
        ++wrong;
      } else {
        seen |= 1 << corner;
      }
    }
    if (seen != 15) {
      if (wrong < 4) say("SO_CORNERS iteration %d quad %d mask %x", iteration, q, seen);
      ++wrong;
    }
  }
  return wrong;
}

int main(int argc, char **argv) {
  IDXGIFactory1 *factory = NULL;
  IDXGIAdapter1 *adapter = NULL, *pick = NULL;
  ID3D11Device *device = NULL;
  ID3D11DeviceContext *ctx = NULL;
  ID3D11Texture2D *target = NULL, *staging = NULL;
  ID3D11RenderTargetView *rtv = NULL;
  ID3D11Buffer *cb = NULL, *so = NULL, *so_staging = NULL;
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
  int stream, adjacency, iterations, bad_frames = 0, bad_streams = 0;

  if (argc < 2) {
    printf("usage: agx_gs_smoke.exe <out.txt> [gs|so|adj] [iterations]\n");
    return 2;
  }
  stream = argc > 2 && !strcmp(argv[2], "so");
  adjacency = argc > 2 && !strcmp(argv[2], "adj");
  iterations = argc > 3 ? atoi(argv[3]) : 60;
  if (iterations < 2) iterations = 2;
  out = CreateFileA(argv[1], GENERIC_WRITE, FILE_SHARE_READ, NULL, CREATE_ALWAYS,
                    FILE_ATTRIBUTE_NORMAL | FILE_FLAG_WRITE_THROUGH, NULL);
  say("MODE %s iterations %d", stream ? "so" : adjacency ? "adj" : "gs", iterations);
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
    td.Usage = D3D11_USAGE_DEFAULT; td.BindFlags = D3D11_BIND_RENDER_TARGET;
    hr = ID3D11Device_CreateTexture2D(device, &td, NULL, &target);
    if (SUCCEEDED(hr)) hr = ID3D11Device_CreateRenderTargetView(device, (ID3D11Resource *)target, NULL, &rtv);
    td.Usage = D3D11_USAGE_STAGING; td.BindFlags = 0; td.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    if (SUCCEEDED(hr)) hr = ID3D11Device_CreateTexture2D(device, &td, NULL, &staging);
    memset(&bd, 0, sizeof(bd));
    bd.ByteWidth = 16; bd.Usage = D3D11_USAGE_DYNAMIC;
    bd.BindFlags = D3D11_BIND_CONSTANT_BUFFER; bd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    if (SUCCEEDED(hr)) hr = ID3D11Device_CreateBuffer(device, &bd, NULL, &cb);
    if (stream) {
      memset(&bd, 0, sizeof(bd));
      bd.ByteWidth = 1024; bd.Usage = D3D11_USAGE_DEFAULT;
      bd.BindFlags = D3D11_BIND_VERTEX_BUFFER | D3D11_BIND_STREAM_OUTPUT;
      if (SUCCEEDED(hr)) hr = ID3D11Device_CreateBuffer(device, &bd, NULL, &so);
      bd.Usage = D3D11_USAGE_STAGING; bd.BindFlags = 0; bd.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
      if (SUCCEEDED(hr)) hr = ID3D11Device_CreateBuffer(device, &bd, NULL, &so_staging);
    }
    say("RESOURCES 0x%08lx", (unsigned long)hr);
    if (FAILED(hr)) return 1;
  }
  vsb = adjacency ? compile(compile_fn, vs_adj_source, sizeof(vs_adj_source) - 1, "vs_4_0")
                  : compile(compile_fn, vs_source, sizeof(vs_source) - 1, "vs_4_0");
  gsb = adjacency ? compile(compile_fn, gs_adj_source, sizeof(gs_adj_source) - 1, "gs_4_0")
                  : compile(compile_fn, gs_source, sizeof(gs_source) - 1, "gs_4_0");
  psb = compile(compile_fn, ps_source, sizeof(ps_source) - 1, "ps_4_0");
  if (!vsb || !gsb || !psb) return 1;
  hr = ID3D11Device_CreateVertexShader(device, ID3D10Blob_GetBufferPointer(vsb),
                                       ID3D10Blob_GetBufferSize(vsb), NULL, &vs);
  if (SUCCEEDED(hr) && stream) {
    D3D11_SO_DECLARATION_ENTRY decl = {0, "SV_Position", 0, 0, 4, 0};
    UINT stride = 16;
    hr = ID3D11Device_CreateGeometryShaderWithStreamOutput(device,
        ID3D10Blob_GetBufferPointer(gsb), ID3D10Blob_GetBufferSize(gsb), &decl, 1,
        &stride, 1, 0, NULL, &gs);
  } else if (SUCCEEDED(hr)) {
    hr = ID3D11Device_CreateGeometryShader(device, ID3D10Blob_GetBufferPointer(gsb),
                                           ID3D10Blob_GetBufferSize(gsb), NULL, &gs);
  }
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
  ID3D11DeviceContext_OMSetRenderTargets(ctx, 1, &rtv, NULL);
  ID3D11DeviceContext_IASetPrimitiveTopology(ctx, adjacency ?
      D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST_ADJ : D3D11_PRIMITIVE_TOPOLOGY_POINTLIST);
  ID3D11DeviceContext_VSSetShader(ctx, vs, NULL, 0);
  ID3D11DeviceContext_VSSetConstantBuffers(ctx, 0, 1, &cb);
  ID3D11DeviceContext_GSSetShader(ctx, gs, NULL, 0);
  ID3D11DeviceContext_PSSetShader(ctx, ps, NULL, 0);
  for (int i = 0; i < iterations; ++i) {
    const float black[4] = {0.0f, 0.0f, 0.0f, 1.0f};
    /* Layout B is 4 pixels right and down: +0.125 x, -0.125 y in NDC. */
    const int shift = (i & 1) ? 4 : 0;
    const float offset[4] = {shift ? 0.125f : 0.0f, shift ? -0.125f : 0.0f, 0.0f, 0.0f};
    D3D11_MAPPED_SUBRESOURCE map;
    HRESULT removed;
    hr = ID3D11DeviceContext_Map(ctx, (ID3D11Resource *)cb, 0, D3D11_MAP_WRITE_DISCARD, 0, &map);
    if (FAILED(hr)) { say("CB_MAP 0x%08lx at %d", (unsigned long)hr, i); return 1; }
    memcpy(map.pData, offset, sizeof(offset));
    ID3D11DeviceContext_Unmap(ctx, (ID3D11Resource *)cb, 0);
    ID3D11DeviceContext_ClearRenderTargetView(ctx, rtv, black);
    if (stream) {
      UINT zero = 0;
      ID3D11DeviceContext_SOSetTargets(ctx, 1, &so, &zero);
    }
    ID3D11DeviceContext_Draw(ctx, adjacency ? 24 : 4, 0);
    if (stream) {
      ID3D11DeviceContext_SOSetTargets(ctx, 0, NULL, NULL);
      ID3D11DeviceContext_CopyResource(ctx, (ID3D11Resource *)so_staging, (ID3D11Resource *)so);
    }
    ID3D11DeviceContext_CopyResource(ctx, (ID3D11Resource *)staging, (ID3D11Resource *)target);
    hr = ID3D11DeviceContext_Map(ctx, (ID3D11Resource *)staging, 0, D3D11_MAP_READ, 0, &map);
    removed = ID3D11Device_GetDeviceRemovedReason(device);
    if (FAILED(hr)) {
      say("READBACK_MAP 0x%08lx at %d removed 0x%08lx", (unsigned long)hr, i,
          (unsigned long)removed);
      return 1;
    }
    if (check_frame((const unsigned char *)map.pData, map.RowPitch, shift, i)) ++bad_frames;
    ID3D11DeviceContext_Unmap(ctx, (ID3D11Resource *)staging, 0);
    if (stream) {
      hr = ID3D11DeviceContext_Map(ctx, (ID3D11Resource *)so_staging, 0, D3D11_MAP_READ, 0, &map);
      if (FAILED(hr)) { say("SO_MAP 0x%08lx at %d", (unsigned long)hr, i); return 1; }
      if (check_stream((const float *)map.pData, shift, i)) ++bad_streams;
      ID3D11DeviceContext_Unmap(ctx, (ID3D11Resource *)so_staging, 0);
    }
    if (i < 2 || i == iterations - 1)
      say("ITERATION %d shift %d bad_frames %d bad_streams %d removed 0x%08lx", i, shift,
          bad_frames, bad_streams, (unsigned long)removed);
  }
  say("RESULT frames %d bad_frames %d bad_streams %d removed 0x%08lx", iterations,
      bad_frames, bad_streams, (unsigned long)ID3D11Device_GetDeviceRemovedReason(device));
  say(bad_frames || bad_streams ? "AGX_GS_SMOKE: FAIL" : "AGX_GS_SMOKE: PASS");
  return bad_frames || bad_streams ? 1 : 0;
}
