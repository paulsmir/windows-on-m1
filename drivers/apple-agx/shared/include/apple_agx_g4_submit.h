/* SPDX-License-Identifier: MIT
 * Native command framing follows the Asahi DRM UAPI:
 * Copyright (C) The Asahi Linux Contributors
 * Copyright (C) 2018-2023 Collabora Ltd.
 * Copyright (C) 2014-2018 Broadcom
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software. THE SOFTWARE IS
 * PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 */
#ifndef APPLE_AGX_G4_SUBMIT_H
#define APPLE_AGX_G4_SUBMIT_H

#define APPLE_AGX_G4_PRIVATE_MAGIC 0x34584741u /* "AGX4" */
#define APPLE_AGX_G4_PRIVATE_VERSION 1u
#define APPLE_AGX_G4_PRIVATE_VERSION_PROCESS_VA 2u
#define APPLE_AGX_G4_PRIVATE_VERSION_PRIVATE_VA 3u
/* G2: a compute command (Asahi drm_asahi_cmd_compute) with its
 * process-mapped compute preemption buffer. */
#define APPLE_AGX_G4_PRIVATE_VERSION_COMPUTE 4u
#define APPLE_AGX_G4_PROCESS_RANGE_COUNT 9u
#define APPLE_AGX_G4_NATIVE_MAX_BYTES 4096u
#define APPLE_AGX_G4_RENDER 0u
#define APPLE_AGX_G4_COMPUTE 1u
#define APPLE_AGX_G4_FRAGMENT_ATTACHMENTS 3u
#define APPLE_AGX_G4_COLOR_BGRA8 1u
/* EXP1035: colour classes by bytes per pixel. BGRA8 also names the existing
 * 4-byte (and wider) class; R8/A8 upload blits render 1- or 2-byte targets. */
#define APPLE_AGX_G4_COLOR_1BYTE 2u
#define APPLE_AGX_G4_COLOR_2BYTE 3u
static inline unsigned int AppleAgxG4ColorBytes(unsigned int color_format) {
  return color_format == APPLE_AGX_G4_COLOR_BGRA8 ? 4u :
      color_format == APPLE_AGX_G4_COLOR_1BYTE ? 1u :
      color_format == APPLE_AGX_G4_COLOR_2BYTE ? 2u : 0u;
}
/* Native USC fields are 32-bit offsets within this process execution window.
 * UMD placement, native encoding, parser and firmware work must agree. */
/* EXP1150: a fragment job lists its colour attachment first, then the
 * depth and separate stencil (ZLS) attachments (Mesa append_attachments).
 * The parser proves each is mapped and writable; only attachment 0, the
 * colour target, is resolved and bound. */
#define APPLE_AGX_G4_MAX_ATTACHMENTS 3u
#define APPLE_AGX_G4_USC_EXECUTION_BASE 0x1100000000ULL
#define APPLE_AGX_G4_USC_WINDOW_BYTES 0x100000000ULL
/* G2 HW2: the KMD's per-process private range (32 MiB, one leaf table) is
 * the top span of the USC window, so the KMD's own kernels (present blt)
 * execute from it; the UMD cannot map there (AllowUserModeMapping 0). */
#define APPLE_AGX_G4_PRIVATE_VA_BYTES 0x02000000ULL
#define APPLE_AGX_G4_PRIVATE_VA_BASE \
  (APPLE_AGX_G4_USC_EXECUTION_BASE + APPLE_AGX_G4_USC_WINDOW_BYTES - \
   APPLE_AGX_G4_PRIVATE_VA_BYTES)

typedef struct {
  unsigned int Magic;
  unsigned short Version;
  unsigned short HeaderBytes;
  unsigned int CommandBytes;
  unsigned int Reserved;
  unsigned long long CommandVa;
} APPLE_AGX_G4_PRIVATE_HEADER;

/* EXP1003: an empty render-context submission. Its only effect is that
 * VidSch schedules the device, so VidMm makes the residency list resident and
 * populates its PTEs (Residency overview) before CPU staging copies run. */
#define APPLE_AGX_G4_TOUCH_MAGIC 0x48435554u /* 'TUCH' */
typedef struct {
  unsigned int Magic;
  unsigned int Bytes;
} APPLE_AGX_G4_TOUCH;

