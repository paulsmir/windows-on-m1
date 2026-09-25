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

APPLE_AGX_BOOL AppleAgxG4StageJob(
    const APPLE_AGX_EXP208_JOB_PARAMETERS *Parameters,
    APPLE_AGX_EXP208_RELOCATION_OBJECT *Objects,
    APPLE_AGX_U32 ObjectCount,
    APPLE_AGX_BACKEND_JOB_IMAGE *Job);

#endif
