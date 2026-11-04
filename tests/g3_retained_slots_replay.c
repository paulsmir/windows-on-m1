/* V1 (investigation/analysis/V1-retained-vm-slots.md): retained per-process
 * VM slots through the real KMD graph, the real broker client, the real m1n1
 * MMIO wire/dispatch and the real m1n1 v5 broker (slot TTBRs and
 * invalidations counted by the broker's own test fixture).
 *
 * WHAT REAL BUG OR INVARIANT WILL THIS TEST CATCH?
 * - a stale translation: a JOB_BEGIN on slot S after a descriptor change of
 *   its owner without an invalidation of S in between (checked on every
 *   broker command of every scenario);
 * - a per-leaf TLBI regression: a paging batch on a process that retains an
 *   idle lease must cost one RELEASE invalidation, not one per leaf;
 * - RELOCATE_ROOT/DESTROY refused BUSY (poison) for an idle retained lease;
 * - the allocator stealing a lease with a job in flight, or using a slot
 *   outside 1..K;
 * - switch off: any difference from the baseline LEASE, JOB_BEGIN, JOB_END,
 *   RELEASE sequence on slot 1. */
#include <stdio.h>
#define main broker_selftest
#include "hv_agx_gpuva_v5_test.c"
#undef main
#include "hv_agx_gpuva_v5_mmio.h"
static bool request_powered = true, gpuva_capacity_reported;
static struct hv_agx_gpuva_v5_wire gpuva_v5_wire;
#define gpuva_v5 (((struct fixture *)context)->broker)
/* GPUVA_EXECUTE */
#undef gpuva_v5
#include "apple_agx_gpuva_g3_graph.h"

#define LOG_MAX 8192u
static struct fixture *F;
static struct {
  unsigned lease, release, begin, end, leaf, parent, relocate, flush, destroy;
  unsigned max_slot, refused_lease;
  unsigned lease_slot[64];
} N;
static unsigned log_count;
static unsigned log_entry[LOG_MAX];
/* Per process id (1..3) and slot: a descriptor change not yet followed by an
 * invalidation of that slot, with the slot's invalidation count before it. */
static int pending[4][64];
static unsigned mark[4][64];

static unsigned total_invalidations(void) {
  unsigned s, n = 0;
  for (s = 1; s < 64; ++s) n += F->invalidations[s];
  return n;
}

static void observe(void *context, const AGX_GPUVA_V5_REQUEST *q,
                    AGX_GPUVA_V5_RESPONSE *r) {
  unsigned before[64], s;
  unsigned pid = (unsigned)q->ProcessId;
  memcpy(before, F->invalidations, sizeof(before));
  gpuva_execute(context, q, r);
  if (q->Command == AGX_GPUVA_V5_LEASE && r->Status) ++N.refused_lease;
  if (r->Status) return;
  assert(pid < 4u);
  if (q->Command == AGX_GPUVA_V5_LEASE || q->Command == AGX_GPUVA_V5_JOB_BEGIN ||
      q->Command == AGX_GPUVA_V5_JOB_END || q->Command == AGX_GPUVA_V5_RELEASE) {
    assert(log_count < LOG_MAX);
    log_entry[log_count++] = q->Command | (q->Slot << 8);
    if (q->Slot > N.max_slot) N.max_slot = q->Slot;
  }
  switch (q->Command) {
  case AGX_GPUVA_V5_LEASE: ++N.lease; ++N.lease_slot[q->Slot]; break;
  case AGX_GPUVA_V5_RELEASE: ++N.release; break;
  case AGX_GPUVA_V5_JOB_END: ++N.end; break;
  case AGX_GPUVA_V5_FLUSH_TLB: ++N.flush; break;
  case AGX_GPUVA_V5_DESTROY: ++N.destroy; break;
  case AGX_GPUVA_V5_JOB_BEGIN:
    ++N.begin;
    /* The invariant: no job runs on a translation older than its owner's
     * last descriptor change. */
    if (pending[pid][q->Slot])
      assert(F->invalidations[q->Slot] > mark[pid][q->Slot]);
    pending[pid][q->Slot] = 0;
    break;
  case AGX_GPUVA_V5_UPDATE_LEAF:
  case AGX_GPUVA_V5_UPDATE_PARENT:
  case AGX_GPUVA_V5_RELOCATE_ROOT:
    if (q->Command == AGX_GPUVA_V5_UPDATE_LEAF) ++N.leaf;
    else if (q->Command == AGX_GPUVA_V5_UPDATE_PARENT) ++N.parent;
    else ++N.relocate;
    for (s = 1; s < 64; ++s) {
      pending[pid][s] = 1;
      mark[pid][s] = before[s];
    }
    break;
  default: break;
  }
}