static inline int AppleAgxG4IsTouch(const void *Data, unsigned int Bytes) {
  const APPLE_AGX_G4_TOUCH *touch = (const APPLE_AGX_G4_TOUCH *)Data;
  return Data != 0 && Bytes == sizeof(*touch) &&
         touch->Magic == APPLE_AGX_G4_TOUCH_MAGIC &&
         touch->Bytes == sizeof(*touch);
}

/* Version 2 passes VidMm-owned process mappings to the KMD constructor.
 * Order: TVB page list, block list, block heap, user buffer, tilemap,
 * heap metadata, tail-pointer cache, preemption scratch, auxiliary FB.
 * Bytes describe the whole GPU-visible allocation, not a logical subrange. */
typedef struct {
  unsigned long long Va;
  unsigned int Bytes;
  unsigned int Reserved;
} APPLE_AGX_G4_PROCESS_RANGE;

typedef struct {
  APPLE_AGX_G4_PRIVATE_HEADER Base;
  APPLE_AGX_G4_PROCESS_RANGE Process[APPLE_AGX_G4_PROCESS_RANGE_COUNT];
} APPLE_AGX_G4_PRIVATE_HEADER_V2;

typedef struct {
  unsigned long long ManagerId, ManagerGeneration, SceneId, SceneGeneration;
} APPLE_AGX_G4_PRIVATE_LEASE;
typedef struct {
  APPLE_AGX_G4_PRIVATE_HEADER_V2 V2;
  APPLE_AGX_G4_PRIVATE_LEASE Lease;
} APPLE_AGX_G4_PRIVATE_HEADER_V3;


typedef struct {
  unsigned short Type;
  unsigned short Size;
  unsigned short VdmBarrier;
  unsigned short CdmBarrier;
} APPLE_AGX_G4_NATIVE_HEADER;

typedef struct {
  unsigned long long Pointer;
  unsigned long long Size;
  unsigned int Pad;
  unsigned int Flags;
} APPLE_AGX_G4_ATTACHMENT;

typedef struct {
  unsigned int Binary, Cfg;
  unsigned long long Data;
} APPLE_AGX_G4_HELPER;

typedef struct {
  unsigned long long Base, CompBase;
  unsigned int Stride, CompStride;
} APPLE_AGX_G4_ZLS;

typedef struct {
  unsigned int Usc, ResourceSpec;
} APPLE_AGX_G4_BG_EOT;

typedef struct {
  unsigned int StartHandle, StartOffset, EndHandle, EndOffset;
} APPLE_AGX_G4_TIMESTAMPS;

/* G2: the MIT Asahi UAPI compute payload (drm_asahi_cmd_compute). The KMD
 * builds the firmware RunCompute work item around it; the CDM control
 * stream, USC programs and sampler heap stay in the process GPU VA space,
 * and USC addresses are relative to APPLE_AGX_G4_USC_EXECUTION_BASE. */
typedef struct {
  unsigned int Flags, SamplerCount;
  unsigned long long CdmCtrlStreamBase, CdmCtrlStreamEnd, SamplerHeap;
  APPLE_AGX_G4_HELPER Helper;
  APPLE_AGX_G4_TIMESTAMPS Timestamps;
} APPLE_AGX_G4_NATIVE_COMPUTE;

/* The compute preemption buffer (Asahi queue/compute.rs "CPMT", allocated
 * in the user VM): the firmware's compute_preempt1_size plus four 8-byte
 * pointers. The size is per GPU generation; the KMD passes its requirement
 * to AppleAgxG4ParseComputeEx. */
typedef struct {
  APPLE_AGX_G4_PRIVATE_HEADER Base;
  APPLE_AGX_G4_PROCESS_RANGE Preemption;
} APPLE_AGX_G4_PRIVATE_HEADER_COMPUTE;
/* The whole compute envelope as the UMD writes it into the DMA private data
 * (header, then the native command it copies from its DMA buffer) and as the
 * KMD keeps it for the job it builds. */
