# D3D12 C11: query heaps (occlusion, timestamps, pipeline statistics)

Status 2026-10-17. Query heaps were fail-closed; applications create
timestamp heaps for GPU timing and occlusion heaps for visibility.

Sources inspected: WDK 26100 d3d12umddi.h (D3D12DDIARG_CREATE_QUERY_HEAP_0001
{Type, Count, NodeMask}, D3D12DDI_QUERY_HEAP_TYPE, D3D12DDI_QUERY_TYPE,
PFND3D12DDI_BEGIN_END_QUERY_0003 (list, heap, type, index),
PFND3D12DDI_RESOLVE_QUERY_DATA (list, heap, type, start, count, buffer,
offset), D3D12DDI_QUERY_DATA_PIPELINE_STATISTICS (11 UINT64, the Gallium
PIPE_STAT_QUERY order)); Mesa asahi agx_query.c (occlusion counters and
predicates in the occlusion heap, TIMESTAMP via batch timestamps with a
CPU-copy resolve, PIPELINE_STATISTICS_SINGLE per counter,
get_query_result_resource copying on the GPU with libagx_copy_query_gl,
destroy_query syncing occlusion writers); the repository's winsys
(agx_win32_gpuva_batch.c: firmware batch timestamps, agx_get_gpu_timestamp
in nanoseconds); the KMD's AdmissionDdiCalibrateGpuClock (STATUS_NOT_SUPPORTED).

## WINDOWS CONTRACT

- Occlusion heaps serve OCCLUSION (sample count) and BINARY_OCCLUSION
  (0/1) queries; timestamp heaps take EndQuery only; Resolve writes UINT64
  values (statistics: the 88-byte struct) into a buffer at an 8-byte
  aligned offset, in list order.
- GetTimestampFrequency / GetClockCalibration report the timestamp rate --
  the runtime's source for them is not documented (measured by C11C).

## AGX/ASAHI CONTRACT

- create_query(OCCLUSION_COUNTER | OCCLUSION_PREDICATE | TIMESTAMP |
  PIPELINE_STATISTICS_SINGLE i), begin_query/end_query in context order,
  get_query_result_resource(WAIT, U64) to a buffer.

## TRANSLATION

- A query heap record {type, count, owner heap}; the owner heap creates
  Gallium queries on first use (occlusion 2 slots per index: counter and
  predicate; timestamp 1; statistics 11) and destroys them with the heap.
- Begin/End/Resolve are list commands; Resolve writes each value with
  get_query_result_resource, zero for queries never used.
- Video, SO and copy-queue timestamp heaps refuse (E_NOTIMPL).

## WHAT IS STILL UNKNOWN

- The runtime's timestamp frequency source (the KMD clock calibration
  returns NOT_SUPPORTED today) and whether Asahi's statistics counters are
  populated on this firmware -- C11B/C11C print them.

## Checkpoint

Probe C11 (fresh device, mode 3 of the C6b draw): occlusion + binary
occlusion + statistics queries around the triangle between two
timestamps, all resolved to a READBACK buffer. C11A PASS = occlusion ==
drawn pixels and binary == 1; C11B PASS = IAVertices 3, IAPrimitives 1;
C11C PASS = t0 != 0, t1 >= t0 and GetTimestampFrequency S_OK non-zero.

## C11d: KMD clock calibration (after EXP1373)

- Measured (EXP1373): GetTimestampFrequency and GetClockCalibration
  returned E_NOTIMPL with no UMD DDI call -- the runtime's source is
  D3DKMTQueryClockCalibration ("QueryClockCalibration" in d3d12core;
  D3DKMT_QUERYCLOCKCALIBRATION carries DXGK_GPUCLOCKDATA), i.e. the KMD's
  DxgkDdiCalibrateGpuClock, which returned STATUS_NOT_SUPPORTED.
- Contract: DXGKARG_CALIBRATEGPUCLOCK = DXGK_GPUCLOCKDATA {GpuFrequency,
  GpuClockCounter, CpuClockCounter}, IRQL <= DISPATCH_LEVEL. The firmware
  timebase runs at the system-counter frequency (EXP1112) and the UMD's
  Gallium timestamps are nanoseconds of it (agx_get_gpu_timestamp), so the
  KMD reports GpuFrequency 1e9 and GpuClockCounter = (QPC + the newest
  job's End - Qpc offset) in nanoseconds, CpuClockCounter = QPC
  (AdmissionGpuClockCalibrate, host-tested; node 0 / engine 0 only).
- UNKNOWN: whether the 26100 runtime then reports the frequency to
  applications unchanged -- probe C11C.

## EXP1375 result notes

- GetTimestampFrequency 1e9 / GetClockCalibration S_OK through C11d;
  statistics populated (IA 3/1, VS 3, C 1/1, PS 512); binary occlusion 1.
- Asahi keeps one active occlusion query per context (agx_begin_query sets
  ctx->occlusion_query): overlapping occlusion and binary-occlusion queries
  leave the earlier one at 0 (the probe overlapped them; the counter read
  0). The probe now runs them sequentially; overlapping occlusion queries
  remain a known limitation of this translation.
- t0 == t1: both EndQuery(TIMESTAMP) fell into the same render batch (the
  clear had opened it); Asahi resolves a timestamp to MAX(its CPU value,
  the batch's firmware end time) at batch cleanup (agx_finish_batch_queries;
  the winsys fills vtx_start/frag_end and comp_start/comp_end from the KMD
  job record, agx_win32_gpuva_batch.c write_timestamps). Timestamps are GPU
  time at batch granularity, not per command.
