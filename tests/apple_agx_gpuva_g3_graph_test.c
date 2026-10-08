#include "apple_agx_gpuva_g3_graph.h"
#include <assert.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

struct fixture {
  AGX_GPUVA_V5_REQUEST request;
  AGX_GPUVA_V5_RESPONSE response;
  unsigned int commands[32], count, bulk;
};

static bool write64(void *opaque, unsigned offset, unsigned long long value) {
  struct fixture *f = opaque;
  if (offset < AGX_GPUVA_V5_OFFSET ||
      offset + 8u > AGX_GPUVA_V5_OFFSET + sizeof(f->request)) return false;
  memcpy((unsigned char *)&f->request + offset - AGX_GPUVA_V5_OFFSET,
         &value, sizeof(value));
  return true;
}
static bool write32(void *opaque, unsigned offset, unsigned int value) {
  struct fixture *f = opaque;
  if (offset != AGX_GPUVA_V5_OFFSET + AGX_GPUVA_V5_DOORBELL || value != 1u ||
      f->request.Version != AGX_GPUVA_V5_VERSION ||
      (!f->bulk && f->count >= 32u)) return false;
  if (f->count < 32u) f->commands[f->count] = f->request.Command;
  ++f->count;
  memset(&f->response, 0, sizeof(f->response));
  f->response.Receipt = f->request.Sequence;
  f->response.Epoch = 7u;
  if (f->request.Command == AGX_GPUVA_V5_LEASE)
    f->response.Token = 0x77u;
  return true;
}
static bool read64(void *opaque, unsigned offset, unsigned long long *value) {
  struct fixture *f = opaque;
  if (offset < AGX_GPUVA_V5_OFFSET + AGX_GPUVA_V5_RESPONSE_OFFSET ||
      offset + 8u > AGX_GPUVA_V5_OFFSET + AGX_GPUVA_V5_RESPONSE_OFFSET +
                        sizeof(f->response)) return false;
  memcpy(value, (const unsigned char *)&f->response + offset -
      AGX_GPUVA_V5_OFFSET - AGX_GPUVA_V5_RESPONSE_OFFSET, sizeof(*value));
  return true;
}
static void barrier(void *opaque) { (void)opaque; }
static void *allocate(void *opaque, unsigned long long bytes) {
  (void)opaque; return malloc((size_t)bytes);
}
static void release(void *opaque, void *ptr) { (void)opaque; free(ptr); }

static unsigned long long update_visits;
static unsigned long long scaling_visits(unsigned int leaves) {
  struct fixture f = {0};
  APPLE_AGX_GPUVA_V5_CLIENT client;
  APPLE_AGX_GPUVA_V5_IO io = {&f, write64, read64, write32, barrier, 0};
  APPLE_AGX_GPUVA_G3_GRAPH graph;
  unsigned long long ipa = 0, visits;
  f.bulk = 1u;
  assert(AppleAgxGpuvaV5ClientInit(&client, &io));
  assert(AppleAgxGpuvaG3GraphInit(&graph, &client, 1u, 1u,
                                  allocate, release, 0));
  assert(AppleAgxGpuvaG3GraphCreate(&graph, 0x10000000ULL, false));
  assert(AppleAgxGpuvaG3GraphRegisterTable(&graph, 0x10004000ULL, 1u));
  assert(AppleAgxGpuvaG3GraphUpdateParent(&graph, 0x10000000ULL, 0u,
                                          0x10004000ULL));
  for (unsigned int t = 0; t < (leaves + 2047u) / 2048u; ++t) {
    unsigned long long table = 0x10008000ULL + t * 0x4000ULL;
    assert(AppleAgxGpuvaG3GraphRegisterTable(&graph, table, 2u));
    assert(AppleAgxGpuvaG3GraphUpdateParent(&graph, 0x10004000ULL, t, table));
    for (unsigned int i = 0; i < 2048u && t * 2048u + i < leaves; ++i)
      assert(AppleAgxGpuvaG3GraphUpdateLeaf(&graph, table, i,
          0x20000000ULL + (t * 2048u + i) * 0x4000ULL, true));
  }
  AppleAgxGpuvaG3LookupStatsReset();
  assert(AppleAgxGpuvaG3GraphTranslateVa(&graph, 0u, &ipa));
  assert(ipa == 0x20000000ULL);
  visits = AppleAgxGpuvaG3LookupStatsVisits();
  /* Remap, unmap and remap one leaf: leaf, backing and list lookups must not
   * walk the other mappings of the process. */
  AppleAgxGpuvaG3LookupStatsReset();
  assert(AppleAgxGpuvaG3GraphUpdateLeaf(&graph, 0x10008000ULL, 0u,
                                        0x60000000ULL, true));
  assert(AppleAgxGpuvaG3GraphTranslateVa(&graph, 0u, &ipa));
  assert(ipa == 0x60000000ULL);
  assert(AppleAgxGpuvaG3GraphUpdateLeaf(&graph, 0x10008000ULL, 0u,
                                        0ULL, false));
  update_visits = AppleAgxGpuvaG3LookupStatsVisits();
  assert(!AppleAgxGpuvaG3GraphTranslateVa(&graph, 0u, &ipa));
  assert(AppleAgxGpuvaG3GraphRegisterTable(&graph, 0x10020000ULL, 0u));
  assert(AppleAgxGpuvaG3GraphBindRoot(&graph, 0x10020000ULL));
  assert(!AppleAgxGpuvaG3GraphTranslateVa(&graph, 0u, &ipa));
  assert(AppleAgxGpuvaG3GraphBindRoot(&graph, 0x10000000ULL));
  assert(AppleAgxGpuvaG3GraphUpdateLeaf(&graph, 0x10008000ULL, 0u,
                                        0x20000000ULL, true));
  assert(AppleAgxGpuvaG3GraphTranslateVa(&graph, 0u, &ipa));
  assert(ipa == 0x20000000ULL);
  assert(AppleAgxGpuvaG3GraphDestroy(&graph));
  return visits;
}

