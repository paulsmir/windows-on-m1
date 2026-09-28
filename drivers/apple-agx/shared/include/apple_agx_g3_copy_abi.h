#ifndef APPLE_AGX_G3_COPY_ABI_H
#define APPLE_AGX_G3_COPY_ABI_H
#include "apple_agx_win32_abi.h"

/* Buffered CPU staging transfer, never a user pointer or physical address. */
#define APPLE_AGX_G3_COPY_MAGIC 0x43565041u
#define APPLE_AGX_G3_COPY_VERSION 1u
#define APPLE_AGX_G3_COPY_CAPACITY 65536u
#define APPLE_AGX_G3_COPY_QUERY 0u
#define APPLE_AGX_G3_COPY_UPLOAD 1u
#define APPLE_AGX_G3_COPY_DOWNLOAD 2u
typedef struct _APPLE_AGX_G3_COPY_REQUEST {
  APPLE_AGX_U32 Magic, Version, Bytes, Operation;
  APPLE_AGX_U32 Allocation, Reserved;
  APPLE_AGX_U64 Offset, GpuVa, MappingGeneration, ProcessGeneration;
  APPLE_AGX_U32 TransferBytes, Reserved2;
  unsigned char Data[APPLE_AGX_G3_COPY_CAPACITY];
} APPLE_AGX_G3_COPY_REQUEST;
#endif
