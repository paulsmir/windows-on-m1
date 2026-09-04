#ifndef APPLE_AGX_GDI_H
#define APPLE_AGX_GDI_H

#include "apple_agx_state.h"
#include "apple_agx_memory.h"

#define APPLE_AGX_GDI_COMMAND_HEADER_SIZE 8u
#define APPLE_AGX_GDI_MEMORY_ALIGNMENT 0x10000ULL
#define APPLE_AGX_GDI_DMA_MAGIC 0x44494741u /* "AGID" */
#define APPLE_AGX_GDI_DMA_VERSION 1u

typedef enum _APPLE_AGX_GDI_OPCODE {
  AppleAgxGdiBitBlt = 1,
  AppleAgxGdiColorFill = 2,
  AppleAgxGdiAlphaBlend = 3,
  AppleAgxGdiStretchBlt = 4,
  AppleAgxGdiEscape = 5,
  AppleAgxGdiTransparentBlt = 6,
  AppleAgxGdiClearTypeBlend = 7,
} APPLE_AGX_GDI_OPCODE;

typedef enum _APPLE_AGX_GDI_BITBLT_ROP {
  AppleAgxGdiBitBltSrcCopy = 1,
  AppleAgxGdiBitBltSrcInvert = 2,
  AppleAgxGdiBitBltSrcAnd = 3,
  AppleAgxGdiBitBltSrcOr = 4,
  AppleAgxGdiBitBltRop3 = 5,
} APPLE_AGX_GDI_BITBLT_ROP;

typedef enum _APPLE_AGX_GDI_COLORFILL_ROP {
  AppleAgxGdiColorFillPatCopy = 1,
  AppleAgxGdiColorFillPatInvert = 2,
  AppleAgxGdiColorFillPdxn = 3,
  AppleAgxGdiColorFillDstInvert = 4,
  AppleAgxGdiColorFillPatAnd = 5,
  AppleAgxGdiColorFillPatOr = 6,
  AppleAgxGdiColorFillRop3 = 7,
} APPLE_AGX_GDI_COLORFILL_ROP;

#define APPLE_AGX_GDI_OPCODE_BIT(Value) (1u << (Value))
#define APPLE_AGX_GDI_MINIMUM_OPCODE_MASK                                  \
  (APPLE_AGX_GDI_OPCODE_BIT(AppleAgxGdiBitBlt) |                           \
   APPLE_AGX_GDI_OPCODE_BIT(AppleAgxGdiColorFill) |                        \
   APPLE_AGX_GDI_OPCODE_BIT(AppleAgxGdiAlphaBlend) |                       \
   APPLE_AGX_GDI_OPCODE_BIT(AppleAgxGdiStretchBlt) |                       \
   APPLE_AGX_GDI_OPCODE_BIT(AppleAgxGdiTransparentBlt) |                   \
   APPLE_AGX_GDI_OPCODE_BIT(AppleAgxGdiClearTypeBlend))
#define APPLE_AGX_GDI_MINIMUM_BITBLT_ROP_MASK 0x1eu
#define APPLE_AGX_GDI_MINIMUM_COLORFILL_ROP_MASK 0x7eu
#define APPLE_AGX_GDI_EXCLUDE_ALL_SAME_BITMAP_VARIANTS 0x7fu

#define APPLE_AGX_GDI_PRIMITIVE_DESTINATION_WRITE (1u << 0)
#define APPLE_AGX_GDI_PRIMITIVE_DESTINATION_READ (1u << 1)
#define APPLE_AGX_GDI_PRIMITIVE_SOURCE_READ (1u << 2)
#define APPLE_AGX_GDI_PRIMITIVE_NEAREST_SAMPLE (1u << 3)
#define APPLE_AGX_GDI_PRIMITIVE_BOOLEAN_ROP (1u << 4)
#define APPLE_AGX_GDI_PRIMITIVE_SOURCE_ALPHA_BLEND (1u << 5)
#define APPLE_AGX_GDI_PRIMITIVE_COLOR_KEY (1u << 6)
#define APPLE_AGX_GDI_PRIMITIVE_CLEARTYPE (1u << 7)
#define APPLE_AGX_GDI_PRIMITIVE_AUXILIARY_SURFACES (1u << 8)

