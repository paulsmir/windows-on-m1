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
#define APPLE_AGX_G4_PROCESS_RANGE_COUNT 9u
#define APPLE_AGX_G4_NATIVE_MAX_BYTES 4096u
#define APPLE_AGX_G4_RENDER 0u
#define APPLE_AGX_G4_COMPUTE 1u
#define APPLE_AGX_G4_FRAGMENT_ATTACHMENTS 3u

typedef struct {
  unsigned int Magic;
  unsigned short Version;
  unsigned short HeaderBytes;
  unsigned int CommandBytes;
  unsigned int Reserved;
  unsigned long long CommandVa;
} APPLE_AGX_G4_PRIVATE_HEADER;

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

typedef enum {
  AppleAgxG4ParseOk = 0,
  AppleAgxG4ParseInvalid,
  AppleAgxG4ParseUnsupported,
  AppleAgxG4ParseUnmapped
} APPLE_AGX_G4_PARSE_RESULT;

typedef int (*APPLE_AGX_G4_ACCESS)(void *Context, unsigned long long GpuVa,
                                   unsigned int Bytes, int Write);

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
  APPLE_AGX_G4_PROCESS_RANGE Process[APPLE_AGX_G4_PROCESS_RANGE_COUNT];
} APPLE_AGX_G4_SUBMIT_VIEW;

APPLE_AGX_G4_PARSE_RESULT AppleAgxG4ParseSubmit(
    const void *PrivateData, unsigned int PrivateCapacity,
    unsigned int UmdPrivateBytes, unsigned long long DmaVa,
    unsigned int DmaBytes, APPLE_AGX_G4_ACCESS Access, void *AccessContext,
    APPLE_AGX_G4_SUBMIT_VIEW *View);

#endif