typedef struct {
  APPLE_AGX_G4_PRIVATE_HEADER_COMPUTE Header;
  APPLE_AGX_G4_NATIVE_HEADER Native;
  APPLE_AGX_G4_NATIVE_COMPUTE Compute;
} APPLE_AGX_G4_COMPUTE_ENVELOPE;
#define APPLE_AGX_G4_SAMPLER_BYTES 16u
#define APPLE_AGX_G4_MAX_SAMPLERS 1024u
#define APPLE_AGX_G4_CDM_STREAM_MAX_BYTES 0x10000000u

/* The current MIT Asahi UAPI render payload. Offset and size assertions in
 * the production parser and native Mesa build gate future layout changes. */
typedef struct {
  unsigned int Flags, IspZlsPixels;
  unsigned long long VdmCtrlStreamBase;
  APPLE_AGX_G4_HELPER VertexHelper, FragmentHelper;
  unsigned long long IspScissorBase, IspDbiasBase, IspOclQryBase;
  APPLE_AGX_G4_ZLS Depth, Stencil;
  unsigned long long ZlsCtrl, PppMultisampleCtrl, SamplerHeap;
  unsigned int PppCtrl;
  unsigned short WidthPx, HeightPx, Layers, SamplerCount;
  unsigned char UtileWidthPx, UtileHeightPx, Samples, SampleSizeBytes;
  unsigned int IspMergeUpperX, IspMergeUpperY;
  APPLE_AGX_G4_BG_EOT Bg, Eot, PartialBg, PartialEot;
  unsigned int IspBgObjDepth, IspBgObjVals;
  APPLE_AGX_G4_TIMESTAMPS TimestampsVertex, TimestampsFragment;
} APPLE_AGX_G4_NATIVE_RENDER;

/* Asahi queue/render.rs get_tiling_params: the TVB blocks a render of this
 * size needs at least, align(ceil(tiles / 128), 8) for one cluster. */
/* M2-f9a (EXP1269): the G13 buffer manager owns a 32-block TVB window
 * (APPLE_AGX_G3_PRIVATE_HEAP_BLOCKS). Asahi buffer.rs: when the heap runs out
 * of pages, the vertex pass stops and the firmware performs a partial render
 * transparently; min_tvb_blocks only sizes the heap to avoid that. A render
 * whose tile count asks for more than the window gets the window, instead
 * of a refused scene (2048x8192 asked 128 blocks). */
#define APPLE_AGX_G4_TVB_WINDOW_BLOCKS 32u

static inline unsigned int AppleAgxG4MinTvbBlocks(unsigned int width,
    unsigned int height) {
  unsigned long long tiles = ((width + 31ULL) / 32ULL) * ((height + 31ULL) / 32ULL);
  unsigned long long blocks;
  if (!width || !height || width > 16384u || height > 16384u) return 0u;
  blocks = (((tiles + 127ULL) / 128ULL + 7ULL) / 8ULL) * 8ULL;
  return blocks > APPLE_AGX_G4_TVB_WINDOW_BLOCKS
             ? APPLE_AGX_G4_TVB_WINDOW_BLOCKS : (unsigned int)blocks;
}

/* The TVB and per-scene capacities follow Asahi buffer.rs/render.rs.  The
 * TPC reserves the t600x maximum of eight GPU clusters, so the UMD can
 * allocate before the KMD supplies a measured cluster count.  All returned
 * allocation extents are 64 KiB multiples for the G1b16 Windows profile. */
