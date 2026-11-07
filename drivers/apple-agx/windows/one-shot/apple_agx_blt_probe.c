#include <windows.h>
#include <d3dkmthk.h>
#include <stdio.h>
#include <stdlib.h>
#include <wchar.h>

#include "render_qualification.h"
#include "../../shared/include/apple_agx_vsync.h"

/* RC49: read-only bounded capture. The KMD snapshots into nonpaged storage
 * at DIRQL. Keep all event kinds so an offline consumer can detect ring loss;
 * Sequence orders insertion, while kind 8 carries retrospective queue time. */
static int follow_vsync(D3DKMT_HANDLE adapter, unsigned seconds) {
  ULONGLONG end = GetTickCount64() + (ULONGLONG)seconds * 1000;
  ULONGLONG last = 0;
  UINT generation = 0;
  int captured = 0;
  LARGE_INTEGER frequency;
  (void)QueryPerformanceFrequency(&frequency);
  do {
    APPLE_AGX_VSYNC_QUERY q = {0};
    D3DKMT_ESCAPE e = {0};
    LARGE_INTEGER before, after;
    ULONGLONG interruptTime;
    NTSTATUS status;
    q.Magic = APPLE_AGX_VSYNC_QUERY_MAGIC;
    q.Version = 1u;
    q.Bytes = sizeof(q);
    e.hAdapter = adapter;
    e.Type = D3DKMT_ESCAPE_DRIVERPRIVATE;
    e.pPrivateDriverData = &q;
    e.PrivateDriverDataSize = sizeof(q);
    (void)QueryPerformanceCounter(&before);
    QueryInterruptTimePrecise(&interruptTime);
    (void)QueryPerformanceCounter(&after);
    status = D3DKMTEscape(&e);
    if (status < 0 || q.Magic != APPLE_AGX_VSYNC_QUERY_MAGIC ||
        q.Version != 1u || q.Bytes != sizeof(q)) {
      wprintf(L"VSYNC_FOLLOW_ERROR status=0x%x captured=%d\n", (UINT)status, captured);
      return captured ? 1 : 2;
    }
    captured = 1;
    if (q.Generation != generation) {
      generation = q.Generation;
      last = 0;
    }
    wprintf(L"VSYNC_SAMPLE generation=%u events=%llu qpc_before=%llu qpc_after=%llu qpc_hz=%llu time100ns=%llu\n",
            generation, q.EventCount, (ULONGLONG)before.QuadPart,
            (ULONGLONG)after.QuadPart, (ULONGLONG)frequency.QuadPart, interruptTime);
    for (UINT n = 0; n < APPLE_AGX_VSYNC_RECEIPT_CAPACITY; ++n) {
      const APPLE_AGX_VSYNC_EVENT *v = &q.Events[n];
      if (!v->Sequence || v->Sequence <= last) continue;
      wprintf(L"VSYNC_EVENT generation=%u seq=%llu time100ns=%llu kind=%u enabled=%u status=0x%x irql=%u period=%llu notify_ordinal=%llu pending_seq=%llu pending=0x%llx active_seq=%llu active=0x%llx\n",
              generation, v->Sequence, v->Time100ns, v->Kind, v->Enabled,
              v->Status, v->Irql, v->Period, v->NotifyOrdinal, v->PendingSequence,
              v->PendingAddress, v->ActiveSequence, v->ActiveAddress);
    }
    last = q.EventCount;
    fflush(stdout);
    if (GetTickCount64() >= end) break;
    Sleep(50);
  } while (GetTickCount64() < end);
  return 0;
}

