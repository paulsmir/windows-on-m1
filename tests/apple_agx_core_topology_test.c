/* EXP1317: the SGX core topology derivation (Asahi drm/asahi regs.rs
 * get_gpu_id) the KMD reports to the UMD. */
#include <assert.h>
#include "apple_agx_core_topology.h"

int main(void) {
  APPLE_AGX_U32 cores = 99u, mask = 99u;
  const APPLE_AGX_U32 g13 = 0x04000000u, one_cluster = 1u << 12;
  /* t8103 8-core and 7-core parts. */
  assert(AppleAgxG13CoreTopology(g13, 8u, one_cluster, 0xffu, 0u, &cores, &mask));
  assert(cores == 8u && mask == 0xffu);
  assert(AppleAgxG13CoreTopology(g13 | 0x1100u, 0x00010008u, one_cluster | 0x12u, 0xfeu, 0u,
                                 &cores, &mask));
  assert(cores == 8u && mask == 0xfeu);
  /* Refused, and reported as 0/0: no core, a bit beyond the cluster, a
   * leftover second mask, another generation, more than one cluster, no
   * or too many cores. */
  assert(!AppleAgxG13CoreTopology(g13, 8u, one_cluster, 0u, 0u, &cores, &mask));
  assert(cores == 0u && mask == 0u);
  assert(!AppleAgxG13CoreTopology(g13, 8u, one_cluster, 0x1ffu, 0u, &cores, &mask));
  assert(!AppleAgxG13CoreTopology(g13, 8u, one_cluster, 0xffu, 1u, &cores, &mask));
  assert(!AppleAgxG13CoreTopology(0x05000000u, 8u, one_cluster, 0xffu, 0u, &cores, &mask));
  assert(!AppleAgxG13CoreTopology(g13, 8u, 2u << 12, 0xffu, 0u, &cores, &mask));
  assert(!AppleAgxG13CoreTopology(g13, 0u, one_cluster, 0xffu, 0u, &cores, &mask));
  assert(!AppleAgxG13CoreTopology(g13, 33u, one_cluster, 0xffu, 0u, &cores, &mask));
  assert(cores == 0u && mask == 0u);
  assert(AppleAgxG13CoreTopology(g13, 32u, one_cluster, 0x80000000u, 0u, &cores, &mask) &&
         cores == 32u);
  assert(!AppleAgxG13CoreTopology(g13, 8u, one_cluster, 0xffu, 0u, 0, &mask));
  return 0;
}
