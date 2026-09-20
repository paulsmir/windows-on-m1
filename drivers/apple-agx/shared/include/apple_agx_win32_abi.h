#ifndef APPLE_AGX_WIN32_ABI_H
#define APPLE_AGX_WIN32_ABI_H

#include "apple_agx_state.h"

typedef unsigned short APPLE_AGX_U16;

#define APPLE_AGX_WIN32_COMMAND_MAGIC 0x43474157u /* "WAGC" */
#define APPLE_AGX_WIN32_COMMAND_VERSION 1u
#define APPLE_AGX_WIN32_COMMAND_VERSION_NATIVE_PIPELINES 2u
#define APPLE_AGX_WIN32_COMMAND_VERSION_NATIVE_USC 3u
#define APPLE_AGX_WIN32_COMMAND_VERSION_NATIVE_BATCH 4u
#define APPLE_AGX_WIN32_COMMAND_VERSION_INDEXED_BATCH 5u
#define APPLE_AGX_WIN32_COMMAND_VERSION_TEXTURED_BATCH 6u
#define APPLE_AGX_WIN32_COMMAND_VERSION_DEPTH_BATCH 7u
#define APPLE_AGX_WIN32_COMMAND_VERSION_MIXED_BATCH 8u
#define APPLE_AGX_WIN32_COMMAND_LEGACY_MAX_BYTES 4096u
#define APPLE_AGX_WIN32_COMMAND_LEGACY_MAX_REFERENCES 16u
#define APPLE_AGX_WIN32_COMMAND_LEGACY_MAX_RELOCATIONS 64u
#define APPLE_AGX_WIN32_COMMAND_MAX_BYTES 32768u
#define APPLE_AGX_WIN32_COMMAND_NATIVE_MAX_REFERENCES 64u
#define APPLE_AGX_WIN32_COMMAND_MAX_REFERENCES 69u
#define APPLE_AGX_WIN32_COMMAND_MAX_RELOCATIONS 133u
#define APPLE_AGX_WIN32_COMMAND_INDEXED_MAX_REFERENCES 34u
#define APPLE_AGX_WIN32_COMMAND_INDEXED_MAX_RELOCATIONS 134u
#define APPLE_AGX_WIN32_COMMAND_TEXTURED_MAX_REFERENCES 36u
#define APPLE_AGX_WIN32_COMMAND_TEXTURED_MAX_RELOCATIONS 136u
#define APPLE_AGX_WIN32_COMMAND_DEPTH_MAX_REFERENCES 38u
#define APPLE_AGX_WIN32_COMMAND_DEPTH_MAX_RELOCATIONS 136u
#define APPLE_AGX_WIN32_COMMAND_MIXED_MAX_REFERENCES 69u
#define APPLE_AGX_WIN32_COMMAND_MIXED_MAX_RELOCATIONS 760u
#define APPLE_AGX_WIN32_COMMAND_STORAGE_MAX_RELOCATIONS 760u
#define APPLE_AGX_WIN32_COMMAND_IS_NATIVE(Version)                         \
  ((Version) == APPLE_AGX_WIN32_COMMAND_VERSION_NATIVE_BATCH ||           \
   (Version) == APPLE_AGX_WIN32_COMMAND_VERSION_INDEXED_BATCH ||          \
   (Version) == APPLE_AGX_WIN32_COMMAND_VERSION_TEXTURED_BATCH ||         \
   (Version) == APPLE_AGX_WIN32_COMMAND_VERSION_DEPTH_BATCH ||             \
   (Version) == APPLE_AGX_WIN32_COMMAND_VERSION_MIXED_BATCH)
#define APPLE_AGX_WIN32_COMMAND_HAS_INDEX(Version)                         \
  ((Version) == APPLE_AGX_WIN32_COMMAND_VERSION_INDEXED_BATCH ||          \
   (Version) == APPLE_AGX_WIN32_COMMAND_VERSION_MIXED_BATCH)
#define APPLE_AGX_WIN32_COMMAND_REFERENCE_LIMIT(Version)                   \
  ((Version) == APPLE_AGX_WIN32_COMMAND_VERSION_MIXED_BATCH               \
       ? APPLE_AGX_WIN32_COMMAND_MIXED_MAX_REFERENCES                     \
       : (Version) == APPLE_AGX_WIN32_COMMAND_VERSION_DEPTH_BATCH         \
       ? APPLE_AGX_WIN32_COMMAND_DEPTH_MAX_REFERENCES                     \
       : (Version) == APPLE_AGX_WIN32_COMMAND_VERSION_TEXTURED_BATCH      \
       ? APPLE_AGX_WIN32_COMMAND_TEXTURED_MAX_REFERENCES                  \
       : (Version) == APPLE_AGX_WIN32_COMMAND_VERSION_INDEXED_BATCH       \
             ? APPLE_AGX_WIN32_COMMAND_INDEXED_MAX_REFERENCES            \
             : (Version) == APPLE_AGX_WIN32_COMMAND_VERSION_NATIVE_BATCH  \
                   ? APPLE_AGX_WIN32_COMMAND_NATIVE_MAX_REFERENCES        \
                   : APPLE_AGX_WIN32_COMMAND_LEGACY_MAX_REFERENCES)