typedef struct _APPLE_AGX_GDI_CAPS_PROFILE {
  APPLE_AGX_U32 OpcodeMask;
  APPLE_AGX_U32 BitBltRopMask;
  APPLE_AGX_U32 ColorFillRopMask;
  APPLE_AGX_U32 ExcludedVariantMask;
  APPLE_AGX_BOOL SupportKernelModeCommandBuffer;
  APPLE_AGX_BOOL CacheCoherentAperture;
  APPLE_AGX_BOOL NoCacheCoherentApertureMemory;
  APPLE_AGX_BOOL SupportAllBltRops;
  APPLE_AGX_BOOL SupportMirrorStretchBlt;
  APPLE_AGX_BOOL SupportMonoStretchBltModes;
  APPLE_AGX_BOOL NoTempSurfaceForClearTypeBlend;
} APPLE_AGX_GDI_CAPS_PROFILE;

typedef struct _APPLE_AGX_GDI_SURFACE {
  APPLE_AGX_U32 Width;
  APPLE_AGX_U32 Height;
  APPLE_AGX_U32 BytesPerPixel;
  APPLE_AGX_U32 Pitch;
  APPLE_AGX_U64 Size;
} APPLE_AGX_GDI_SURFACE;

/*
 * Pointer-free command representation owned by the KMD.  WDDM's
 * DXGK_RENDERKM_COMMAND contains a kernel pointer to its sub-rectangle list;
 * that pointer must never be copied into a DMA buffer consumed later at
 * submit time.  Every record is self describing and is followed by
 * SubRectCount inline APPLE_AGX_GDI_RECT values.
 */
typedef struct _APPLE_AGX_GDI_RECT {
  APPLE_AGX_U32 Left;
  APPLE_AGX_U32 Top;
  APPLE_AGX_U32 Right;
  APPLE_AGX_U32 Bottom;
} APPLE_AGX_GDI_RECT;

typedef struct _APPLE_AGX_GDI_DMA_COMMAND {
  APPLE_AGX_U32 Magic;
  APPLE_AGX_U32 Version;
  APPLE_AGX_U32 RecordBytes;
  APPLE_AGX_U32 Opcode;
  APPLE_AGX_U32 SubRectCount;
  APPLE_AGX_U32 Reserved;
  APPLE_AGX_GDI_RECT Source;
  APPLE_AGX_GDI_RECT Destination;
  APPLE_AGX_U32 SourceAllocationIndex;
  APPLE_AGX_U32 DestinationAllocationIndex;
  APPLE_AGX_U32 TemporaryAllocationIndex;
  APPLE_AGX_U32 GammaAllocationIndex;
  APPLE_AGX_U32 AlphaAllocationIndex;
  APPLE_AGX_U64 SourceGpuAddress;
  APPLE_AGX_U64 DestinationGpuAddress;
  APPLE_AGX_U64 TemporaryGpuAddress;
  APPLE_AGX_U64 GammaGpuAddress;
  APPLE_AGX_U64 AlphaGpuAddress;
  APPLE_AGX_U32 Flags;
  APPLE_AGX_U32 Rop;
  APPLE_AGX_U32 Rop3;
  APPLE_AGX_U32 Color;
  APPLE_AGX_U32 Color2;
  APPLE_AGX_U32 Gamma;
  APPLE_AGX_U32 SourcePitch;
  APPLE_AGX_U32 DestinationPitch;
  APPLE_AGX_U32 DestinationToAlphaOffsetX;
  APPLE_AGX_U32 DestinationToAlphaOffsetY;
  APPLE_AGX_U32 SourceConstantAlpha;
  APPLE_AGX_U32 SourceHasAlpha;
} APPLE_AGX_GDI_DMA_COMMAND;

