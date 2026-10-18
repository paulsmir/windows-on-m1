/* Fresh-allocation content probe (EXP1301 copyinit: a multisampled texture
 * read back unrelated image content).  Windows contract: VidMm initializes an
 * allocation without content when it is first paged in (DXGK_OPERATION_FILL /
 * FILL_VIRTUAL, DXGKARG_BUILDPAGINGBUFFER.Fill.FillPattern), so no resource
 * created without initial data may show another allocation's bytes.  The KMD
 * demand pool hands recycled slab slots to new granules without clearing
 * them (pool_windows.c AdmissionPoolCommitRun), so that guarantee rests on
 * the fill reaching the granule.
 *
 * Each round first creates, writes, reads back and releases a 4 MiB marker
 * texture (every word 0xA5C3E1F7), then creates fresh resources without
 * initial data and reports their content:
 * - CONTROL: a texture with initial data copied to staging (the copy works);
 * - STAGING: staging textures/buffers mapped directly (no GPU copy);
 * - DEFAULT: default textures/buffers copied to a zero-initialised staging
 *   resource.
 * Per resource: nonzero bytes, marker words, the first nonzero word.
 * EXP1306: in one process, fresh resources of a just-released size return
 * its content (Mesa's BO cache; D3D11 leaves such content undefined). The
 * cross-process contract is checked by two runs:
 * - write: 16 marker textures of 4 MiB, written, verified, released, exit;
 * - scan (a new process): no marker round; fresh resources (the sizes above
 *   plus 16 textures of 4 MiB) -- any marker word is another process's data.
 * Usage: agx_fresh_alloc_smoke.exe <out.txt> [rounds] [write|scan] */
#define COBJMACROS
#define INITGUID
#include <windows.h>
#include <d3d11.h>
#include <dxgi.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MARKER 0xA5C3E1F7u
#define CONTROL_WORD 0x11223344u

static HANDLE out = INVALID_HANDLE_VALUE;
static ID3D11Device *device;
static ID3D11DeviceContext *ctx;
static unsigned long long total_nonzero, total_marker;
static unsigned nonzero_resources, failures;

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

typedef struct {
  unsigned long long nonzero, marker, control;
  long long first;
  UINT first_word;
} SCAN;

/* rows of row_bytes at pitch */
static void scan(const BYTE *data, UINT rows, UINT row_bytes, UINT pitch, SCAN *s) {
  UINT r, i;
  memset(s, 0, sizeof(*s));
  s->first = -1;
  for (r = 0; r < rows; ++r) {
    const BYTE *row = data + (size_t)r * pitch;
    for (i = 0; i + 4 <= row_bytes; i += 4) {
      UINT w;
      memcpy(&w, row + i, 4);
      if (w == MARKER) ++s->marker;
      if (w == CONTROL_WORD) ++s->control;
      if (w != 0) {
        s->nonzero += (w & 0xffu ? 1 : 0) + (w & 0xff00u ? 1 : 0) +
                      (w & 0xff0000u ? 1 : 0) + (w & 0xff000000u ? 1 : 0);
        if (s->first < 0) {
          s->first = (long long)r * row_bytes + i;
          s->first_word = w;
        }
      }
    }
  }
}

static void report(const char *phase, int round, const char *kind, UINT a, UINT b,
                   unsigned long long bytes, const SCAN *s) {
  say("FRESH %s round %d %s %ux%u bytes %llu nonzero %llu marker %llu first %lld word 0x%08x",
      phase, round, kind, a, b, bytes, s->nonzero, s->marker, s->first,
      s->first_word);
  if (strcmp(phase, "CONTROL") != 0) {
    total_nonzero += s->nonzero;
    total_marker += s->marker;
    if (s->nonzero) ++nonzero_resources;
  }
}