#define APPLE_AGX_WIN32_COMMAND_RELOCATION_LIMIT(Version)                  \
  ((Version) == APPLE_AGX_WIN32_COMMAND_VERSION_MIXED_BATCH               \
       ? APPLE_AGX_WIN32_COMMAND_MIXED_MAX_RELOCATIONS                    \
       : (Version) == APPLE_AGX_WIN32_COMMAND_VERSION_DEPTH_BATCH         \
       ? APPLE_AGX_WIN32_COMMAND_DEPTH_MAX_RELOCATIONS                    \
       : (Version) == APPLE_AGX_WIN32_COMMAND_VERSION_TEXTURED_BATCH      \
       ? APPLE_AGX_WIN32_COMMAND_TEXTURED_MAX_RELOCATIONS                 \
       : (Version) == APPLE_AGX_WIN32_COMMAND_VERSION_INDEXED_BATCH       \
             ? APPLE_AGX_WIN32_COMMAND_INDEXED_MAX_RELOCATIONS           \
             : (Version) == APPLE_AGX_WIN32_COMMAND_VERSION_NATIVE_BATCH  \
                   ? APPLE_AGX_WIN32_COMMAND_MAX_RELOCATIONS              \
                   : APPLE_AGX_WIN32_COMMAND_LEGACY_MAX_RELOCATIONS)
#define APPLE_AGX_WIN32_OPTIONAL_REFERENCE 0xffffffffu
#define APPLE_AGX_WIN32_DRAW_FLAG_EXPECTED_FOREGROUND 0x1u
#define APPLE_AGX_WIN32_NATIVE_RENDER_PROCESS_EMPTY_TILES 0x1u
#define APPLE_AGX_WIN32_NATIVE_RENDER_DEPTH_BIAS_IS_INT 0x2u
#define APPLE_AGX_WIN32_DRAW_V2_FRAGMENT_USC_PIPELINE_RESERVED_INDEX 0u

typedef enum _APPLE_AGX_WIN32_OPCODE {
  AppleAgxWin32OpcodeClear = 1u,
  AppleAgxWin32OpcodeDraw = 2u,
} APPLE_AGX_WIN32_OPCODE;

typedef enum _APPLE_AGX_WIN32_ACCESS {
  AppleAgxWin32AccessRead = 0x1u,
  AppleAgxWin32AccessWrite = 0x2u,
  AppleAgxWin32AccessExecute = 0x4u,
} APPLE_AGX_WIN32_ACCESS;

typedef enum _APPLE_AGX_WIN32_ROLE {
  AppleAgxWin32RoleRenderTarget = 1u,
  AppleAgxWin32RoleVertex = 2u,
  AppleAgxWin32RoleIndex = 3u,
  AppleAgxWin32RoleConstant = 4u,
  AppleAgxWin32RoleTexture = 5u,
  AppleAgxWin32RoleShader = 6u,
  AppleAgxWin32RoleDescriptor = 7u,
  AppleAgxWin32RoleShaderRodata = 8u,
  AppleAgxWin32RoleUscPipeline = 9u,
  AppleAgxWin32RoleEncoder = 10u,
  AppleAgxWin32RoleScissor = 11u,
  AppleAgxWin32RoleDepthBias = 12u,
  AppleAgxWin32RolePppState = 13u,
  AppleAgxWin32RoleUniform = 14u,
  AppleAgxWin32RoleDepthAttachment = 15u,
  AppleAgxWin32RoleSharedGeometry = 16u,
} APPLE_AGX_WIN32_ROLE;

typedef enum _APPLE_AGX_WIN32_FORMAT {
  AppleAgxWin32FormatBgra8Unorm = 1u,
  AppleAgxWin32FormatRgba8Unorm = 2u,
} APPLE_AGX_WIN32_FORMAT;

typedef enum _APPLE_AGX_WIN32_ABI_RESULT {
  AppleAgxWin32AbiSuccess = 0,
  AppleAgxWin32AbiArgument,
  AppleAgxWin32AbiMagic,
  AppleAgxWin32AbiVersion,
  AppleAgxWin32AbiLayout,
  AppleAgxWin32AbiFlags,
  AppleAgxWin32AbiStaleGeneration,
  AppleAgxWin32AbiReferenceCount,
  AppleAgxWin32AbiHash,
  AppleAgxWin32AbiOpcode,
  AppleAgxWin32AbiAllocationIndex,
  AppleAgxWin32AbiAccess,
  AppleAgxWin32AbiRole,
  AppleAgxWin32AbiReserved,
  AppleAgxWin32AbiRange,
  AppleAgxWin32AbiPayload,
  AppleAgxWin32AbiRelocation,
  AppleAgxWin32AbiReachability,
} APPLE_AGX_WIN32_ABI_RESULT;

