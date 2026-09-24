#include "apple_agx_gpuva_g3_graph.h"
#include <string.h>

#define G3_PAGE 0x4000ULL

static APPLE_AGX_GPUVA_G3_NODE *node(APPLE_AGX_GPUVA_G3_GRAPH *graph) {
  APPLE_AGX_GPUVA_G3_NODE *item = graph->Allocate(
      graph->MemoryContext, sizeof(*item));
  if (item) memset(item, 0, sizeof(*item));
  return item;
}

static bool call_with_response(APPLE_AGX_GPUVA_G3_GRAPH *graph,
                 AGX_GPUVA_V5_REQUEST *request,
                 AGX_GPUVA_V5_RESPONSE *response_out) {
  AGX_GPUVA_V5_RESPONSE response = {0};
  if (!graph || !graph->Client || graph->Uncertain) return false;
  request->ProcessId = graph->ProcessId;
  request->ProcessGeneration = graph->ProcessGeneration;
  if (!AppleAgxGpuvaV5ClientCall(graph->Client, request, &response)) {
    graph->LastStatus = ~0u;
    graph->Uncertain = 1u;
    return false;
  }
  graph->LastStatus = response.Status;
  if (response_out) *response_out = response;
  if (response.Flags || response.Status == 5u || response.Status == 6u)
    graph->Uncertain = 1u;
  return response.Status == 0u && response.Flags == 0u;
}

static bool call(APPLE_AGX_GPUVA_G3_GRAPH *graph,
                 AGX_GPUVA_V5_REQUEST *request) {
  return call_with_response(graph, request, 0);
}

static APPLE_AGX_GPUVA_G3_NODE *find_table(
    APPLE_AGX_GPUVA_G3_GRAPH *graph, unsigned long long ipa,
    unsigned int level) {
  APPLE_AGX_GPUVA_G3_NODE *item;
  for (item = graph->Tables; item; item = item->Next)
    if (item->Ipa == ipa && item->Level == level) return item;
  return 0;
}

static APPLE_AGX_GPUVA_G3_NODE *find_edge(
    APPLE_AGX_GPUVA_G3_NODE *head, unsigned long long ipa,
    unsigned int index) {
  for (; head; head = head->Next)
    if (head->Ipa == ipa && head->Index == index) return head;
  return 0;
}

static APPLE_AGX_GPUVA_G3_NODE *find_backing(
    APPLE_AGX_GPUVA_G3_GRAPH *graph, unsigned long long ipa) {
  APPLE_AGX_GPUVA_G3_NODE *item;
  for (item = graph->Backings; item; item = item->Next)
    if (item->Ipa == ipa) return item;
  return 0;
}

static void remove_node(APPLE_AGX_GPUVA_G3_GRAPH *graph,
    APPLE_AGX_GPUVA_G3_NODE **head, APPLE_AGX_GPUVA_G3_NODE *item) {
  APPLE_AGX_GPUVA_G3_NODE **link = head;
  while (*link && *link != item) link = &(*link)->Next;
  if (*link) {
    *link = item->Next;
    graph->Free(graph->MemoryContext, item);
  }
}

static bool revoke_unused_backing(APPLE_AGX_GPUVA_G3_GRAPH *graph,
    APPLE_AGX_GPUVA_G3_NODE *backing) {
  AGX_GPUVA_V5_REQUEST revoke = {0};
  if (!backing || backing->References) return true;
  revoke.Command = AGX_GPUVA_V5_REVOKE_BACKING;
  revoke.AuxIpa = backing->Ipa;
  revoke.AllocationGeneration = backing->Generation;
  if (!call(graph, &revoke)) {
    graph->Uncertain = 1u;
    return false;
  }
  remove_node(graph, &graph->Backings, backing);
  return true;
}

