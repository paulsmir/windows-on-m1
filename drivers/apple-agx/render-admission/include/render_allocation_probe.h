#ifndef APPLE_AGX_RENDER_ALLOCATION_PROBE_H
#define APPLE_AGX_RENDER_ALLOCATION_PROBE_H

#include "render_win32_transport.h"

/* R105 uses the two reserved DWORDs of the existing 72-byte private request.
 * Ordinary UMD requests leave them zero; production validation still rejects
 * all nonzero reserved fields. No ABI size or version changes. */
#define ADMISSION_R105_MAGIC 0x35303152u /* R105 */
#define ADMISSION_R105_MODE_MASK 0x0000ff3fu

typedef struct _ADMISSION_R105_OVERRIDE {
  APPLE_AGX_U32 Token;
  APPLE_AGX_U32 ReadMode;
  APPLE_AGX_U32 AccessedPhysically;
  APPLE_AGX_U32 PageMode;
  APPLE_AGX_U32 CloneClass0;
} ADMISSION_R105_OVERRIDE;

typedef struct _ADMISSION_R105_ECHO {
  APPLE_AGX_U32 Version, Bytes, Token, ClassId;
  APPLE_AGX_U32 RequestBits, AlignmentOrPages;
  APPLE_AGX_U64 Size, PitchAlignedSize;
  APPLE_AGX_U32 PreferredSegment, HintedBank;
  APPLE_AGX_U32 ReadOrMmuSet, WriteSegmentSet, EvictionSegmentSet;
  APPLE_AGX_U32 FlagsWddm2, AllocationPriority, Flags2;
  APPLE_AGX_U32 PhysicalAdapterIndex, OutputPresent;
} ADMISSION_R105_ECHO;

static __inline APPLE_AGX_BOOL AdmissionR105Decode(
    const void *PrivateData, APPLE_AGX_U32 PrivateBytes,
    APPLE_AGX_BOOL QualificationEnabled, ADMISSION_R105_OVERRIDE *Result) {
  const ADMISSION_WIN32_ALLOCATION_CREATE *request =
      (const ADMISSION_WIN32_ALLOCATION_CREATE *)PrivateData;
  APPLE_AGX_U32 bits;
  if (!QualificationEnabled || request == 0 || Result == 0 ||
      PrivateBytes != sizeof(*request) ||
      request->Magic != ADMISSION_WIN32_ALLOCATION_MAGIC ||
      request->Version != ADMISSION_WIN32_ALLOCATION_VERSION ||
      request->Bytes != sizeof(*request) ||
      request->Reserved[0] != ADMISSION_R105_MAGIC)
    return 0;
  bits = request->Reserved[1];
  if ((bits & ~ADMISSION_R105_MODE_MASK) != 0u ||
      (bits & 3u) > 2u || ((bits >> 3) & 3u) > 2u ||
      (((bits >> 5) & 1u) != 0u && (bits & 0x1fu) != 0u))
    return 0;
  Result->Token = (bits >> 8) & 0xffu;
  Result->ReadMode = bits & 3u;
  Result->AccessedPhysically = (bits >> 2) & 1u;
  Result->PageMode = (bits >> 3) & 3u;
  Result->CloneClass0 = (bits >> 5) & 1u;
  return 1;
}

#endif
