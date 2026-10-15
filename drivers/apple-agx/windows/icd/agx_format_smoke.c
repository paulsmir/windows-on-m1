/* D3D-G10a hardware checkpoint: render targets and depth targets per the
 * D3D10 contract.
 * - formats: every FL10_0 render-target format the device reports as a
 *   render target (CheckFormatSupport) is created 32x32 with two mips,
 *   cleared at mip 1 and read back through a staging copy; float, unorm,
 *   snorm and sRGB formats are cleared to (0.25, 0.5, 0.75, 1), integer
 *   formats to (1, 2, 3, 4), and every component is decoded and compared;
 * - views: an R8G8B8A8 mip level, array slice, slice range, cube face and
 *   3D slice are cleared through their render-target views; the
 *   neighbouring subresources must keep their earlier clear;
 * - shadow map: an R32_TYPELESS depth target with a shader-resource
 *   binding is cleared (not bound) to 0.25, then sampled through an
 *   R32_FLOAT view into an R32_FLOAT target.
 * Formats the device does not report as render targets are skipped.
 * Every line is written through to the output file before the next step.
 * Usage: agx_format_smoke.exe <out.txt> [iterations] */
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

enum kind { UNORM, SNORM, UINT_, SINT_, FLOAT_, SRGB, R11G11B10 };

/* Components from bit 0 upward, in memory order; swap_rb: B before R. */
struct format {
  DXGI_FORMAT dxgi;
  const char *name;
  enum kind kind;
  unsigned comps, bits[4], swap_rb;
};

