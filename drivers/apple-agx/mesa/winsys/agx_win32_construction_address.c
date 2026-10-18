#include "agx_win32_construction_address.h"
#include <string.h>

#define ALIGN16K(x) (((x) + 0x3fffULL) & ~0x3fffULL)

AGX_WIN32_CONSTRUCTION_RESULT AgxWin32ConstructionInitialize(
    AGX_WIN32_CONSTRUCTION_SPACE *s, APPLE_AGX_U64 base, APPLE_AGX_U32 gen) {
  if (!s || !base || (base & 0x3fffULL) || !gen) return AgxWin32ConstructionArgument;
  memset(s, 0, sizeof(*s)); s->Base=base; s->Next=base; s->Generation=gen;
  return AgxWin32ConstructionSuccess;
}
AGX_WIN32_CONSTRUCTION_RESULT AgxWin32ConstructionReserve(
    AGX_WIN32_CONSTRUCTION_SPACE *s, APPLE_AGX_U64 token, APPLE_AGX_U64 serial,
    APPLE_AGX_U64 bytes, APPLE_AGX_U64 *address) {
  APPLE_AGX_U64 size; unsigned i;
  if(address) *address=0; if(!s||!address||!token||!serial||!bytes||!s->Generation) return AgxWin32ConstructionArgument;
  size=ALIGN16K(bytes); if(size<bytes||s->Next>~0ULL-size) return AgxWin32ConstructionRange;
  for(i=0;i<AGX_WIN32_CONSTRUCTION_MAX_OBJECTS;i++) if(s->Objects[i].Active&&s->Objects[i].Token==token) return s->Objects[i].Serial==serial?AgxWin32ConstructionState:AgxWin32ConstructionStale;
  for(i=0;i<AGX_WIN32_CONSTRUCTION_MAX_OBJECTS;i++) if(!s->Objects[i].Active) { s->Objects[i]=(AGX_WIN32_CONSTRUCTION_OBJECT){token,serial,s->Next,bytes,s->Generation,APPLE_AGX_TRUE}; *address=s->Next; s->Next+=size; return AgxWin32ConstructionSuccess; }
  return AgxWin32ConstructionCapacity;
}
AGX_WIN32_CONSTRUCTION_RESULT AgxWin32ConstructionResolve(
    const AGX_WIN32_CONSTRUCTION_SPACE *s, APPLE_AGX_U64 token, APPLE_AGX_U64 serial,
    APPLE_AGX_U64 off, APPLE_AGX_U64 bytes, APPLE_AGX_U64 *address) {
  unsigned i; if(address)*address=0; if(!s||!address||!token||!serial||!bytes)return AgxWin32ConstructionArgument;
  for(i=0;i<AGX_WIN32_CONSTRUCTION_MAX_OBJECTS;i++) { const AGX_WIN32_CONSTRUCTION_OBJECT *o=&s->Objects[i]; if(o->Active&&o->Token==token) { if(o->Serial!=serial||o->Generation!=s->Generation)return AgxWin32ConstructionStale; if(off>o->Bytes||bytes>o->Bytes-off)return AgxWin32ConstructionRange; *address=o->Address+off; return AgxWin32ConstructionSuccess; }} return AgxWin32ConstructionStale;
}
AGX_WIN32_CONSTRUCTION_RESULT AgxWin32ConstructionRelease(
    AGX_WIN32_CONSTRUCTION_SPACE *s, APPLE_AGX_U64 token, APPLE_AGX_U64 serial) { unsigned i; if(!s||!token||!serial)return AgxWin32ConstructionArgument; for(i=0;i<AGX_WIN32_CONSTRUCTION_MAX_OBJECTS;i++)if(s->Objects[i].Active&&s->Objects[i].Token==token){if(s->Objects[i].Serial!=serial)return AgxWin32ConstructionStale;memset(&s->Objects[i],0,sizeof(s->Objects[i]));return AgxWin32ConstructionSuccess;}return AgxWin32ConstructionStale; }
