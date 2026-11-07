/*
 * Host test for the Mu PEI iBoot-carveout reservation (RamCarveoutReservation.h and the
 * ReserveRamCarveouts/ReserveMemoryRegion/AgxLocalReserveHobContains bodies extracted
 * verbatim from MemoryInitPeiLib.c by tests/test_mu_ram_carveout_reservation.py into
 * "extracted_pei.inc").
 *
 * Invariants:
 *  - the plan equals m1n1 stage2_policy.in_ram_carveouts() for both J313 guest windows;
 *  - replaying the GPU-line (EXP2196, expanded profile) PEI sequence reproduces the
 *    observed DXE map, and adding the carveout step changes exactly the planned pages
 *    from conventional (7) to reserved (0) -- every other page is identical;
 *  - with a reserved RAM-disk window (exp392 behaviour) the step reserves nothing;
 *  - overlap with firmware (FD / UEFI memory) fails closed; owned ranges are subtracted.
 */
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "RamCarveoutReservation.h"

/* ------------------------------------------------------------------ PEI stubs */
typedef uint32_t UINT32;
typedef uint8_t UINT8;
typedef char CHAR8;
typedef void VOID;
typedef uint64_t EFI_PHYSICAL_ADDRESS;
typedef uint32_t EFI_RESOURCE_ATTRIBUTE_TYPE;
#define STATIC static
#define EFI_HOB_TYPE_RESOURCE_DESCRIPTOR 3
#define EFI_RESOURCE_SYSTEM_MEMORY 0
#define EFI_RESOURCE_MEMORY_RESERVED 5
#define EfiReservedMemoryType 0
#define DEBUG_INFO 1
#define DEBUG_ERROR 2
#define DEBUG(x) do { if (verbose) debug_print x; } while (0)
#define ASSERT(x) do { if (!(x)) assert_hits++; } while (0)
#define AsciiSPrint(buf, len, fmt, ...) snprintf((buf), (len), (fmt), __VA_ARGS__)
#define PcdGet64(name) pcd_##name

static int verbose, assert_hits;
static void debug_print(int level, const char *fmt, ...) { (void)level; (void)fmt; }

typedef struct {
  struct { int HobType; } Header;
  int ResourceType;
  EFI_RESOURCE_ATTRIBUTE_TYPE ResourceAttribute;
  uint64_t PhysicalStart;
  uint64_t ResourceLength;
} FAKE_RESOURCE;
typedef struct { FAKE_RESOURCE *ResourceDescriptor; } FAKE_PTR;
typedef union { void *Raw; FAKE_RESOURCE *ResourceDescriptor; } EFI_PEI_HOB_POINTERS;

#define MAX_HOBS 64
static FAKE_RESOURCE hobs[MAX_HOBS + 1];
static int hob_count;
static struct { uint64_t base, size; int type; } allocs[64];
static int alloc_count;

static void *GetHobList(void) { return hob_count ? &hobs[0] : NULL; }
static void *GetNextHob(int type, void *raw)
{
  FAKE_RESOURCE *h = raw;
  for (; h && h < &hobs[hob_count]; ++h)
    if (h->Header.HobType == type) return h;
  return NULL;
}
#define GET_NEXT_HOB(p) ((void *)((p).ResourceDescriptor + 1))
static void BuildResourceDescriptorHob(int type, EFI_RESOURCE_ATTRIBUTE_TYPE attr,
                                       uint64_t base, uint64_t len)
{
  assert(hob_count < MAX_HOBS);
  hobs[hob_count++] = (FAKE_RESOURCE){{EFI_HOB_TYPE_RESOURCE_DESCRIPTOR}, type, attr, base, len};
}
static void BuildMemoryAllocationHob(uint64_t base, uint64_t size, int type)
{
  allocs[alloc_count++] = (__typeof__(allocs[0])){base, size, type};
}

