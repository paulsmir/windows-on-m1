#ifndef APPLE_AGX_UAT_PUBLICATION_H
#define APPLE_AGX_UAT_PUBLICATION_H

#include "apple_agx_config_snapshot.h"
#include "apple_agx_uat.h"
#include "j313_agx_g2.generated.h"

typedef enum _APPLE_AGX_UAT_PUBLICATION_RESULT {
  AppleAgxUatPublicationResultOk = 0,
  AppleAgxUatPublicationResultInvalidArgument,
  AppleAgxUatPublicationResultMapFailed,
  AppleAgxUatPublicationResultUnmapFailed,
} APPLE_AGX_UAT_PUBLICATION_RESULT;

typedef struct _APPLE_AGX_UAT_PUBLICATION_IO {
  void *Context;
  unsigned char (*Map)(void *Context, unsigned long long PhysicalAddress,
                       unsigned int Length,
                       volatile unsigned char **VirtualAddress);
  void (*Barrier)(void *Context);
  unsigned char (*Unmap)(void *Context,
                         volatile unsigned char *VirtualAddress);
} APPLE_AGX_UAT_PUBLICATION_IO;

typedef struct _APPLE_AGX_UAT_PUBLICATION_STATE {
  volatile unsigned char *MappedBase;
  unsigned long long OriginalTtbr0;
  unsigned long long OriginalTtbr1;
  unsigned long long PublishedTtbr0;
  unsigned long long PublishedTtbr1;
  unsigned char Active;
} APPLE_AGX_UAT_PUBLICATION_STATE;

APPLE_AGX_UAT_PUBLICATION_RESULT AppleAgxUatPublishJ313(
    const APPLE_AGX_CONFIG_SNAPSHOT *Snapshot,
    const APPLE_AGX_UAT_TTBR_PAIR *Pair,
    const APPLE_AGX_UAT_PUBLICATION_IO *Io,
    APPLE_AGX_UAT_PUBLICATION_STATE *State);
APPLE_AGX_UAT_PUBLICATION_RESULT AppleAgxUatUnpublishJ313(
    const APPLE_AGX_UAT_PUBLICATION_IO *Io,
    APPLE_AGX_UAT_PUBLICATION_STATE *State);

#endif /* APPLE_AGX_UAT_PUBLICATION_H */
