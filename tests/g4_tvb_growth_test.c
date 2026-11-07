/* rc80: TVB growth policy, window geometry, cap and fallback bookkeeping,
 * and root parking with the TVB window's leaves (real shared code). */
#include "apple_agx_g4_submit.h"
#include "apple_agx_g3_private_pool.h"
#include "apple_agx_g3_tvb.h"
#include "apple_agx_gpuva_g3_graph.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static void policy(void) {
  /* No valid firmware Stats: nothing to grow beyond the render's size. */
  assert(AppleAgxG3TvbTarget(0u, 8u, 32u, APPLE_AGX_G3_TVB_MAX_BLOCKS) == 32u);
  /* Asahi auto_grow: 2 x used pages above the backed pages grows to 3 x.
   * Wom1G3PrivateProcs (EXP2003b HD 50k): max_pages 1104, 32 blocks. */
  assert(AppleAgxG3TvbTarget(1104u, 32u, 32u, APPLE_AGX_G3_TVB_MAX_BLOCKS) == 828u);
  /* EXP2010 HD median render: 383 pages -> need 192, want 288 blocks. */
  assert(AppleAgxG3TvbTarget(383u, 32u, 32u, APPLE_AGX_G3_TVB_MAX_BLOCKS) == 288u);
  /* Once 2 x used fits, the heap stays (no oscillation, never shrinks). */
  assert(AppleAgxG3TvbTarget(1104u, 32u, 828u, APPLE_AGX_G3_TVB_MAX_BLOCKS) == 828u);
  assert(AppleAgxG3TvbTarget(1104u, 32u, 552u, APPLE_AGX_G3_TVB_MAX_BLOCKS) == 552u);
  assert(AppleAgxG3TvbTarget(1105u, 32u, 552u, APPLE_AGX_G3_TVB_MAX_BLOCKS) == 829u);
  assert(AppleAgxG3TvbTarget(10u, 8u, 900u, APPLE_AGX_G3_TVB_MAX_BLOCKS) == 900u);
  /* FHD 100k (EXP2002c): about 2150 pages -> 1613 blocks, inside the range. */
  assert(AppleAgxG3TvbTarget(2150u, 32u, 828u, APPLE_AGX_G3_TVB_MAX_BLOCKS) == 1613u);
  assert(1613u <= APPLE_AGX_G4_TVB_HEAP_BLOCKS);
  /* Cap: the range, then the floor for a process without a TVB window. */
  assert(AppleAgxG3TvbTarget(100000u, 32u, 32u, APPLE_AGX_G3_TVB_MAX_BLOCKS) ==
         APPLE_AGX_G3_TVB_MAX_BLOCKS);
  assert(AppleAgxG3TvbTarget(100000u, 32u, 32u, 5000u) == APPLE_AGX_G3_TVB_MAX_BLOCKS);
  assert(AppleAgxG3TvbTarget(1104u, 32u, 32u, APPLE_AGX_G3_TVB_FLOOR_BLOCKS) == 32u);
  assert(AppleAgxG3TvbTarget(1104u, 32u, 8u, APPLE_AGX_G3_TVB_FLOOR_BLOCKS) == 32u);
  /* Asahi ensure_blocks(min_tvb_blocks), uncapped, best effort. */
  assert(AppleAgxG3TvbTarget(0u, AppleAgxG4TvbBlocksForSize(2048u, 8192u), 32u,
         APPLE_AGX_G3_TVB_MAX_BLOCKS) == 128u);
  assert(AppleAgxG3TvbTarget(0u, AppleAgxG4TvbBlocksForSize(2048u, 8192u), 32u,
         APPLE_AGX_G3_TVB_FLOOR_BLOCKS) == 32u);
  assert(AppleAgxG3TvbTarget(0u, 4000u, 32u, APPLE_AGX_G3_TVB_MAX_BLOCKS) ==
         APPLE_AGX_G3_TVB_MAX_BLOCKS);
  /* Steps: the private-pool floor first, then bounded window steps. */
  assert(AppleAgxG3TvbStep(8u, 828u) == 32u);
  assert(AppleAgxG3TvbStep(8u, 16u) == 16u);
  assert(AppleAgxG3TvbStep(32u, 828u) == 828u);
  assert(AppleAgxG3TvbStep(32u, 2080u) == 32u + APPLE_AGX_G3_TVB_STEP_BLOCKS);
  assert(AppleAgxG3TvbStep(1056u, 2080u) == 2080u);
  assert(AppleAgxG3TvbStep(900u, 828u) == 900u);
}

