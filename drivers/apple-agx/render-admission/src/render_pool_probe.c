#include "render_pool_probe.h"

#define ADMISSION_POOL_FRAME 0x1000ULL

void AdmissionPoolAnalyzeFrames(const unsigned long long *Frames,
                                unsigned long long FrameCount,
                                ADMISSION_POOL_ATTEMPT *Attempt) {
  unsigned long long index, run_start = 0ULL, run_frames = 0ULL;
  if (Attempt == (void *)0) return;
  Attempt->ObtainedBytes = 0ULL;
  Attempt->LowestPhysical = Attempt->HighestPhysical = 0ULL;
  Attempt->LargestRunBytes = 0ULL;
  Attempt->Runs = Attempt->Misaligned = 0u;
  if (Frames == (void *)0 || FrameCount == 0ULL) return;
  Attempt->ObtainedBytes = FrameCount * ADMISSION_POOL_FRAME;
  for (index = 0ULL; index <= FrameCount; ++index) {
    /* A run ends at the array end or where the next frame is not adjacent. */
    if (index < FrameCount && run_frames != 0ULL &&
        Frames[index] == run_start + run_frames) {
      ++run_frames;
      continue;
    }
    if (run_frames != 0ULL) {
      unsigned long long base = run_start * ADMISSION_POOL_FRAME;
      unsigned long long bytes = run_frames * ADMISSION_POOL_FRAME;
      if (Attempt->Runs != 0xffffffffu) ++Attempt->Runs;
      if ((base & (ADMISSION_POOL_UAT_PAGE - 1ULL)) != 0ULL &&
          Attempt->Misaligned != 0xffffffffu)
        ++Attempt->Misaligned;
      if (bytes > Attempt->LargestRunBytes) Attempt->LargestRunBytes = bytes;
      if (Attempt->Runs == 1u || base < Attempt->LowestPhysical)
        Attempt->LowestPhysical = base;
      if (base + bytes > Attempt->HighestPhysical)
        Attempt->HighestPhysical = base + bytes;
    }
    if (index < FrameCount) {
      run_start = Frames[index];
      run_frames = 1ULL;
    }
  }
}
