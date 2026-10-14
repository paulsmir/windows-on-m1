/* M2-f5 measurement: GPU allocation under real memory pressure.
 * 1. Lock system memory (VirtualLock, working set raised to match) until
 *    GlobalMemoryStatusEx reports at most <leave-MiB> available, so Windows
 *    cannot hand those pages to the driver.
 * 2. On the AGX adapter, create render-target textures and clear each
 *    (the clear makes VidMm place it) until a creation fails, the device is
 *    removed or <max-MiB> is reached.
 * 3. Clear and read back the first texture to show whether the device still
 *    works, then free everything.
 * Each line is written through to <out.txt>, so the log survives a crash.
 * Usage: agx_mempressure.exe <out.txt> <leave-MiB> <max-MiB> [height]
 * (textures are 2048 x height RGBA8, default 4096 = 32 MiB).
 * M2-f11: the LOCAL segment-group budget and usage (DXGI
 * QueryVideoMemoryInfo) are printed with the fill progress, so a run shows
 * whether it crossed the process residency budget. */
#define COBJMACROS
#define INITGUID
#include <windows.h>
#include <d3d11.h>
#include <dxgi1_4.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MIB(x) ((unsigned long long)(x) >> 20)
#define LOCK_CHUNK (64ull << 20)
#define MAX_TEXTURES 256

static HANDLE out = INVALID_HANDLE_VALUE;
static IDXGIAdapter3 *adapter3;

static const char *budget(char *text, size_t bytes) {
  DXGI_QUERY_VIDEO_MEMORY_INFO info;
  if (adapter3 && SUCCEEDED(IDXGIAdapter3_QueryVideoMemoryInfo(
                      adapter3, 0, DXGI_MEMORY_SEGMENT_GROUP_LOCAL, &info)))
    snprintf(text, bytes, "budget %llu MiB usage %llu MiB",
             MIB(info.Budget), MIB(info.CurrentUsage));
  else
    snprintf(text, bytes, "budget n/a");
  return text;
}

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

static unsigned long long available(void) {
  MEMORYSTATUSEX m;
  m.dwLength = sizeof(m);
  return GlobalMemoryStatusEx(&m) ? m.ullAvailPhys : 0ull;
}

