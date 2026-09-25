#include <windows.h>
#include <d3dkmthk.h>
#include <stdio.h>

#include "render_qualification.h"

int wmain(void) {
  DWORD index;
  for (index = 0u; index < 32u; ++index) {
    DISPLAY_DEVICEW display = {0};
    D3DKMT_OPENADAPTERFROMHDC open = {0};
    D3DKMT_CLOSEADAPTER close = {0};
    D3DKMT_ESCAPE escape = {0};
    ADMISSION_BLT_PROBE probe = {0};
    HDC dc;
    NTSTATUS status;
    UINT eventIndex;
    display.cb = sizeof(display);
    if (!EnumDisplayDevicesW(NULL, index, &display, 0))
      break;
    if ((display.StateFlags & DISPLAY_DEVICE_ATTACHED_TO_DESKTOP) == 0 ||
        (display.StateFlags & DISPLAY_DEVICE_MIRRORING_DRIVER) != 0)
      continue;
    dc = CreateDCW(display.DeviceName, display.DeviceName, NULL, NULL);
    if (dc == NULL)
      continue;
    open.hDc = dc;
    status = D3DKMTOpenAdapterFromHdc(&open);
    if (status < 0 || open.hAdapter == 0u) {
      DeleteDC(dc);
      continue;
    }
    probe.Magic = ADMISSION_BLT_PROBE_MAGIC;
    probe.Version = ADMISSION_BLT_PROBE_VERSION;
    probe.Bytes = sizeof(probe);
    escape.hAdapter = open.hAdapter;
    escape.Type = D3DKMT_ESCAPE_DRIVERPRIVATE;
    escape.pPrivateDriverData = &probe;
    escape.PrivateDriverDataSize = sizeof(probe);
    status = D3DKMTEscape(&escape);
    close.hAdapter = open.hAdapter;
    (void)D3DKMTCloseAdapter(&close);
    DeleteDC(dc);
    if (status < 0 || probe.Magic != ADMISSION_BLT_PROBE_MAGIC ||
        probe.Version != ADMISSION_BLT_PROBE_VERSION ||
        probe.Bytes != sizeof(probe))
      continue;
    wprintf(L"BLT_PROBE display=%ls build=%u boot=%u adapter=0x%llx present=%u present_blt=%u virtual_submit=%u physical_present_submit=%u cpu_blt=%u events=%u overflow=%u\n",
            display.DeviceName, probe.CandidateBuild, probe.BootGeneration,
            probe.AdapterToken,
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
    return 0;
  }
  fputs("BLT_PROBE no Apple display adapter answered the read-only escape\n",
        stderr);
  return 1;
}