static unsigned long long frame_visits(unsigned int frames) {
  struct fixture f = {0};
  APPLE_AGX_GPUVA_V5_CLIENT client;
  APPLE_AGX_GPUVA_V5_IO io = {&f, write64, read64, write32, barrier, 0};
  APPLE_AGX_GPUVA_G3_GRAPH graph;
  static APPLE_AGX_GPUVA_G3_REGISTRY registry;
  unsigned long long visits;
  memset(&registry, 0, sizeof(registry));
  assert(AppleAgxGpuvaV5ClientInit(&client, &io));
  assert(AppleAgxGpuvaG3GraphInit(&graph, &client, 1u, 1u,
                                  allocate, release, 0));
  graph.Registry = &registry;
  for (unsigned int i = 0; i < frames; ++i)
    assert(AppleAgxGpuvaG3MappingAcquire(&graph,
        0x40000000ULL + (unsigned long long)i * 0x4000ULL));
  AppleAgxGpuvaG3LookupStatsReset();
  /* A new frame, a repeat reference and both releases. */
  assert(AppleAgxGpuvaG3MappingAcquire(&graph, 0x30000000ULL));
  assert(AppleAgxGpuvaG3MappingAcquire(&graph, 0x30001000ULL));
  AppleAgxGpuvaG3MappingRelease(&graph, 0x30001000ULL);
  AppleAgxGpuvaG3MappingRelease(&graph, 0x30000000ULL);
  visits = AppleAgxGpuvaG3LookupStatsVisits();
  assert(!graph.Uncertain);
  for (unsigned int i = 0; i < frames; ++i)
    AppleAgxGpuvaG3MappingRelease(&graph,
        0x40000000ULL + (unsigned long long)i * 0x4000ULL);
  assert(!graph.Uncertain && registry.Frames == 0);
  return visits;
}

static void lookup_scale(void) {
  unsigned long long small = scaling_visits(64u), small_update;
  unsigned long long large, large_update;
  small_update = update_visits;
  large = scaling_visits(8192u);
  large_update = update_visits;
  assert(large <= small + 3u);
  assert(large_update <= small_update + 16u);
  assert(frame_visits(8192u) <= frame_visits(64u) + 16u);
}

