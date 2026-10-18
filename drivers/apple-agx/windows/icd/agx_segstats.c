/* M2-f2c measurement: what VidMm keeps in each memory segment of every
 * adapter (D3DKMTQueryStatistics, D3DKMT_QUERYSTATISTICS_SEGMENT: CommitLimit,
 * BytesCommitted, BytesResident). Samples every 200 ms for the given seconds
 * and prints each segment's limit, peaks and last values -- the input for
 * sizing the firmware reserve (segment 2) and the KMD pool (segment 3).
 * Usage: agx_segstats.exe [seconds] (default 1: one sample) */
#include <windows.h>
#include <winternl.h>
#include <d3dkmthk.h>
#include <stdio.h>
#include <stdlib.h>

typedef NTSTATUS (APIENTRY *ENUM2)(D3DKMT_ENUMADAPTERS2 *);
typedef NTSTATUS (APIENTRY *STATS)(const D3DKMT_QUERYSTATISTICS *);

#define MAX_ADAPTERS 8u
#define MAX_SEGMENTS 8u

typedef struct {
  ULONGLONG Limit, PeakCommitted, PeakResident, LastCommitted, LastResident;
  ULONG Aperture;
  BOOL Valid;
} SEGMENT;

static BOOL segment_count(STATS stats, LUID luid, ULONG *count) {
  D3DKMT_QUERYSTATISTICS q;
  memset(&q, 0, sizeof(q));
  q.Type = D3DKMT_QUERYSTATISTICS_ADAPTER;
  q.AdapterLuid = luid;
  if (!NT_SUCCESS(stats(&q))) return FALSE;
  *count = q.QueryResult.AdapterInformation.NbSegments;
  return TRUE;
}

static BOOL sample(STATS stats, LUID luid, ULONG id, SEGMENT *s) {
  D3DKMT_QUERYSTATISTICS q;
  memset(&q, 0, sizeof(q));
  q.Type = D3DKMT_QUERYSTATISTICS_SEGMENT;
  q.AdapterLuid = luid;
  q.QuerySegment.SegmentId = id;
  if (!NT_SUCCESS(stats(&q))) return FALSE;
  s->Limit = q.QueryResult.SegmentInformation.CommitLimit;
  s->LastCommitted = q.QueryResult.SegmentInformation.BytesCommitted;
  s->LastResident = q.QueryResult.SegmentInformation.BytesResident;
  s->Aperture = q.QueryResult.SegmentInformation.Aperture;
  if (s->LastCommitted > s->PeakCommitted) s->PeakCommitted = s->LastCommitted;
  if (s->LastResident > s->PeakResident) s->PeakResident = s->LastResident;
  s->Valid = TRUE;
  return TRUE;
}

int main(int argc, char **argv) {
  HMODULE gdi = LoadLibraryA("gdi32.dll");
  ENUM2 enum2 = gdi ? (ENUM2)(void *)GetProcAddress(gdi, "D3DKMTEnumAdapters2") : NULL;
  STATS stats = gdi ? (STATS)(void *)GetProcAddress(gdi, "D3DKMTQueryStatistics") : NULL;
  D3DKMT_ENUMADAPTERS2 e;
  static SEGMENT seg[MAX_ADAPTERS][MAX_SEGMENTS];
  ULONG counts[MAX_ADAPTERS] = {0};
  LUID luids[MAX_ADAPTERS];
  int seconds = argc > 1 ? atoi(argv[1]) : 1;
  ULONG adapters, i, j, samples = 0;
  DWORD end;

  if (!enum2 || !stats) { printf("SEGSTATS NO_THUNKS\n"); return 2; }
  memset(&e, 0, sizeof(e));
  if (!NT_SUCCESS(enum2(&e)) || !e.NumAdapters) { printf("SEGSTATS NO_ADAPTERS\n"); return 3; }
  e.pAdapters = (D3DKMT_ADAPTERINFO *)calloc(e.NumAdapters, sizeof(D3DKMT_ADAPTERINFO));
  if (!e.pAdapters || !NT_SUCCESS(enum2(&e))) { printf("SEGSTATS ENUM_FAILED\n"); return 3; }
  adapters = e.NumAdapters < MAX_ADAPTERS ? e.NumAdapters : MAX_ADAPTERS;
  for (i = 0; i < adapters; ++i) {
    luids[i] = e.pAdapters[i].AdapterLuid;
    if (!segment_count(stats, luids[i], &counts[i])) counts[i] = 0;
    if (counts[i] > MAX_SEGMENTS) counts[i] = MAX_SEGMENTS;
  }
  if (seconds < 1) seconds = 1;
  end = GetTickCount() + (DWORD)seconds * 1000u;
  do {
    for (i = 0; i < adapters; ++i)
      for (j = 0; j < counts[i]; ++j) (void)sample(stats, luids[i], j, &seg[i][j]);
    ++samples;
    if ((LONG)(end - GetTickCount()) > 0) Sleep(200);
  } while ((LONG)(end - GetTickCount()) > 0);
  printf("SEGSTATS samples %lu seconds %d adapters %lu\n", samples, seconds, adapters);
  for (i = 0; i < adapters; ++i) {
    printf("ADAPTER %lu luid %08lx:%08lx segments %lu\n", i,
           (unsigned long)luids[i].HighPart, (unsigned long)luids[i].LowPart, counts[i]);
    for (j = 0; j < counts[i]; ++j) {
      const SEGMENT *s = &seg[i][j];
      if (!s->Valid) { printf("  SEGMENT %lu query failed\n", j); continue; }
      printf("  SEGMENT %lu aperture %lu limit %llu MiB committed peak %llu MiB last %llu MiB"
             " resident peak %llu MiB last %llu MiB\n", j, s->Aperture,
             s->Limit >> 20, s->PeakCommitted >> 20, s->LastCommitted >> 20,
             s->PeakResident >> 20, s->LastResident >> 20);
    }
  }
  return 0;
}
