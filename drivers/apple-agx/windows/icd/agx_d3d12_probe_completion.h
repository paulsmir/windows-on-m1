#ifndef AGX_D3D12_PROBE_COMPLETION_H
#define AGX_D3D12_PROBE_COMPLETION_H
/* Read-only APTS v3 wire ABI from render-admission/include/render_job_timing.h.
 * The standalone probe build stages probe_*.h only. Keep this narrow wire
 * declaration in that closure; test_d3d12_rc_pair_probe checks every offset,
 * field size, constant and total size against the KMD on all three ABIs. */
#define Q3_GPU_TIMESTAMP_QUERY_MAGIC 0x53545041u
#define Q3_GPU_COMPLETION_VERSION 3u
#define Q3_GPU_COMPLETION_PAGE 32u
#define Q3_GPU_TIMESTAMP_RENDER 1u
#define Q3_GPU_TIMESTAMP_COMPUTE 2u
typedef struct _Q3_GPU_COMPLETION {
  unsigned long long Sequence, CommandVa, SubmitQpc, CompleteQpc;
  unsigned long long Start, End, TaStart, TaEnd;
  unsigned long long PreludeStart, PreludeEnd;
  unsigned int Fence, ProcessId, Kind, Status;
  unsigned int TvbOverflows, TvbMaxPages;
} Q3_GPU_COMPLETION;
typedef struct _Q3_GPU_COMPLETION_QUERY {
  unsigned int Magic, Version, Bytes, Count;
  unsigned long long Cursor, Newest, Lost, Frequency, MissingTiming;
  unsigned int Reset, Reserved;
  Q3_GPU_COMPLETION Record[Q3_GPU_COMPLETION_PAGE];
} Q3_GPU_COMPLETION_QUERY;
#endif