static uint64_t pcd_PcdSystemMemoryBase, pcd_PcdSystemMemorySize;

/* Fake ADT: /chosen/carveout-memory-map with 4-byte-aligned 16-byte values. */
typedef struct { int unused; } dt_node_t;
static dt_node_t carveout_node;
static struct { unsigned id; uint64_t base, size; } adt[64];
static int adt_count;
static unsigned char adt_blob[64][20];
static dt_node_t *dt_get(const char *name)
{
  return strcmp(name, "/chosen/carveout-memory-map") ? NULL : &carveout_node;
}
static void *dt_node_prop(dt_node_t *node, const char *name, size_t *len)
{
  assert(node == &carveout_node);
  for (int i = 0; i < adt_count; ++i) {
    char want[24];
    snprintf(want, sizeof(want), "region-id-%u", adt[i].id);
    if (!strcmp(want, name)) {
      memcpy(adt_blob[i] + 4, &adt[i].base, 8);   /* +4: not 8-byte aligned */
      memcpy(adt_blob[i] + 12, &adt[i].size, 8);
      *len = 16;
      return adt_blob[i] + 4;
    }
  }
  return NULL;
}

#include "extracted_pei.inc"

/* ------------------------------------------------------------------ J313 data */
/* ANSDART001-R3 /chosen/carveout-memory-map entries touching DRAM 0x800000000.. */
static const struct { unsigned id; uint64_t base, size; } J313[] = {
  {24, 0x802078000, 0x1dd690000},                               /* kernel DRAM descriptor */
  {19, 0x860500000, 0xfa0000},  {23, 0x864000000, 0x4000},
  {158, 0x864004000, 0x200000}, {167, 0x864204000, 0x7a00000},
  {163, 0x86bc08000, 0x40000},  {165, 0x86bc4c000, 0x440000},
  {159, 0x86c090000, 0x1000000}, {181, 0x86d094000, 0x10000},
  {190, 0x86d0a4000, 0x10000},  {182, 0x86d0b4000, 0x48000},
  {176, 0x86d0fc000, 0x400000}, {180, 0x86d4fc000, 0x2000000},
  {179, 0x86f4fc000, 0x1c000},
  {25, 0x8019b0000, 0x6c8000},  {53, 0x801578000, 0x438000},    /* below both windows */
  {49, 0x800220000, 0x704000},  {14, 0x9e2638000, 0x2d5c000},   /* vram above mem_top */
  {5, 0x9df708000, 0x8000},     {4, 0x9df710000, 0x1c28000},
};
#define MEM_TOP 0x9df708000ULL

static void load_adt(void)
{
  adt_count = 0;
  for (size_t i = 0; i < sizeof(J313) / sizeof(J313[0]); ++i)
    adt[adt_count++] = (__typeof__(adt[0])){J313[i].id, J313[i].base, J313[i].size};
}

/* ------------------------------------------------------------------ planner tests */
static void expect_hv_ranges(uint64_t ram_base)
{
  RAM_CARVEOUT_RANGE in[32], out[32];
  UINTN n = 0, count = 0;
  for (size_t i = 0; i < sizeof(J313) / sizeof(J313[0]); ++i)
    in[n++] = (RAM_CARVEOUT_RANGE){J313[i].base, J313[i].size};
  assert(RamCarveoutPlan(in, n, ram_base, MEM_TOP - ram_base, NULL, 0, out, 32, &count));
  assert(count == 2);
  assert(out[0].Base == 0x860500000 && out[0].Size == 0xfa0000);
  assert(out[1].Base == 0x864000000 && out[1].Base + out[1].Size == 0x86f518000);
}