typedef struct _APPLE_AGX_GDI_COMMAND_DESCRIPTION {
  APPLE_AGX_GDI_DMA_COMMAND Command;
  const APPLE_AGX_GDI_RECT *SubRects;
} APPLE_AGX_GDI_COMMAND_DESCRIPTION;

typedef struct _APPLE_AGX_GDI_LOWERING_RECEIPT {
  APPLE_AGX_U64 StreamHash;
  APPLE_AGX_U32 StreamBytes;
  APPLE_AGX_U32 CommandCount;
  APPLE_AGX_U32 OperationMask;
  APPLE_AGX_U32 RequiredPrimitiveMask;
} APPLE_AGX_GDI_LOWERING_RECEIPT;

typedef struct _APPLE_AGX_GDI_MEMORY_POOL {
  APPLE_AGX_MEMORY_OBJECT Object;
  APPLE_AGX_BOOL Active;
} APPLE_AGX_GDI_MEMORY_POOL;

/*
 * Describes the minimum contract the implementation would have to satisfy;
 * it is not evidence that the current hardware/backend satisfies that
 * contract and must not be copied directly into DXGK_PRESENTATIONCAPS.
 */
void AppleAgxGdiMinimumCapsProfile(APPLE_AGX_GDI_CAPS_PROFILE *Profile);
APPLE_AGX_BOOL AppleAgxGdiCapsProfileValid(
    const APPLE_AGX_GDI_CAPS_PROFILE *Profile);
APPLE_AGX_BOOL AppleAgxGdiCapsSupportsOperation(
    const APPLE_AGX_GDI_CAPS_PROFILE *Profile, APPLE_AGX_U32 Opcode,
    APPLE_AGX_U32 Rop);

APPLE_AGX_BOOL AppleAgxGdiDescribeSurface(
    APPLE_AGX_U32 Width, APPLE_AGX_U32 Height,
    APPLE_AGX_U32 BytesPerPixel, APPLE_AGX_U32 AlignmentShift,
    APPLE_AGX_GDI_SURFACE *Surface);

APPLE_AGX_BOOL AppleAgxGdiValidateCommandStream(
    const unsigned char *Commands, APPLE_AGX_U32 CommandBytes,
    APPLE_AGX_U32 *CommandCount);

APPLE_AGX_BOOL AppleAgxGdiDmaRecordBytes(
    APPLE_AGX_U32 SubRectCount, APPLE_AGX_U32 *RecordBytes);
APPLE_AGX_BOOL AppleAgxGdiEncodeDmaCommand(
    const APPLE_AGX_GDI_COMMAND_DESCRIPTION *Description,
    unsigned char *DmaBuffer, APPLE_AGX_U32 DmaBytes,
    APPLE_AGX_U32 *BytesWritten);
APPLE_AGX_BOOL AppleAgxGdiValidateDmaStream(
    const unsigned char *DmaBuffer, APPLE_AGX_U32 DmaBytes,
    APPLE_AGX_U32 *CommandCount);
APPLE_AGX_BOOL AppleAgxGdiBuildLoweringReceipt(
    const unsigned char *DmaBuffer, APPLE_AGX_U32 DmaBytes,
    APPLE_AGX_GDI_LOWERING_RECEIPT *Receipt);

APPLE_AGX_BOOL AppleAgxGdiMemoryPoolCreate(
    const APPLE_AGX_MEMORY_IO *Io, APPLE_AGX_U64 Length,
    APPLE_AGX_GDI_MEMORY_POOL *Pool);
APPLE_AGX_BOOL AppleAgxGdiMemoryPoolDestroy(
    const APPLE_AGX_MEMORY_IO *Io, APPLE_AGX_GDI_MEMORY_POOL *Pool);

#endif /* APPLE_AGX_GDI_H */