static void geometry(void) {
  const unsigned long long private_va = APPLE_AGX_G4_PRIVATE_VA_BASE;
  const unsigned long long heap = private_va + APPLE_AGX_G3_PRIVATE_HEAP_VA_OFFSET;
  const unsigned long long tvb = private_va + APPLE_AGX_G3_PRIVATE_VA_BYTES;
  unsigned leaf = 99u, pte = 99u, required[APPLE_AGX_G4_PROCESS_RANGE_COUNT];
  APPLE_AGX_G4_NATIVE_RENDER r;
  /* The floor fills the private window to its end; block 32 starts the TVB
   * window, so the heap is VA-contiguous and in one root slot. */
  assert(heap + 32ULL * APPLE_AGX_G3_TVB_BLOCK_BYTES == tvb);
  assert((private_va >> 36) == ((tvb + APPLE_AGX_G3_TVB_VA_BYTES - 1ULL) >> 36));
  assert((tvb & (APPLE_AGX_G3_TVB_LEAF_BYTES - 1ULL)) == 0ULL);
  assert(heap + (unsigned long long)APPLE_AGX_G4_TVB_HEAP_BLOCKS *
         APPLE_AGX_G3_TVB_BLOCK_BYTES == tvb + APPLE_AGX_G3_TVB_VA_BYTES);
  memset(&r, 0, sizeof(r));
  r.WidthPx = 1920u; r.HeightPx = 1080u; r.Layers = 1u;
  r.UtileWidthPx = r.UtileHeightPx = 32u;
  assert(AppleAgxG4ProcessRequiredBytes(&r, required));
  /* The UMD-visible heap range stays the 32-block floor (unchanged ABI);
   * the window continues it right after its end. */
  assert(required[2] == APPLE_AGX_G3_TVB_FLOOR_BLOCKS * APPLE_AGX_G3_TVB_BLOCK_BYTES);
  /* Page and block lists for every block still fit one 64 KiB unit. */
  assert(required[0] == 0x10000u && required[1] == 0x10000u &&
         APPLE_AGX_G4_TVB_HEAP_BLOCKS * 16u <= required[0] &&
         APPLE_AGX_G4_TVB_HEAP_BLOCKS * 8u <= required[1]);
  assert(!AppleAgxG3TvbBlockSlot(31u, &leaf, &pte) && leaf == 99u);
  assert(!AppleAgxG3TvbBlockSlot(APPLE_AGX_G3_TVB_MAX_BLOCKS, &leaf, &pte));
  for (unsigned b = 32u; b < APPLE_AGX_G3_TVB_MAX_BLOCKS; ++b) {
    unsigned long long va = heap + (unsigned long long)b * APPLE_AGX_G3_TVB_BLOCK_BYTES;
    assert(AppleAgxG3TvbBlockSlot(b, &leaf, &pte));
    assert(leaf < APPLE_AGX_G3_TVB_LEAVES && pte + 8u <= 2048u);
    /* The leaf covers the block's VA and the PTE index is its 16 KiB page. */
    assert(((va - tvb) >> 25) == leaf);
    assert(((va >> 14) & 2047u) == pte);
  }
  assert(AppleAgxG3TvbBlockSlot(287u, &leaf, &pte) && leaf == 0u && pte == 2040u);
  assert(AppleAgxG3TvbBlockSlot(288u, &leaf, &pte) && leaf == 1u && pte == 0u);
  /* Global lease budget: RAM / 8, at most 1 GiB, in 64 KiB granules. */
  assert(AppleAgxG3TvbGlobalGranules(8ULL << 30) == 16384ULL);
  assert(AppleAgxG3TvbGlobalGranules(4ULL << 30) == 8192ULL);
  assert(AppleAgxG3TvbGlobalGranules(64ULL << 30) == 16384ULL);
  assert(AppleAgxG3TvbGlobalGranules(0ULL) == 0ULL);
  /* The FHD target (1613 blocks) is 201.6 MiB: two of them fit the budget. */
  assert(2ULL * (1613ULL - 32ULL) * 2ULL <= AppleAgxG3TvbGlobalGranules(8ULL << 30));
}

static void fallback(void) {
  APPLE_AGX_G3_TVB_STATE s;
  memset(&s, 0, sizeof(s));
  assert(AppleAgxG3TvbShouldTry(&s, 828u, 32u, 1000u));
  assert(!AppleAgxG3TvbShouldTry(&s, 32u, 32u, 1000u));
  /* A refused lease after 100 blocks keeps them and holds the target off. */
  AppleAgxG3TvbNoteResult(&s, 828u, 32u, 132u, APPLE_AGX_G3_TVB_FAIL_LEASE, 1000u);
  assert(s.Grows == 1u && s.Failures == 1u && s.LastReason == APPLE_AGX_G3_TVB_FAIL_LEASE);
  assert(!AppleAgxG3TvbShouldTry(&s, 828u, 132u, 1000u + APPLE_AGX_G3_TVB_RETRY_MS - 1u));
  assert(AppleAgxG3TvbShouldTry(&s, 828u, 132u, 1000u + APPLE_AGX_G3_TVB_RETRY_MS));
  /* A higher target (more pages used) is tried at once. */
  assert(AppleAgxG3TvbShouldTry(&s, 900u, 132u, 1001u));
  /* A refusal with nothing mapped is not a growth. */
  AppleAgxG3TvbNoteResult(&s, 900u, 132u, 132u, APPLE_AGX_G3_TVB_FAIL_MAP, 1001u);
  assert(s.Grows == 1u && s.Failures == 2u && s.FailedTarget == 900u);
  /* Success clears the hold-off. */
  AppleAgxG3TvbNoteResult(&s, 900u, 132u, 900u, APPLE_AGX_G3_TVB_FAIL_NONE, 5000u);
  assert(s.Grows == 2u && !s.FailedTarget && !s.FailedAtMs);
  assert(!AppleAgxG3TvbShouldTry(&s, 900u, 900u, 5001u));
}

