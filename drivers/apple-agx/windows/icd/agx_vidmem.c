/* M2-f4b measurement: how Windows accounts the adapter's memory.
 * - DXGI_ADAPTER_DESC1 (DedicatedVideoMemory, DedicatedSystemMemory,
 *   SharedSystemMemory);
 * - IDXGIAdapter3::QueryVideoMemoryInfo for the LOCAL and NON_LOCAL segment
 *   groups (Budget, CurrentUsage, AvailableForReservation, CurrentReservation);
 * - D3DKMTQueryAdapterInfo KMTQAITYPE_GETSEGMENTSIZE and
 *   KMTQAITYPE_GETSEGMENTGROUPSIZE (local / non-local / non-budget bytes),
 *   which show the budget group each segment counts in.
 * Usage: agx_vidmem.exe (one line per adapter and group). */
#define COBJMACROS
#include <windows.h>
#include <winternl.h>
#include <d3dkmthk.h>
#include <dxgi1_4.h>
#include <stdio.h>

typedef NTSTATUS (APIENTRY *OPENLUID)(D3DKMT_OPENADAPTERFROMLUID *);
typedef NTSTATUS (APIENTRY *QUERYINFO)(const D3DKMT_QUERYADAPTERINFO *);
typedef NTSTATUS (APIENTRY *CLOSEADAPTER)(const D3DKMT_CLOSEADAPTER *);

#define MIB(x) ((unsigned long long)(x) >> 20)

static void kmt(LUID luid) {
  HMODULE gdi = GetModuleHandleW(L"gdi32.dll");
  OPENLUID open = (OPENLUID)(void *)GetProcAddress(gdi, "D3DKMTOpenAdapterFromLuid");
  QUERYINFO query = (QUERYINFO)(void *)GetProcAddress(gdi, "D3DKMTQueryAdapterInfo");
  CLOSEADAPTER close = (CLOSEADAPTER)(void *)GetProcAddress(gdi, "D3DKMTCloseAdapter");
  D3DKMT_OPENADAPTERFROMLUID o;
  D3DKMT_QUERYADAPTERINFO q;
  D3DKMT_SEGMENTSIZEINFO size;
  D3DKMT_SEGMENTGROUPSIZEINFO group;
  D3DKMT_CLOSEADAPTER c;
  NTSTATUS st;
  if (!open || !query || !close) { printf("  KMT unavailable\n"); return; }
  memset(&o, 0, sizeof(o));
  o.AdapterLuid = luid;
  if (!NT_SUCCESS(open(&o))) { printf("  KMT open failed\n"); return; }
  memset(&q, 0, sizeof(q));
  memset(&size, 0, sizeof(size));
  q.hAdapter = o.hAdapter;
  q.Type = KMTQAITYPE_GETSEGMENTSIZE;
  q.pPrivateDriverData = &size;
  q.PrivateDriverDataSize = sizeof(size);
  st = query(&q);
  printf("  KMT SEGMENTSIZE status 0x%08lx dedicated-video %llu MiB dedicated-system %llu MiB shared-system %llu MiB\n",
         (unsigned long)st, MIB(size.DedicatedVideoMemorySize),
         MIB(size.DedicatedSystemMemorySize), MIB(size.SharedSystemMemorySize));
  memset(&group, 0, sizeof(group));
  q.Type = KMTQAITYPE_GETSEGMENTGROUPSIZE;
  q.pPrivateDriverData = &group;
  q.PrivateDriverDataSize = sizeof(group);
  st = query(&q);
  printf("  KMT SEGMENTGROUPSIZE status 0x%08lx local %llu MiB non-local %llu MiB non-budget %llu MiB\n",
         (unsigned long)st, MIB(group.LocalMemory), MIB(group.NonLocalMemory),
         MIB(group.NonBudgetMemory));
  memset(&c, 0, sizeof(c));
  c.hAdapter = o.hAdapter;
  close(&c);
}

int main(void) {
  IDXGIFactory1 *factory = NULL;
  IDXGIAdapter1 *adapter = NULL;
  UINT i;
  HRESULT hr = CreateDXGIFactory1(&IID_IDXGIFactory1, (void **)&factory);
  printf("CreateDXGIFactory1 0x%08lx\n", (unsigned long)hr);
  if (FAILED(hr)) return 1;
  for (i = 0; IDXGIFactory1_EnumAdapters1(factory, i, &adapter) == S_OK; ++i) {
    DXGI_ADAPTER_DESC1 d;
    IDXGIAdapter3 *a3 = NULL;
    int g;
    IDXGIAdapter1_GetDesc1(adapter, &d);
    printf("ADAPTER %u luid %08lx:%08lx vendor %04x flags %u '%ls'\n", i,
           (unsigned long)d.AdapterLuid.HighPart, (unsigned long)d.AdapterLuid.LowPart,
           d.VendorId, d.Flags, d.Description);
    printf("  DESC dedicated-video %llu MiB dedicated-system %llu MiB shared-system %llu MiB\n",
           MIB(d.DedicatedVideoMemory), MIB(d.DedicatedSystemMemory),
           MIB(d.SharedSystemMemory));
    if (SUCCEEDED(IDXGIAdapter1_QueryInterface(adapter, &IID_IDXGIAdapter3, (void **)&a3))) {
      for (g = 0; g < 2; ++g) {
        DXGI_QUERY_VIDEO_MEMORY_INFO m;
        memset(&m, 0, sizeof(m));
        hr = IDXGIAdapter3_QueryVideoMemoryInfo(a3, 0, (DXGI_MEMORY_SEGMENT_GROUP)g, &m);
        printf("  GROUP %s hr 0x%08lx budget %llu MiB usage %llu MiB available-for-reservation %llu MiB reservation %llu MiB\n",
               g ? "NON_LOCAL" : "LOCAL", (unsigned long)hr, MIB(m.Budget),
               MIB(m.CurrentUsage), MIB(m.AvailableForReservation),
               MIB(m.CurrentReservation));
      }
      IDXGIAdapter3_Release(a3);
    }
    kmt(d.AdapterLuid);
    IDXGIAdapter1_Release(adapter);
  }
  IDXGIFactory1_Release(factory);
  return 0;
}
