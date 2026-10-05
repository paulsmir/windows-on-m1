#ifndef APPLE_AGX_RENDER_PROVIDER_H
#define APPLE_AGX_RENDER_PROVIDER_H

#include "apple_agx_backend_memory_bridge.h"
#include "apple_agx_backend_runtime.h"
#include "apple_agx_exp208_adapter.h"
#include "apple_agx_gdi.h"
#include "apple_agx_initdata_memory.h"
#include "apple_agx_uat_publication.h"

#define APPLE_AGX_RENDER_PROVIDER_CONTEXT 63u
#define APPLE_AGX_RENDER_PROVIDER_ARENA_OFFSET                            \
  (APPLE_AGX_EXP208_ARENA_GPU_BASE - APPLE_AGX_BACKEND_BRIDGE_GDI_GPU_BASE)

/*
 * Immutable owner references used by the final platform provider.  The
 * render slice borrows all of them: it never allocates, maps or materializes
 * the EXP208 arena and never destroys the initdata graph.
 */
typedef struct _APPLE_AGX_RENDER_PROVIDER_CONFIG {
  APPLE_AGX_MEMORY_OBJECT *PreparedPool;
  APPLE_AGX_RENDER_TEMPLATE_ROOTS PreparedRoots;
  const APPLE_AGX_INITDATA_MEMORY_GRAPH *Initdata;
  const APPLE_AGX_CONFIG_SNAPSHOT *Snapshot;
  const APPLE_AGX_UAT_ROOTS *RenderRoots;
  const APPLE_AGX_UAT_PUBLICATION_IO *PublicationIo;
} APPLE_AGX_RENDER_PROVIDER_CONFIG;

/*
 * Queue/event ownership remains outside this slice.  The final queue provider
 * must stage every exact job instance, including its event, stamp and
 * done-pointer values, before the matching backend Submit call.
 */
typedef struct _APPLE_AGX_RENDER_PROVIDER_JOB_CONFIG {
  APPLE_AGX_BACKEND_U64 ContextIdentity;
  APPLE_AGX_BACKEND_U32 Fence;
  APPLE_AGX_EXP208_JOB_PARAMETERS Parameters;
  APPLE_AGX_EXP208_RELOCATION_OBJECT *Objects;
  APPLE_AGX_BACKEND_U32 ObjectCount;
  APPLE_AGX_BACKEND_U32 ArenaObject;
  const APPLE_AGX_EXP208_RELOCATION *Relocations;
  APPLE_AGX_BACKEND_U32 RelocationCount;
} APPLE_AGX_RENDER_PROVIDER_JOB_CONFIG;

typedef struct _APPLE_AGX_RENDER_PROVIDER {
  APPLE_AGX_MEMORY_OBJECT *PreparedPool;
  APPLE_AGX_RENDER_TEMPLATE_ROOTS PreparedRoots;
  const APPLE_AGX_INITDATA_MEMORY_GRAPH *Initdata;
  APPLE_AGX_CONFIG_SNAPSHOT Snapshot;
  APPLE_AGX_UAT_PUBLICATION_IO PublicationIo;
  APPLE_AGX_UAT_TTBR_PAIR RenderTtbrPair;
  APPLE_AGX_UAT_PUBLICATION_STATE ContextPublication;
  APPLE_AGX_RENDER_PROVIDER_JOB_CONFIG StagedJob;
  APPLE_AGX_BACKEND_U64 StagedSubmissionHash;
  APPLE_AGX_BACKEND_U32 StagedSubmissionBytes;
  APPLE_AGX_BACKEND_BOOL Initialized;
  APPLE_AGX_BACKEND_BOOL JobStaged;
} APPLE_AGX_RENDER_PROVIDER;

APPLE_AGX_BACKEND_BOOL AppleAgxRenderProviderInitialize(
    APPLE_AGX_RENDER_PROVIDER *Provider,
    const APPLE_AGX_RENDER_PROVIDER_CONFIG *Config);

/* Installs only the callbacks owned by this slice. */
APPLE_AGX_BACKEND_BOOL AppleAgxRenderProviderInstallIo(
    APPLE_AGX_RENDER_PROVIDER *Provider, APPLE_AGX_BACKEND_IO *Io);

APPLE_AGX_BACKEND_BOOL AppleAgxRenderProviderPublish(void *Context);
APPLE_AGX_BACKEND_BOOL AppleAgxRenderProviderUnpublish(void *Context);

APPLE_AGX_BACKEND_BOOL AppleAgxRenderProviderStageJob(
    APPLE_AGX_RENDER_PROVIDER *Provider,
    const APPLE_AGX_RENDER_PROVIDER_JOB_CONFIG *JobConfig,
    const unsigned char *SubmissionBytes,
    APPLE_AGX_BACKEND_U32 SubmissionByteCount);

APPLE_AGX_BACKEND_BOOL AppleAgxRenderProviderRelocate(
    void *Context, void *Arena, APPLE_AGX_BACKEND_U32 ArenaBytes,
    const APPLE_AGX_RENDER_TEMPLATE_ROOTS *Roots,
    const unsigned char *SubmissionBytes,
    APPLE_AGX_BACKEND_U32 SubmissionByteCount,
    const APPLE_AGX_BACKEND_SUBMISSION *Submission,
    APPLE_AGX_BACKEND_JOB_IMAGE *Job);

#endif /* APPLE_AGX_RENDER_PROVIDER_H */