static inline int AppleAgxG4ProcessRequiredBytes(
    const APPLE_AGX_G4_NATIVE_RENDER *render,
    unsigned int bytes[APPLE_AGX_G4_PROCESS_RANGE_COUNT]) {
  unsigned long long tiles_x, tiles_y, mtiles_x, mtiles_y, per_mtile;
  unsigned long long utiles, blocks, tilemap_words, tilemap, tpc;
  unsigned long long raw[APPLE_AGX_G4_PROCESS_RANGE_COUNT];
  unsigned int i;
  if (!render || !bytes || !render->WidthPx || !render->HeightPx ||
      !render->Layers || render->WidthPx > 16384u ||
      render->HeightPx > 16384u || render->Layers > 2048u ||
      !((render->UtileWidthPx == 32u && render->UtileHeightPx == 32u) ||
        (render->UtileWidthPx == 32u && render->UtileHeightPx == 16u) ||
        (render->UtileWidthPx == 16u && render->UtileHeightPx == 16u)))
    return 0;
  tiles_x = ((unsigned long long)render->WidthPx + 31ULL) / 32ULL;
  tiles_y = ((unsigned long long)render->HeightPx + 31ULL) / 32ULL;
  mtiles_x = ((((tiles_x + 3ULL) / 4ULL) + 3ULL) / 4ULL) * 4ULL;
  mtiles_y = ((((tiles_y + 3ULL) / 4ULL) + 3ULL) / 4ULL) * 4ULL;
  per_mtile = mtiles_x * mtiles_y;
  utiles = (32ULL / render->UtileWidthPx) *
           (32ULL / render->UtileHeightPx);
  /* One queue-lifetime G13 buffer manager owns 32 blocks. The recorded
   * image names the first 16; the UMD page/block lists name all 32. A larger
   * render uses the same window (M2-f9a: firmware partial renders). */
  blocks = APPLE_AGX_G4_TVB_WINDOW_BLOCKS;
  tilemap_words = (5ULL * per_mtile * utiles + 3ULL) / 4ULL;
  tilemap = 4ULL * tilemap_words * 16ULL * render->Layers;
  tpc = 8ULL * utiles * per_mtile * 16ULL * render->Layers * 8ULL;
  raw[0] = blocks * 4ULL * 4ULL; /* page numbers, four per block */
  raw[1] = blocks * 2ULL * 4ULL; /* two words per block */
  raw[2] = blocks * 0x20000ULL;
  raw[3] = 0x10080ULL;           /* upper bound for clustered user buffer */
  raw[4] = tilemap;
  raw[5] = 0x200ULL + (render->Layers > 1u ? 0x100ULL : 0ULL);
  raw[6] = tpc;
  raw[7] = 9ULL * (0x540ULL + 0x280ULL + 0x20ULL);
  raw[8] = 0x8000ULL;
  for (i = 0u; i < APPLE_AGX_G4_PROCESS_RANGE_COUNT; ++i) {
    if (!raw[i] || raw[i] > 0xffff0000ULL) return 0;
    bytes[i] = (unsigned int)((raw[i] + 0xffffULL) & ~0xffffULL);
  }
  return 1;
}

static inline int AppleAgxG4ComposeHeaderV2(
    APPLE_AGX_G4_PRIVATE_HEADER_V2 *header,
    const APPLE_AGX_G4_NATIVE_RENDER *render,
    unsigned long long command_va, unsigned int command_bytes,
    unsigned int color_format,
    const APPLE_AGX_G4_PROCESS_RANGE
        ranges[APPLE_AGX_G4_PROCESS_RANGE_COUNT]) {
  unsigned int required[APPLE_AGX_G4_PROCESS_RANGE_COUNT];
  unsigned int i, j;
  if (!header || !ranges || !AppleAgxG4ProcessRequiredBytes(render, required) ||
      !AppleAgxG4ColorBytes(color_format) ||
      command_va < 0x10000ULL || command_va >= (1ULL << 39) ||
      !command_bytes || command_bytes > APPLE_AGX_G4_NATIVE_MAX_BYTES ||
      command_bytes > (1ULL << 39) - command_va) return 0;
  for (i = 0u; i < APPLE_AGX_G4_PROCESS_RANGE_COUNT; ++i) {
    const APPLE_AGX_G4_PROCESS_RANGE *range = &ranges[i];
    if (range->Reserved || range->Va < 0x10000ULL ||
        range->Va >= (1ULL << 39) || (range->Va & 0xffffULL) ||
        (range->Bytes & 0xffffu) || range->Bytes < required[i] ||
        range->Bytes > (1ULL << 39) - range->Va ||
        (range->Va < command_va + command_bytes &&
         command_va < range->Va + range->Bytes)) return 0;
    for (j = 0u; j < i; ++j)
      if (range->Va < ranges[j].Va + ranges[j].Bytes &&
          ranges[j].Va < range->Va + range->Bytes) return 0;
  }
  header->Base.Magic = APPLE_AGX_G4_PRIVATE_MAGIC;
  header->Base.Version = APPLE_AGX_G4_PRIVATE_VERSION_PROCESS_VA;
  header->Base.HeaderBytes = (unsigned short)sizeof(*header);
  header->Base.CommandBytes = command_bytes;
  /* In the private v2 envelope this word identifies the sole color target
   * format. Native Asahi command bytes remain unchanged. */
  header->Base.Reserved = color_format;
  header->Base.CommandVa = command_va;
  for (i = 0u; i < APPLE_AGX_G4_PROCESS_RANGE_COUNT; ++i)
    header->Process[i] = ranges[i];
  return 1;
}