typedef enum _APPLE_AGX_WIN32_TOPOLOGY {
  AppleAgxWin32TopologyTriangleList = 1u,
} APPLE_AGX_WIN32_TOPOLOGY;

typedef enum _APPLE_AGX_WIN32_RELOCATION_KIND {
  AppleAgxWin32RelocationEncoderAddress = 1u,
  AppleAgxWin32RelocationPipelineAddress = 2u,
  AppleAgxWin32RelocationDescriptorAddress = 3u,
  AppleAgxWin32RelocationUscShaderOffset32 = 4u,
  AppleAgxWin32RelocationUscBufferAddress40 = 5u,
  AppleAgxWin32RelocationVdmPipelineOffset32 = 6u,
  AppleAgxWin32RelocationPppStateAddress40 = 7u,
  /* v3 only; retains the v2 draw layout and separate fragment USC. */
  AppleAgxWin32RelocationUscPreshaderOffset32 = 8u,
  AppleAgxWin32RelocationUscTableAddress39 = 9u,
  AppleAgxWin32RelocationPppPipelineOffset32 = 10u,
  AppleAgxWin32RelocationPppCfBindingsOffset32 = 11u,
  AppleAgxWin32RelocationUniformAddress64 = 12u,
  AppleAgxWin32RelocationTextureAddress40 = 13u,
  AppleAgxWin32RelocationPbeAddress40 = 14u,
  AppleAgxWin32RelocationVdmIndexBufferAddress40 = 15u,
} APPLE_AGX_WIN32_RELOCATION_KIND;

typedef struct _APPLE_AGX_WIN32_COMMAND_HEADER {
  APPLE_AGX_U32 Magic;
  APPLE_AGX_U16 Version;
  APPLE_AGX_U16 HeaderBytes;
  APPLE_AGX_U32 TotalBytes;
  APPLE_AGX_U32 Opcode;
  APPLE_AGX_U32 Flags;
  APPLE_AGX_U32 Generation;
  APPLE_AGX_U32 ReferenceCount;
  APPLE_AGX_U32 ReferencesOffset;
  APPLE_AGX_U32 PayloadOffset;
  APPLE_AGX_U32 PayloadBytes;
  APPLE_AGX_U64 ContentHash;
} APPLE_AGX_WIN32_COMMAND_HEADER;

typedef struct _APPLE_AGX_WIN32_ALLOCATION_REFERENCE {
  APPLE_AGX_U32 AllocationIndex;
  APPLE_AGX_U32 Access;
  APPLE_AGX_U32 Role;
  APPLE_AGX_U32 Reserved;
  APPLE_AGX_U64 Offset;
  APPLE_AGX_U64 Bytes;
} APPLE_AGX_WIN32_ALLOCATION_REFERENCE;

typedef struct _APPLE_AGX_WIN32_CLEAR_PAYLOAD {
  APPLE_AGX_U32 StructBytes;
  APPLE_AGX_U32 Format;
  APPLE_AGX_U32 Color;
  APPLE_AGX_U32 SurfaceWidth;
  APPLE_AGX_U32 SurfaceHeight;
  APPLE_AGX_U32 SurfacePitch;
  APPLE_AGX_U32 Left;
  APPLE_AGX_U32 Top;
  APPLE_AGX_U32 Right;
  APPLE_AGX_U32 Bottom;
  APPLE_AGX_U32 DestinationReference;
  APPLE_AGX_U32 Reserved;
} APPLE_AGX_WIN32_CLEAR_PAYLOAD;