static const struct format formats[] = {
  {DXGI_FORMAT_R32G32B32A32_FLOAT, "R32G32B32A32_FLOAT", FLOAT_, 4, {32, 32, 32, 32}, 0},
  {DXGI_FORMAT_R32G32B32A32_UINT, "R32G32B32A32_UINT", UINT_, 4, {32, 32, 32, 32}, 0},
  {DXGI_FORMAT_R32G32B32A32_SINT, "R32G32B32A32_SINT", SINT_, 4, {32, 32, 32, 32}, 0},
  {DXGI_FORMAT_R16G16B16A16_FLOAT, "R16G16B16A16_FLOAT", FLOAT_, 4, {16, 16, 16, 16}, 0},
  {DXGI_FORMAT_R16G16B16A16_UNORM, "R16G16B16A16_UNORM", UNORM, 4, {16, 16, 16, 16}, 0},
  {DXGI_FORMAT_R16G16B16A16_UINT, "R16G16B16A16_UINT", UINT_, 4, {16, 16, 16, 16}, 0},
  {DXGI_FORMAT_R16G16B16A16_SNORM, "R16G16B16A16_SNORM", SNORM, 4, {16, 16, 16, 16}, 0},
  {DXGI_FORMAT_R16G16B16A16_SINT, "R16G16B16A16_SINT", SINT_, 4, {16, 16, 16, 16}, 0},
  {DXGI_FORMAT_R32G32_FLOAT, "R32G32_FLOAT", FLOAT_, 2, {32, 32}, 0},
  {DXGI_FORMAT_R32G32_UINT, "R32G32_UINT", UINT_, 2, {32, 32}, 0},
  {DXGI_FORMAT_R32G32_SINT, "R32G32_SINT", SINT_, 2, {32, 32}, 0},
  {DXGI_FORMAT_R10G10B10A2_UNORM, "R10G10B10A2_UNORM", UNORM, 4, {10, 10, 10, 2}, 0},
  {DXGI_FORMAT_R10G10B10A2_UINT, "R10G10B10A2_UINT", UINT_, 4, {10, 10, 10, 2}, 0},
  {DXGI_FORMAT_R11G11B10_FLOAT, "R11G11B10_FLOAT", R11G11B10, 3, {11, 11, 10}, 0},
  {DXGI_FORMAT_R8G8B8A8_UNORM, "R8G8B8A8_UNORM", UNORM, 4, {8, 8, 8, 8}, 0},
  {DXGI_FORMAT_R8G8B8A8_UNORM_SRGB, "R8G8B8A8_UNORM_SRGB", SRGB, 4, {8, 8, 8, 8}, 0},
  {DXGI_FORMAT_R8G8B8A8_UINT, "R8G8B8A8_UINT", UINT_, 4, {8, 8, 8, 8}, 0},
  {DXGI_FORMAT_R8G8B8A8_SNORM, "R8G8B8A8_SNORM", SNORM, 4, {8, 8, 8, 8}, 0},
  {DXGI_FORMAT_R8G8B8A8_SINT, "R8G8B8A8_SINT", SINT_, 4, {8, 8, 8, 8}, 0},
  {DXGI_FORMAT_B8G8R8A8_UNORM, "B8G8R8A8_UNORM", UNORM, 4, {8, 8, 8, 8}, 1},
  {DXGI_FORMAT_B8G8R8A8_UNORM_SRGB, "B8G8R8A8_UNORM_SRGB", SRGB, 4, {8, 8, 8, 8}, 1},
  {DXGI_FORMAT_R16G16_FLOAT, "R16G16_FLOAT", FLOAT_, 2, {16, 16}, 0},
  {DXGI_FORMAT_R16G16_UNORM, "R16G16_UNORM", UNORM, 2, {16, 16}, 0},
  {DXGI_FORMAT_R16G16_UINT, "R16G16_UINT", UINT_, 2, {16, 16}, 0},
  {DXGI_FORMAT_R16G16_SNORM, "R16G16_SNORM", SNORM, 2, {16, 16}, 0},
  {DXGI_FORMAT_R16G16_SINT, "R16G16_SINT", SINT_, 2, {16, 16}, 0},
  {DXGI_FORMAT_R32_FLOAT, "R32_FLOAT", FLOAT_, 1, {32}, 0},
  {DXGI_FORMAT_R32_UINT, "R32_UINT", UINT_, 1, {32}, 0},
  {DXGI_FORMAT_R32_SINT, "R32_SINT", SINT_, 1, {32}, 0},
  {DXGI_FORMAT_R8G8_UNORM, "R8G8_UNORM", UNORM, 2, {8, 8}, 0},
  {DXGI_FORMAT_R8G8_UINT, "R8G8_UINT", UINT_, 2, {8, 8}, 0},
  {DXGI_FORMAT_R8G8_SNORM, "R8G8_SNORM", SNORM, 2, {8, 8}, 0},
  {DXGI_FORMAT_R8G8_SINT, "R8G8_SINT", SINT_, 2, {8, 8}, 0},
  {DXGI_FORMAT_R16_FLOAT, "R16_FLOAT", FLOAT_, 1, {16}, 0},
  {DXGI_FORMAT_R16_UNORM, "R16_UNORM", UNORM, 1, {16}, 0},
  {DXGI_FORMAT_R16_UINT, "R16_UINT", UINT_, 1, {16}, 0},
  {DXGI_FORMAT_R16_SNORM, "R16_SNORM", SNORM, 1, {16}, 0},
  {DXGI_FORMAT_R16_SINT, "R16_SINT", SINT_, 1, {16}, 0},
  {DXGI_FORMAT_R8_UNORM, "R8_UNORM", UNORM, 1, {8}, 0},
  {DXGI_FORMAT_R8_UINT, "R8_UINT", UINT_, 1, {8}, 0},
  {DXGI_FORMAT_R8_SNORM, "R8_SNORM", SNORM, 1, {8}, 0},
  {DXGI_FORMAT_R8_SINT, "R8_SINT", SINT_, 1, {8}, 0},
};

static const float clear_float[4] = {0.25f, 0.5f, 0.75f, 1.0f};
static const float clear_int[4] = {1.0f, 2.0f, 3.0f, 4.0f};

static unsigned long long field(const unsigned char *p, unsigned bit, unsigned bits) {
  unsigned long long v = 0;
  for (unsigned i = 0; i < bits; ++i)
    if (p[(bit + i) / 8] & (1u << ((bit + i) % 8))) v |= 1ull << i;
  return v;
}