static bool w64(void *opaque, unsigned offset, unsigned long long value) {
  uint64_t word = value;
  return hv_agx_gpuva_v5_mmio(&gpuva_v5_wire, offset - AGX_GPUVA_V5_OFFSET,
                              &word, true, 3, observe, opaque);
}
static bool r64(void *opaque, unsigned offset, unsigned long long *value) {
  uint64_t word = 0;
  bool ok = hv_agx_gpuva_v5_mmio(&gpuva_v5_wire, offset - AGX_GPUVA_V5_OFFSET,
                                 &word, false, 3, observe, opaque);
  if (ok) *value = word;
  return ok;
}
static bool w32(void *opaque, unsigned offset, unsigned value) {
  uint64_t word = value;
  return hv_agx_gpuva_v5_mmio(&gpuva_v5_wire, offset - AGX_GPUVA_V5_OFFSET,
                              &word, true, 2, observe, opaque);
}
static void barrier(void *opaque) { (void)opaque; }
static void *allocate(void *opaque, unsigned long long bytes) {
  (void)opaque; return calloc(1, (size_t)bytes);
}
static void release_node(void *opaque, void *p) { (void)opaque; free(p); }

static APPLE_AGX_GPUVA_V5_CLIENT client;

static void open_broker(void) {
  APPLE_AGX_GPUVA_V5_IO io;
  AGX_GPUVA_V5_REQUEST probe;
  AGX_GPUVA_V5_RESPONSE response;
  F = new_fixture();
  F->range_backing = true;
  memset(&gpuva_v5_wire, 0, sizeof(gpuva_v5_wire));
  memset(&N, 0, sizeof(N));
  memset(pending, 0, sizeof(pending));
  log_count = 0;
  io = (APPLE_AGX_GPUVA_V5_IO){F, w64, r64, w32, barrier, NULL};
  assert(AppleAgxGpuvaV5ClientInit(&client, &io));
  memset(&probe, 0, sizeof(probe));
  memset(&response, 0, sizeof(response));
  probe.Command = AGX_GPUVA_V5_CREATE; /* learn the epoch, as G3 start */
  assert(AppleAgxGpuvaV5ClientCall(&client, &probe, &response) &&
         response.Status == 2u && client.Epoch == 7u);
}

static void reset_counts(void) {
  memset(&N, 0, sizeof(N));
  log_count = 0;
}

static void setup(APPLE_AGX_GPUVA_G3_GRAPH *g, unsigned long long id,
                  uint64_t root, uint64_t l1, uint64_t l2, uint64_t data,
                  APPLE_AGX_GPUVA_G3_SLOTS *table) {
  assert(AppleAgxGpuvaG3GraphInit(g, &client, id, 1u, allocate, release_node, 0));
  assert(AppleAgxGpuvaG3GraphCreate(g, root, false));
  g->SlotTable = table;
  if (!l1) return;
  assert(AppleAgxGpuvaG3GraphRegisterTable(g, l1, 1u));
  assert(AppleAgxGpuvaG3GraphRegisterTable(g, l2, 2u));
  assert(AppleAgxGpuvaG3GraphUpdateParent(g, root, 0u, l1));
  assert(AppleAgxGpuvaG3GraphUpdateParent(g, l1, 0u, l2));
  assert(AppleAgxGpuvaG3GraphUpdateLeaf(g, l2, 8u, data, true));
}

static void job(APPLE_AGX_GPUVA_G3_GRAPH *g) {
  assert(AppleAgxGpuvaG3GraphBeginJob(g, 1u));
  assert(g->JobInFlight && g->LeaseToken && g->Slot);
  assert(AppleAgxGpuvaG3GraphEndJob(g));
  assert(!g->JobInFlight);
}