bool AppleAgxGpuvaG3GraphInit(APPLE_AGX_GPUVA_G3_GRAPH *graph,
    APPLE_AGX_GPUVA_V5_CLIENT *client, unsigned long long id,
    unsigned long long generation, APPLE_AGX_GPUVA_G3_ALLOC allocate,
    APPLE_AGX_GPUVA_G3_FREE free_node, void *memory_context) {
  if (!graph || !client || !id || !generation || !allocate || !free_node)
    return false;
  memset(graph, 0, sizeof(*graph));
  graph->Client = client;
  graph->Allocate = allocate;
  graph->Free = free_node;
  graph->MemoryContext = memory_context;
  graph->ProcessId = id;
  graph->ProcessGeneration = generation;
  return true;
}

bool AppleAgxGpuvaG3GraphCreate(APPLE_AGX_GPUVA_G3_GRAPH *graph,
    unsigned long long root_ipa, bool paging) {
  AGX_GPUVA_V5_REQUEST request = {0};
  APPLE_AGX_GPUVA_G3_NODE *root;
  if (!graph || graph->Created || !root_ipa || (root_ipa & (G3_PAGE - 1u)) ||
      graph->Uncertain) return false;
  root = node(graph);
  if (!root) return false;
  request.Command = AGX_GPUVA_V5_CREATE;
  request.TableIpa = root_ipa;
  request.Flags = paging ? 1u : 0u;
  if (!call(graph, &request)) {
    graph->Free(graph->MemoryContext, root);
    return false;
  }
  root->Ipa = root_ipa;
  root->Level = 0u;
  graph->Tables = root;
  graph->RootIpa = root_ipa;
  graph->Created = 1u;
  return true;
}

bool AppleAgxGpuvaG3GraphRegisterTable(APPLE_AGX_GPUVA_G3_GRAPH *graph,
    unsigned long long ipa, unsigned int level) {
  AGX_GPUVA_V5_REQUEST request = {0};
  APPLE_AGX_GPUVA_G3_NODE *item, *existing;
  if (!graph || !graph->Created || graph->Uncertain || !ipa ||
      (ipa & (G3_PAGE - 1u)) || level > 2u) return false;
  for (existing = graph->Tables; existing; existing = existing->Next)
    if (existing->Ipa == ipa) return existing->Level == level;
  item = node(graph);
  if (!item) return false;
  request.Command = AGX_GPUVA_V5_REGISTER_TABLE;
  request.AuxIpa = ipa;
  request.Index = level;
  if (!call(graph, &request)) {
    graph->Free(graph->MemoryContext, item);
    return false;
  }
  item->Ipa = ipa;
  item->Level = level;
  item->Next = graph->Tables;
  graph->Tables = item;
  return true;
}

bool AppleAgxGpuvaG3GraphBindRoot(APPLE_AGX_GPUVA_G3_GRAPH *graph,
    unsigned long long ipa) {
  AGX_GPUVA_V5_REQUEST request = {0};
  if (!graph || !graph->Created || graph->Uncertain ||
      !find_table(graph, ipa, 0u)) return false;
  if (graph->RootIpa == ipa) return true;
  request.Command = AGX_GPUVA_V5_RELOCATE_ROOT;
  request.TableIpa = ipa;
  if (!call(graph, &request)) return false;
  graph->RootIpa = ipa;
  return true;
}

