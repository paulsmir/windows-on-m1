#ifndef APPLE_AGX_UAT_TABLE_H
#define APPLE_AGX_UAT_TABLE_H

#include "apple_agx_uat.h"

typedef struct _APPLE_AGX_UAT_PAGE {
  unsigned long long PhysicalAddress;
  unsigned long long *Entries;
  unsigned int Level;
} APPLE_AGX_UAT_PAGE;

typedef struct _APPLE_AGX_UAT_MAPPING {
  unsigned int Context;
  unsigned long long VirtualAddress;
  unsigned long long PhysicalAddress;
  unsigned long long Length;
  APPLE_AGX_UAT_PROTECTION Protection;
} APPLE_AGX_UAT_MAPPING;

typedef struct _APPLE_AGX_UAT_INVENTORY {
  APPLE_AGX_UAT_PAGE *Pages;
  unsigned int PageCapacity;
  unsigned int PageCount;
  APPLE_AGX_UAT_MAPPING *Mappings;
  unsigned int MappingCapacity;
  unsigned int MappingCount;
} APPLE_AGX_UAT_INVENTORY;

typedef struct _APPLE_AGX_UAT_ALLOCATOR {
  void *Context;
  unsigned char (*AllocatePage)(void *Context, APPLE_AGX_UAT_PAGE *Page);
  void (*ReleasePage)(void *Context, const APPLE_AGX_UAT_PAGE *Page);
} APPLE_AGX_UAT_ALLOCATOR;

APPLE_AGX_UAT_RESULT AppleAgxUatCreateAddressSpace(
    unsigned int Context, const APPLE_AGX_UAT_ALLOCATOR *Allocator,
    APPLE_AGX_UAT_INVENTORY *Inventory, APPLE_AGX_UAT_ROOTS *Roots);
APPLE_AGX_UAT_RESULT AppleAgxUatMap(
    unsigned int Context, const APPLE_AGX_UAT_ROOTS *Roots,
    unsigned long long VirtualAddress, unsigned long long PhysicalAddress,
    unsigned long long Length, APPLE_AGX_UAT_PROTECTION Protection,
    const APPLE_AGX_UAT_ALLOCATOR *Allocator,
    APPLE_AGX_UAT_INVENTORY *Inventory);
void AppleAgxUatDestroy(const APPLE_AGX_UAT_ALLOCATOR *Allocator,
                        APPLE_AGX_UAT_INVENTORY *Inventory);

#endif /* APPLE_AGX_UAT_TABLE_H */
