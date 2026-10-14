/* D3D-G3 hardware checkpoint: occlusion queries and predicates.
 * Each iteration issues, inside one frame, four occlusion queries around four
 * draws of different exact pixel counts (a full-viewport triangle clipped to a
 * w x h viewport covers w*h single-sample pixels), then two predicates: one
 * around an off-screen draw (FALSE) and one around a visible draw (TRUE).
 * Results are polled with GetData until S_OK; every count must be exact (a
 * missing heap, a heap the CPU cannot see, a slot not zeroed at Begin, or
 * counts landing in the wrong slot all fail). The render target is read back
 * at the end so a query path that broke rendering also fails.
 * Every line is written through to the output file before the next step.
 * Usage: agx_query_smoke.exe <out.txt> [iterations] */
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
#define QUERIES 4
#define POLL_MS 5000u

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

/* A full-viewport triangle, moved right by `shift` clip units (4: off-screen). */
static const char vs_source[] =
    "cbuffer C : register(b0) { float shift; float3 pad; };\n"
    "float4 main(uint id : SV_VertexID) : SV_Position {\n"
    "  float2 uv = float2((id << 1) & 2, id & 2);\n"
    "  return float4(uv * float2(2, -2) + float2(-1 + shift, 1), 0, 1);\n"
    "}\n";
static const char ps_source[] =
    "float4 main(float4 p : SV_Position) : SV_Target { return float4(0, 1, 0, 1); }\n";

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

static ID3D11DeviceContext *ctx;
static ID3D11Buffer *cb;

static int set_shift(float shift) {
  D3D11_MAPPED_SUBRESOURCE map;
  HRESULT hr = ID3D11DeviceContext_Map(ctx, (ID3D11Resource *)cb, 0,
                                       D3D11_MAP_WRITE_DISCARD, 0, &map);
  if (FAILED(hr)) { say("CB_MAP 0x%08lx", (unsigned long)hr); return 0; }
  memset(map.pData, 0, 16);
  *(float *)map.pData = shift;
  ID3D11DeviceContext_Unmap(ctx, (ID3D11Resource *)cb, 0);
  return 1;
}

static void draw_rect(int x, int y, int w, int h) {
  D3D11_VIEWPORT vp;
  memset(&vp, 0, sizeof(vp));
  vp.TopLeftX = (float)x; vp.TopLeftY = (float)y;
  vp.Width = (float)w; vp.Height = (float)h; vp.MaxDepth = 1.0f;
  ID3D11DeviceContext_RSSetViewports(ctx, 1, &vp);
  ID3D11DeviceContext_Draw(ctx, 3, 0);
}

/* Polls until S_OK; returns the number of S_FALSE polls, or -1 on timeout or
 * error. Odd polls pass DONOTFLUSH (the driver must still complete). */
static int poll(ID3D11Asynchronous *query, void *data, UINT bytes) {
  DWORD start = GetTickCount();
  int polls = 0;
  for (;;) {
    UINT flags = (polls & 1) ? D3D11_ASYNC_GETDATA_DONOTFLUSH : 0u;
    HRESULT hr = ID3D11DeviceContext_GetData(ctx, query, data, bytes, flags);
    if (hr == S_OK) return polls;
    if (hr != S_FALSE) { say("GETDATA 0x%08lx", (unsigned long)hr); return -1; }
    if (GetTickCount() - start > POLL_MS) return -1;
    ++polls;
    Sleep(polls < 8 ? 0 : 1);
  }
}