/* G2: the compute envelope header; Base.Reserved stays 0. */
static inline int AppleAgxG4ComposeHeaderCompute(
    APPLE_AGX_G4_PRIVATE_HEADER_COMPUTE *header,
    unsigned long long command_va, unsigned int command_bytes,
    const APPLE_AGX_G4_PROCESS_RANGE *preemption) {
  if (!header || !preemption || command_va < 0x10000ULL ||
      command_va >= (1ULL << 39) || !command_bytes ||
      command_bytes > APPLE_AGX_G4_NATIVE_MAX_BYTES ||
      command_bytes > (1ULL << 39) - command_va || preemption->Reserved ||
      preemption->Va < 0x10000ULL || preemption->Va >= (1ULL << 39) ||
      (preemption->Va & 0xffffULL) || !preemption->Bytes ||
      (preemption->Bytes & 0xffffu) ||
      preemption->Bytes > (1ULL << 39) - preemption->Va ||
      (preemption->Va < command_va + command_bytes &&
       command_va < preemption->Va + preemption->Bytes)) return 0;
  header->Base.Magic = APPLE_AGX_G4_PRIVATE_MAGIC;
  header->Base.Version = APPLE_AGX_G4_PRIVATE_VERSION_COMPUTE;
  header->Base.HeaderBytes = (unsigned short)sizeof(*header);
  header->Base.CommandBytes = command_bytes;
  header->Base.Reserved = 0u;
  header->Base.CommandVa = command_va;
  header->Preemption = *preemption;
  return 1;
}

/* G2: TRUE when the private data is a compute envelope (checks only the
 * magic and version; AppleAgxG4ParseComputeEx validates it). */
static inline int AppleAgxG4IsCompute(const void *data, unsigned int bytes) {
  /* Zeroed first: MSVC (C4701 under /WX) does not see the byte copy. */
  APPLE_AGX_G4_PRIVATE_HEADER header = {0};
  const unsigned char *p = (const unsigned char *)data;
  unsigned int i;
  if (!data || bytes < sizeof(header)) return 0;
  for (i = 0u; i < sizeof(header); ++i) ((unsigned char *)&header)[i] = p[i];
  return header.Magic == APPLE_AGX_G4_PRIVATE_MAGIC &&
         header.Version == APPLE_AGX_G4_PRIVATE_VERSION_COMPUTE;
}

static inline int AppleAgxG4ComposeHeaderV3(APPLE_AGX_G4_PRIVATE_HEADER_V3 *header,
    const APPLE_AGX_G4_NATIVE_RENDER *render, unsigned long long command_va,
    unsigned command_bytes, unsigned color_format,
    const APPLE_AGX_G4_PROCESS_RANGE ranges[9],
    const APPLE_AGX_G4_PRIVATE_LEASE *lease) {
  if (!header || !lease || !lease->ManagerId || !lease->ManagerGeneration ||
      !lease->SceneId || !lease->SceneGeneration ||
      !AppleAgxG4ComposeHeaderV2(&header->V2,render,command_va,command_bytes,
                               color_format,ranges)) return 0;
  header->V2.Base.Version=APPLE_AGX_G4_PRIVATE_VERSION_PRIVATE_VA;
  header->V2.Base.HeaderBytes=(unsigned short)sizeof(*header);
  header->Lease=*lease;
  return 1;
}

typedef enum {
  AppleAgxG4ParseOk = 0,
  AppleAgxG4ParseInvalid,
  AppleAgxG4ParseUnsupported,
  AppleAgxG4ParseUnmapped
} APPLE_AGX_G4_PARSE_RESULT;

typedef int (*APPLE_AGX_G4_ACCESS)(void *Context, unsigned long long GpuVa,
                                   unsigned int Bytes, int Write);

