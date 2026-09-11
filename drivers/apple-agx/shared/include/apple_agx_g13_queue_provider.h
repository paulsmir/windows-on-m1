#ifndef APPLE_AGX_G13_QUEUE_PROVIDER_H
#define APPLE_AGX_G13_QUEUE_PROVIDER_H

#include "apple_agx_g13_queue_runtime.h"

#define APPLE_AGX_G13_QUEUE_PROVIDER_MAX_OBSERVATIONS 2u

typedef enum _APPLE_AGX_G13_QUEUE_PROVIDER_PHASE {
  AppleAgxG13QueueProviderInitialized = 0,
  AppleAgxG13QueueProviderCreated,
  AppleAgxG13QueueProvider3dStaged,
  AppleAgxG13QueueProviderSubmitted,
  AppleAgxG13QueueProviderFaulted,
} APPLE_AGX_G13_QUEUE_PROVIDER_PHASE;

typedef enum _APPLE_AGX_G13_QUEUE_PROVIDER_INGEST_GUARD {
  AppleAgxG13QueueProviderIngestGuardOk = 0u,
  AppleAgxG13QueueProviderIngestGuardInvalid = 1u,
  AppleAgxG13QueueProviderIngestGuardRuntime = 2u,
  AppleAgxG13QueueProviderIngestGuardD3Observation = 3u,
  AppleAgxG13QueueProviderIngestGuardTaObservation = 4u,
  AppleAgxG13QueueProviderIngestGuardCompletionFence = 5u,
} APPLE_AGX_G13_QUEUE_PROVIDER_INGEST_GUARD;

typedef struct _APPLE_AGX_G13_QUEUE_PROVIDER_IO {
  void *Context;
  APPLE_AGX_BACKEND_BOOL (*BuildSubmission)(
      void *Context, const APPLE_AGX_BACKEND_JOB_IMAGE *Job,
      APPLE_AGX_BACKEND_U32 Fence,
      APPLE_AGX_G13_QUEUE_RUNTIME_SUBMISSION *Submission);
} APPLE_AGX_G13_QUEUE_PROVIDER_IO;

typedef struct _APPLE_AGX_G13_QUEUE_PROVIDER_EVENT_BATCH {
  APPLE_AGX_BACKEND_OBSERVATION
      Observations[APPLE_AGX_G13_QUEUE_PROVIDER_MAX_OBSERVATIONS];
  APPLE_AGX_BACKEND_U32 ObservationCount;
  /* Durable apply cursor; retries never replay an accepted observation. */
  APPLE_AGX_BACKEND_U32 AppliedObservationCount;
  APPLE_AGX_BACKEND_U32 CompletedFence;
} APPLE_AGX_G13_QUEUE_PROVIDER_EVENT_BATCH;

typedef struct _APPLE_AGX_G13_QUEUE_JOB_PLAN {
  APPLE_AGX_BACKEND_U32 TaExpectedDonePointer;
  APPLE_AGX_BACKEND_U32 D3ExpectedDonePointer;
  APPLE_AGX_BACKEND_BOOL IncludeInitBm;
} APPLE_AGX_G13_QUEUE_JOB_PLAN;

/* A side-effect-free sample of the exact fence currently submitted to AGX. */
typedef struct _APPLE_AGX_G13_QUEUE_PROGRESS {
  APPLE_AGX_BACKEND_U32 Fence;
  APPLE_AGX_G13_QUEUE_PROVIDER_PHASE ProviderPhase;
  APPLE_AGX_G13_QUEUE_RUNTIME_PHASE RuntimePhase;
  APPLE_AGX_BACKEND_U32 TaDonePointer;
  APPLE_AGX_BACKEND_U32 TaStamp;
  APPLE_AGX_BACKEND_BOOL TaEventSeen;
  APPLE_AGX_BACKEND_BOOL TaComplete;
  APPLE_AGX_BACKEND_U32 D3DonePointer;
  APPLE_AGX_BACKEND_U32 D3Stamp;
  APPLE_AGX_BACKEND_BOOL D3EventSeen;
  APPLE_AGX_BACKEND_BOOL D3Complete;
} APPLE_AGX_G13_QUEUE_PROGRESS;

