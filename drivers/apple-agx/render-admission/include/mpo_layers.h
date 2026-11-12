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

#endif
