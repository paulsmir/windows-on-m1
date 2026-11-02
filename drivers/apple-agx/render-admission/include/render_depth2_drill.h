#ifndef APPLE_AGX_RENDER_DEPTH2_DRILL_H
#define APPLE_AGX_RENDER_DEPTH2_DRILL_H
#include "apple_agx_g13_queue_runtime.h"

/* Wom1Depth2DrillReceipt v1: all little-endian u32, pinned for this boot.
 * Mode remains the consumed request; Queue.Target is nonzero only on injection.
 * Attempts saturates at 64. Reports are AFTER the WDDM interrupt transaction,
 * separate from the queue-runtime terminal verdicts. No ring displacement. */
typedef struct _ADMISSION_DEPTH2_DRILL_RECEIPT {
  unsigned int Version, Bytes, Mode, Attempts;
  unsigned int ProcessId, QpcLow, QpcHigh, HeadFlight, NextFlight;
  unsigned int RunningKind, NextKind, Barrier;
  unsigned int ReportedCount, ReportFence[2], ReportStatus[2], ReportOverflow;
  APPLE_AGX_G13_DEPTH2_DRILL Queue;
} ADMISSION_DEPTH2_DRILL_RECEIPT;

static __inline void AdmissionDepth2DrillReport(
    ADMISSION_DEPTH2_DRILL_RECEIPT *r, unsigned int fence, unsigned int status) {
  unsigned int i;
  if (!fence || (fence != r->Queue.HeadFence && fence != r->Queue.NextFence))
    return;
  for (i = 0u; i < r->ReportedCount; ++i)
    if (r->ReportFence[i] == fence) {
      if (r->ReportOverflow != ~0u) ++r->ReportOverflow;
      return;
    }
  if (r->ReportedCount == 2u) {
    if (r->ReportOverflow != ~0u) ++r->ReportOverflow;
    return;
  }
  i = r->ReportedCount++;
  r->ReportFence[i] = fence;
  r->ReportStatus[i] = status;
}
#endif
