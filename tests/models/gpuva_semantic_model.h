/* Offline finite model only: paging process plus two user processes, one16KiB reservation each,
 * one reused hardware-slot abstraction. Not a production DDI or allocator. */
#ifndef GPUVA_SEMANTIC_MODEL_H
#define GPUVA_SEMANTIC_MODEL_H
#include "apple_agx_uat_table.h"
#include <stdint.h>
#define GM_PAGE 0x4000ULL
#define GM_TABLES 24u
#define GM_SYSTEM 2u
#define GM_PROCESSES 3u
enum gm_result { GM_OK, GM_STALE, GM_RANGE, GM_BUSY, GM_DEPENDENCY,
                 GM_NOT_RESIDENT, GM_UNREPRESENTABLE, GM_TABLE_ERROR, GM_UNMAP };
enum gm_domain { GM_PAGING = 1, GM_RENDER = 2 };
typedef struct { uint64_t pa; unsigned valid, writable; } gm_pte4;
typedef struct { unsigned process; uint64_t process_gen, allocation_gen, va, size; } gm_binding;
typedef struct { unsigned process; uint64_t process_gen, root_gen, map_gen, slot_gen; } gm_lease;
typedef struct { enum gm_domain domain; unsigned process; uint64_t process_gen, sequence; } gm_fence;
typedef struct {
  unsigned long long storage[GM_TABLES][2048];
  unsigned allocations;
  uint64_t allocator_base;
  APPLE_AGX_UAT_PAGE pages[GM_TABLES];
  APPLE_AGX_UAT_MAPPING mappings[4];
  APPLE_AGX_UAT_INVENTORY inventory;
  APPLE_AGX_UAT_ALLOCATOR allocator;
  APPLE_AGX_UAT_ROOTS roots;
  uint64_t generation, root_gen, map_gen, allocation_gen, va;
  uint64_t paging_issued, paging_done, flushed_map_gen;
  unsigned live, reserved, mapped, resident, bootstrap_done, id;
} gm_process;
typedef struct {
  gm_process process[GM_PROCESSES];
  uint64_t serial, slot_gen, render_issued, render_done;
  int slot_owner;
  unsigned busy, invalidated;
  gm_lease current;
  gm_fence active_render;
  uint64_t firmware_identity, firmware_prefix[2];
} gm_model;
void gm_initialize(gm_model *m);
enum gm_result gm_create(gm_model *m, unsigned process);
enum gm_result gm_destroy(gm_model *m, unsigned process);
enum gm_result gm_bootstrap(gm_model *m, unsigned process);
enum gm_result gm_reserve(gm_model *m, unsigned process, uint64_t va, gm_binding *out);
enum gm_result gm_coarsen(const gm_pte4 pte[4], uint64_t *pa, unsigned *writable);
enum gm_result gm_merge4(const gm_pte4 before[4], unsigned first, unsigned count,
                        const gm_pte4 *updates, gm_pte4 after[4]);
enum gm_result gm_map(gm_model *m, gm_binding b, const gm_pte4 pte[4], gm_fence *f);
enum gm_result gm_flush(gm_model *m, unsigned process);
enum gm_result gm_paging_complete(gm_model *m, gm_fence f);
enum gm_result gm_resident(gm_model *m, gm_binding b);
enum gm_result gm_evict(gm_model *m, gm_binding b, gm_fence *f);
enum gm_result gm_close(gm_model *m, gm_binding b, gm_fence *f);
enum gm_result gm_relocate_root(gm_model *m, unsigned process);
enum gm_result gm_bind_slot(gm_model *m, unsigned process, gm_lease *out);
enum gm_result gm_invalidate_slot(gm_model *m);
enum gm_result gm_submit(gm_model *m, gm_binding b, gm_lease lease, gm_fence *out);
enum gm_result gm_render_complete(gm_model *m, gm_fence f);
enum gm_result gm_translate(gm_model *m, unsigned process, uint64_t va, uint64_t *pa);
#endif