typedef enum {
  AppleAgxG4AccessProcess = 1,
  AppleAgxG4AccessCpuEnvelope = 2,
  AppleAgxG4AccessAttachment = 3,
  AppleAgxG4AccessRender = 4,
  /* G2: compute envelope: CDM control stream, sampler heap, helper data,
   * USC helper program, compute preemption buffer. */
  AppleAgxG4AccessComputeStream = 5,
  AppleAgxG4AccessSamplerHeap = 6,
  AppleAgxG4AccessComputePreemption = 7
} APPLE_AGX_G4_ACCESS_KIND;
typedef enum {
  AppleAgxG4FailureNone = 0,
  AppleAgxG4FailureAccess = 1,
  AppleAgxG4FailureOutput = 2
} APPLE_AGX_G4_FAILURE_SUBSITE;
typedef struct {
  unsigned int Subsite, Kind, Ordinal;
  unsigned long long Va;
  unsigned int Bytes, Write;
} APPLE_AGX_G4_FAILURE;
typedef int (*APPLE_AGX_G4_ACCESS_EX)(void *Context,
    unsigned long long GpuVa, unsigned int Bytes, int Write,
    APPLE_AGX_G4_ACCESS_KIND Kind, unsigned int Ordinal);

/* Parsing proves envelope framing and attachment access only. The render
 * payload's native VA fields still need a separate validator before any GPU
 * access or success return from SubmitCommandVirtual. */
typedef struct {
  const unsigned char *Native;
  const unsigned char *Render;
  const APPLE_AGX_G4_ATTACHMENT *Attachments;
  unsigned int CommandBytes;
  unsigned int RenderBytes;
  unsigned int AttachmentCount;
  unsigned long long CommandVa;
  unsigned int ColorFormat;
  APPLE_AGX_G4_PROCESS_RANGE Process[APPLE_AGX_G4_PROCESS_RANGE_COUNT];
  APPLE_AGX_G4_PRIVATE_LEASE Lease;
  /* Set by the KMD, never parsed: TVB blocks backed at the start of
   * Process[2] (0: all of Process[2]). */
  unsigned int HeapBlocks;
} APPLE_AGX_G4_SUBMIT_VIEW;

/* G2: a validated compute envelope. */
typedef struct {
  const unsigned char *Native;
  APPLE_AGX_G4_NATIVE_COMPUTE Compute;
  APPLE_AGX_G4_PROCESS_RANGE Preemption;
  unsigned int CommandBytes;
  unsigned long long CommandVa;
} APPLE_AGX_G4_COMPUTE_VIEW;

/* G2: validate a compute envelope: header, the native compute command
 * (flags 0, no helper program, no timestamps: unsupported until the KMD
 * owns them), a mapped CDM control stream [base, end), a mapped sampler
 * heap when samplers are used, and a mapped writable preemption buffer of
 * at least PreemptBytes that overlaps neither the command nor the stream. */
APPLE_AGX_G4_PARSE_RESULT AppleAgxG4ParseComputeEx(
    const void *PrivateData, unsigned int PrivateCapacity,
    unsigned int UmdPrivateBytes, unsigned long long DmaVa,
    unsigned int DmaBytes, unsigned int PreemptBytes,
    APPLE_AGX_G4_ACCESS_EX Access, void *AccessContext,
    APPLE_AGX_G4_COMPUTE_VIEW *View, APPLE_AGX_G4_FAILURE *Failure);

APPLE_AGX_G4_PARSE_RESULT AppleAgxG4ParseSubmit(
    const void *PrivateData, unsigned int PrivateCapacity,
    unsigned int UmdPrivateBytes, unsigned long long DmaVa,
    unsigned int DmaBytes, APPLE_AGX_G4_ACCESS Access, void *AccessContext,
    APPLE_AGX_G4_SUBMIT_VIEW *View);
APPLE_AGX_G4_PARSE_RESULT AppleAgxG4ParseSubmitEx(
    const void *PrivateData, unsigned int PrivateCapacity,
    unsigned int UmdPrivateBytes, unsigned long long DmaVa,
    unsigned int DmaBytes, APPLE_AGX_G4_ACCESS_EX Access,
    void *AccessContext, APPLE_AGX_G4_SUBMIT_VIEW *View,
    APPLE_AGX_G4_FAILURE *Failure);

#endif