/* Switch off: unattached graphs (production default) and a graph attached
 * to a table that was never enabled lease slot 1 per job, as before V1. */
static void switch_off(int attach_disabled) {
  APPLE_AGX_GPUVA_G3_GRAPH a, b;
  APPLE_AGX_GPUVA_G3_SLOTS table;
  unsigned i;
  memset(&table, 0, sizeof(table));
  open_broker();
  setup(&a, 1u, P_ROOT, P_L1, P_L2, P_DATA, attach_disabled ? &table : NULL);
  setup(&b, 2u, Q_ROOT, Q_L1, Q_L2, Q_DATA, attach_disabled ? &table : NULL);
  reset_counts();
  for (i = 0; i < 100u; ++i) { job(&a); job(&b); }
  assert(N.lease == 200u && N.release == 200u && N.begin == 200u && N.end == 200u);
  assert(log_count == 800u);
  for (i = 0; i < log_count; i += 4u) {
    assert(log_entry[i] == (AGX_GPUVA_V5_LEASE | (1u << 8)));
    assert(log_entry[i + 1u] == (AGX_GPUVA_V5_JOB_BEGIN | (1u << 8)));
    assert(log_entry[i + 2u] == (AGX_GPUVA_V5_JOB_END | (1u << 8)));
    assert(log_entry[i + 3u] == (AGX_GPUVA_V5_RELEASE | (1u << 8)));
  }
  assert(!a.LeaseToken && !a.Slot && !b.LeaseToken && !b.Slot);
  assert(!table.Owner[1] && !table.Leases);
  /* ReleaseRetained is a no-op without an enabled table. */
  reset_counts();
  assert(AppleAgxGpuvaG3GraphReleaseRetained(&a) && N.release == 0u);
  assert(AppleAgxGpuvaG3GraphDestroy(&a) && AppleAgxGpuvaG3GraphDestroy(&b));
}

