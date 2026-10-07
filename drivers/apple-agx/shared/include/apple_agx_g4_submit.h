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
#define APPLE_AGX_G4_PROCESS_RANGE_COUNT 9u
#define APPLE_AGX_G4_NATIVE_MAX_BYTES 4096u
#define APPLE_AGX_G4_RENDER 0u
#define APPLE_AGX_G4_COMPUTE 1u
#define APPLE_AGX_G4_FRAGMENT_ATTACHMENTS 3u
#define APPLE_AGX_G4_COLOR_BGRA8 1u
/* Native USC fields are 32-bit offsets within this process execution window.
 * UMD placement, native encoding, parser and firmware work must agree. */
#define APPLE_AGX_G4_USC_EXECUTION_BASE 0x1100000000ULL
#define APPLE_AGX_G4_USC_WINDOW_BYTES 0x100000000ULL

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
  blocks = (((tiles_x * tiles_y + 127ULL) / 128ULL + 7ULL) / 8ULL) * 8ULL;
  /* One queue-lifetime G13 buffer manager owns 32 blocks. The recorded
   * image names the first 16; the UMD page/block lists name all 32. */
  if (blocks < 32ULL) blocks = 32ULL;
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
      color_format != APPLE_AGX_G4_COLOR_BGRA8 ||
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
  AppleAgxG4AccessRender = 4
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
} APPLE_AGX_G4_SUBMIT_VIEW;

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
