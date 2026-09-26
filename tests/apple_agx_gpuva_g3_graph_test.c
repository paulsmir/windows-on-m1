#include "apple_agx_gpuva_g3_graph.h"
#include <assert.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

struct fixture {
  AGX_GPUVA_V5_REQUEST request;
  AGX_GPUVA_V5_RESPONSE response;
  unsigned int commands[32], count;
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
      f->request.Version != AGX_GPUVA_V5_VERSION || f->count >= 32u) return false;
  f->commands[f->count++] = f->request.Command;
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

static void level_reuse(void) {
  struct fixture f = {0};
  APPLE_AGX_GPUVA_V5_CLIENT client;
  APPLE_AGX_GPUVA_V5_IO io = {&f, write64, read64, write32, barrier};
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

int main(void) {
  struct fixture f = {0};
  APPLE_AGX_GPUVA_V5_CLIENT client;
  APPLE_AGX_GPUVA_V5_IO io = {&f, write64, read64, write32, barrier};
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
  return 0;
}