static void level_reuse(void) {
  struct fixture f = {0};
  APPLE_AGX_GPUVA_V5_CLIENT client;
  APPLE_AGX_GPUVA_V5_IO io = {&f, write64, read64, write32, barrier, 0};
  APPLE_AGX_GPUVA_G3_GRAPH graph;
  unsigned int before;
  static const unsigned int retire[] = {
      AGX_GPUVA_V5_UPDATE_LEAF, AGX_GPUVA_V5_REVOKE_BACKING,
      AGX_GPUVA_V5_REVOKE_TABLE, AGX_GPUVA_V5_REGISTER_TABLE};
  assert(AppleAgxGpuvaV5ClientInit(&client, &io));
  assert(AppleAgxGpuvaG3GraphInit(&graph, &client, 1u, 1u,
                                  allocate, release, 0));
  assert(AppleAgxGpuvaG3GraphCreate(&graph, 0x10000000ULL, false));
  assert(AppleAgxGpuvaG3GraphRegisterTable(&graph,0x10004000ULL,1u));
  assert(AppleAgxGpuvaG3GraphRegisterTable(&graph,0x10008000ULL,2u));
  assert(AppleAgxGpuvaG3GraphUpdateParent(&graph,0x10000000ULL,0u,
                                          0x10004000ULL));
  assert(AppleAgxGpuvaG3GraphUpdateParent(&graph,0x10004000ULL,0u,
                                          0x10008000ULL));
  assert(AppleAgxGpuvaG3GraphUpdateLeaf(&graph,0x10008000ULL,8u,
                                        0x20000000ULL,true));
  /* Still reachable from the root: a level change is a real conflict. */
  before = f.count;
  assert(!AppleAgxGpuvaG3GraphRegisterTable(&graph,0x10008000ULL,1u));
  assert(f.count == before);
  /* EXP847: VidMm frees the subtree by clearing only the root link; the
   * freed level-1 table still holds its stale link to the leaf page. */
  assert(AppleAgxGpuvaG3GraphUpdateParent(&graph,0x10000000ULL,0u,0ULL));
  before = f.count;
  assert(AppleAgxGpuvaG3GraphRegisterTable(&graph,0x10008000ULL,1u));
  assert(f.commands[before] == AGX_GPUVA_V5_UPDATE_PARENT);
  before++;
  assert(f.count - before == sizeof(retire)/sizeof(retire[0]));
  for (unsigned i = 0; i < f.count - before; i++)
    assert(f.commands[before + i] == retire[i]);
  assert(f.request.Index == 1u);
  /* The reused page is now a level-1 table the root may link. */
  assert(AppleAgxGpuvaG3GraphUpdateParent(&graph,0x10000000ULL,1u,
                                          0x10008000ULL));
  /* The root is never retired. */
  assert(!AppleAgxGpuvaG3GraphRegisterTable(&graph,0x10000000ULL,1u));
}

/* SetRootPageTable is per context and cannot fail; another context of the
 * same process may be executing.  Re-attaching the already linked private
 * branch changes nothing and must not depend on process quiescence, while a
 * real link change still requires it. */
static void private_attach_is_idempotent_while_busy(void) {
  struct fixture f = {0};
  APPLE_AGX_GPUVA_V5_CLIENT client;
  APPLE_AGX_GPUVA_V5_IO io = {&f, write64, read64, write32, barrier, 0};
  APPLE_AGX_GPUVA_G3_GRAPH graph;
  const unsigned long long va = 1ULL << 36;
  f.bulk = 1u;
  assert(AppleAgxGpuvaV5ClientInit(&client, &io));
  assert(AppleAgxGpuvaG3GraphInit(&graph, &client, 1u, 1u,
                                  allocate, release, 0));
  assert(AppleAgxGpuvaG3GraphCreate(&graph, 0x10000000ULL, false));
  assert(AppleAgxGpuvaG3GraphRegisterTable(&graph, 0x10004000ULL, 1u));
  assert(AppleAgxGpuvaG3GraphRegisterTable(&graph, 0x10008000ULL, 2u));
  assert(AppleAgxGpuvaG3GraphRegisterTable(&graph, 0x1000c000ULL, 2u));
  graph.JobInFlight = 1u;
  assert(!AppleAgxGpuvaG3GraphAttachPrivate(&graph, va, 0x10004000ULL,
                                            0x10008000ULL));
  assert(graph.AttachFailure == 2u);
  graph.JobInFlight = 0u;
  assert(AppleAgxGpuvaG3GraphAttachPrivate(&graph, va, 0x10004000ULL,
                                           0x10008000ULL));
  unsigned int commands = f.count;
  graph.JobInFlight = 1u;
  assert(AppleAgxGpuvaG3GraphAttachPrivate(&graph, va, 0x10004000ULL,
                                           0x10008000ULL));
  assert(f.count == commands && graph.AttachFailure == 0u);
  assert(!AppleAgxGpuvaG3GraphAttachPrivate(&graph, va, 0x10004000ULL,
                                            0x1000c000ULL));
  graph.LeaseToken = 9u;
  graph.JobInFlight = 0u;
  assert(AppleAgxGpuvaG3GraphAttachPrivate(&graph, va, 0x10004000ULL,
                                           0x10008000ULL));
  assert(f.count == commands);
  graph.LeaseToken = 0u;
  assert(AppleAgxGpuvaG3GraphDestroy(&graph));
}

