#ifndef APPLE_AGX_GPUVA_B1_ROOTS_H
#define APPLE_AGX_GPUVA_B1_ROOTS_H
#include "apple_agx_gpuva_b1_graph.h"
#include "apple_agx_gpuva_broker_v5_client.h"
#define APPLE_AGX_GPUVA_B1_MAX_GRANTS 285u

typedef struct _APPLE_AGX_GPUVA_B1_ROOT_INPUT {
  unsigned long long ProcessId, Generation;
  unsigned long long RootIpa, L1Ipa;
  unsigned long long BackendL2Ipa, AliasL2Ipa, OutputL2Ipa;
  unsigned long long GraphGeneration, OutputGeneration;
  unsigned int Paging;
} APPLE_AGX_GPUVA_B1_ROOT_INPUT;
typedef struct _APPLE_AGX_GPUVA_B1_GRANT {
  unsigned long long Ipa, Generation;
  unsigned int Shared;
} APPLE_AGX_GPUVA_B1_GRANT;
typedef struct _APPLE_AGX_GPUVA_B1_ROOT {
  APPLE_AGX_GPUVA_B1_ROOT_INPUT Input;
  APPLE_AGX_GPUVA_B1_PAGE Mapped[APPLE_AGX_GPUVA_B1_GRAPH_PAGES];
  APPLE_AGX_GPUVA_B1_GRANT Grants[APPLE_AGX_GPUVA_B1_MAX_GRANTS];
  unsigned int MappedCount, GrantCount, TableCount, ParentCount;
  unsigned int Created, LastStatus, CleanupStatus, Uncertain;
} APPLE_AGX_GPUVA_B1_ROOT;

APPLE_AGX_BOOL AppleAgxGpuvaB1BuildRoot(
    APPLE_AGX_GPUVA_V5_CLIENT *, const APPLE_AGX_GPUVA_B1_ROOT_INPUT *,
    const APPLE_AGX_GPUVA_B1_PAGE *, unsigned int PageCount,
    APPLE_AGX_GPUVA_B1_ROOT *);
APPLE_AGX_BOOL AppleAgxGpuvaB1DestroyRoot(
    APPLE_AGX_GPUVA_V5_CLIENT *, APPLE_AGX_GPUVA_B1_ROOT *);
#endif
