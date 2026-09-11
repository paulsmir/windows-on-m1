#ifndef APPLE_AGX_RETAINED_ROOT_CLIENT_H
#define APPLE_AGX_RETAINED_ROOT_CLIENT_H
#include "apple_agx_retained_root_abi.h"
typedef struct _AGX_RR_IO {
  void *Context;
  unsigned long long (*Read64)(void *, unsigned int);
  void (*Write64)(void *, unsigned int, unsigned long long);
  void (*Write32)(void *, unsigned int, unsigned int);
} AGX_RR_IO;
static inline unsigned char AgxRrExchange(const AGX_RR_IO *io,
    AGX_RR_REQUEST *q, AGX_RR_RESPONSE *r) {
  unsigned int i,j;
  unsigned long long word;
  if (!io || !q || !r || !io->Read64 || !io->Write64 || !io->Write32) return 0;
  q->Version=AGX_RR_ABI_VERSION; q->Bytes=sizeof(*q); q->Reserved=0;
  q->Sequence=io->Read64(io->Context,AGX_RR_OFFSET+AGX_RR_RESPONSE_OFFSET)+1;
  if (!q->Sequence) return 0;
  for(i=0;i<sizeof(*q);i+=8) {
    word=0;
    for(j=0;j<8;++j) word|=(unsigned long long)((unsigned char *)q)[i+j]<<(8*j);
    io->Write64(io->Context,AGX_RR_OFFSET+i,word);
  }
  io->Write32(io->Context,AGX_RR_OFFSET+AGX_RR_DOORBELL,1);
  for(i=0;i<sizeof(*r);i+=8) {
    word=io->Read64(io->Context,AGX_RR_OFFSET+AGX_RR_RESPONSE_OFFSET+i);
    for(j=0;j<8;++j) ((unsigned char *)r)[i+j]=(unsigned char)(word>>(8*j));
  }
  return r->Receipt==q->Sequence && r->Status==0;
}
#endif
