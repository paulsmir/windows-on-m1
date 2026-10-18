#ifndef APPLE_AGX_SUBMISSION_COORDINATOR_H
#define APPLE_AGX_SUBMISSION_COORDINATOR_H

#include "apple_agx_event_allocator.h"
#include "apple_agx_exp208_dynamic.h"
#include "apple_agx_g13_queue_provider.h"
#include "apple_agx_render_provider.h"
#include "apple_agx_render_template.h"

typedef struct _APPLE_AGX_SUBMISSION_COORDINATOR_CONFIG {
  APPLE_AGX_RENDER_PROVIDER *RenderProvider;
  APPLE_AGX_G13_QUEUE_PROVIDER *QueueProvider;
  APPLE_AGX_EVENT_PAIR EventPair;
} APPLE_AGX_SUBMISSION_COORDINATOR_CONFIG;

typedef struct _APPLE_AGX_SUBMISSION_COORDINATOR {
  APPLE_AGX_RENDER_PROVIDER *RenderProvider;
  APPLE_AGX_G13_QUEUE_PROVIDER *QueueProvider;
  APPLE_AGX_RENDER_SHARED_MEMORY_OWNER *RenderSharedMemory;
  APPLE_AGX_EVENT_PAIR EventPair;
  APPLE_AGX_EXP208_RELOCATION_OBJECT
      Objects[APPLE_AGX_RENDER_TEMPLATE_RUNTIME_OBJECT_COUNT];
  APPLE_AGX_U32 Sequence;
  APPLE_AGX_BOOL Initialized;
} APPLE_AGX_SUBMISSION_COORDINATOR;

APPLE_AGX_BOOL AppleAgxSubmissionCoordinatorInitialize(
    APPLE_AGX_SUBMISSION_COORDINATOR *Coordinator,
    const APPLE_AGX_SUBMISSION_COORDINATOR_CONFIG *Config);

APPLE_AGX_BOOL AppleAgxSubmissionCoordinatorStage(
    APPLE_AGX_SUBMISSION_COORDINATOR *Coordinator,
    const APPLE_AGX_BACKEND_SUBMISSION *Submission,
    const unsigned char *SubmissionBytes,
    APPLE_AGX_U32 SubmissionByteCount);

APPLE_AGX_BOOL AppleAgxSubmissionCoordinatorReset(
    APPLE_AGX_SUBMISSION_COORDINATOR *Coordinator);

#endif /* APPLE_AGX_SUBMISSION_COORDINATOR_H */
