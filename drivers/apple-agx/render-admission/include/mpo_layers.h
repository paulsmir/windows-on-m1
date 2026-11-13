#ifndef APPLE_AGX_MPO_LAYERS_H
#define APPLE_AGX_MPO_LAYERS_H

/* S2: pure translation of an MPO rule-table answer (mpo_contract.h) into
 * scanout broker ABI V3 layer records (apple_agx_scanout.h).  No Windows
 * header; the kernel and a host test compile the same file.
 *
 * Two planes (specification v1 + amendment 2026-11-11T12:20Z):
 *   record 0 = DCP slot 0 (bottom) = LayerIndex 1, the application plane,
 *              premultiplied (shown over black, so any alpha byte shows the
 *              stored RGB; Asahi plane.c bottommost-plane rule);
 *   record 1 = DCP slot 1 (top)    = LayerIndex 0, the desktop plane,
 *              premultiplied.
 * Removal (two planes -> one): ONE record = the LayerIndex 0 plane, flags 0
 * (today's one-plane swap is not premultiplied either). */

#include "mpo_contract.h"
#include "apple_agx_scanout.h"

/* 2 when Result is a supported two-slot answer and the records were
 * written; 0 otherwise (Records untouched). */
unsigned int AdmissionMpoBuildCommit(const ADMISSION_MPO_RESULT *Result,
                                     APPLE_AGX_SCANOUT_LAYER Records[2]);

/* 1 when Plane (a LayerIndex 0 plane with a known placement in the pool)
 * fits a record and *Record was written; 0 otherwise. */
unsigned int AdmissionMpoBuildRemoval(const ADMISSION_MPO_PLANE_INPUT *Plane,
                                      APPLE_AGX_SCANOUT_LAYER *Record);

/* FS-1: the lone application plane (LayerIndex 1, known pool placement)
 * as the one record of a commit (slot 0; slot 1 off).  1 when written. */
unsigned int AdmissionMpoBuildLone(const ADMISSION_MPO_PLANE_INPUT *Plane,
                                   APPLE_AGX_SCANOUT_LAYER *Record);

/* ---- CUR-1 v2: the clipped top plane through a driver-owned scratch ----
 * Two 64x64 32-bpp scratch surfaces (stride 256) in the pool.  Pure and
 * IRQL-agnostic: no allocation, no wait, no mapping (Set3 runs at DIRQL,
 * EXP2374). */

/* The 64x64 layer that shows Clip (non-empty, inside the panel, at most
 * 64x64): at the clip origin, pulled back inside the panel; *Offset is the
 * block's position inside the scratch.  1 when written, 0 otherwise. */
unsigned int AdmissionMpoScratchPlace(const ADMISSION_MPO_RECT *Clip,
                                      unsigned int PanelWidth,
                                      unsigned int PanelHeight,
                                      ADMISSION_MPO_RECT *Layer,
                                      unsigned int *OffsetX,
                                      unsigned int *OffsetY);

/* 1 when every byte of the Width x Height 32-bpp block is 0 (transparent
 * under premultiplied blending: out = src + (1 - a) * dst). */
unsigned int AdmissionMpoBlockIsZero(const unsigned char *Source,
                                     unsigned int SourcePitch,
                                     unsigned int Width, unsigned int Height);

/* Writes the whole scratch: the block at (OffsetX, OffsetY), zero
 * elsewhere (the previous block's pixels are cleared). */
/* CUR-1c (EXP2384: ~126 us per 24x27 block, byte reads from the
 * write-combined pool): copy the visible block once, row by row with wide
 * loads (memcpy), into a cached stage buffer (Width * 4 bytes per row, at
 * most ADMISSION_MPO_SCRATCH_BYTES); 1 when every byte of the block is 0.
 * The zero test runs on the stage, never on the source. */
unsigned int AdmissionMpoScratchStage(unsigned char *Stage,
                                      const unsigned char *Source,
                                      unsigned int SourcePitch,
                                      unsigned int Width, unsigned int Height);
void AdmissionMpoScratchFill(unsigned char *Scratch,
                             const unsigned char *Source,
                             unsigned int SourcePitch, unsigned int Width,
                             unsigned int Height, unsigned int OffsetX,
                             unsigned int OffsetY);

/* Double buffering: Shown = the scratch DCP scans (2 = none).  One commit
 * is outstanding until its latch (scan-out PresentGate), so when Set3 may
 * write, the scanned scratch is the last committed one and the other is
 * free.  Zero-skip: an all-zero block over an all-zero scanned scratch
 * keeps the scanned scratch, unwritten. */
typedef struct _ADMISSION_MPO_SCRATCH_STATE {
  unsigned int Shown;
  unsigned int ShownZero;
} ADMISSION_MPO_SCRATCH_STATE;

/* The scratch index to show; *Write = 1 when it must be filled first. */
unsigned int AdmissionMpoScratchSelect(const ADMISSION_MPO_SCRATCH_STATE *State,
                                       unsigned int BlockZero,
                                       unsigned int *Write);
/* After the commit showing Index was accepted. */
void AdmissionMpoScratchCommitted(ADMISSION_MPO_SCRATCH_STATE *State,
                                  unsigned int Index, unsigned int BlockZero);
/* At scan-out start and whenever slot 1 stops showing a scratch. */
void AdmissionMpoScratchReset(ADMISSION_MPO_SCRATCH_STATE *State);

#endif
