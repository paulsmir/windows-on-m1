#include "agx_win32_native_pool_bridge.h"

#include <stddef.h>
#include <stdint.h>

AGX_WIN32_RELOC_RESULT AgxWin32NativePoolReference(
    AGX_WIN32_RELOC_CAPTURE *Capture,
    const AGX_WIN32_NATIVE_POOL_SLICE *Slice, APPLE_AGX_U32 Role,
    APPLE_AGX_U32 Access, APPLE_AGX_U32 *Index) {
  AGX_WIN32_RELOC_ALLOCATION allocation;
  uintptr_t cpuBase;
  uintptr_t sliceCpu;
  APPLE_AGX_U64 offset;

  if (Capture == NULL || Slice == NULL || Index == NULL ||
      Slice->Owner == 0ULL || Slice->Token == 0ULL || Slice->Serial == 0ULL ||
      Slice->Generation == 0u || Slice->CpuBase == NULL ||
      Slice->SliceCpu == NULL || Slice->ConstructionBase == 0ULL ||
      Slice->SliceConstruction == 0ULL || Slice->BoBytes == 0ULL ||
      Slice->SliceBytes == 0ULL || Capture->Context == NULL ||
      Capture->Operations.Query == NULL)
    return AgxRelocArgument;
  if (Capture->Owner != Slice->Owner ||
      Capture->Generation != Slice->Generation)
    return AgxRelocStale;

  cpuBase = (uintptr_t)Slice->CpuBase;
  sliceCpu = (uintptr_t)Slice->SliceCpu;
  if (sliceCpu < cpuBase || (APPLE_AGX_U64)(sliceCpu - cpuBase) > Slice->BoBytes ||
      Slice->SliceBytes > Slice->BoBytes - (APPLE_AGX_U64)(sliceCpu - cpuBase) ||
      Slice->SliceConstruction < Slice->ConstructionBase ||
      Slice->SliceConstruction - Slice->ConstructionBase !=
          (APPLE_AGX_U64)(sliceCpu - cpuBase))
    return AgxRelocRange;
  offset = Slice->SliceConstruction - Slice->ConstructionBase;

  if (!Capture->Operations.Query(Capture->Context, Slice->Token, &allocation))
    return AgxRelocCallback;
  if (allocation.Owner != Slice->Owner || allocation.Token != Slice->Token ||
      allocation.Serial != Slice->Serial ||
      allocation.Generation != Slice->Generation ||
      allocation.Bytes != Slice->BoBytes)
    return AgxRelocStale;

  return AgxWin32RelocReferenceExpected(Capture, &allocation, Role, Access,
                                        offset, Slice->SliceBytes, Index);
}