static APPLE_AGX_GPUVA_G3_NODE nodes[16];
static unsigned used_nodes;
static void edge(APPLE_AGX_GPUVA_G3_GRAPH *g, unsigned long long table,
    unsigned index, unsigned long long child) {
  APPLE_AGX_GPUVA_G3_NODE *n = &nodes[used_nodes++];
  memset(n, 0, sizeof(*n));
  n->Ipa = table; n->Index = index; n->AuxIpa = child;
  n->Next = g->Parents; g->Parents = n;
}

/* Root parking (gpuva_g3_paging_windows.c AdmissionG3PrepareTableReuse):
 * with TVB leaves below the private middle table the single-leaf check
 * refuses, so the KMD must pass the whole private leaf set. */
static void root_parking(void) {
  static APPLE_AGX_GPUVA_G3_GRAPH g;
  const unsigned long long root = 0x10000000ULL, middle = 0x10004000ULL;
  const unsigned long long private_leaf = 0x10008000ULL;
  const unsigned long long tvb_leaf[2] = {0x1000c000ULL, 0x10010000ULL};
  unsigned long long vas[3], leaves[3];
  const unsigned long long pva = APPLE_AGX_G4_PRIVATE_VA_BASE;
  const unsigned long long tva = pva + APPLE_AGX_G3_PRIVATE_VA_BYTES;
  memset(&g, 0, sizeof(g));
  used_nodes = 0u;
  vas[0] = pva; leaves[0] = private_leaf;
  vas[1] = tva; leaves[1] = tvb_leaf[0];
  vas[2] = tva + APPLE_AGX_G3_TVB_LEAF_BYTES; leaves[2] = tvb_leaf[1];
  edge(&g, root, (unsigned)(pva >> 36), middle);
  edge(&g, middle, (unsigned)((pva >> 25) & 2047u), private_leaf);
  /* Only the private leaf: both forms agree (pre-rc80 behaviour). */
  assert(AppleAgxGpuvaG3GraphCanDetachPrivateRoot(&g, pva, root, private_leaf));
  assert(AppleAgxGpuvaG3GraphCanDetachPrivateRootSet(&g, root, vas, leaves, 1u));
  assert(AppleAgxGpuvaG3GraphCanDetachPrivateRootSet(&g, root, vas, leaves, 3u));
  edge(&g, middle, (unsigned)((tva >> 25) & 2047u), tvb_leaf[0]);
  /* RED for the single-leaf form, GREEN for the set. */
  assert(!AppleAgxGpuvaG3GraphCanDetachPrivateRoot(&g, pva, root, private_leaf));
  assert(AppleAgxGpuvaG3GraphCanDetachPrivateRootSet(&g, root, vas, leaves, 3u));
  assert(!AppleAgxGpuvaG3GraphCanDetachPrivateRootSet(&g, root, vas, leaves, 1u));
  /* A leaf that is not ours below the middle table still blocks parking. */
  edge(&g, middle, (unsigned)(((tva >> 25) + 2u) & 2047u), 0x10014000ULL);
  assert(!AppleAgxGpuvaG3GraphCanDetachPrivateRootSet(&g, root, vas, leaves, 3u));
  g.Parents = g.Parents->Next;
  /* So does another root child, a job in flight, or a set across roots. */
  edge(&g, root, 0u, 0x10018000ULL);
  assert(!AppleAgxGpuvaG3GraphCanDetachPrivateRootSet(&g, root, vas, leaves, 3u));
  g.Parents = g.Parents->Next;
  g.JobInFlight = 1u;
  assert(!AppleAgxGpuvaG3GraphCanDetachPrivateRootSet(&g, root, vas, leaves, 3u));
  g.JobInFlight = 0u;
  vas[2] = 1ULL << 37;
  assert(!AppleAgxGpuvaG3GraphCanDetachPrivateRootSet(&g, root, vas, leaves, 3u));
  vas[2] = tva + APPLE_AGX_G3_TVB_LEAF_BYTES;
  /* The private window's own leaf must be linked. */
  leaves[0] = 0x1001c000ULL;
  assert(!AppleAgxGpuvaG3GraphCanDetachPrivateRootSet(&g, root, vas, leaves, 3u));
}

int main(void) {
  policy();
  geometry();
  fallback();
  root_parking();
  puts("g4_tvb_growth_test: PASS");
  return 0;
}