static ID3D11Texture2D *texture(UINT w, UINT h, D3D11_USAGE usage, const UINT *fill) {
  D3D11_TEXTURE2D_DESC td;
  D3D11_SUBRESOURCE_DATA init;
  ID3D11Texture2D *t = NULL;
  UINT *words = NULL;
  HRESULT hr;
  memset(&td, 0, sizeof(td));
  td.Width = w;
  td.Height = h;
  td.MipLevels = 1;
  td.ArraySize = 1;
  td.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
  td.SampleDesc.Count = 1;
  td.Usage = usage;
  if (usage == D3D11_USAGE_STAGING)
    td.CPUAccessFlags = D3D11_CPU_ACCESS_READ | D3D11_CPU_ACCESS_WRITE;
  else
    td.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET;
  if (fill) {
    size_t i, n = (size_t)w * h;
    words = (UINT *)malloc(n * 4);
    if (!words) return NULL;
    for (i = 0; i < n; ++i) words[i] = *fill;
    init.pSysMem = words;
    init.SysMemPitch = w * 4;
    init.SysMemSlicePitch = 0;
  }
  hr = ID3D11Device_CreateTexture2D(device, &td, fill ? &init : NULL, &t);
  free(words);
  if (FAILED(hr)) {
    say("CREATE texture %ux%u usage %d hr 0x%08lx", w, h, (int)usage, (unsigned long)hr);
    ++failures;
    return NULL;
  }
  return t;
}

static ID3D11Buffer *buffer(UINT bytes, D3D11_USAGE usage) {
  D3D11_BUFFER_DESC bd;
  ID3D11Buffer *b = NULL;
  HRESULT hr;
  memset(&bd, 0, sizeof(bd));
  bd.ByteWidth = bytes;
  bd.Usage = usage;
  if (usage == D3D11_USAGE_STAGING)
    bd.CPUAccessFlags = D3D11_CPU_ACCESS_READ | D3D11_CPU_ACCESS_WRITE;
  else
    bd.BindFlags = D3D11_BIND_VERTEX_BUFFER;
  hr = ID3D11Device_CreateBuffer(device, &bd, NULL, &b);
  if (FAILED(hr)) {
    say("CREATE buffer %u usage %d hr 0x%08lx", bytes, (int)usage, (unsigned long)hr);
    ++failures;
    return NULL;
  }
  return b;
}

/* Map for read; rows/row_bytes describe the content. */
static int map_scan(ID3D11Resource *r, UINT rows, UINT row_bytes, SCAN *s) {
  D3D11_MAPPED_SUBRESOURCE m;
  HRESULT hr = ID3D11DeviceContext_Map(ctx, r, 0, D3D11_MAP_READ, 0, &m);
  if (FAILED(hr)) {
    say("MAP hr 0x%08lx", (unsigned long)hr);
    ++failures;
    return 0;
  }
  scan((const BYTE *)m.pData, rows, row_bytes, rows > 1 ? m.RowPitch : row_bytes, s);
  ID3D11DeviceContext_Unmap(ctx, r, 0);
  return 1;
}

/* A zeroed staging texture: written by Map(WRITE) so its content is known
 * before the copy lands in it. */
static ID3D11Texture2D *zeroed_staging_texture(UINT w, UINT h) {
  D3D11_MAPPED_SUBRESOURCE m;
  ID3D11Texture2D *t = texture(w, h, D3D11_USAGE_STAGING, NULL);
  UINT r;
  if (!t) return NULL;
  if (FAILED(ID3D11DeviceContext_Map(ctx, (ID3D11Resource *)t, 0, D3D11_MAP_WRITE, 0, &m))) {
    ++failures;
    ID3D11Texture2D_Release(t);
    return NULL;
  }
  for (r = 0; r < h; ++r) memset((BYTE *)m.pData + (size_t)r * m.RowPitch, 0, (size_t)w * 4);
  ID3D11DeviceContext_Unmap(ctx, (ID3D11Resource *)t, 0);
  return t;
}

static ID3D11Buffer *zeroed_staging_buffer(UINT bytes) {
  D3D11_MAPPED_SUBRESOURCE m;
  ID3D11Buffer *b = buffer(bytes, D3D11_USAGE_STAGING);
  if (!b) return NULL;
  if (FAILED(ID3D11DeviceContext_Map(ctx, (ID3D11Resource *)b, 0, D3D11_MAP_WRITE, 0, &m))) {
    ++failures;
    ID3D11Buffer_Release(b);
    return NULL;
  }
  memset(m.pData, 0, bytes);
  ID3D11DeviceContext_Unmap(ctx, (ID3D11Resource *)b, 0);
  return b;
}

static void marker_round(void) {
  UINT word = MARKER;
  SCAN s;
  ID3D11Texture2D *t = texture(1024, 1024, D3D11_USAGE_DEFAULT, &word);
  ID3D11Texture2D *st = zeroed_staging_texture(1024, 1024);
  if (t && st) {
    ID3D11DeviceContext_CopyResource(ctx, (ID3D11Resource *)st, (ID3D11Resource *)t);
    if (map_scan((ID3D11Resource *)st, 1024, 4096, &s))
      say("MARKER written %llu of %u words", s.marker, 1024u * 1024u);
  }
  if (st) ID3D11Texture2D_Release(st);
  if (t) ID3D11Texture2D_Release(t);
  ID3D11DeviceContext_Flush(ctx);
}