int main(int argc, char **argv) {
  unsigned long long leave, max_bytes, locked = 0ull, filled = 0ull;
  void *chunks[256];
  unsigned nchunks = 0;
  IDXGIFactory1 *factory = NULL;
  IDXGIAdapter1 *adapter = NULL, *pick = NULL;
  ID3D11Device *device = NULL;
  ID3D11DeviceContext *ctx = NULL;
  ID3D11Texture2D *tex[MAX_TEXTURES];
  ID3D11RenderTargetView *rtv[MAX_TEXTURES];
  ID3D11Texture2D *staging = NULL;
  D3D_FEATURE_LEVEL level = 0;
  const D3D_FEATURE_LEVEL want[] = {D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_1,
                                    D3D_FEATURE_LEVEL_10_0};
  D3D11_TEXTURE2D_DESC td;
  HRESULT hr, first_failure = S_OK;
  unsigned count = 0, i, height = 4096;
  unsigned long long texture_bytes;
  SIZE_T wsmin, wsmax;
  char text[64];

  if (argc < 4) {
    printf("usage: agx_mempressure.exe <out.txt> <leave-MiB> <max-MiB>\n");
    return 2;
  }
  out = CreateFileA(argv[1], GENERIC_WRITE, FILE_SHARE_READ, NULL, CREATE_ALWAYS,
                    FILE_ATTRIBUTE_NORMAL | FILE_FLAG_WRITE_THROUGH, NULL);
  leave = strtoull(argv[2], NULL, 10) << 20;
  max_bytes = strtoull(argv[3], NULL, 10) << 20;
  if (argc > 4) height = (unsigned)strtoul(argv[4], NULL, 10);
  if (height == 0 || height > 8192) height = 4096;
  texture_bytes = 2048ull * height * 4ull;

  hr = CreateDXGIFactory1(&IID_IDXGIFactory1, (void **)&factory);
  if (FAILED(hr)) { say("CreateDXGIFactory1 0x%08lx", (unsigned long)hr); return 1; }
  for (UINT a = 0; IDXGIFactory1_EnumAdapters1(factory, a, &adapter) != DXGI_ERROR_NOT_FOUND; ++a) {
    DXGI_ADAPTER_DESC1 d;
    IDXGIAdapter1_GetDesc1(adapter, &d);
    if (!pick && !(d.Flags & DXGI_ADAPTER_FLAG_SOFTWARE)) { pick = adapter; continue; }
    IDXGIAdapter1_Release(adapter);
  }
  if (!pick) { say("NO_HARDWARE_ADAPTER"); return 1; }
  if (FAILED(IDXGIAdapter1_QueryInterface(pick, &IID_IDXGIAdapter3, (void **)&adapter3)))
    adapter3 = NULL;
  hr = D3D11CreateDevice((IDXGIAdapter *)pick, D3D_DRIVER_TYPE_UNKNOWN, NULL, 0, want, 3,
                         D3D11_SDK_VERSION, &device, &level, &ctx);
  say("D3D11CreateDevice 0x%08lx FEATURE_LEVEL 0x%x", (unsigned long)hr, level);
  if (FAILED(hr)) return 1;

  /* 1. Pressure. */
  say("AVAILABLE_BEFORE %llu MiB leave %llu MiB", MIB(available()), MIB(leave));
  while (nchunks < 256 && available() > leave + LOCK_CHUNK) {
    void *p = VirtualAlloc(NULL, (SIZE_T)LOCK_CHUNK, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
    if (!p) { say("VirtualAlloc stop %lu", GetLastError()); break; }
    if (!GetProcessWorkingSetSize(GetCurrentProcess(), &wsmin, &wsmax) ||
        !SetProcessWorkingSetSize(GetCurrentProcess(), wsmin + (SIZE_T)LOCK_CHUNK,
                                  wsmax + (SIZE_T)LOCK_CHUNK)) {
      say("SetProcessWorkingSetSize stop %lu", GetLastError());
      VirtualFree(p, 0, MEM_RELEASE);
      break;
    }
    if (!VirtualLock(p, (SIZE_T)LOCK_CHUNK)) {
      say("VirtualLock stop %lu", GetLastError());
      VirtualFree(p, 0, MEM_RELEASE);
      break;
    }
    chunks[nchunks++] = p;
    locked += LOCK_CHUNK;
  }
  say("LOCKED %llu MiB available %llu MiB %s", MIB(locked), MIB(available()),
      budget(text, sizeof(text)));

  /* 2. Fill: 2048 x height RGBA8. */
  say("TEXTURE 2048x%u %llu MiB", height, MIB(texture_bytes));
  memset(&td, 0, sizeof(td));
  td.Width = 2048; td.Height = height; td.MipLevels = td.ArraySize = 1;
  td.Format = DXGI_FORMAT_R8G8B8A8_UNORM; td.SampleDesc.Count = 1;
  td.Usage = D3D11_USAGE_DEFAULT; td.BindFlags = D3D11_BIND_RENDER_TARGET;
  while (count < MAX_TEXTURES && filled < max_bytes) {
    const float colour[4] = {0.25f, 0.5f, 0.75f, 1.0f};
    tex[count] = NULL;
    rtv[count] = NULL;
    hr = ID3D11Device_CreateTexture2D(device, &td, NULL, &tex[count]);
    if (SUCCEEDED(hr))
      hr = ID3D11Device_CreateRenderTargetView(device, (ID3D11Resource *)tex[count], NULL, &rtv[count]);
    if (FAILED(hr)) {
      first_failure = hr;
      if (tex[count]) ID3D11Texture2D_Release(tex[count]);
      break;
    }
    ID3D11DeviceContext_ClearRenderTargetView(ctx, rtv[count], colour);
    ID3D11DeviceContext_Flush(ctx);
    ++count;
    filled += texture_bytes;
    hr = ID3D11Device_GetDeviceRemovedReason(device);
    if (hr != S_OK) { first_failure = hr; break; }
    if (count % 8 == 0)
      say("FILLED %u textures %llu MiB available %llu MiB %s", count, MIB(filled),
          MIB(available()), budget(text, sizeof(text)));
  }
  say("FILL_END textures %u %llu MiB first-failure 0x%08lx removed 0x%08lx available %llu MiB %s",
      count, MIB(filled), (unsigned long)first_failure,
      (unsigned long)ID3D11Device_GetDeviceRemovedReason(device), MIB(available()),
      budget(text, sizeof(text)));

  /* 3. Does the device still work? */
  if (count) {
    D3D11_TEXTURE2D_DESC sd = td;
    D3D11_MAPPED_SUBRESOURCE map;
    const float colour[4] = {1.0f, 0.0f, 0.0f, 1.0f};
    sd.Width = sd.Height = 16; sd.Usage = D3D11_USAGE_STAGING;
    sd.BindFlags = 0; sd.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    hr = ID3D11Device_CreateTexture2D(device, &sd, NULL, &staging);
    if (SUCCEEDED(hr)) {
      D3D11_BOX box = {0, 0, 0, 16, 16, 1};
      ID3D11DeviceContext_ClearRenderTargetView(ctx, rtv[0], colour);
      ID3D11DeviceContext_CopySubresourceRegion(ctx, (ID3D11Resource *)staging, 0, 0, 0, 0,
                                                (ID3D11Resource *)tex[0], 0, &box);
      hr = ID3D11DeviceContext_Map(ctx, (ID3D11Resource *)staging, 0, D3D11_MAP_READ, 0, &map);
      if (SUCCEEDED(hr)) {
        say("READBACK texel 0x%08lx", (unsigned long)*(const UINT *)map.pData);
        ID3D11DeviceContext_Unmap(ctx, (ID3D11Resource *)staging, 0);
      }
    }
    say("VERIFY 0x%08lx removed 0x%08lx", (unsigned long)hr,
        (unsigned long)ID3D11Device_GetDeviceRemovedReason(device));
  }
  if (staging) ID3D11Texture2D_Release(staging);
  for (i = 0; i < count; ++i) {
    ID3D11RenderTargetView_Release(rtv[i]);
    ID3D11Texture2D_Release(tex[i]);
  }
  ID3D11DeviceContext_Flush(ctx);
  for (i = 0; i < nchunks; ++i) {
    VirtualUnlock(chunks[i], (SIZE_T)LOCK_CHUNK);
    VirtualFree(chunks[i], 0, MEM_RELEASE);
  }
  say("RELEASED available %llu MiB", MIB(available()));
  say("MEMPRESSURE_DONE");
  return 0;
}
