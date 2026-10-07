/* EXP1024 diagnostic: poll the KMD VSync receipt ring and print every new
 * event once, giving a continuous flip timeline (queue via PendingSequence
 * on ticks, kind 3 latch, kind 6 notify). Usage: AppleAgxVsyncTrace <seconds> */
#include <windows.h>
#include <d3dkmthk.h>
#include <stdio.h>
#include <stdlib.h>

#include "../../shared/include/apple_agx_vsync.h"

static APPLE_AGX_VSYNC_QUERY g_query;

int wmain(int argc, wchar_t **argv) {
  D3DKMT_ENUMADAPTERS2 enumeration = {0};
  D3DKMT_ADAPTERINFO *adapters;
  D3DKMT_HANDLE adapter = 0;
  ULONGLONG last = 0ULL, seconds = argc > 1 ? _wtoi(argv[1]) : 60;
  ULONGLONG deadline, lost = 0ULL;
  UINT index;
  if (D3DKMTEnumAdapters2(&enumeration) < 0 || enumeration.NumAdapters == 0u ||
      enumeration.NumAdapters > 64u)
    return 1;
  adapters = (D3DKMT_ADAPTERINFO *)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY,
      enumeration.NumAdapters * sizeof(*adapters));
  if (adapters == NULL)
    return 1;
  enumeration.pAdapters = adapters;
  if (D3DKMTEnumAdapters2(&enumeration) < 0)
    return 1;
  for (index = 0u; index < enumeration.NumAdapters && !adapter; ++index) {
    D3DKMT_ESCAPE escape = {0};
    ZeroMemory(&g_query, sizeof(g_query));
    g_query.Magic = APPLE_AGX_VSYNC_QUERY_MAGIC;
    g_query.Bytes = sizeof(g_query);
    escape.hAdapter = adapters[index].hAdapter;
    escape.Type = D3DKMT_ESCAPE_DRIVERPRIVATE;
    escape.pPrivateDriverData = &g_query;
    escape.PrivateDriverDataSize = sizeof(g_query);
    if (D3DKMTEscape(&escape) >= 0 && g_query.Magic == APPLE_AGX_VSYNC_QUERY_MAGIC)
      adapter = adapters[index].hAdapter;
  }
  if (!adapter) {
    fprintf(stderr, "VSYNC_TRACE no adapter\n");
    return 2;
  }
  deadline = GetTickCount64() + seconds * 1000ULL;
  while (GetTickCount64() < deadline) {
    D3DKMT_ESCAPE escape = {0};
    LARGE_INTEGER qpc;
    ZeroMemory(&g_query, sizeof(g_query));
    g_query.Magic = APPLE_AGX_VSYNC_QUERY_MAGIC;
    g_query.Bytes = sizeof(g_query);
    escape.hAdapter = adapter;
    escape.Type = D3DKMT_ESCAPE_DRIVERPRIVATE;
    escape.pPrivateDriverData = &g_query;
    escape.PrivateDriverDataSize = sizeof(g_query);
    QueryPerformanceCounter(&qpc);
    if (D3DKMTEscape(&escape) >= 0) {
      ULONGLONG first = g_query.EventCount > 32ULL ? g_query.EventCount - 32ULL : 0ULL;
      if (last < first && last >= 32ULL) lost += first - last;
      for (index = 0u; index < APPLE_AGX_VSYNC_RECEIPT_CAPACITY; ++index) {
        const APPLE_AGX_VSYNC_EVENT *e = &g_query.Events[index];
        if (e->Sequence == 0ULL || e->Sequence <= last) continue;
        if (e->Kind == 5u && e->PendingSequence == 0ULL) continue;
        printf("E %llu t=%llu k=%u ps=%llu as=%llu n=%llu\n", e->Sequence,
               e->Time100ns, e->Kind, e->PendingSequence, e->ActiveSequence,
               e->NotifyOrdinal);
      }
      for (index = 0u; index < APPLE_AGX_VSYNC_RECEIPT_CAPACITY; ++index)
        if (g_query.Events[index].Sequence > last) last = g_query.Events[index].Sequence;
    }
    Sleep(5);
  }
  printf("END events=%llu notify=%llu lost=%llu\n", g_query.EventCount,
         g_query.NotifyCount, lost);
  return 0;
}