static float half(unsigned h) {
  unsigned e = (h >> 10) & 31u, m = h & 1023u;
  float v = e ? ldexpf(1.0f + m / 1024.0f, (int)e - 15) : ldexpf(m / 1024.0f, -14);
  return (h & 0x8000u) ? -v : v;
}

static float small_float(unsigned v, unsigned mantissa_bits) {
  unsigned e = v >> mantissa_bits, m = v & ((1u << mantissa_bits) - 1u);
  float scale = (float)(1u << mantissa_bits);
  return e ? ldexpf(1.0f + m / scale, (int)e - 15) : ldexpf(m / scale, -14);
}

static float srgb(float c) {
  return c <= 0.0031308f ? 12.92f * c : 1.055f * powf(c, 1.0f / 2.4f) - 0.055f;
}

/* Decodes one texel; returns the number of components that differ. */
static int check_texel(const struct format *f, const unsigned char *p, char *got, size_t len) {
  int wrong = 0;
  unsigned bit = 0, used = 0;
  got[0] = 0;
  for (unsigned c = 0; c < f->comps; ++c) {
    unsigned b = f->bits[c];
    unsigned long long raw = field(p, bit, b);
    unsigned logical = f->swap_rb && c < 3 ? 2 - c : c;
    double value, want, tolerance;
    bit += b;
    switch (f->kind) {
    case UNORM: value = raw / (double)((1ull << b) - 1); want = clear_float[logical];
      tolerance = 1.5 / ((1ull << b) - 1); break;
    case SRGB: value = raw / 255.0;
      want = logical == 3 ? clear_float[3] : srgb(clear_float[logical]); tolerance = 2.5 / 255.0; break;
    case SNORM: {
      long long s = (raw & (1ull << (b - 1))) ? (long long)raw - (1ll << b) : (long long)raw;
      value = s / (double)((1ull << (b - 1)) - 1); if (value < -1.0) value = -1.0;
      want = clear_float[logical]; tolerance = 1.5 / ((1ull << (b - 1)) - 1); break;
    }
    case UINT_: value = (double)raw; want = clear_int[logical]; tolerance = 0.0; break;
    case SINT_: {
      long long s = (raw & (1ull << (b - 1))) ? (long long)raw - (1ll << b) : (long long)raw;
      value = (double)s; want = clear_int[logical]; tolerance = 0.0; break;
    }
    case FLOAT_: {
      if (b == 32) { unsigned u = (unsigned)raw; float fv; memcpy(&fv, &u, 4); value = fv; }
      else value = half((unsigned)raw);
      want = clear_float[logical]; tolerance = 1e-3; break;
    }
    default: /* R11G11B10 */
      value = small_float((unsigned)raw, b == 11 ? 6 : 5);
      want = clear_float[logical]; tolerance = 2e-2; break;
    }
    if (fabs(value - want) > tolerance) ++wrong;
    used += (unsigned)_snprintf_s(got + used, len - used, _TRUNCATE, "%s%g", c ? "," : "", value);
  }
  return wrong;
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

static ID3D11Device *device;
static ID3D11DeviceContext *ctx;

/* Copies one subresource to a matching staging texture and returns its
 * first texel (Map left open in *map; caller unmaps). */
static HRESULT read_subresource(ID3D11Resource *src, UINT sub, const D3D11_TEXTURE2D_DESC *desc,
                                UINT level, ID3D11Texture2D **staging, D3D11_MAPPED_SUBRESOURCE *map) {
  D3D11_TEXTURE2D_DESC sd = *desc;
  HRESULT hr;
  sd.Width = desc->Width >> level ? desc->Width >> level : 1;
  sd.Height = desc->Height >> level ? desc->Height >> level : 1;
  sd.MipLevels = 1; sd.ArraySize = 1; sd.SampleDesc.Count = 1; sd.SampleDesc.Quality = 0;
  sd.Usage = D3D11_USAGE_STAGING; sd.BindFlags = 0; sd.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
  sd.MiscFlags = 0;
  hr = ID3D11Device_CreateTexture2D(device, &sd, NULL, staging);
  if (FAILED(hr)) return hr;
  ID3D11DeviceContext_CopySubresourceRegion(ctx, (ID3D11Resource *)*staging, 0, 0, 0, 0, src, sub, NULL);
  return ID3D11DeviceContext_Map(ctx, (ID3D11Resource *)*staging, 0, D3D11_MAP_READ, 0, map);
}

static int test_format(const struct format *f, int iteration) {
  D3D11_TEXTURE2D_DESC td;
  ID3D11Texture2D *tex = NULL, *staging = NULL;
  ID3D11RenderTargetView *rtv = NULL;
  D3D11_RENDER_TARGET_VIEW_DESC rd;
  D3D11_MAPPED_SUBRESOURCE map;
  UINT support = 0;
  char got[160];
  int wrong;
  HRESULT hr = ID3D11Device_CheckFormatSupport(device, f->dxgi, &support);
  if (FAILED(hr) || !(support & D3D11_FORMAT_SUPPORT_RENDER_TARGET)) {
    if (iteration == 0) say("FORMAT %s SKIP support 0x%08x", f->name, support);
    return 0;
  }
  memset(&td, 0, sizeof(td));
  td.Width = td.Height = 32; td.MipLevels = 2; td.ArraySize = 1; td.Format = f->dxgi;
  td.SampleDesc.Count = 1; td.Usage = D3D11_USAGE_DEFAULT; td.BindFlags = D3D11_BIND_RENDER_TARGET;
  hr = ID3D11Device_CreateTexture2D(device, &td, NULL, &tex);
  if (FAILED(hr)) { say("FORMAT %s CREATE 0x%08lx", f->name, (unsigned long)hr); return 1; }
  memset(&rd, 0, sizeof(rd));
  rd.Format = f->dxgi; rd.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2D; rd.Texture2D.MipSlice = 1;
  hr = ID3D11Device_CreateRenderTargetView(device, (ID3D11Resource *)tex, &rd, &rtv);
  if (FAILED(hr)) { say("FORMAT %s RTV 0x%08lx", f->name, (unsigned long)hr); ID3D11Texture2D_Release(tex); return 1; }
  ID3D11DeviceContext_ClearRenderTargetView(ctx, rtv,
      f->kind == UINT_ || f->kind == SINT_ ? clear_int : clear_float);
  hr = read_subresource((ID3D11Resource *)tex, 1, &td, 1, &staging, &map);
  if (FAILED(hr)) {
    say("FORMAT %s READBACK 0x%08lx removed 0x%08lx", f->name, (unsigned long)hr,
        (unsigned long)ID3D11Device_GetDeviceRemovedReason(device));
    wrong = 1;
  } else {
    wrong = check_texel(f, (const unsigned char *)map.pData + 7 * map.RowPitch, got, sizeof(got));
    ID3D11DeviceContext_Unmap(ctx, (ID3D11Resource *)staging, 0);
    if (wrong || iteration == 0) say("FORMAT %s %s %s", f->name, wrong ? "WRONG" : "ok", got);
  }
  if (staging) ID3D11Texture2D_Release(staging);
  ID3D11RenderTargetView_Release(rtv);
  ID3D11Texture2D_Release(tex);
  return wrong != 0;
}

/* Clears every subresource black, then the view's range to the colour, and
 * checks one texel of each listed subresource. */
static int test_view(const char *name, ID3D11Resource *res, const D3D11_TEXTURE2D_DESC *desc,
                     const D3D11_RENDER_TARGET_VIEW_DESC *view, UINT first, UINT count,
                     UINT level, UINT subresources, UINT levels) {
  static const float black[4] = {0, 0, 0, 0};
  int bad = 0;
  for (UINT s = 0; s < subresources; ++s) {
    D3D11_RENDER_TARGET_VIEW_DESC all = *view;
    ID3D11RenderTargetView *rtv = NULL;
    UINT slice = s / levels, mip = s % levels;
    if (all.ViewDimension == D3D11_RTV_DIMENSION_TEXTURE2DARRAY) {
      all.Texture2DArray.MipSlice = mip; all.Texture2DArray.FirstArraySlice = slice;
      all.Texture2DArray.ArraySize = 1;
    } else if (all.ViewDimension == D3D11_RTV_DIMENSION_TEXTURE3D) {
      all.Texture3D.MipSlice = mip; all.Texture3D.FirstWSlice = slice; all.Texture3D.WSize = 1;
    } else {
      all.Texture2D.MipSlice = mip;
    }
    if (SUCCEEDED(ID3D11Device_CreateRenderTargetView(device, res, &all, &rtv))) {
      ID3D11DeviceContext_ClearRenderTargetView(ctx, rtv, black);
      ID3D11RenderTargetView_Release(rtv);
    } else bad = 1;
  }
  {
    ID3D11RenderTargetView *rtv = NULL;
    HRESULT hr = ID3D11Device_CreateRenderTargetView(device, res, view, &rtv);
    if (FAILED(hr)) { say("VIEW %s RTV 0x%08lx", name, (unsigned long)hr); return 1; }
    ID3D11DeviceContext_ClearRenderTargetView(ctx, rtv, clear_float);
    ID3D11RenderTargetView_Release(rtv);
  }
  for (UINT slice = 0; slice < subresources / levels; ++slice) {
    ID3D11Texture2D *staging = NULL;
    D3D11_MAPPED_SUBRESOURCE map;
    UINT sub = slice * levels + level;
    unsigned want_set = slice >= first && slice < first + count;
    HRESULT hr;
    if (desc) {
      hr = read_subresource(res, sub, desc, level, &staging, &map);
    } else {
      /* 3D: copy the whole level to a 3D staging texture. */
      ID3D11Texture3D *s3 = NULL;
      D3D11_TEXTURE3D_DESC d3;
      ID3D11Texture3D_GetDesc((ID3D11Texture3D *)res, &d3);
      d3.MipLevels = 1; d3.Usage = D3D11_USAGE_STAGING; d3.BindFlags = 0;
      d3.CPUAccessFlags = D3D11_CPU_ACCESS_READ; d3.MiscFlags = 0;
      hr = ID3D11Device_CreateTexture3D(device, &d3, NULL, &s3);
      if (SUCCEEDED(hr)) {
        ID3D11DeviceContext_CopySubresourceRegion(ctx, (ID3D11Resource *)s3, 0, 0, 0, 0, res, 0, NULL);
        hr = ID3D11DeviceContext_Map(ctx, (ID3D11Resource *)s3, 0, D3D11_MAP_READ, 0, &map);
        if (SUCCEEDED(hr)) {
          unsigned px = *(const unsigned *)((const unsigned char *)map.pData + slice * map.DepthPitch);
          unsigned want = want_set ? 0xffbf8040u : 0u;
          if ((px & 0xfefefefeu) != (want & 0xfefefefeu)) {
            say("VIEW %s WRONG slice %u got 0x%08x want 0x%08x", name, slice, px, want); bad = 1;
          }
          ID3D11DeviceContext_Unmap(ctx, (ID3D11Resource *)s3, 0);
        }
        ID3D11Texture3D_Release(s3);
      }
      if (FAILED(hr)) { say("VIEW %s READBACK 0x%08lx", name, (unsigned long)hr); bad = 1; }
      continue;
    }
    if (FAILED(hr)) { say("VIEW %s READBACK 0x%08lx", name, (unsigned long)hr); bad = 1; }
    else {
      unsigned px = *(const unsigned *)map.pData;
      unsigned want = want_set ? 0xffbf8040u : 0u;
      if ((px & 0xfefefefeu) != (want & 0xfefefefeu)) {
        say("VIEW %s WRONG slice %u got 0x%08x want 0x%08x", name, slice, px, want); bad = 1;
      }
      ID3D11DeviceContext_Unmap(ctx, (ID3D11Resource *)staging, 0);
    }
    if (staging) ID3D11Texture2D_Release(staging);
  }
  return bad;
}

static int test_views(void) {
  D3D11_TEXTURE2D_DESC td;
  D3D11_RENDER_TARGET_VIEW_DESC rd;
  ID3D11Texture2D *tex = NULL;
  int bad = 0;
  memset(&td, 0, sizeof(td));
  td.Width = td.Height = 32; td.Format = DXGI_FORMAT_R8G8B8A8_UNORM; td.SampleDesc.Count = 1;
  td.Usage = D3D11_USAGE_DEFAULT; td.BindFlags = D3D11_BIND_RENDER_TARGET;
  /* mip level 2 of three */
  td.MipLevels = 3; td.ArraySize = 1;
  if (SUCCEEDED(ID3D11Device_CreateTexture2D(device, &td, NULL, &tex))) {
    memset(&rd, 0, sizeof(rd)); rd.Format = td.Format; rd.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2D;
    rd.Texture2D.MipSlice = 2;
    bad |= test_view("mip2", (ID3D11Resource *)tex, &td, &rd, 0, 1, 2, 3, 3);
    ID3D11Texture2D_Release(tex); tex = NULL;
  } else { say("VIEW mip2 CREATE failed"); bad = 1; }
  /* array slice 2 of four, then slices 1..2 */
  td.MipLevels = 1; td.ArraySize = 4;
  if (SUCCEEDED(ID3D11Device_CreateTexture2D(device, &td, NULL, &tex))) {
    memset(&rd, 0, sizeof(rd)); rd.Format = td.Format; rd.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2DARRAY;
    rd.Texture2DArray.FirstArraySlice = 2; rd.Texture2DArray.ArraySize = 1;
    bad |= test_view("slice2", (ID3D11Resource *)tex, &td, &rd, 2, 1, 0, 4, 1);
    rd.Texture2DArray.FirstArraySlice = 1; rd.Texture2DArray.ArraySize = 2;
    bad |= test_view("slices1-2", (ID3D11Resource *)tex, &td, &rd, 1, 2, 0, 4, 1);
    ID3D11Texture2D_Release(tex); tex = NULL;
  } else { say("VIEW array CREATE failed"); bad = 1; }
  /* cube face 3 */
  td.ArraySize = 6; td.MiscFlags = D3D11_RESOURCE_MISC_TEXTURECUBE;
  if (SUCCEEDED(ID3D11Device_CreateTexture2D(device, &td, NULL, &tex))) {
    memset(&rd, 0, sizeof(rd)); rd.Format = td.Format; rd.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2DARRAY;
    rd.Texture2DArray.FirstArraySlice = 3; rd.Texture2DArray.ArraySize = 1;
    bad |= test_view("cube3", (ID3D11Resource *)tex, &td, &rd, 3, 1, 0, 6, 1);
    ID3D11Texture2D_Release(tex); tex = NULL;
  } else { say("VIEW cube CREATE failed"); bad = 1; }
  /* 3D slice 2 of four */
  {
    D3D11_TEXTURE3D_DESC d3;
    ID3D11Texture3D *t3 = NULL;
    memset(&d3, 0, sizeof(d3));
    d3.Width = d3.Height = 16; d3.Depth = 4; d3.MipLevels = 1; d3.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    d3.Usage = D3D11_USAGE_DEFAULT; d3.BindFlags = D3D11_BIND_RENDER_TARGET;
    if (SUCCEEDED(ID3D11Device_CreateTexture3D(device, &d3, NULL, &t3))) {
      memset(&rd, 0, sizeof(rd)); rd.Format = d3.Format; rd.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE3D;
      rd.Texture3D.FirstWSlice = 2; rd.Texture3D.WSize = 1;
      bad |= test_view("3d2", (ID3D11Resource *)t3, NULL, &rd, 2, 1, 0, 4, 1);
      ID3D11Texture3D_Release(t3);
    } else { say("VIEW 3d CREATE failed"); bad = 1; }
  }
  return bad;
}

static const char vs_source[] =
    "float4 main(uint id : SV_VertexID) : SV_Position {\n"
    "  float2 uv = float2((id << 1) & 2, id & 2);\n"
    "  return float4(uv * float2(2, -2) + float2(-1, 1), 0, 1);\n"
    "}\n";
static const char ps_source[] =
    "Texture2D<float> depth : register(t0);\n"
    "float main(float4 p : SV_Position) : SV_Target { return depth.Load(int3(p.xy, 0)); }\n";

static ID3D11VertexShader *vs;
static ID3D11PixelShader *ps;

/* A shadow map: R32_TYPELESS depth target with an SRV, cleared while
 * unbound, then sampled through an R32_FLOAT view. */
static int test_shadow(void) {
  D3D11_TEXTURE2D_DESC td;
  D3D11_DEPTH_STENCIL_VIEW_DESC dd;
  D3D11_SHADER_RESOURCE_VIEW_DESC sd;
  ID3D11Texture2D *depth = NULL, *target = NULL, *staging = NULL;
  ID3D11DepthStencilView *dsv = NULL;
  ID3D11ShaderResourceView *srv = NULL;
  ID3D11RenderTargetView *rtv = NULL;
  D3D11_MAPPED_SUBRESOURCE map;
  D3D11_VIEWPORT vp;
  HRESULT hr;
  int bad = 0;
  memset(&td, 0, sizeof(td));
  td.Width = td.Height = 32; td.MipLevels = 1; td.ArraySize = 1; td.Format = DXGI_FORMAT_R32_TYPELESS;
  td.SampleDesc.Count = 1; td.Usage = D3D11_USAGE_DEFAULT;
  td.BindFlags = D3D11_BIND_DEPTH_STENCIL | D3D11_BIND_SHADER_RESOURCE;
  hr = ID3D11Device_CreateTexture2D(device, &td, NULL, &depth);
  if (FAILED(hr)) { say("SHADOW CREATE 0x%08lx", (unsigned long)hr); return 1; }
  memset(&dd, 0, sizeof(dd)); dd.Format = DXGI_FORMAT_D32_FLOAT; dd.ViewDimension = D3D11_DSV_DIMENSION_TEXTURE2D;
  hr = ID3D11Device_CreateDepthStencilView(device, (ID3D11Resource *)depth, &dd, &dsv);
  memset(&sd, 0, sizeof(sd)); sd.Format = DXGI_FORMAT_R32_FLOAT; sd.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
  sd.Texture2D.MipLevels = 1;
  if (SUCCEEDED(hr)) hr = ID3D11Device_CreateShaderResourceView(device, (ID3D11Resource *)depth, &sd, &srv);
  td.Format = DXGI_FORMAT_R32_FLOAT; td.BindFlags = D3D11_BIND_RENDER_TARGET;
  if (SUCCEEDED(hr)) hr = ID3D11Device_CreateTexture2D(device, &td, NULL, &target);
  if (SUCCEEDED(hr)) hr = ID3D11Device_CreateRenderTargetView(device, (ID3D11Resource *)target, NULL, &rtv);
  if (FAILED(hr)) { say("SHADOW VIEWS 0x%08lx", (unsigned long)hr); bad = 1; goto done; }
  ID3D11DeviceContext_ClearDepthStencilView(ctx, dsv, D3D11_CLEAR_DEPTH, 0.25f, 0);
  memset(&vp, 0, sizeof(vp)); vp.Width = vp.Height = 32.0f; vp.MaxDepth = 1.0f;
  ID3D11DeviceContext_RSSetViewports(ctx, 1, &vp);
  ID3D11DeviceContext_OMSetRenderTargets(ctx, 1, &rtv, NULL);
  ID3D11DeviceContext_IASetPrimitiveTopology(ctx, D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
  ID3D11DeviceContext_VSSetShader(ctx, vs, NULL, 0);
  ID3D11DeviceContext_PSSetShader(ctx, ps, NULL, 0);
  ID3D11DeviceContext_PSSetShaderResources(ctx, 0, 1, &srv);
  ID3D11DeviceContext_Draw(ctx, 3, 0);
  {
    ID3D11ShaderResourceView *none = NULL;
    ID3D11DeviceContext_PSSetShaderResources(ctx, 0, 1, &none);
    ID3D11DeviceContext_OMSetRenderTargets(ctx, 0, NULL, NULL);
  }
  hr = read_subresource((ID3D11Resource *)target, 0, &td, 0, &staging, &map);
  if (FAILED(hr)) { say("SHADOW READBACK 0x%08lx", (unsigned long)hr); bad = 1; goto done; }
  {
    float v = *(const float *)((const unsigned char *)map.pData + 16 * map.RowPitch + 16 * 4);
    if (fabsf(v - 0.25f) > 1e-6f) { say("SHADOW WRONG got %g want 0.25", v); bad = 1; }
    ID3D11DeviceContext_Unmap(ctx, (ID3D11Resource *)staging, 0);
  }
done:
  if (staging) ID3D11Texture2D_Release(staging);
  if (rtv) ID3D11RenderTargetView_Release(rtv);
  if (target) ID3D11Texture2D_Release(target);
  if (srv) ID3D11ShaderResourceView_Release(srv);
  if (dsv) ID3D11DepthStencilView_Release(dsv);
  ID3D11Texture2D_Release(depth);
  return bad;
}

int main(int argc, char **argv) {
  IDXGIFactory1 *factory = NULL;
  IDXGIAdapter1 *adapter = NULL, *pick = NULL;
  ID3DBlob *vsb, *psb;
  D3D_FEATURE_LEVEL level = 0;
  const D3D_FEATURE_LEVEL want[] = {D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_1,
                                    D3D_FEATURE_LEVEL_10_0};
  HMODULE compiler;
  pD3DCompile compile_fn;
  HRESULT hr;
  int iterations, bad = 0;

  if (argc < 2) {
    printf("usage: agx_format_smoke.exe <out.txt> [iterations]\n");
    return 2;
  }
  iterations = argc > 2 ? atoi(argv[2]) : 10;
  if (iterations < 1) iterations = 1;
  out = CreateFileA(argv[1], GENERIC_WRITE, FILE_SHARE_READ, NULL, CREATE_ALWAYS,
                    FILE_ATTRIBUTE_NORMAL | FILE_FLAG_WRITE_THROUGH, NULL);
  say("FORMATS %u iterations %d", (unsigned)(sizeof(formats) / sizeof(formats[0])), iterations);
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
  vsb = compile(compile_fn, vs_source, sizeof(vs_source) - 1, "vs_4_0");
  psb = compile(compile_fn, ps_source, sizeof(ps_source) - 1, "ps_4_0");
  if (!vsb || !psb) return 1;
  hr = ID3D11Device_CreateVertexShader(device, ID3D10Blob_GetBufferPointer(vsb),
                                       ID3D10Blob_GetBufferSize(vsb), NULL, &vs);
  if (SUCCEEDED(hr)) hr = ID3D11Device_CreatePixelShader(device, ID3D10Blob_GetBufferPointer(psb),
                                       ID3D10Blob_GetBufferSize(psb), NULL, &ps);
  say("SHADERS 0x%08lx", (unsigned long)hr);
  if (FAILED(hr)) return 1;
  for (int i = 0; i < iterations; ++i) {
    int iteration_bad = 0, tested = 0;
    for (unsigned f = 0; f < sizeof(formats) / sizeof(formats[0]); ++f) {
      UINT support = 0;
      if (SUCCEEDED(ID3D11Device_CheckFormatSupport(device, formats[f].dxgi, &support)) &&
          (support & D3D11_FORMAT_SUPPORT_RENDER_TARGET)) ++tested;
      iteration_bad += test_format(&formats[f], i);
    }
    iteration_bad += test_views();
    iteration_bad += test_shadow();
    say("ITERATION %d formats %d bad %d removed 0x%08lx", i, tested, iteration_bad,
        (unsigned long)ID3D11Device_GetDeviceRemovedReason(device));
    bad += iteration_bad;
  }
  say("AGX_FORMAT_SMOKE: %s", bad ? "FAIL" : "PASS");
  return bad ? 1 : 0;
}
