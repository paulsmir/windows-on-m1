#ifndef APPLE_AGX_G3_PRIVATE_FAILURE_H
#define APPLE_AGX_G3_PRIVATE_FAILURE_H

/* Versioned, immutable diagnostic only. No handles are dereferenced by readers. */
typedef struct {
  unsigned Version, Bytes, Branch, Status;
  unsigned Pid, Operation, TablePredicate, MapPredicate;
  unsigned PreparePredicate, FailedRange, MapLeafOffset, Reserved;
  unsigned Width, Height, UtileWidth, UtileHeight, Layers, Samples;
  unsigned GlobalUnits, OwnerUnits, LargestFreeUnits, PoolUnits, OwnerLimitUnits;
  unsigned SceneCount, ContextSceneCount, QueuedScenes, ReleaseRequestedScenes,
      QuarantinedScenes;
  unsigned ManagerPresent, FreshManager, Poisoned, GraphUncertain, JobInFlight;
  unsigned RequiredBytes[9];
  unsigned long long ProcessId, ProcessHandle, ContextHandle, DeviceHandle,
      ManagerGeneration, PrivateVa, RootIpa, LeaseToken;
} APPLE_AGX_G3_PRIVATE_FAILURE;

#endif
