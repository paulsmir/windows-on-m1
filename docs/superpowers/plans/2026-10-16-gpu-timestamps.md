# GPU timestamps (D3D TIMESTAMP queries, Mesa batch timestamps)

Status: design, 2026-10-16. Stage T1 (compute) first.

## Current behaviour

- Mesa `agx_end_query(PIPE_QUERY_TIMESTAMP)` is projected to `return false`
  (native-asahi-batch-lifecycle.py); the D3D frontend emulates TIMESTAMP with
  the CPU counter at fence completion and reports every TIMESTAMPDISJOINT as
  `Disjoint = TRUE` (build-native-asahi-state.py, D3D-H5). Applications work
  but discard the values.
- winsys refuses any batch whose render/compute command carries `ts.*`
  handles (agx_win32_gpuva_batch.c); the G4 parser returns Unsupported for
  compute `Timestamps` (apple_agx_g4_submit.c).

## WINDOWS CONTRACT

- D3D10/11 `D3D10DDI_QUERY_TIMESTAMP`: a UINT64 GPU tick value written when
  all previous commands finished; `D3D10DDI_QUERY_TIMESTAMPDISJOINT`:
  `{Frequency, Disjoint}`; Disjoint FALSE only when the tick frequency was
  constant over the interval.
- The UMD reads results after the query's fence; delivery from the KMD may go
  through `pfnEscapeCb` (already used by the UMD, umd_gpuva_windows.c).

## AGX/ASAHI CONTRACT (primary sources)

- `drm_asahi_cmd_compute.ts` / `drm_asahi_cmd_render.ts_vtx|ts_frag`:
  `{handle, offset}` into a timestamp buffer object (queue/common.rs
  `get_timestamp_object`), mapped into the **kernel (firmware) VM**
  (`gpu.rs map_timestamp_buffer`, IOVA_KERN_TIMESTAMP_RANGE,
  PROT_FW_SHARED_RW): the firmware writes the values.
- Microsequence (queue/compute.rs 164-200, render.rs 525-556, 1036-1067):
  when any user timestamp is requested, `Timestamp(start)` before and
  `Timestamp(end)` after the stage's `WaitForIdle`.
  `microseq::op::Timestamp::new(flag)` = opcode 0x19 | flag << 31.
  `Timestamp::ver<G13,V13_5>` (packed, 0x3c bytes): header u32,
  command_time ptr, ts_pointers ptr, update_ts ptr (address of
  timestamp_pointers.start_addr / end_addr), work_queue (queue info),
  user_ts_pointers ptr, unk_ts ptr (= &context_store_req), uuid u32 (0),
  pad u32.
- RunCompute (fw/compute.rs; offsets pinned by
  apple_agx_g13_compute_work_test.c): command_time 0x2b0,
  timestamp_pointers {start,end} 0x2b8/0x2c0 (job's own JobTimestamps),
  user_timestamp_pointers 0x2c8/0x2d0, context_store_req 0x2e1.
- Ticks: Mesa converts with `agx_gpu_time_to_ns` (device timer frequency).

## TRANSLATION

1. Firmware writes into KMD-owned firmware-visible memory: the compute
   sidecar gains a timestamp block (job start/end + user start/end); the
   work's timestamp_pointers and user_timestamp_pointers point there. No
   process memory is mapped into the kernel VM.
2. The G13 microsequence builder takes an optional timestamp request and
   inserts the two Timestamp ops around WaitForIdle(Compute), moving
   FinalizeCompute (its back offset to StartCompute is recomputed) and
   RetireStamp. The sidecar layout must grow the microsequence slot
   (0x8400..0x8600 holds 0x200 bytes; with timestamps 0x268).
3. At completion of the fence the KMD copies the user timestamp values into
   a per-process ring `{fence, start, end}` (the sidecar is reused by the
   next job).
4. A new escape returns the record for a fence. The winsys, when it retires
   a batch that requested timestamps, fetches it and writes start/end into
   the Mesa timestamp BO's CPU copy at the requested offsets; the projected
   `agx_end_query(PIPE_QUERY_TIMESTAMP)` path is restored.
5. D3D TIMESTAMP returns ticks; TIMESTAMPDISJOINT returns the timer
   frequency and Disjoint FALSE when every timestamp in the interval came
   from the GPU.
6. Render (ts_vtx / ts_frag) follows the same pattern as T2.

## WHAT IS STILL UNKNOWN

- Whether the G13 13.5 firmware writes the user timestamps with this op
  layout and pointer placement (hardware behaviour).
- The tick frequency the firmware uses on J313 (expected the 24 MHz system
  counter; to be measured against QueryPerformanceCounter).

## Stages

- T1 compute: builder + sidecar + G4/winsys admission + KMD capture +
  escape + winsys write-back; offline tests for the microsequence layout,
  sidecar bounds, parser and the ring; hardware discriminator: a compute
  smoke that requests start/end timestamps around a known-length dispatch
  (nonzero, end > start, within the CPU interval).
- T2 render (vertex/fragment).
- T3 D3D TIMESTAMP/DISJOINT on real values.