int main(void) {
  struct fixture f = {0};
  APPLE_AGX_GPUVA_V5_CLIENT client;
  APPLE_AGX_GPUVA_V5_IO io = {&f, write64, read64, write32, barrier, 0};
  APPLE_AGX_GPUVA_G3_GRAPH graph;
  static const unsigned int order[] = {
      AGX_GPUVA_V5_CREATE, AGX_GPUVA_V5_REGISTER_TABLE,
      AGX_GPUVA_V5_REGISTER_TABLE, AGX_GPUVA_V5_UPDATE_PARENT,
      AGX_GPUVA_V5_UPDATE_PARENT, AGX_GPUVA_V5_REGISTER_BACKING,
      AGX_GPUVA_V5_UPDATE_LEAF,
      AGX_GPUVA_V5_REGISTER_BACKING, AGX_GPUVA_V5_UPDATE_LEAF,
      AGX_GPUVA_V5_REVOKE_BACKING,
      AGX_GPUVA_V5_REGISTER_BACKING, AGX_GPUVA_V5_UPDATE_LEAF,
      AGX_GPUVA_V5_REVOKE_BACKING,
      AGX_GPUVA_V5_FLUSH_TLB,
      AGX_GPUVA_V5_LEASE, AGX_GPUVA_V5_JOB_BEGIN,
      AGX_GPUVA_V5_JOB_END, AGX_GPUVA_V5_RELEASE,
      AGX_GPUVA_V5_UPDATE_LEAF, AGX_GPUVA_V5_REVOKE_BACKING,
      AGX_GPUVA_V5_UPDATE_PARENT, AGX_GPUVA_V5_UPDATE_PARENT,
      AGX_GPUVA_V5_REVOKE_TABLE, AGX_GPUVA_V5_REVOKE_TABLE,
      AGX_GPUVA_V5_DESTROY};
  assert(AppleAgxGpuvaV5ClientInit(&client, &io));
  assert(AppleAgxGpuvaG3GraphInit(&graph, &client, 1u, 1u,
                                  allocate, release, 0));
  assert(AppleAgxGpuvaG3GraphCreate(&graph, 0x10000000ULL, false));
  assert(AppleAgxGpuvaG3GraphRegisterTable(&graph,0x10004000ULL,1u));
  assert(AppleAgxGpuvaG3GraphRegisterTable(&graph,0x10008000ULL,2u));
  assert(AppleAgxGpuvaG3GraphUpdateParent(&graph,0x10000000ULL,0u,
                                          0x10004000ULL));
  assert(AppleAgxGpuvaG3GraphUpdateParent(&graph,0x10004000ULL,0u,
                                          0x10008000ULL));
  assert(AppleAgxGpuvaG3GraphUpdateLeaf(&graph,0x10008000ULL,8u,
                                        0x20000000ULL,true));
  assert(AppleAgxGpuvaG3GraphUpdateLeaf(&graph,0x10008000ULL,8u,
                                        0x20004000ULL,false));
  assert(AppleAgxGpuvaG3GraphContainsRangeAccess(
      &graph, 0x20000u, 0x4000u, false));
  assert(!AppleAgxGpuvaG3GraphContainsRangeAccess(
      &graph, 0x20000u, 0x4000u, true));
  assert(AppleAgxGpuvaG3GraphUpdateLeaf(&graph,0x10008000ULL,8u,
                                        0x20000000ULL,true));
  assert(AppleAgxGpuvaG3GraphContainsRangeAccess(
      &graph, 0x20000u, 0x4000u, true));
  assert(!AppleAgxGpuvaG3GraphContainsRangeAccess(
      &graph, 0x23fffu, 2u, true));
  assert(!AppleAgxGpuvaG3GraphContainsRangeAccess(
      &graph, (1ULL << 39) - 1u, 2u, false));
  assert(AppleAgxGpuvaG3GraphFlush(&graph,0u,0u));
  assert(AppleAgxGpuvaG3GraphContainsRange(&graph, 0x20000u, 0x1000u));
  assert(!AppleAgxGpuvaG3GraphContainsRange(&graph, 0x24000u, 0x1000u));
  {
    unsigned long long ipa = 0;
    assert(AppleAgxGpuvaG3GraphTranslateVa(&graph, 0x21234u, &ipa));
    assert(ipa == 0x20001234ULL);
  }
  assert(AppleAgxGpuvaG3GraphBeginJob(&graph, 1u));
  assert(!AppleAgxGpuvaG3GraphDestroy(&graph));
  assert(AppleAgxGpuvaG3GraphEndJob(&graph));
  assert(AppleAgxGpuvaG3GraphDestroy(&graph));
  assert(f.count == sizeof(order)/sizeof(order[0]));
  for (unsigned i=0;i<f.count;i++) assert(f.commands[i] == order[i]);
  level_reuse();
  lookup_scale();
  private_attach_is_idempotent_while_busy();
  return 0;
}
