#include <windows.h>
#include <d3dkmthk.h>
#include <stdio.h>

#include "render_qualification.h"

int wmain(void) {
  D3DKMT_ENUMADAPTERS2 enumeration = {0};
  D3DKMT_ADAPTERINFO *adapters;
  NTSTATUS status;
  UINT index;
  BOOL found = FALSE;
  status = D3DKMTEnumAdapters2(&enumeration);
  if (status < 0 || enumeration.NumAdapters == 0u ||
      enumeration.NumAdapters > 64u) {
    fprintf(stderr, "BLT_PROBE enumeration status=0x%08lx capacity=%lu\n",
            (ULONG)status, enumeration.NumAdapters);
    return 1;
  }
  adapters = (D3DKMT_ADAPTERINFO *)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY,
      enumeration.NumAdapters * sizeof(*adapters));
  if (adapters == NULL)
    return 1;
  enumeration.pAdapters = adapters;
  status = D3DKMTEnumAdapters2(&enumeration);
  if (status < 0) {
    fprintf(stderr, "BLT_PROBE enumeration fill status=0x%08lx\n", (ULONG)status);
    HeapFree(GetProcessHeap(), 0, adapters);
    return 1;
  }
  for (index = 0u; index < enumeration.NumAdapters; ++index) {
    D3DKMT_CLOSEADAPTER close = {0};
    D3DKMT_ESCAPE escape = {0};
    ADMISSION_BLT_PROBE probe = {0};
    UINT eventIndex;
    probe.Magic = ADMISSION_BLT_PROBE_MAGIC;
    probe.Version = ADMISSION_BLT_PROBE_VERSION;
    probe.Bytes = sizeof(probe);
    escape.hAdapter = adapters[index].hAdapter;
    escape.Type = D3DKMT_ESCAPE_DRIVERPRIVATE;
    escape.pPrivateDriverData = &probe;
    escape.PrivateDriverDataSize = sizeof(probe);
    status = D3DKMTEscape(&escape);
    close.hAdapter = adapters[index].hAdapter;
    (void)D3DKMTCloseAdapter(&close);
    if (status < 0 || probe.Magic != ADMISSION_BLT_PROBE_MAGIC ||
        probe.Version != ADMISSION_BLT_PROBE_VERSION ||
        probe.Bytes != sizeof(probe))
      continue;
    found = TRUE;
    wprintf(L"BLT_PROBE luid=%08x:%08x build=%u boot=%u adapter=0x%llx present=%u present_blt=%u virtual_submit=%u physical_present_submit=%u cpu_blt=%u events=%u overflow=%u\n",
            (UINT)adapters[index].AdapterLuid.HighPart,
            adapters[index].AdapterLuid.LowPart,
            probe.CandidateBuild, probe.BootGeneration, probe.AdapterToken,
            probe.PresentCalls, probe.PresentBltCalls, probe.VirtualSubmitCalls,
            probe.PhysicalPresentSubmits, probe.CpuBltExecutions,
            probe.EventCount, probe.Overflow);
    for (eventIndex = 0u;
         eventIndex < probe.EventCount && eventIndex < ADMISSION_BLT_PROBE_CAPACITY;
         ++eventIndex) {
      const ADMISSION_BLT_EXECUTION *event = &probe.Events[eventIndex];
      wprintf(L"BLT_EXEC seq=%u valid=%u status=0x%08x src_seg=%u dst_seg=%u src_addr=0x%llx dst_addr=0x%llx src_cpu=0x%llx dst_cpu=0x%llx src_pa=0x%llx dst_pa=0x%llx src_ipa=0x%llx dst_ipa=0x%llx src_local_va=0x%llx dst_local_va=0x%llx bytes=%llu clean=%u\n",
             event->Sequence, event->Valid, event->Status,
             event->SourceSegment, event->DestinationSegment,
             event->SourceAddress, event->DestinationAddress,
             event->SourceCpuAddress, event->DestinationCpuAddress,
             event->SourceHostPa, event->DestinationHostPa,
             event->SourceGuestIpa, event->DestinationGuestIpa,
             event->SourceLocalGpuVa, event->DestinationLocalGpuVa,
             event->BytesCopied, event->CacheCleanPerformed);
    }
  }
  HeapFree(GetProcessHeap(), 0, adapters);
  if (found)
    return 0;
  fputs("BLT_PROBE no Apple adapter answered the read-only escape\n", stderr);
  return 1;
}
