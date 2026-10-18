#ifndef APPLE_AGX_RESIDENCY_H
#define APPLE_AGX_RESIDENCY_H

#include "apple_agx_state.h"
#include "apple_agx_memory.h"
#include "apple_agx_uat_memory.h"
#include "apple_agx_uat_table.h"

#define APPLE_AGX_RESIDENCY_WDDM_PAGE_SIZE 0x10000ULL
#define APPLE_AGX_RESIDENCY_GEM_VA_BASE 0x1500000000ULL

typedef enum _APPLE_AGX_RESIDENCY_CONTEXT_RESULT {
  AppleAgxResidencyContextResultOk = 0,
  AppleAgxResidencyContextResultInvalidArgument,
  AppleAgxResidencyContextResultBusy,
  AppleAgxResidencyContextResultMemoryOwner,
  AppleAgxResidencyContextResultUat,
} APPLE_AGX_RESIDENCY_CONTEXT_RESULT;

typedef struct _APPLE_AGX_RESIDENCY_CONTEXT {
  unsigned int Context;
  APPLE_AGX_BOOL Initialized;
  APPLE_AGX_UAT_MEMORY_OWNER MemoryOwner;
  APPLE_AGX_UAT_ALLOCATOR Allocator;
  APPLE_AGX_UAT_INVENTORY Inventory;
  APPLE_AGX_UAT_ROOTS Roots;
} APPLE_AGX_RESIDENCY_CONTEXT;

typedef struct _APPLE_AGX_RESIDENCY_STATUS {
  APPLE_AGX_MEMORY_RESULT MemoryResult;
  APPLE_AGX_UAT_RESULT UatResult;
  APPLE_AGX_UAT_MEMORY_RESULT UatMemoryResult;
  APPLE_AGX_RESIDENCY_CONTEXT_RESULT ContextResult;
} APPLE_AGX_RESIDENCY_STATUS;

/*
 * Creates one independently owned Apple UAT address space.  The caller owns
 * the fixed-capacity inventories; table backing pages are allocated through
 * MemoryIo and released by AppleAgxResidencyContextDestroy.
 */
APPLE_AGX_BOOL AppleAgxResidencyContextCreate(
    APPLE_AGX_RESIDENCY_CONTEXT *ResidencyContext, unsigned int Context,
    const APPLE_AGX_MEMORY_IO *MemoryIo,
    APPLE_AGX_MEMORY_OBJECT *PageObjects, unsigned int PageObjectCapacity,
    APPLE_AGX_UAT_PAGE *Pages, unsigned int PageCapacity,
    APPLE_AGX_UAT_MAPPING *Mappings, unsigned int MappingCapacity,
    APPLE_AGX_RESIDENCY_STATUS *Status);

/* Refuses to destroy a context while any exact mapping is still resident. */
APPLE_AGX_BOOL AppleAgxResidencyContextDestroy(
    APPLE_AGX_RESIDENCY_CONTEXT *ResidencyContext,
    APPLE_AGX_RESIDENCY_STATUS *Status);

/*
 * Atomically connects one 64-KiB-granular WDDM allocation to an Apple UAT
 * context.  AppleAgxUatMap expands the range into 16-KiB hardware leaves.
 */
APPLE_AGX_BOOL AppleAgxResidencyMap64K(
    APPLE_AGX_MEMORY_OBJECT *Object, unsigned int Context,
    const APPLE_AGX_UAT_ROOTS *Roots, unsigned long long GpuVirtualAddress,
    const APPLE_AGX_UAT_ALLOCATOR *Allocator,
    APPLE_AGX_UAT_INVENTORY *Inventory, APPLE_AGX_RESIDENCY_STATUS *Status);

/* Removes the exact mapping only when no submitted fence remains in flight. */
APPLE_AGX_BOOL AppleAgxResidencyUnmap64K(
    APPLE_AGX_MEMORY_OBJECT *Object, const APPLE_AGX_UAT_ROOTS *Roots,
    const APPLE_AGX_UAT_ALLOCATOR *Allocator,
    APPLE_AGX_UAT_INVENTORY *Inventory, APPLE_AGX_RESIDENCY_STATUS *Status);

#endif /* APPLE_AGX_RESIDENCY_H */