static const UINT sizes[][2] = {{64, 64}, {256, 256}, {1024, 1024}, {2048, 2048}};
static const UINT buffer_sizes[] = {65536u, 1u << 20, 16u << 20};

static void fresh_round(int round) {
  UINT i;
  SCAN s;
  {
    UINT word = CONTROL_WORD;
    ID3D11Texture2D *t = texture(256, 256, D3D11_USAGE_DEFAULT, &word);
    ID3D11Texture2D *st = zeroed_staging_texture(256, 256);
    if (t && st) {
      ID3D11DeviceContext_CopyResource(ctx, (ID3D11Resource *)st, (ID3D11Resource *)t);
      if (map_scan((ID3D11Resource *)st, 256, 1024, &s)) {
        report("CONTROL", round, "texture", 256, 256, 256ull * 1024, &s);
        if (s.control != 256u * 256u) {
          say("CONTROL WRONG copied %llu of %u words", s.control, 256u * 256u);
          ++failures;
        }
      }
    }
    if (st) ID3D11Texture2D_Release(st);
    if (t) ID3D11Texture2D_Release(t);
  }
  for (i = 0; i < sizeof(sizes) / sizeof(sizes[0]); ++i) {
    UINT w = sizes[i][0], h = sizes[i][1];
    ID3D11Texture2D *st = texture(w, h, D3D11_USAGE_STAGING, NULL);
    if (st && map_scan((ID3D11Resource *)st, h, w * 4, &s))
      report("STAGING", round, "texture", w, h, (unsigned long long)w * h * 4, &s);
    if (st) ID3D11Texture2D_Release(st);
  }
  for (i = 0; i < sizeof(buffer_sizes) / sizeof(buffer_sizes[0]); ++i) {
    ID3D11Buffer *st = buffer(buffer_sizes[i], D3D11_USAGE_STAGING);
    if (st && map_scan((ID3D11Resource *)st, 1, buffer_sizes[i], &s))
      report("STAGING", round, "buffer", buffer_sizes[i], 1, buffer_sizes[i], &s);
    if (st) ID3D11Buffer_Release(st);
  }
  for (i = 0; i < sizeof(sizes) / sizeof(sizes[0]); ++i) {
    UINT w = sizes[i][0], h = sizes[i][1];
    ID3D11Texture2D *t = texture(w, h, D3D11_USAGE_DEFAULT, NULL);
    ID3D11Texture2D *st = zeroed_staging_texture(w, h);
    if (t && st) {
      ID3D11DeviceContext_CopyResource(ctx, (ID3D11Resource *)st, (ID3D11Resource *)t);
      if (map_scan((ID3D11Resource *)st, h, w * 4, &s))
        report("DEFAULT", round, "texture", w, h, (unsigned long long)w * h * 4, &s);
    }
    if (st) ID3D11Texture2D_Release(st);
    if (t) ID3D11Texture2D_Release(t);
  }
  for (i = 0; i < sizeof(buffer_sizes) / sizeof(buffer_sizes[0]); ++i) {
    ID3D11Buffer *b = buffer(buffer_sizes[i], D3D11_USAGE_DEFAULT);
    ID3D11Buffer *st = zeroed_staging_buffer(buffer_sizes[i]);
    if (b && st) {
      ID3D11DeviceContext_CopyResource(ctx, (ID3D11Resource *)st, (ID3D11Resource *)b);
      if (map_scan((ID3D11Resource *)st, 1, buffer_sizes[i], &s))
        report("DEFAULT", round, "buffer", buffer_sizes[i], 1, buffer_sizes[i], &s);
    }
    if (st) ID3D11Buffer_Release(st);
    if (b) ID3D11Buffer_Release(b);
  }
  ID3D11DeviceContext_Flush(ctx);
}