static void planner_tests(void)
{
  RAM_CARVEOUT_RANGE in[2] = {{0x860500000, 0xfa0000}, {0x864000000, 0x4000}}, out[8];
  UINTN count = 99;

  expect_hv_ranges(0x850000000);
  expect_hv_ranges(0x818000000);

  /* Owned range splits a carveout; owned range covering it removes it. */
  RAM_CARVEOUT_EXCLUSION owned = {0x860600000, 0x100000, FALSE};
  assert(RamCarveoutPlan(in, 1, 0x850000000, MEM_TOP - 0x850000000, &owned, 1, out, 8, &count));
  assert(count == 2 && out[0].Base == 0x860500000 && out[0].Size == 0x100000 &&
         out[1].Base == 0x860700000 && out[1].Base + out[1].Size == 0x8614a0000);
  RAM_CARVEOUT_EXCLUSION window = {0x860000000, 0x40000000, FALSE};
  assert(RamCarveoutPlan(in, 2, 0x850000000, MEM_TOP - 0x850000000, &window, 1, out, 8, &count));
  assert(count == 0);

  /* Firmware overlap fails closed. */
  RAM_CARVEOUT_EXCLUSION fd = {0x860400000, 0x200000, TRUE};
  assert(!RamCarveoutPlan(in, 1, 0x850000000, MEM_TOP - 0x850000000, &fd, 1, out, 8, &count));
  assert(count == 0);

  /* Malformed input and capacity overflow fail closed. */
  RAM_CARVEOUT_RANGE wrap = {~0ULL - 0x1000, 0x2000};
  assert(!RamCarveoutPlan(&wrap, 1, 0x850000000, 0x4000, NULL, 0, out, 8, &count));
  assert(!RamCarveoutPlan(in, 1, 0x850000001, 0x4000, NULL, 0, out, 8, &count));
  assert(!RamCarveoutPlan(in, 2, 0x850000000, MEM_TOP - 0x850000000, NULL, 0, out, 1, &count));
  assert(count == 0);

  /* Unaligned carveout widened to pages, clipped to RAM. */
  RAM_CARVEOUT_RANGE odd = {0x84fffe000, 0x5000};
  assert(RamCarveoutPlan(&odd, 1, 0x850000000, 0x100000, NULL, 0, out, 8, &count));
  assert(count == 1 && out[0].Base == 0x850000000 && out[0].Size == 0x4000);
}

/* ------------------------------------------------------------------ PEI replay */
#define RAM_BASE 0x818000000ULL
#define PAGES ((MEM_TOP - RAM_BASE) / 0x4000)
static unsigned char map_without[PAGES], map_with[PAGES];