int wmain(int argc, wchar_t **argv) {
  D3DKMT_ENUMADAPTERS2 enumeration = {0};
  D3DKMT_ADAPTERINFO *adapters;
  NTSTATUS status;
  UINT index;
  BOOL found = FALSE;
  unsigned followSeconds = 0;
  if (argc != 1) {
    wchar_t *end = NULL;
    unsigned long value = argc == 3 ? wcstoul(argv[2], &end, 10) : 0;
    if (argc != 3 || wcscmp(argv[1], L"--vsync-follow") ||
        end == argv[2] || *end || value == 0 || value > 600) {
      fprintf(stderr, "usage: apple_agx_blt_probe [--vsync-follow SECONDS(1..600)]\n");
      return 1;
    }
    followSeconds = (unsigned)value;
  }
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
  if (followSeconds) {
    int result = 2;
    for (index = 0u; index < enumeration.NumAdapters && result == 2; ++index)
      result = follow_vsync(adapters[index].hAdapter, followSeconds);
    for (index = 0u; index < enumeration.NumAdapters; ++index) {
      D3DKMT_CLOSEADAPTER close = {0};
      close.hAdapter = adapters[index].hAdapter;
      (void)D3DKMTCloseAdapter(&close);
    }
    HeapFree(GetProcessHeap(), 0, adapters);
    return result == 0 ? 0 : 1;
  }
  for (index = 0u; index < enumeration.NumAdapters; ++index) {
    D3DKMT_CLOSEADAPTER close = {0};
    D3DKMT_ESCAPE escape = {0};
    ADMISSION_BLT_PROBE probe = {0};
    ADMISSION_DWM_DDI_PROBE dwm = {0};
    ADMISSION_DWM_FRAME_PROBE frame = {0};
    APPLE_AGX_VSYNC_QUERY vsync = {0};
    UINT eventIndex;
    probe.Magic = ADMISSION_BLT_PROBE_MAGIC;
    probe.Version = ADMISSION_BLT_PROBE_VERSION;
    probe.Bytes = sizeof(probe);
    escape.hAdapter = adapters[index].hAdapter;
    escape.Type = D3DKMT_ESCAPE_DRIVERPRIVATE;
    escape.pPrivateDriverData = &probe;
    escape.PrivateDriverDataSize = sizeof(probe);
    status = D3DKMTEscape(&escape);
    wprintf(L"BLT_ADAPTER index=%u luid=%08x:%08x sources=%u escape_status=0x%08x\n",
            index, (UINT)adapters[index].AdapterLuid.HighPart,
            adapters[index].AdapterLuid.LowPart,
            adapters[index].NumOfSources, (UINT)status);
    {
      D3DKMT_ADAPTERTYPE adapterType = {0};
      D3DKMT_QUERYADAPTERINFO query = {0};
      DWORD sessionId = MAXDWORD;
      query.hAdapter = adapters[index].hAdapter;
      query.Type = KMTQAITYPE_ADAPTERTYPE;
      query.pPrivateDriverData = &adapterType;
      query.PrivateDriverDataSize = sizeof(adapterType);
      NTSTATUS typeStatus = D3DKMTQueryAdapterInfo(&query);
      (void)ProcessIdToSessionId(GetCurrentProcessId(), &sessionId);
      wprintf(L"BLT_ADAPTER_TYPE index=%u flags=0x%08x status=0x%08x pid=%lu session=%lu\n",
              index, adapterType.Value, (UINT)typeStatus,
              GetCurrentProcessId(), sessionId);
    }
    close.hAdapter = adapters[index].hAdapter;
    dwm.Magic = ADMISSION_DWM_DDI_PROBE_MAGIC;
    dwm.Version = ADMISSION_DWM_DDI_PROBE_VERSION;
    dwm.Bytes = sizeof(dwm);
    escape.pPrivateDriverData = &dwm;
    escape.PrivateDriverDataSize = sizeof(dwm);
    {
      NTSTATUS dwmStatus = D3DKMTEscape(&escape);
      if (dwmStatus >= 0 && dwm.Magic == ADMISSION_DWM_DDI_PROBE_MAGIC &&
          dwm.Version == ADMISSION_DWM_DDI_PROBE_VERSION &&
          dwm.Bytes == sizeof(dwm)) {
        UINT kind;
        found = TRUE;
        wprintf(L"DWM_DDI build=%u boot=%u incomplete=%u adapter=%u\n",
                dwm.CandidateBuild, dwm.BootGeneration, dwm.Incomplete, index);
        for (kind = 0u; kind < AdmissionDwmDdiCount; ++kind) {
          const ADMISSION_DWM_DDI_ENTRY *entry = &dwm.Entries[kind];
          wprintf(L"DWM_DDI_ENTRY kind=%u count=%ld dropped=%ld seq=%ld status=0x%08x flags=0x%x source=%u segment=%u src_count=%u dst_count=%u allocation=0x%llx address=0x%llx context=0x%llx fence=%llu pid=%llu tid=%llu allocations=%u\n",
                  kind, entry->Count, entry->Dropped, entry->Sequence,
                  entry->Last.Status, entry->Last.Flags, entry->Last.SourceId,
                  entry->Last.Segment, entry->Last.SourceCount,
                  entry->Last.DestinationCount, entry->Last.Allocation,
                  entry->Last.Address, entry->Last.Context, entry->Last.Fence, entry->Last.ProcessId,
              entry->Last.ThreadId, entry->Last.AllocationCount);
        }
      }
    }
    vsync.Magic = APPLE_AGX_VSYNC_QUERY_MAGIC;
    vsync.Version = 1u;
    vsync.Bytes = sizeof(vsync);
    escape.pPrivateDriverData = &vsync;
    escape.PrivateDriverDataSize = sizeof(vsync);
    {
      NTSTATUS vsyncStatus = D3DKMTEscape(&escape);
      wprintf(L"VSYNC_QUERY adapter=%u status=0x%x bytes=%u\n",
              index, (UINT)vsyncStatus, (UINT)sizeof(vsync));
      if (vsyncStatus >= 0 && vsync.Magic == APPLE_AGX_VSYNC_QUERY_MAGIC &&
          vsync.Version == 1u && vsync.Bytes == sizeof(vsync)) {
        wprintf(L"VSYNC_TIMELINE generation=%u events=%llu notified=%llu dpc=%llu ack_notify=%llu phase100ns=%llu active_seq=%llu active=0x%llx period_index=%llu rate=%u/%u enabled=%u stopping=%u running=%u paused=%u overwritten=%llu\n",
                vsync.Generation, vsync.EventCount, vsync.NotifyCount,
                vsync.DpcCount, vsync.AcknowledgedNotifyCount, vsync.Phase100ns, vsync.ActiveSequence,
                vsync.ActiveAddress, vsync.LastPeriod, vsync.RateNumerator,
                vsync.RateDenominator, vsync.Enabled, vsync.Stopping, vsync.Running, vsync.Paused,
                vsync.EventCount > 64ULL ? vsync.EventCount - 64ULL : 0ULL);
        for (UINT n = 0; n < APPLE_AGX_VSYNC_RECEIPT_CAPACITY; ++n) {
          const APPLE_AGX_VSYNC_EVENT *e = &vsync.Events[n];
          if (!e->Sequence) continue;
          wprintf(L"VSYNC_EVENT slot=%u seq=%llu time100ns=%llu kind=%u enabled=%u status=0x%x irql=%u period=%llu notify_ordinal=%llu pending_seq=%llu pending=0x%llx active_seq=%llu active=0x%llx\n",
                  n, e->Sequence, e->Time100ns, e->Kind, e->Enabled,
                  e->Status, e->Irql, e->Period, e->NotifyOrdinal, e->PendingSequence,
                  e->PendingAddress, e->ActiveSequence, e->ActiveAddress);
        }
      }
    }
    frame.Magic = ADMISSION_DWM_FRAME_PROBE_MAGIC;
    frame.Version = ADMISSION_DWM_FRAME_VERSION;
    frame.Bytes = sizeof(frame);
    escape.pPrivateDriverData = &frame;
    escape.PrivateDriverDataSize = sizeof(frame);
    {
      NTSTATUS frameStatus = D3DKMTEscape(&escape);
      wprintf(L"DWM_FRAME_QUERY adapter=%u status=0x%08x\n",
              index, (UINT)frameStatus);
      if (frameStatus >= 0 && frame.Magic == ADMISSION_DWM_FRAME_PROBE_MAGIC &&
          frame.Version == ADMISSION_DWM_FRAME_VERSION &&
          frame.Bytes == sizeof(frame)) {
        found = TRUE;
        wprintf(L"DWM_FRAME build=%u boot=%u armed=%u dropped=%u incomplete=%u\n",
                frame.CandidateBuild, frame.BootGeneration, frame.ArmedCount,
                frame.Dropped, frame.Incomplete);
        for (UINT frameIndex = 0u; frameIndex < ADMISSION_DWM_FRAME_CAPACITY;
             ++frameIndex) {
          const ADMISSION_DWM_FRAME_ENTRY *entry = &frame.Entries[frameIndex];
          if (entry->Context == 0ULL) continue;
          wprintf(L"DWM_FRAME_ENTRY index=%u pid=%u graph=%llu context=0x%llx alloc=0x%llx va=0x%llx query=%u predicate=%u query_status=0x%x resident_pages=%u submit=%u submit_status=0x%x branch=%u command_va=0x%llx submitted_fence=%llu complete=%u completed_fence=%llu present=%u present_status=0x%x virtual_present=%u virtual_status=0x%x src_alloc=0x%llx dst_alloc=0x%llx src_va=0x%llx dst_va=0x%llx dst_ipa=0x%llx present_fence=%llu copied=%llu copy_status=0x%x\n",
                  frameIndex, entry->OsProcessId, entry->GraphProcessId,
                  entry->Context, entry->Allocation, entry->CanonicalGpuVa,
                  entry->QueryCount, entry->QueryPredicate, entry->QueryStatus,
                  entry->QueryResidentPages, entry->SubmitCount,
                  entry->SubmitStatus, entry->SubmitBranch, entry->CommandGpuVa,
                  entry->SubmittedFence, entry->CompleteCount,
                  entry->CompletedFence, entry->PresentCount,
                  entry->PresentStatus, entry->VirtualPresentCount,
                  entry->VirtualPresentStatus, entry->SourceAllocation,
                  entry->DestinationAllocation, entry->SourceGpuVa,
                  entry->DestinationGpuVa, entry->DestinationGuestIpa,
                  entry->PresentFence, entry->CopiedBytes, entry->CopyStatus);
          const ADMISSION_DWM_ENVELOPE_RECEIPT *r = &entry->Envelope;
          wprintf(L"DWM_ENVELOPE context=0x%llx stage=%u predicate=%u runtime_predicate=%u irql=%u flags=0x%x context_state=0x%x fence=%u private=%u preempt=%u cancel=%u time100ns=%llu device=0x%llx manager_gen=%llu context_gen=%llu lease_manager=%llu lease_gen=%llu lease_scene=%llu lease_scene_gen=%llu scene_state=0x%x scene_fence=%u scene_gen=%llu\n",
              entry->Context,r->Stage,r->Predicate,r->RuntimePredicate,r->Irql,r->Flags,r->ContextState,
              r->Fence,r->PrivateFence,r->PreemptFence,r->CancelFence,
              r->InterruptTime,r->Device,r->ManagerGeneration,r->ContextGeneration,
              r->LeaseManagerId,r->LeaseManagerGeneration,r->LeaseSceneId,
              r->LeaseSceneGeneration,r->SceneState,r->SceneFence,r->SceneGeneration);
        }
        wprintf(L"DWM_FRAME_TDR captured=%u packet_state=%u context=0x%llx fence=%llu completed=%u submitted=%u active=%u paging=%u private_reset=0x%x reset=0x%x\n",
                frame.Tdr.Captured, frame.Tdr.PacketState,
                frame.Tdr.PacketContext, frame.Tdr.PacketFence,
                frame.Tdr.SchedulerCompletedFence,
                frame.Tdr.SchedulerLastSubmittedFence,
                frame.Tdr.SchedulerActiveFence, frame.Tdr.PagingPending,
                frame.Tdr.PrivateResetStatus, frame.Tdr.ResetStatus);
      }
    }
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
