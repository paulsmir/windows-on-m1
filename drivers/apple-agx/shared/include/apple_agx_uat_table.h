#ifndef APPLE_AGX_UAT_TABLE_H
#define APPLE_AGX_UAT_TABLE_H

#include "apple_agx_uat.h"

typedef struct _APPLE_AGX_UAT_PAGE {
  unsigned long long PhysicalAddress;
  unsigned long long *Entries;
  unsigned int Level;
} APPLE_AGX_UAT_PAGE;

/* A mapping is neither contiguous nor a repeated dummy page. */
#define APPLE_AGX_UAT_PHYSICAL_STRIDE_SCATTER (~0ULL)

typedef struct _APPLE_AGX_UAT_MAPPING {
  unsigned int Context;
  unsigned long long VirtualAddress;
  unsigned long long PhysicalAddress;
  unsigned long long Length;
  APPLE_AGX_UAT_PROTECTION Protection;
  /* 16 KiB for contiguous, zero for dummy, SCATTER for a physical page list. */
  unsigned long long PhysicalStride;
} APPLE_AGX_UAT_MAPPING;

typedef struct _APPLE_AGX_UAT_RANGE {
  unsigned long long VirtualAddress;
  unsigned long long PhysicalAddress;
  unsigned long long Length;
  APPLE_AGX_UAT_PROTECTION Protection;
} APPLE_AGX_UAT_RANGE;

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
/*
 * Maps one 16-KiB physical page per contiguous GPU VA leaf.  This is the
 * truthful form for Windows MDL/ADL backing: CPU virtual contiguity must not
 * be misrepresented as one contiguous physical AGX range.
 */
APPLE_AGX_UAT_RESULT AppleAgxUatMapPageList(
    unsigned int Context, const APPLE_AGX_UAT_ROOTS *Roots,
    unsigned long long VirtualAddress, const unsigned long long *PhysicalPages,
    unsigned int PageCount, APPLE_AGX_UAT_PROTECTION Protection,
    const APPLE_AGX_UAT_ALLOCATOR *Allocator,
    APPLE_AGX_UAT_INVENTORY *Inventory);
/* Read-only walk of the exact CPU-owned page-table image.  This is used by
 * qualification and diagnostics; it never creates tables or mappings. */
APPLE_AGX_UAT_RESULT AppleAgxUatResolvePage(
    unsigned int Context, const APPLE_AGX_UAT_ROOTS *Roots,
    unsigned long long VirtualAddress, APPLE_AGX_UAT_INVENTORY *Inventory,
    unsigned long long *PhysicalAddress,
    unsigned long long *Descriptor);
APPLE_AGX_UAT_RESULT AppleAgxUatUnmap(
    unsigned int Context, const APPLE_AGX_UAT_ROOTS *Roots,
    unsigned long long VirtualAddress, unsigned long long Length,
    const APPLE_AGX_UAT_ALLOCATOR *Allocator,
    APPLE_AGX_UAT_INVENTORY *Inventory);
APPLE_AGX_UAT_RESULT AppleAgxUatMapBatch(
    unsigned int Context, const APPLE_AGX_UAT_ROOTS *Roots,
    const APPLE_AGX_UAT_RANGE *Ranges, unsigned int RangeCount,
    const APPLE_AGX_UAT_ALLOCATOR *Allocator,
    APPLE_AGX_UAT_INVENTORY *Inventory);
APPLE_AGX_UAT_RESULT AppleAgxUatUnmapBatch(
    unsigned int Context, const APPLE_AGX_UAT_ROOTS *Roots,
    const APPLE_AGX_UAT_RANGE *Ranges, unsigned int RangeCount,
    const APPLE_AGX_UAT_ALLOCATOR *Allocator,
    APPLE_AGX_UAT_INVENTORY *Inventory);
APPLE_AGX_UAT_RESULT AppleAgxUatReplaceBatchWithPage(
    unsigned int Context, const APPLE_AGX_UAT_ROOTS *Roots,
    const APPLE_AGX_UAT_RANGE *Ranges, unsigned int RangeCount,
    unsigned long long ReplacementPhysicalAddress,
    APPLE_AGX_UAT_PROTECTION ReplacementProtection,
    const APPLE_AGX_UAT_ALLOCATOR *Allocator,
    APPLE_AGX_UAT_INVENTORY *Inventory);
APPLE_AGX_UAT_RESULT AppleAgxUatReplaceBatch(
    unsigned int Context, const APPLE_AGX_UAT_ROOTS *Roots,
    const APPLE_AGX_UAT_RANGE *OldRanges,
    const APPLE_AGX_UAT_RANGE *NewRanges, unsigned int RangeCount,
    const APPLE_AGX_UAT_ALLOCATOR *Allocator,
    APPLE_AGX_UAT_INVENTORY *Inventory);
void AppleAgxUatDestroy(const APPLE_AGX_UAT_ALLOCATOR *Allocator,
                        APPLE_AGX_UAT_INVENTORY *Inventory);

#endif /* APPLE_AGX_UAT_TABLE_H */