typedef struct _APPLE_AGX_G13_QUEUE_PROVIDER_STAGED_3D {
  APPLE_AGX_BACKEND_U64
      WorkAddresses[APPLE_AGX_BACKEND_QUEUE_WORK_COUNT];
  APPLE_AGX_BACKEND_U32 WorkAddressCount;
  APPLE_AGX_BACKEND_U32 Event;
  APPLE_AGX_BACKEND_U32 ExpectedStamp;
  APPLE_AGX_BACKEND_U32 ExpectedDonePointer;
} APPLE_AGX_G13_QUEUE_PROVIDER_STAGED_3D;

typedef struct _APPLE_AGX_G13_QUEUE_PROVIDER {
  APPLE_AGX_G13_QUEUE_PROVIDER_PHASE Phase;
  APPLE_AGX_G13_QUEUE_RUNTIME_CONFIG Config;
  APPLE_AGX_G13_QUEUE_RUNTIME_IO RuntimeIo;
  APPLE_AGX_G13_QUEUE_PROVIDER_IO ProviderIo;
  APPLE_AGX_G13_QUEUE_RUNTIME Runtime;
  APPLE_AGX_G13_QUEUE_PROVIDER_STAGED_3D Staged3d;
  APPLE_AGX_BACKEND_U32 PendingFence;
  APPLE_AGX_BACKEND_BOOL FailureQuiesced;
  APPLE_AGX_BACKEND_U32 LastIngestGuard;
  APPLE_AGX_BACKEND_U32 LastIngestRuntimeResult;
} APPLE_AGX_G13_QUEUE_PROVIDER;

APPLE_AGX_BACKEND_BOOL AppleAgxG13QueueProviderInitialize(
    APPLE_AGX_G13_QUEUE_PROVIDER *Provider,
    const APPLE_AGX_G13_QUEUE_RUNTIME_CONFIG *Config,
    const APPLE_AGX_G13_QUEUE_RUNTIME_IO *RuntimeIo,
    const APPLE_AGX_G13_QUEUE_PROVIDER_IO *ProviderIo);

/* Installs only Io->Queues. Io->Context must address this Provider. */
APPLE_AGX_BACKEND_BOOL AppleAgxG13QueueProviderInstall(
    APPLE_AGX_BACKEND_IO *Io);

APPLE_AGX_BACKEND_BOOL AppleAgxG13QueueProviderIngestEvent(
    APPLE_AGX_G13_QUEUE_PROVIDER *Provider, const unsigned char *Message,
    APPLE_AGX_BACKEND_U32 MessageBytes,
    APPLE_AGX_G13_QUEUE_PROVIDER_EVENT_BATCH *Batch);

/*
 * Check the queue deadline without inventing completion.  A timeout is
 * returned as an observation only after the runtime's platform Quiesce
 * callback has synchronously stopped the exact pending fence.
 */
APPLE_AGX_BACKEND_BOOL AppleAgxG13QueueProviderCheckTimeout(
    APPLE_AGX_G13_QUEUE_PROVIDER *Provider,
    APPLE_AGX_BACKEND_U64 NowTicks,
    APPLE_AGX_G13_QUEUE_PROVIDER_EVENT_BATCH *Batch);

/* Read-only plan for the next atomic D3+TA publication. */
APPLE_AGX_BACKEND_BOOL AppleAgxG13QueueProviderPlanJob(
    APPLE_AGX_G13_QUEUE_PROVIDER *Provider,
    APPLE_AGX_G13_QUEUE_JOB_PLAN *Plan);

APPLE_AGX_BACKEND_BOOL AppleAgxG13QueueProviderQueryProgress(
    const APPLE_AGX_G13_QUEUE_PROVIDER *Provider,
    APPLE_AGX_G13_QUEUE_PROGRESS *Progress);

APPLE_AGX_BACKEND_BOOL AppleAgxG13QueueProgressHasAdvanced(
    const APPLE_AGX_G13_QUEUE_PROGRESS *Previous,
    const APPLE_AGX_G13_QUEUE_PROGRESS *Current);

#endif /* APPLE_AGX_G13_QUEUE_PROVIDER_H */
