#ifndef APPLE_AGX_G4_BUILDER_H
#define APPLE_AGX_G4_BUILDER_H

#include "apple_agx_g4_submit.h"
#include "apple_agx_render_template.h"
#include "apple_agx_exp208_adapter.h"

/* Bind the source-backed scene allocations to the rebased firmware objects.
 * The caller retains ownership of all VidMm process ranges and must apply
 * relocations before patching native render fields. A zero GPU VA marks a
 * template object which current Asahi firmware does not use for this mode. */
APPLE_AGX_BOOL AppleAgxG4BindProcessObjects(
    const APPLE_AGX_G4_NATIVE_RENDER *Render,
    const APPLE_AGX_G4_PROCESS_RANGE
        Process[APPLE_AGX_G4_PROCESS_RANGE_COUNT],
    APPLE_AGX_EXP208_RELOCATION_OBJECT *Objects,
    APPLE_AGX_U32 ObjectCount);

/* Apply the retained firmware graph after scene binding. Source-only template
 * process buffers are never submitted; optional native scene pointers are
 * replaced with null as in current Asahi render.rs. */
APPLE_AGX_BOOL AppleAgxG4ApplySceneRelocations(
    APPLE_AGX_EXP208_RELOCATION_OBJECT *Objects,
    APPLE_AGX_U32 ObjectCount);

APPLE_AGX_BOOL AppleAgxG4BindNativeObjects(
    const APPLE_AGX_G4_SUBMIT_VIEW *View,
    APPLE_AGX_EXP208_RELOCATION_OBJECT *Objects,
    APPLE_AGX_U32 ObjectCount);
/* The same, naming the first failing predicate in *Refused (1 arguments,
 * 2 depth-only ZLS outside attachment 0, 3 zero sample size with colour,
 * 4 empty size, 5 layers != 1, 6 samples, 7 tile buffer, 8 flag 2 clear,
 * 9 unknown flags, 10 vertex helper, 11 VDM/ISP bases, 12 ZLS,
 * 13 attachment pad/flags, 14 attachment pointer, 15 attachment too small,
 * 16 attachment too large; 0 on success). AppleAgxG4BuildTa3dEx reports it
 * as step 300 + predicate. */
APPLE_AGX_BOOL AppleAgxG4BindNativeObjectsEx(
    const APPLE_AGX_G4_SUBMIT_VIEW *View,
    APPLE_AGX_EXP208_RELOCATION_OBJECT *Objects,
    APPLE_AGX_U32 ObjectCount, APPLE_AGX_U32 *Refused);

APPLE_AGX_BOOL AppleAgxG4PatchRenderScalars(
    const APPLE_AGX_G4_NATIVE_RENDER *Render,
    APPLE_AGX_EXP208_RELOCATION_OBJECT *Objects,
    APPLE_AGX_U32 ObjectCount);

/* Build one isolated firmware image after AGX4 parsing and rebase. The
 * caller supplies a fresh materialized arena for each job and owns ctx0
 * stamps, event lease, broker JOB_BEGIN/END and fence retirement. */
APPLE_AGX_BOOL AppleAgxG4BuildTa3d(
    const APPLE_AGX_G4_SUBMIT_VIEW *View,
    void *Arena, APPLE_AGX_U32 ArenaBytes,
    APPLE_AGX_U32 VmSlot,
    APPLE_AGX_EXP208_RELOCATION_OBJECT *Objects,
    APPLE_AGX_U32 ObjectCount);
/* The same, naming the refused step in *FailedStep (1 arguments, 2 process
 * objects, 3 native objects, 4 buffer manager, 5 VM slot, 6 relocations,
 * 7 render scalars, 8 fragment attachments; 0 on success). */
APPLE_AGX_BOOL AppleAgxG4BuildTa3dEx(
    const APPLE_AGX_G4_SUBMIT_VIEW *View,
    void *Arena, APPLE_AGX_U32 ArenaBytes,
    APPLE_AGX_U32 VmSlot,
    APPLE_AGX_EXP208_RELOCATION_OBJECT *Objects,
    APPLE_AGX_U32 ObjectCount, APPLE_AGX_U32 *FailedStep);

APPLE_AGX_BOOL AppleAgxG4StageJob(
    const APPLE_AGX_EXP208_JOB_PARAMETERS *Parameters,
    APPLE_AGX_EXP208_RELOCATION_OBJECT *Objects,
    APPLE_AGX_U32 ObjectCount,
    APPLE_AGX_BACKEND_JOB_IMAGE *Job);

#endif