bool AppleAgxGpuvaG3GraphUpdateParent(APPLE_AGX_GPUVA_G3_GRAPH *graph,
    unsigned long long table_ipa, unsigned int index,
    unsigned long long child_ipa) {
  AGX_GPUVA_V5_REQUEST request = {0};
  APPLE_AGX_GPUVA_G3_NODE *parent, *edge, *new_edge = 0;
  if (!graph || !graph->Created || graph->Uncertain) return false;
  parent = find_table(graph, table_ipa, 0u);
  if (!parent) parent = find_table(graph, table_ipa, 1u);
  if (!parent || index >= (parent->Level == 0u ? 8u : 2048u) ||
      (child_ipa && !find_table(graph, child_ipa, parent->Level + 1u)))
    return false;
  edge = find_edge(graph->Parents, table_ipa, index);
  if (child_ipa && edge && edge->AuxIpa == child_ipa) return true;
  if (!child_ipa && !edge) return true;
  if (child_ipa && !edge && !(new_edge = node(graph))) return false;
  request.Command = AGX_GPUVA_V5_UPDATE_PARENT;
  request.TableIpa = table_ipa;
  request.Index = index;
  request.AuxIpa = child_ipa;
  if (!call(graph, &request)) {
    if (new_edge) graph->Free(graph->MemoryContext, new_edge);
    return false;
  }
  if (child_ipa && edge) {
    edge->AuxIpa = child_ipa;
  } else if (child_ipa) {
    new_edge->Ipa = table_ipa;
    new_edge->AuxIpa = child_ipa;
    new_edge->Index = index;
    new_edge->Next = graph->Parents;
    graph->Parents = new_edge;
  } else {
    remove_node(graph, &graph->Parents, edge);
  }
  return true;
}

bool AppleAgxGpuvaG3GraphUpdateLeaf(APPLE_AGX_GPUVA_G3_GRAPH *graph,
    unsigned long long table_ipa, unsigned int index,
    unsigned long long guest_ipa, bool writable) {
  AGX_GPUVA_V5_REQUEST request = {0};
  APPLE_AGX_GPUVA_G3_NODE *leaf, *new_leaf = 0, *backing = 0;
  APPLE_AGX_GPUVA_G3_NODE *old_backing = 0;
  bool new_backing = false;
  if (!graph || !graph->Created || graph->Uncertain ||
      !find_table(graph, table_ipa, 2u) || index >= 2048u ||
      (guest_ipa && (guest_ipa & (G3_PAGE - 1u)))) return false;
  leaf = find_edge(graph->Leaves, table_ipa, index);
  if (guest_ipa && leaf && leaf->AuxIpa == guest_ipa &&
      leaf->Writable == (unsigned)writable) return true;
  if (!guest_ipa && !leaf) return true;
  if (guest_ipa) {
    if (!leaf) {
      new_leaf = node(graph);
      if (!new_leaf) return false;
    } else {
      old_backing = find_backing(graph, leaf->AuxIpa);
      if (!old_backing || !old_backing->References) return false;
    }
    backing = find_backing(graph, guest_ipa);
    if (!backing) {
      AGX_GPUVA_V5_REQUEST grant = {0};
      backing = node(graph);
      if (!backing) {
        if (new_leaf) graph->Free(graph->MemoryContext, new_leaf);
        return false;
      }
      backing->Ipa = guest_ipa;
      backing->Generation = ++graph->NextGeneration;
      grant.Command = AGX_GPUVA_V5_REGISTER_BACKING;
      grant.AuxIpa = guest_ipa;
      grant.AllocationGeneration = backing->Generation;
      if (!call(graph, &grant)) {
        graph->Free(graph->MemoryContext, backing);
        if (new_leaf) graph->Free(graph->MemoryContext, new_leaf);
        return false;
      }
      new_backing = true;
      backing->Next = graph->Backings;
      graph->Backings = backing;
    }
  } else {
    backing = find_backing(graph, leaf->AuxIpa);
    if (!backing || !backing->References) return false;
  }
  request.Command = AGX_GPUVA_V5_UPDATE_LEAF;
  request.TableIpa = table_ipa;
  request.Index = index;
  request.Flags = 15u;
  if (guest_ipa) {
    request.AllocationGeneration = backing->Generation;
    request.ValidMask = 15u;
    request.WritableMask = writable ? 15u : 0u;
    for (unsigned i = 0; i < 4u; ++i)
      request.LogicalIpa[i] = guest_ipa + i * 0x1000ULL;
  }
  if (!call(graph, &request)) {
    if (new_backing) {
      AGX_GPUVA_V5_REQUEST revoke = {0};
      revoke.Command = AGX_GPUVA_V5_REVOKE_BACKING;
      revoke.AuxIpa = guest_ipa;
      revoke.AllocationGeneration = backing->Generation;
      if (graph->Uncertain || !call(graph, &revoke)) graph->Uncertain = 1u;
      else remove_node(graph, &graph->Backings, backing);
    }
    if (new_leaf) graph->Free(graph->MemoryContext, new_leaf);
    return false;
  }
  if (guest_ipa) {
    if (leaf) {
      if (old_backing != backing) {
        --old_backing->References;
        ++backing->References;
      }
      leaf->AuxIpa = guest_ipa;
      leaf->Writable = writable ? 1u : 0u;
      if (old_backing != backing &&
          !revoke_unused_backing(graph, old_backing)) return false;
    } else {
      new_leaf->Ipa = table_ipa;
      new_leaf->AuxIpa = guest_ipa;
      new_leaf->Index = index;
      new_leaf->Writable = writable ? 1u : 0u;
      new_leaf->Next = graph->Leaves;
      graph->Leaves = new_leaf;
      ++backing->References;
    }
  } else {
    remove_node(graph, &graph->Leaves, leaf);
    --backing->References;
    if (!revoke_unused_backing(graph, backing)) return false;
  }
  return true;
}