static void retained_two_processes(void) {
  APPLE_AGX_GPUVA_G3_GRAPH a, b;
  APPLE_AGX_GPUVA_G3_SLOTS table;
  unsigned i, aslot, bslot, inv_a, inv_b, inv_total, leaves;
  memset(&table, 0, sizeof(table));
  /* The KMD's table: slots 2..5; slot 1 stays the baseline per-job slot. */
  assert(AppleAgxGpuvaG3SlotsEnable(&table, 2u, 4u));
  open_broker();
  setup(&a, 1u, P_ROOT, P_L1, P_L2, P_DATA, &table);
  setup(&b, 2u, Q_ROOT, Q_L1, Q_L2, Q_DATA, &table);
  reset_counts();
  inv_total = total_invalidations();
  /* Alternating processes: one LEASE each, JOB_BEGIN/JOB_END only after. */
  for (i = 0; i < 100u; ++i) { job(&a); job(&b); }
  assert(N.lease == 2u && N.release == 0u && N.begin == 200u && N.end == 200u);
  assert(total_invalidations() == inv_total + 2u);
  aslot = a.Slot; bslot = b.Slot;
  assert(aslot >= 2u && bslot >= 2u && aslot != bslot && aslot <= 5u && bslot <= 5u);
  assert(table.Owner[aslot] == &a && table.Owner[bslot] == &b);
  assert(a.LeaseToken && b.LeaseToken && table.Reuses == 198u);
  /* A held lease with a job in flight is never released. */
  assert(AppleAgxGpuvaG3GraphBeginJob(&a, 1u));
  assert(!AppleAgxGpuvaG3GraphReleaseIdle(&a) && a.LeaseToken && !a.Uncertain);
  /* The rc102 nested successor shares the lease: no broker call. */
  reset_counts();
  assert(AppleAgxGpuvaG3GraphBeginNestedJob(&a));
  assert(AppleAgxGpuvaG3GraphEndJob(&a) && a.JobInFlight && log_count == 0u);
  assert(AppleAgxGpuvaG3GraphEndJob(&a) && !a.JobInFlight && a.LeaseToken);
  assert(log_count == 1u && log_entry[0] == (AGX_GPUVA_V5_JOB_END | (aslot << 8)));

  /* A VidMm FlushTlb of a still leased idle process: one FLUSH_TLB, one
   * invalidation of its slot, the lease stays. */
  reset_counts();
  inv_a = F->invalidations[aslot];
  assert(AppleAgxGpuvaG3GraphFlush(&a, 0ULL, 0ULL));
  assert(N.flush == 1u && F->invalidations[aslot] == inv_a + 1u && a.LeaseToken);

  /* A 225-leaf paging batch of A while A and B retain idle leases: one
   * RELEASE invalidation of A's slot, no per-leaf invalidation. */
  reset_counts();
  inv_a = F->invalidations[aslot];
  inv_b = F->invalidations[bslot];
  inv_total = total_invalidations();
  for (leaves = 0; leaves < 225u; ++leaves)
    assert(AppleAgxGpuvaG3GraphUpdateLeaf(&a, P_L2, 100u + leaves,
        P_DATA + 0x100000ULL + (uint64_t)leaves * 0x4000ULL, true));
  assert(N.leaf == 225u && N.release == 1u && N.lease == 0u);
  assert(F->invalidations[aslot] == inv_a + 1u);
  assert(F->invalidations[bslot] == inv_b && total_invalidations() == inv_total + 1u);
  assert(!a.LeaseToken && !a.Slot && !table.Owner[aslot] && b.LeaseToken);
  /* A's next job re-leases its preferred slot (TTBR write + invalidate
   * before JOB_BEGIN: the observer asserts it). */
  reset_counts();
  job(&a);
  assert(N.lease == 1u && a.Slot == aslot && table.Owner[aslot] == &a);
  job(&b);
  assert(N.lease == 1u && b.Slot == bslot);

  /* A root move (RELOCATE_ROOT) of an idle retained lease releases first
   * instead of being refused BUSY. */
  reset_counts();
  assert(AppleAgxGpuvaG3GraphRegisterTable(&a, P_ROOT2, 0u));
  assert(AppleAgxGpuvaG3GraphBindRoot(&a, P_ROOT2));
  assert(N.release == 1u && N.relocate == 1u && !a.Uncertain && !a.LeaseToken);
  assert(AppleAgxGpuvaG3GraphBindRoot(&a, P_ROOT));
  job(&a);
  assert(a.Slot == aslot);

  /* DESTROY of idle retained leases: no BUSY, the slots return. */
  reset_counts();
  assert(AppleAgxGpuvaG3GraphDestroy(&b));
  assert(N.destroy == 1u && N.release == 1u && !table.Owner[bslot]);
  assert(AppleAgxGpuvaG3GraphDestroy(&a));
  assert(N.destroy == 2u && N.release == 2u && !table.Owner[aslot]);
  assert(N.max_slot <= 5u);
}

/* Allocator: preferred slot, LRU free slot, LRU steal of an idle lease
 * only, never a lease with a job, a refused slot is quarantined. */