int main(int argc, char **argv) {
  IDXGIFactory1 *factory = NULL;
  IDXGIAdapter1 *adapter = NULL, *pick = NULL;
  ID3D11Device *device = NULL;
  ID3D11Texture2D *target = NULL, *staging = NULL;
  ID3D11RenderTargetView *rtv = NULL;
  ID3D11VertexShader *vs = NULL;
  ID3D11PixelShader *ps = NULL;
  ID3D11Query *query[QUERIES] = {0};
  ID3D11Predicate *hidden = NULL, *visible = NULL;
  ID3DBlob *vsb, *psb;
  D3D_FEATURE_LEVEL level = 0;
  const D3D_FEATURE_LEVEL want[] = {D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_1,
                                    D3D_FEATURE_LEVEL_10_0};
  HMODULE compiler;
  pD3DCompile compile_fn;
  HRESULT hr;
  int iterations, bad = 0, still = 0;

  if (argc < 2) {
    printf("usage: agx_query_smoke.exe <out.txt> [iterations]\n");
    return 2;
  }
  iterations = argc > 2 ? atoi(argv[2]) : 30;
  if (iterations < 2) iterations = 2;
  out = CreateFileA(argv[1], GENERIC_WRITE, FILE_SHARE_READ, NULL, CREATE_ALWAYS,
                    FILE_ATTRIBUTE_NORMAL | FILE_FLAG_WRITE_THROUGH, NULL);
  say("QUERIES %d iterations %d", QUERIES, iterations);
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
    memset(&td, 0, sizeof(td));
    td.Width = td.Height = SIZE; td.MipLevels = td.ArraySize = 1;
    td.Format = DXGI_FORMAT_R8G8B8A8_UNORM; td.SampleDesc.Count = 1;
    td.Usage = D3D11_USAGE_DEFAULT; td.BindFlags = D3D11_BIND_RENDER_TARGET;
    hr = ID3D11Device_CreateTexture2D(device, &td, NULL, &target);
    if (SUCCEEDED(hr))
      hr = ID3D11Device_CreateRenderTargetView(device, (ID3D11Resource *)target, NULL, &rtv);
    td.Usage = D3D11_USAGE_STAGING; td.BindFlags = 0; td.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    if (SUCCEEDED(hr)) hr = ID3D11Device_CreateTexture2D(device, &td, NULL, &staging);
    memset(&bd, 0, sizeof(bd));
    bd.ByteWidth = 16; bd.Usage = D3D11_USAGE_DYNAMIC;
    bd.BindFlags = D3D11_BIND_CONSTANT_BUFFER; bd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    if (SUCCEEDED(hr)) hr = ID3D11Device_CreateBuffer(device, &bd, NULL, &cb);
    say("RESOURCES 0x%08lx", (unsigned long)hr);
    if (FAILED(hr)) return 1;
    memset(&qd, 0, sizeof(qd));
    qd.Query = D3D11_QUERY_OCCLUSION;
    for (int q = 0; q < QUERIES && SUCCEEDED(hr); ++q)
      hr = ID3D11Device_CreateQuery(device, &qd, &query[q]);
    say("CreateQuery OCCLUSION 0x%08lx", (unsigned long)hr);
    if (FAILED(hr)) return 1;
    qd.Query = D3D11_QUERY_OCCLUSION_PREDICATE;
    hr = ID3D11Device_CreatePredicate(device, &qd, &hidden);
    if (SUCCEEDED(hr)) hr = ID3D11Device_CreatePredicate(device, &qd, &visible);
    say("CreatePredicate OCCLUSION_PREDICATE 0x%08lx", (unsigned long)hr);
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
  ID3D11DeviceContext_OMSetRenderTargets(ctx, 1, &rtv, NULL);
  ID3D11DeviceContext_IASetPrimitiveTopology(ctx, D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
  ID3D11DeviceContext_VSSetShader(ctx, vs, NULL, 0);
  ID3D11DeviceContext_PSSetShader(ctx, ps, NULL, 0);
  ID3D11DeviceContext_VSSetConstantBuffers(ctx, 0, 1, &cb);
  for (int i = 0; i < iterations; ++i) {
    const float black[4] = {0.0f, 0.0f, 0.0f, 1.0f};
    unsigned want_count[QUERIES];
    BOOL seen_hidden = TRUE, seen_visible = FALSE;
    int polls;
    if (!set_shift(0.0f)) return 1;
    ID3D11DeviceContext_ClearRenderTargetView(ctx, rtv, black);
    /* Distinct exact counts per query and iteration. */
    for (int q = 0; q < QUERIES; ++q) {
      int w = 4 + ((i + q * 5) % 13) * 4, h = 8 + q * 8;
      want_count[q] = (unsigned)(w * h);
      ID3D11DeviceContext_Begin(ctx, (ID3D11Asynchronous *)query[q]);
      draw_rect(q * 2, q * 3, w, h);
      ID3D11DeviceContext_End(ctx, (ID3D11Asynchronous *)query[q]);
    }
    if (!set_shift(4.0f)) return 1;
    ID3D11DeviceContext_Begin(ctx, (ID3D11Asynchronous *)hidden);
    draw_rect(0, 0, SIZE, SIZE);
    ID3D11DeviceContext_End(ctx, (ID3D11Asynchronous *)hidden);
    if (!set_shift(0.0f)) return 1;
    ID3D11DeviceContext_Begin(ctx, (ID3D11Asynchronous *)visible);
    draw_rect(SIZE - 8, SIZE - 8, 8, 8);
    ID3D11DeviceContext_End(ctx, (ID3D11Asynchronous *)visible);
    for (int q = 0; q < QUERIES; ++q) {
      UINT64 count = ~0ULL;
      polls = poll((ID3D11Asynchronous *)query[q], &count, sizeof(count));
      if (polls < 0) {
        say("TIMEOUT iteration %d query %d removed 0x%08lx", i, q,
            (unsigned long)ID3D11Device_GetDeviceRemovedReason(device));
        return 1;
      }
      still += polls;
      if (count != want_count[q]) {
        if (bad < 8)
          say("WRONG iteration %d query %d got %llu want %u polls %d", i, q,
              (unsigned long long)count, want_count[q], polls);
        ++bad;
      } else if (i < 2) {
        say("QUERY iteration %d query %d count %llu polls %d", i, q,
            (unsigned long long)count, polls);
      }
    }
    if (poll((ID3D11Asynchronous *)hidden, &seen_hidden, sizeof(seen_hidden)) < 0 ||
        poll((ID3D11Asynchronous *)visible, &seen_visible, sizeof(seen_visible)) < 0) {
      say("TIMEOUT iteration %d predicate removed 0x%08lx", i,
          (unsigned long)ID3D11Device_GetDeviceRemovedReason(device));
      return 1;
    }
    if (seen_hidden != FALSE || seen_visible != TRUE) {
      if (bad < 8)
        say("WRONG iteration %d predicate hidden %d visible %d", i, seen_hidden, seen_visible);
      ++bad;
    }
    if (i < 2 || i == iterations - 1)
      say("ITERATION %d bad %d stillDrawing %d removed 0x%08lx", i, bad, still,
          (unsigned long)ID3D11Device_GetDeviceRemovedReason(device));
  }
  {
    /* The last frame: the visible predicate's 8x8 corner and the query
     * rectangles are green, the untouched centre-right column black. */
    D3D11_MAPPED_SUBRESOURCE map;
    unsigned corner, empty;
    ID3D11DeviceContext_CopyResource(ctx, (ID3D11Resource *)staging, (ID3D11Resource *)target);
    hr = ID3D11DeviceContext_Map(ctx, (ID3D11Resource *)staging, 0, D3D11_MAP_READ, 0, &map);
    if (FAILED(hr)) {
      say("READBACK_MAP 0x%08lx removed 0x%08lx", (unsigned long)hr,
          (unsigned long)ID3D11Device_GetDeviceRemovedReason(device));
      return 1;
    }
    corner = *(const unsigned *)((const unsigned char *)map.pData +
        (size_t)(SIZE - 4) * map.RowPitch + (size_t)(SIZE - 4) * 4u);
    empty = *(const unsigned *)((const unsigned char *)map.pData +
        (size_t)(SIZE - 12) * map.RowPitch + (size_t)(SIZE - 2) * 4u);
    ID3D11DeviceContext_Unmap(ctx, (ID3D11Resource *)staging, 0);
    say("READBACK corner 0x%08x empty 0x%08x", corner, empty);
    if (corner != 0xff00ff00u || empty != 0xff000000u) ++bad;
  }
  say("RESULT iterations %d bad %d stillDrawing %d removed 0x%08lx", iterations, bad, still,
      (unsigned long)ID3D11Device_GetDeviceRemovedReason(device));
  say(bad ? "AGX_QUERY_SMOKE: FAIL" : "AGX_QUERY_SMOKE: PASS");
  return bad ? 1 : 0;
}