static void replay(int ramdisk_reserved, int with_carveouts, unsigned char *map)
{
  hob_count = alloc_count = assert_hits = 0;
  pcd_PcdSystemMemoryBase = RAM_BASE;
  pcd_PcdSystemMemorySize = MEM_TOP - RAM_BASE;
  BuildResourceDescriptorHob(EFI_RESOURCE_SYSTEM_MEMORY, 7, RAM_BASE, MEM_TOP - RAM_BASE);
  /* MemoryPeim order on 361a2134: framebuffer, cpm-impl-reg, RAM disk, low DRAM, AGX. */
  ReserveMemoryRegion(0x85F000000, 0xFA0000);
  BuildMemoryAllocationHob(0x85F000000, 0xFA0000, EfiReservedMemoryType);
  ReserveMemoryRegion(0x210E40000, 0x10000);           /* outside RAM: no-op */
  if (ramdisk_reserved)
    ReserveMemoryRegion(0x860000000, 0x40000000);
  ReserveMemoryRegion(0x8A0100000, 0x3FF00000);
  BuildResourceDescriptorHob(EFI_RESOURCE_SYSTEM_MEMORY, 7, 0x100000, 0x3FF00000);
  ReserveMemoryRegion(0x8E0000000, 0x18000000);
  BuildMemoryAllocationHob(0x8E0000000, 0x18000000, EfiReservedMemoryType);
  if (with_carveouts) {
    RAM_CARVEOUT_EXCLUSION ex[6];
    UINTN n = 0;
    ex[n++] = (RAM_CARVEOUT_EXCLUSION){0x9D7708000, 0x8000000, TRUE};   /* UEFI memory */
    ex[n++] = (RAM_CARVEOUT_EXCLUSION){0x8510B4000, 0x1E00000, TRUE};   /* FD */
    ex[n++] = (RAM_CARVEOUT_EXCLUSION){0x85F000000, 0xFA0000, FALSE};
    if (ramdisk_reserved)
      ex[n++] = (RAM_CARVEOUT_EXCLUSION){0x860000000, 0x40000000, FALSE};
    ex[n++] = (RAM_CARVEOUT_EXCLUSION){0x8A0100000, 0x3FF00000, FALSE};
    ex[n++] = (RAM_CARVEOUT_EXCLUSION){0x8E0000000, 0x18000000, FALSE};
    ReserveRamCarveouts(ex, n);
    assert(assert_hits == 0);
  }
  for (uint64_t i = 0; i < PAGES; ++i) {
    uint64_t pa = RAM_BASE + i * 0x4000;
    unsigned char t = 'X';
    for (int h = 0; h < hob_count; ++h)
      if (pa >= hobs[h].PhysicalStart && pa < hobs[h].PhysicalStart + hobs[h].ResourceLength)
        t = hobs[h].ResourceType == EFI_RESOURCE_SYSTEM_MEMORY ? 7 : 0;
    for (int a = 0; a < alloc_count; ++a)
      if (pa >= allocs[a].base && pa < allocs[a].base + allocs[a].size)
        t = 0;
    map[i] = t;
  }
}

static unsigned char type_at(const unsigned char *map, uint64_t pa)
{
  return map[(pa - RAM_BASE) / 0x4000];
}

static void replay_tests(void)
{
  load_adt();
  replay(0, 0, map_without);
  /* Baseline reproduces the EXP2196 DXE map at RAM granularity. */
  assert(type_at(map_without, 0x818000000) == 7 && type_at(map_without, 0x85EFFC000) == 7);
  assert(type_at(map_without, 0x85F000000) == 0 && type_at(map_without, 0x85FF9C000) == 0);
  assert(type_at(map_without, 0x85FFA0000) == 7 && type_at(map_without, 0x8A00FC000) == 7);
  assert(type_at(map_without, 0x8A0100000) == 0 && type_at(map_without, 0x8F7FFC000) == 0);
  assert(type_at(map_without, 0x8F8000000) == 7 && type_at(map_without, MEM_TOP - 0x4000) == 7);
  for (uint64_t i = 0; i < PAGES; ++i)
    assert(map_without[i] != 'X');

  replay(0, 1, map_with);
  uint64_t changed = 0;
  for (uint64_t i = 0; i < PAGES; ++i) {
    uint64_t pa = RAM_BASE + i * 0x4000;
    int planned = (pa >= 0x860500000 && pa < 0x8614a0000) ||
                  (pa >= 0x864000000 && pa < 0x86f518000);
    if (planned) {
      assert(map_without[i] == 7 && map_with[i] == 0);
      ++changed;
    } else {
      assert(map_with[i] == map_without[i]);   /* everything else identical */
    }
  }
  assert(changed == (0xfa0000 + 0xb518000) / 0x4000);
  assert(alloc_count == 2 + 2);                /* fb + AGX + two carveout ranges */

  /* exp392 behaviour: window reserved -> nothing to add, map identical. */
  replay(1, 0, map_without);
  replay(1, 1, map_with);
  assert(memcmp(map_without, map_with, PAGES) == 0);
  assert(alloc_count == 2);

  /* No carveout map -> no change. */
  adt_count = 0;
  replay(0, 1, map_with);
  replay(0, 0, map_without);
  assert(memcmp(map_without, map_with, PAGES) == 0);
}

int main(void)
{
  planner_tests();
  replay_tests();
  puts("mu ram carveout: ok");
  return 0;
}
