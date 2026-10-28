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


/* rc81 (EXP2011): the TVB heap proof walked every 16-KiB page of the backed
 * heap (828 blocks = 6624 pages) at submit and again at BeginJob. */
#define LEAF_TABLES 5u
static unsigned long long table_ipa(unsigned t) { return 0x10008000ULL + t * 0x4000ULL; }
static void build(struct fixture *f, APPLE_AGX_GPUVA_V5_CLIENT *client,
                  APPLE_AGX_GPUVA_V5_IO *io, APPLE_AGX_GPUVA_G3_GRAPH *graph) {
  f->bulk = 1u;
  assert(AppleAgxGpuvaV5ClientInit(client, io));
  assert(AppleAgxGpuvaG3GraphInit(graph, client, 1u, 1u, allocate, release, 0));
  assert(AppleAgxGpuvaG3GraphCreate(graph, 0x10000000ULL, false));
  assert(AppleAgxGpuvaG3GraphRegisterTable(graph, 0x10004000ULL, 1u));
  assert(AppleAgxGpuvaG3GraphUpdateParent(graph, 0x10000000ULL, 0u, 0x10004000ULL));
  for (unsigned t = 0; t < LEAF_TABLES; ++t) {
    assert(AppleAgxGpuvaG3GraphRegisterTable(graph, table_ipa(t), 2u));
    assert(AppleAgxGpuvaG3GraphUpdateParent(graph, 0x10004000ULL, t, table_ipa(t)));
    for (unsigned i = 0; i < 2048u; ++i)
      assert(AppleAgxGpuvaG3GraphUpdateLeaf(graph, table_ipa(t), i,
          0x20000000ULL + (t * 2048u + i) * 0x4000ULL, true));
  }
}
static unsigned long long proof(APPLE_AGX_GPUVA_G3_GRAPH *g, unsigned long long va,
                                unsigned bytes, bool write, bool *ok) {
  AppleAgxGpuvaG3LookupStatsReset();
  *ok = AppleAgxGpuvaG3GraphContainsRangeAccessCached(g, va, bytes, write);
  return AppleAgxGpuvaG3LookupStatsVisits();
}
int main(void) {
  struct fixture f = {0};
  APPLE_AGX_GPUVA_V5_CLIENT client;
  APPLE_AGX_GPUVA_V5_IO io = {&f, write64, read64, write32, barrier, 0};
  APPLE_AGX_GPUVA_G3_GRAPH graph;
  /* 828 blocks of 128 KiB from VA 0: 6624 pages in leaf tables 0-3 */
  const unsigned long long va = 0; const unsigned bytes = 828u * 0x20000u;
  unsigned long long walk, again;
  bool ok;
  build(&f, &client, &io, &graph);
  walk = proof(&graph, va, bytes, true, &ok);
  assert(ok && walk >= 6624u);
  again = proof(&graph, va, bytes, true, &ok);
  assert(ok && again <= 4u);                    /* O(1), not O(pages) */
  again = proof(&graph, va, bytes, false, &ok); /* write proof covers read */
  assert(ok && again <= 4u);
  assert(AppleAgxGpuvaG3GraphContainsRangeAccess(&graph, va, bytes, true));
  /* a leaf outside the proven tables keeps the proof */
  assert(AppleAgxGpuvaG3GraphUpdateLeaf(&graph, table_ipa(4), 7u, 0x70000000ULL, true));
  again = proof(&graph, va, bytes, true, &ok);
  assert(ok && again <= 4u);
  /* a read-only remap inside it drops the proof: write fails, read passes */
  assert(AppleAgxGpuvaG3GraphUpdateLeaf(&graph, table_ipa(1), 100u, 0x71000000ULL, false));
  again = proof(&graph, va, bytes, true, &ok);
  assert(!ok && again > 4u);
  again = proof(&graph, va, bytes, true, &ok);
  assert(!ok);                                  /* a failure is never cached */
  assert(proof(&graph, va, bytes, false, &ok) >= 6624u && ok);
  assert(proof(&graph, va, bytes, false, &ok) <= 4u && ok);
  assert(proof(&graph, va, bytes, true, &ok) > 4u && !ok); /* read proof never answers write */
  /* an unmap inside it */
  assert(AppleAgxGpuvaG3GraphUpdateLeaf(&graph, table_ipa(1), 100u, 0x21000000ULL, true));
  assert(proof(&graph, va, bytes, true, &ok) >= 6624u && ok);
  assert(AppleAgxGpuvaG3GraphUpdateLeaf(&graph, table_ipa(3), 5u, 0ULL, false));
  assert(proof(&graph, va, bytes, true, &ok) > 4u && !ok);
  assert(AppleAgxGpuvaG3GraphUpdateLeaf(&graph, table_ipa(3), 5u, 0x23000000ULL, true));
  assert(proof(&graph, va, bytes, true, &ok) >= 6624u && ok);
  /* topology: unlinking a leaf table, then a root move */
  assert(AppleAgxGpuvaG3GraphUpdateParent(&graph, 0x10004000ULL, 2u, 0ULL));
  assert(proof(&graph, va, bytes, true, &ok) > 4u && !ok);
  assert(AppleAgxGpuvaG3GraphUpdateParent(&graph, 0x10004000ULL, 2u, table_ipa(2)));
  assert(proof(&graph, va, bytes, true, &ok) >= 6624u && ok);
  assert(AppleAgxGpuvaG3GraphRegisterTable(&graph, 0x10040000ULL, 0u));
  assert(AppleAgxGpuvaG3GraphBindRoot(&graph, 0x10040000ULL));
  (void)proof(&graph, va, bytes, true, &ok); /* the empty root fails at once */
  assert(!ok);
  assert(AppleAgxGpuvaG3GraphBindRoot(&graph, 0x10000000ULL));
  assert(proof(&graph, va, bytes, true, &ok) >= 6624u && ok);
  /* other ranges keep their own entries; small ranges are not cached */
  assert(proof(&graph, 0x8000000ULL, 0x1000000u, true, &ok) >= 1024u && ok);
  assert(proof(&graph, va, bytes, true, &ok) <= 4u && ok);
  assert(proof(&graph, 0x8000000ULL, 0x1000000u, true, &ok) <= 4u && ok);
  assert(proof(&graph, 0x4000ULL, 0x4000u, true, &ok) >= 1u && ok);
  assert(proof(&graph, 0x4000ULL, 0x4000u, true, &ok) >= 1u && ok);
  /* an uncertain graph proves nothing, cached or not */
  graph.Uncertain = 1u;
  assert(proof(&graph, va, bytes, true, &ok) <= 4u && !ok);
  graph.Uncertain = 0u;
  assert(AppleAgxGpuvaG3GraphDestroy(&graph));
  return 0;
}