static void allocator(void) {
  APPLE_AGX_GPUVA_G3_GRAPH a, b, c;
  APPLE_AGX_GPUVA_G3_SLOTS table;
  memset(&table, 0, sizeof(table));
  assert(!AppleAgxGpuvaG3SlotsEnable(&table, 2u, 0u));
  assert(!AppleAgxGpuvaG3SlotsEnable(&table, 0u, 2u));
  assert(!AppleAgxGpuvaG3SlotsEnable(&table, 1u, 16u) && !table.Count);
  assert(!AppleAgxGpuvaG3SlotsEnable(&table, 14u, 3u) && !table.Count);
  assert(AppleAgxGpuvaG3SlotsEnable(&table, 2u, 2u));
  assert(AppleAgxGpuvaG3SlotsEnable(&table, 2u, 2u) &&
         !AppleAgxGpuvaG3SlotsEnable(&table, 2u, 4u) &&
         !AppleAgxGpuvaG3SlotsEnable(&table, 1u, 2u) && table.Count == 2u);
  open_broker();
  setup(&a, 1u, P_ROOT, 0, 0, 0, &table);
  setup(&b, 2u, Q_ROOT, 0, 0, 0, &table);
  setup(&c, 3u, P_ROOT2, 0, 0, 0, &table);
  job(&a); job(&b);
  assert(a.Slot == 2u && b.Slot == 3u);
  /* C steals the least recently used idle lease (A's). */
  reset_counts();
  job(&c);
  assert(c.Slot == 2u && !a.LeaseToken && table.Steals == 1u);
  assert(N.release == 1u && N.lease == 1u);
  /* B's job in flight is never stolen, although its slot is the least
   * recently used: A takes C's idle slot 2. */
  assert(AppleAgxGpuvaG3GraphBeginJob(&b, 1u));
  job(&c);
  assert(table.LastUse[3] < table.LastUse[2]);
  reset_counts();
  job(&a);
  assert(a.Slot == 2u && !c.LeaseToken && b.LeaseToken && b.Slot == 3u);
  /* Every slot busy: BeginJob fails cleanly, nothing is sent. */
  assert(AppleAgxGpuvaG3GraphBeginJob(&a, 1u));
  reset_counts();
  assert(!AppleAgxGpuvaG3GraphBeginJob(&c, 1u) && !c.Uncertain);
  assert(log_count == 0u && !c.LeaseToken);
  assert(AppleAgxGpuvaG3GraphEndJob(&a) && AppleAgxGpuvaG3GraphEndJob(&b));
  /* C's preferred slot (2, last leased) is free again after A releases. */
  assert(AppleAgxGpuvaG3GraphReleaseIdle(&a) && !a.LeaseToken);
  job(&c);
  assert(c.Slot == 2u && c.PreferredSlot == 2u);
  assert(N.max_slot <= 3u && !table.Quarantined && !N.lease_slot[1]);
  assert(AppleAgxGpuvaG3GraphDestroy(&a) && AppleAgxGpuvaG3GraphDestroy(&b) &&
         AppleAgxGpuvaG3GraphDestroy(&c));
}

static void quarantine(void) {
  APPLE_AGX_GPUVA_G3_GRAPH a, b, c;
  APPLE_AGX_GPUVA_G3_SLOTS table;
  unsigned i;
  memset(&table, 0, sizeof(table));
  assert(AppleAgxGpuvaG3SlotsEnable(&table, 2u, 4u));
  open_broker();
  setup(&a, 1u, P_ROOT, 0, 0, 0, &table);
  setup(&b, 2u, Q_ROOT, 0, 0, 0, &table);
  setup(&c, 3u, P_ROOT2, 0, 0, 0, &table);
  job(&a); job(&b);
  assert(a.Slot == 2u && b.Slot == 3u);
  /* Slot 4 is not clean (OWNERSHIP): quarantined, slot 5 used instead. */
  F->slots[4][0] = UINT64_C(0x123400000001);
  reset_counts();
  job(&c);
  assert(N.refused_lease == 1u && N.lease == 1u && c.Slot == 5u);
  assert((table.Quarantined & (1u << 4)) && table.Quarantines == 1u && !c.Uncertain);
  /* Inside the retry window the quarantine holds, even when preferred. */
  assert(AppleAgxGpuvaG3GraphReleaseIdle(&c));
  c.PreferredSlot = 4u;
  reset_counts();
  job(&c);
  assert(c.Slot == 5u && N.refused_lease == 0u && (table.Quarantined & (1u << 4)));
  /* After APPLE_AGX_GPUVA_G3_QUARANTINE_RETRY ticks a now clean slot is
   * leased again (bounded recovery, counted). */
  reset_counts();
  for (i = 0; i < APPLE_AGX_GPUVA_G3_QUARANTINE_RETRY / 2u; ++i) job(&a);
  F->slots[4][0] = 0;
  assert(AppleAgxGpuvaG3GraphReleaseIdle(&c));
  c.PreferredSlot = 4u;
  reset_counts();
  job(&c);
  assert(c.Slot == 4u && N.lease == 1u && !(table.Quarantined & (1u << 4)));
  assert(table.Quarantines == 1u && table.Recovered == 1u);
  assert(!N.lease_slot[1] && N.max_slot <= 5u);
}

int main(void) {
  switch_off(0);
  switch_off(1);
  retained_two_processes();
  allocator();
  quarantine();
  puts("g3_retained_slots_replay: PASS");
  return 0;
}