static void write_process(void) {
  UINT word = MARKER, i;
  SCAN s;
  ID3D11Texture2D *t[16];
  for (i = 0; i < 16; ++i) t[i] = texture(1024, 1024, D3D11_USAGE_DEFAULT, &word);
  {
    ID3D11Texture2D *st = zeroed_staging_texture(1024, 1024);
    if (t[15] && st) {
      ID3D11DeviceContext_CopyResource(ctx, (ID3D11Resource *)st, (ID3D11Resource *)t[15]);
      if (map_scan((ID3D11Resource *)st, 1024, 4096, &s))
        say("WRITE marker %llu of %u words in the last texture", s.marker, 1024u * 1024u);
    }
    if (st) ID3D11Texture2D_Release(st);
  }
  for (i = 0; i < 16; ++i)
    if (t[i]) ID3D11Texture2D_Release(t[i]);
  ID3D11DeviceContext_Flush(ctx);
}

static void scan_process(int round) {
  UINT i;
  SCAN s;
  fresh_round(round);
  for (i = 0; i < 16; ++i) {
    ID3D11Texture2D *t = texture(1024, 1024, D3D11_USAGE_DEFAULT, NULL);
    ID3D11Texture2D *st = zeroed_staging_texture(1024, 1024);
    if (t && st) {
      ID3D11DeviceContext_CopyResource(ctx, (ID3D11Resource *)st, (ID3D11Resource *)t);
      if (map_scan((ID3D11Resource *)st, 1024, 4096, &s))
        report("SCAN", round, "texture", 1024, 1024, 4194304ull, &s);
    }
    if (st) ID3D11Texture2D_Release(st);
    if (t) ID3D11Texture2D_Release(t);
  }
}

int main(int argc, char **argv) {
  IDXGIFactory1 *factory = NULL;
  IDXGIAdapter1 *adapter = NULL, *chosen = NULL;
  D3D_FEATURE_LEVEL levels[] = {D3D_FEATURE_LEVEL_10_1, D3D_FEATURE_LEVEL_10_0};
  D3D_FEATURE_LEVEL got;
  UINT i;
  int rounds = 3, r, mode = 0; /* 0 in-process, 1 write, 2 scan */
  HRESULT hr;
  if (argc < 2) {
    printf("usage: agx_fresh_alloc_smoke.exe <out.txt> [rounds]\n");
    return 2;
  }
  out = CreateFileA(argv[1], GENERIC_WRITE, FILE_SHARE_READ, NULL, CREATE_ALWAYS,
                    FILE_ATTRIBUTE_NORMAL, NULL);
  if (argc > 2) rounds = atoi(argv[2]);
  if (rounds < 1) rounds = 1;
  if (argc > 3) mode = !strcmp(argv[3], "write") ? 1 : !strcmp(argv[3], "scan") ? 2 : 0;
  if (SUCCEEDED(CreateDXGIFactory1(&IID_IDXGIFactory1, (void **)&factory))) {
    for (i = 0; IDXGIFactory1_EnumAdapters1(factory, i, &adapter) == S_OK; ++i) {
      DXGI_ADAPTER_DESC1 d;
      IDXGIAdapter1_GetDesc1(adapter, &d);
      say("ADAPTER %u vendor %04x '%ls'", i, d.VendorId, d.Description);
      if (!chosen && !(d.Flags & DXGI_ADAPTER_FLAG_SOFTWARE)) {
        chosen = adapter;
        continue;
      }
      IDXGIAdapter1_Release(adapter);
    }
  }
  hr = D3D11CreateDevice((IDXGIAdapter *)chosen, chosen ? D3D_DRIVER_TYPE_UNKNOWN : D3D_DRIVER_TYPE_HARDWARE,
                         NULL, 0, levels, 2, D3D11_SDK_VERSION, &device, &got, &ctx);
  say("D3D11CreateDevice hr 0x%08lx level 0x%x", (unsigned long)hr, (unsigned)got);
  if (FAILED(hr)) return 1;
  if (mode == 1) {
    write_process();
  } else {
    for (r = 0; r < rounds; ++r) {
      if (mode == 0) marker_round();
      if (mode == 2) scan_process(r);
      else fresh_round(r);
    }
  }
  hr = ID3D11Device_GetDeviceRemovedReason(device);
  say("RESULT mode %d nonzero_resources %u nonzero_bytes %llu marker_words %llu failures %u removed 0x%08lx",
      mode, nonzero_resources, total_nonzero, total_marker, failures, (unsigned long)hr);
  ID3D11DeviceContext_Release(ctx);
  ID3D11Device_Release(device);
  if (chosen) IDXGIAdapter1_Release(chosen);
  if (factory) IDXGIFactory1_Release(factory);
  if (out != INVALID_HANDLE_VALUE) CloseHandle(out);
  return 0;
}