bool AppleAgxGpuvaG3GraphFlush(APPLE_AGX_GPUVA_G3_GRAPH *graph,
    unsigned long long start_va, unsigned long long end_va) {
  AGX_GPUVA_V5_REQUEST request = {0};
  if (!graph || !graph->Created || graph->Uncertain) return false;
  request.Command = AGX_GPUVA_V5_FLUSH_TLB;
  request.TableIpa = graph->RootIpa;
  request.LogicalIpa[0] = start_va;
  request.LogicalIpa[1] = end_va;
  return call(graph, &request);
}

bool AppleAgxGpuvaG3GraphTranslateVa(APPLE_AGX_GPUVA_G3_GRAPH *graph,
    unsigned long long va, unsigned long long *guest_ipa) {
  APPLE_AGX_GPUVA_G3_NODE *root_edge, *middle_edge, *leaf;
  if (!graph || !graph->Created || graph->Uncertain || !guest_ipa ||
      va >= (1ULL << 39)) return false;
  root_edge = find_edge(graph->Parents, graph->RootIpa,
                        (unsigned int)((va >> 36) & 7u));
  if (!root_edge) return false;
  middle_edge = find_edge(graph->Parents, root_edge->AuxIpa,
                          (unsigned int)((va >> 25) & 2047u));
  if (!middle_edge) return false;
  leaf = find_edge(graph->Leaves, middle_edge->AuxIpa,
                   (unsigned int)((va >> 14) & 2047u));
  if (!leaf || !leaf->AuxIpa) return false;
  *guest_ipa = leaf->AuxIpa + (va & (G3_PAGE - 1u));
  return true;
}

bool AppleAgxGpuvaG3GraphContainsRange(APPLE_AGX_GPUVA_G3_GRAPH *graph,
    unsigned long long start_va, unsigned int bytes) {
  unsigned long long va, end, ignored;
  if (!bytes || start_va >= (1ULL << 39) ||
      bytes > (1ULL << 39) - start_va) return false;
  end = start_va + bytes;
  for (va = start_va & ~(G3_PAGE - 1u); va < end; va += G3_PAGE)
    if (!AppleAgxGpuvaG3GraphTranslateVa(graph, va, &ignored)) return false;
  return true;
}