typedef struct _APPLE_AGX_WIN32_DRAW_PAYLOAD {
  APPLE_AGX_U32 StructBytes;
  APPLE_AGX_U32 Format;
  APPLE_AGX_U32 SurfaceWidth;
  APPLE_AGX_U32 SurfaceHeight;
  APPLE_AGX_U32 SurfacePitch;
  APPLE_AGX_U32 Topology;
  APPLE_AGX_U32 VertexCount;
  APPLE_AGX_U32 InstanceCount;
  APPLE_AGX_U32 FirstVertex;
  APPLE_AGX_U32 FirstInstance;
  APPLE_AGX_U32 DestinationReference;
  APPLE_AGX_U32 VertexReference;
  APPLE_AGX_U32 IndexReference;
  APPLE_AGX_U32 ConstantReference;
  APPLE_AGX_U32 TextureReference;
  APPLE_AGX_U32 VertexShaderReference;
  APPLE_AGX_U32 FragmentShaderReference;
  APPLE_AGX_U32 VertexRodataReference;
  APPLE_AGX_U32 FragmentRodataReference;
  APPLE_AGX_U32 UscPipelineReference;
  APPLE_AGX_U32 DescriptorReference;
  APPLE_AGX_U32 ScissorReference;
  APPLE_AGX_U32 DepthBiasReference;
  APPLE_AGX_U32 EncoderReference;
  APPLE_AGX_U32 RelocationsOffset;
  APPLE_AGX_U32 RelocationCount;
  APPLE_AGX_U32 Flags;
  APPLE_AGX_U32 ExpectedForegroundColor;
  APPLE_AGX_U32 Reserved[4];
} APPLE_AGX_WIN32_DRAW_PAYLOAD;

typedef struct _APPLE_AGX_WIN32_NATIVE_PIPELINE_ROOT {
  APPLE_AGX_U32 UscReference;
  APPLE_AGX_U32 PackedCounts;
  APPLE_AGX_U32 UscFlags;
} APPLE_AGX_WIN32_NATIVE_PIPELINE_ROOT;

typedef struct _APPLE_AGX_WIN32_NATIVE_BATCH_METADATA {
  APPLE_AGX_U32 StructBytes;
  APPLE_AGX_WIN32_NATIVE_PIPELINE_ROOT Background;
  APPLE_AGX_WIN32_NATIVE_PIPELINE_ROOT PartialBackground;
  APPLE_AGX_WIN32_NATIVE_PIPELINE_ROOT EndOfTile;
  APPLE_AGX_U32 Samples;
  APPLE_AGX_U32 Layers;
  APPLE_AGX_U32 SampleSizeBytes;
  APPLE_AGX_U32 UtileWidth;
  APPLE_AGX_U32 UtileHeight;
  APPLE_AGX_U32 PppControl;
  APPLE_AGX_U32 PppMultisampleControl;
  APPLE_AGX_U32 RenderFlags;
  APPLE_AGX_U32 DepthReference;
  APPLE_AGX_U32 DepthCompressionReference;
  APPLE_AGX_U32 DepthStride;
  APPLE_AGX_U32 DepthCompressionStride;
  APPLE_AGX_U32 StencilReference;
  APPLE_AGX_U32 StencilCompressionReference;
  APPLE_AGX_U32 StencilStride;
  APPLE_AGX_U32 StencilCompressionStride;
  APPLE_AGX_U64 ZlsControl;
  APPLE_AGX_U64 IspZlsPixels;
  APPLE_AGX_U32 IspBgobjDepth;
  APPLE_AGX_U32 IspBgobjValues;
  APPLE_AGX_U32 ComputeEncoderReference;
  APPLE_AGX_U32 ComputeEncoderBytes;
} APPLE_AGX_WIN32_NATIVE_BATCH_METADATA;

typedef struct _APPLE_AGX_WIN32_RELOCATION {
  APPLE_AGX_U32 Kind;
  APPLE_AGX_U16 WidthBytes;
  APPLE_AGX_U16 Reserved;
  APPLE_AGX_U32 DestinationReference;
  APPLE_AGX_U32 TargetReference;
  APPLE_AGX_U64 DestinationOffset;
  APPLE_AGX_U64 TargetOffset;
  APPLE_AGX_U64 AddressFlags;
} APPLE_AGX_WIN32_RELOCATION;

typedef struct _APPLE_AGX_WIN32_COMMAND_VIEW {
  const APPLE_AGX_WIN32_COMMAND_HEADER *Header;
  const APPLE_AGX_WIN32_ALLOCATION_REFERENCE *References;
  const APPLE_AGX_WIN32_CLEAR_PAYLOAD *Clear;
  const APPLE_AGX_WIN32_DRAW_PAYLOAD *Draw;
  const APPLE_AGX_WIN32_RELOCATION *Relocations;
  const APPLE_AGX_WIN32_NATIVE_BATCH_METADATA *NativeBatch;
} APPLE_AGX_WIN32_COMMAND_VIEW;

APPLE_AGX_U64 AppleAgxWin32CommandHash(const void *Command,
                                       APPLE_AGX_U32 CommandBytes);
APPLE_AGX_WIN32_ABI_RESULT AppleAgxWin32CommandValidate(
    const void *Command, APPLE_AGX_U32 CommandBytes,
    APPLE_AGX_U32 ExpectedGeneration, APPLE_AGX_U32 AllocationCount,
    APPLE_AGX_WIN32_COMMAND_VIEW *View);

#endif /* APPLE_AGX_WIN32_ABI_H */
