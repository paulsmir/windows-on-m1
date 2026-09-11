#ifndef APPLE_AGX_CONTEXT0_BROKER_H
#define APPLE_AGX_CONTEXT0_BROKER_H
#include "apple_agx_initdata_memory.h"
#include "apple_agx_retained_root_client.h"
#define APPLE_AGX_CONTEXT0_MAX_LEAVES 256u
typedef struct _APPLE_AGX_CONTEXT0_LEAF {
  unsigned long long Va,Ipa,Pa,Handle;
  unsigned int Range,State;
} APPLE_AGX_CONTEXT0_LEAF;
typedef struct _APPLE_AGX_CONTEXT0_BROKER {
  APPLE_AGX_INITDATA_MEMORY_GRAPH *Graph;
  AGX_RR_IO Io;
  unsigned long long Epoch,Root;
  unsigned int Count,Mapped,Verified,Retired,Absent,FailedRange,LastOperation;
  unsigned char Uncertain;
  AGX_RR_RESPONSE LastResponse;
  APPLE_AGX_CONTEXT0_LEAF Leaves[APPLE_AGX_CONTEXT0_MAX_LEAVES];
} APPLE_AGX_CONTEXT0_BROKER;
typedef unsigned char (*APPLE_AGX_CONTEXT0_IPA)(void *,const APPLE_AGX_MEMORY_OBJECT *,
    unsigned long long,unsigned long long *);
enum {AppleAgxContext0Ok=0,AppleAgxContext0Invalid,AppleAgxContext0MapFailed,
      AppleAgxContext0QueryFailed,AppleAgxContext0RetireFailed};
int AppleAgxContext0BrokerMap(APPLE_AGX_CONTEXT0_BROKER *,APPLE_AGX_INITDATA_MEMORY_GRAPH *,
    const AGX_RR_IO *,unsigned long long,unsigned long long,APPLE_AGX_CONTEXT0_IPA,void *);
int AppleAgxContext0BrokerVerify(APPLE_AGX_CONTEXT0_BROKER *);
int AppleAgxContext0BrokerRetire(APPLE_AGX_CONTEXT0_BROKER *);
#endif