bool AppleAgxGpuvaG3GraphBeginJob(APPLE_AGX_GPUVA_G3_GRAPH *graph,
    unsigned int slot) {
  AGX_GPUVA_V5_REQUEST request = {0};
  AGX_GPUVA_V5_RESPONSE response = {0};
  if (!graph || !graph->Created || graph->Uncertain || graph->LeaseToken ||
      graph->JobInFlight || slot == 0u || slot >= 63u) return false;
  request.Command = AGX_GPUVA_V5_LEASE;
  request.Slot = slot;
  if (!call_with_response(graph, &request, &response) || !response.Token) {
    if (!graph->Uncertain && graph->LastStatus == 0u)
      graph->Uncertain = 1u;
    return false;
  }
  graph->LeaseToken = response.Token;
  graph->Slot = slot;
  request = (AGX_GPUVA_V5_REQUEST){0};
  request.Command = AGX_GPUVA_V5_JOB_BEGIN;
  request.Slot = slot;
  request.Token = graph->LeaseToken;
  if (!call(graph, &request)) {
    graph->Uncertain = 1u;
    return false;
  }
  graph->JobInFlight = 1u;
  return true;
}

bool AppleAgxGpuvaG3GraphEndJob(APPLE_AGX_GPUVA_G3_GRAPH *graph) {
  AGX_GPUVA_V5_REQUEST request = {0};
  if (!graph || !graph->JobInFlight || !graph->LeaseToken ||
      graph->Uncertain) return false;
  request.Command = AGX_GPUVA_V5_JOB_END;
  request.Slot = graph->Slot;
  request.Token = graph->LeaseToken;
  if (!call(graph, &request)) {
    graph->Uncertain = 1u;
    return false;
  }
  graph->JobInFlight = 0u;
  request.Command = AGX_GPUVA_V5_RELEASE;
  if (!call(graph, &request)) {
    graph->Uncertain = 1u;
    return false;
  }
  graph->LeaseToken = 0ULL;
  graph->Slot = 0u;
  return true;
}

bool AppleAgxGpuvaG3GraphDestroy(APPLE_AGX_GPUVA_G3_GRAPH *graph) {
  AGX_GPUVA_V5_REQUEST request = {0};
  APPLE_AGX_GPUVA_G3_NODE *item;
  unsigned int level;
  if (!graph || !graph->Created || graph->Uncertain ||
      graph->LeaseToken || graph->JobInFlight) return false;
  while (graph->Leaves) {
    item = graph->Leaves;
    if (!AppleAgxGpuvaG3GraphUpdateLeaf(graph, item->Ipa,
                                         item->Index, 0ULL, false)) return false;
  }
  while (graph->Backings) {
    item = graph->Backings;
    request = (AGX_GPUVA_V5_REQUEST){0};
    request.Command = AGX_GPUVA_V5_REVOKE_BACKING;
    request.AuxIpa = item->Ipa;
    request.AllocationGeneration = item->Generation;
    if (!call(graph, &request)) return false;
    remove_node(graph, &graph->Backings, item);
  }
  while (graph->Parents) {
    item = graph->Parents;
    if (!AppleAgxGpuvaG3GraphUpdateParent(graph, item->Ipa,
                                           item->Index, 0ULL)) return false;
  }
  for (level = 3u; level > 0u; --level) {
    APPLE_AGX_GPUVA_G3_NODE *next;
    for (item = graph->Tables; item; item = next) {
      next = item->Next;
      if (item->Level != level - 1u || item->Ipa == graph->RootIpa) continue;
      request = (AGX_GPUVA_V5_REQUEST){0};
      request.Command = AGX_GPUVA_V5_REVOKE_TABLE;
      request.AuxIpa = item->Ipa;
      request.Index = item->Level;
      if (!call(graph, &request)) return false;
      remove_node(graph, &graph->Tables, item);
    }
  }
  request = (AGX_GPUVA_V5_REQUEST){0};
  request.Command = AGX_GPUVA_V5_DESTROY;
  if (!call(graph, &request)) return false;
  while (graph->Tables) remove_node(graph, &graph->Tables, graph->Tables);
  graph->Created = 0u;
  graph->RootIpa = 0ULL;
  return true;
}
