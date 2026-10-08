#include "apple_agx_gpuva_g3_graph.h"
#include <string.h>

#define G3_PAGE 0x4000ULL

#ifdef APPLE_AGX_G3_LOOKUP_STATS
static unsigned long long lookup_visits;
void AppleAgxGpuvaG3LookupStatsReset(void) { lookup_visits = 0; }
unsigned long long AppleAgxGpuvaG3LookupStatsVisits(void) { return lookup_visits; }
#define LOOKUP_VISIT() (++lookup_visits)
#else
#define LOOKUP_VISIT() ((void)0)
#endif

static APPLE_AGX_GPUVA_G3_NODE *node(APPLE_AGX_GPUVA_G3_GRAPH *graph) {
  APPLE_AGX_GPUVA_G3_NODE *item = graph->Allocate(
      graph->MemoryContext, sizeof(*item));
  if (item) memset(item, 0, sizeof(*item));
  return item;
}

static unsigned int bucket(unsigned long long ipa) {
  unsigned long long key = (ipa >> 14) * 0x9e3779b97f4a7c15ULL;
  return (unsigned int)(key >> 52); /* 4096 buckets */
}

/* Lists keep their head-first order; Prev makes removal O(1). */
static void push_node(APPLE_AGX_GPUVA_G3_NODE **head,
                      APPLE_AGX_GPUVA_G3_NODE *item) {
  item->Prev = 0;
  item->Next = *head;
  if (*head) (*head)->Prev = item;
  *head = item;
}

static bool allocate_slots(APPLE_AGX_GPUVA_G3_GRAPH *graph,
                           APPLE_AGX_GPUVA_G3_NODE *table) {
  table->Slots = graph->Allocate(graph->MemoryContext,
      2048u * sizeof(*table->Slots));
  if (!table->Slots) return false;
  memset(table->Slots, 0, 2048u * sizeof(*table->Slots));
  return true;
}

static APPLE_AGX_GPUVA_G3_NODE *slot(APPLE_AGX_GPUVA_G3_NODE *table,
                                    unsigned int index) {
  LOOKUP_VISIT();
  return table && table->Slots ? table->Slots[index] : 0;
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
  APPLE_AGX_GPUVA_G3_NODE *item = graph->TableHint;
  if (item && item->Ipa == ipa && item->Level == level) return item;
  for (item = graph->Tables; item; item = item->Next) {
    LOOKUP_VISIT();
    if (item->Ipa == ipa && item->Level == level) {
      graph->TableHint = item;
      return item;
    }
  }
  return 0;
}

static APPLE_AGX_GPUVA_G3_NODE *find_edge(
    APPLE_AGX_GPUVA_G3_NODE *head, unsigned long long ipa,
    unsigned int index) {
  for (; head; head = head->Next) {
    LOOKUP_VISIT();
    if (head->Ipa == ipa && head->Index == index) return head;
  }
  return 0;
}

static APPLE_AGX_GPUVA_G3_NODE *find_backing(
    APPLE_AGX_GPUVA_G3_GRAPH *graph, unsigned long long ipa) {
  APPLE_AGX_GPUVA_G3_NODE *item;
  for (item = graph->BackingBuckets[bucket(ipa)]; item; item = item->HashNext) {
    LOOKUP_VISIT();
    if (item->Ipa == ipa) return item;
  }
  return 0;
}

static void hash_backing(APPLE_AGX_GPUVA_G3_GRAPH *graph,
                         APPLE_AGX_GPUVA_G3_NODE *backing) {
  APPLE_AGX_GPUVA_G3_NODE **head = &graph->BackingBuckets[bucket(backing->Ipa)];
  backing->HashNext = *head;
  *head = backing;
}

static void unhash_backing(APPLE_AGX_GPUVA_G3_GRAPH *graph,
                           APPLE_AGX_GPUVA_G3_NODE *backing) {
  APPLE_AGX_GPUVA_G3_NODE **link = &graph->BackingBuckets[bucket(backing->Ipa)];
  while (*link && *link != backing) { LOOKUP_VISIT(); link = &(*link)->HashNext; }
  if (*link) *link = backing->HashNext;
  backing->HashNext = 0;
}

static void remove_node(APPLE_AGX_GPUVA_G3_GRAPH *graph,
    APPLE_AGX_GPUVA_G3_NODE **head, APPLE_AGX_GPUVA_G3_NODE *item) {
  APPLE_AGX_GPUVA_G3_NODE **link = head;
  if (!item) return;
  if (item->Prev ? item->Prev->Next == item : *head == item) {
    if (item->Prev) item->Prev->Next = item->Next; else *head = item->Next;
  } else {
    /* Not linked through push_node: keep the original search semantics. */
    while (*link && *link != item) { LOOKUP_VISIT(); link = &(*link)->Next; }
    if (!*link) return;
    *link = item->Next;
  }
  if (item->Next) item->Next->Prev = item->Prev;
  if (graph->TableHint == item) graph->TableHint = 0;
  if (item->Slots) graph->Free(graph->MemoryContext, item->Slots);
  graph->Free(graph->MemoryContext, item);
}

static APPLE_AGX_GPUVA_G3_FRAME *find_frame(APPLE_AGX_GPUVA_G3_GRAPH *g,
    unsigned long long ipa) {
  APPLE_AGX_GPUVA_G3_FRAME *f;
  if (!g->Registry) return 0;
  for (f = g->Registry->FrameBuckets[bucket(ipa)]; f; f = f->HashNext) {
    LOOKUP_VISIT();
    if (f->Ipa == ipa) return f;
  }
  return 0;
}

static void release_frame(APPLE_AGX_GPUVA_G3_GRAPH *g,
    APPLE_AGX_GPUVA_G3_FRAME *f) {
  APPLE_AGX_GPUVA_G3_FRAME **link;
  if (!f || f->Mappings || f->Grants) return;
  if (f->Prev ? f->Prev->Next != f : g->Registry->Frames != f) return;
  if (f->Prev) f->Prev->Next = f->Next; else g->Registry->Frames = f->Next;
  if (f->Next) f->Next->Prev = f->Prev;
  for (link = &g->Registry->FrameBuckets[bucket(f->Ipa)]; *link && *link != f;
       link = &(*link)->HashNext) LOOKUP_VISIT();
  if (*link) *link = f->HashNext;
  g->Free(g->MemoryContext, f);
}

bool AppleAgxGpuvaG3MappingAcquire(APPLE_AGX_GPUVA_G3_GRAPH *g,
    unsigned long long ipa) {
  APPLE_AGX_GPUVA_G3_FRAME *f;
  if (!g || !g->Registry || !ipa || (ipa & 0xfffULL) ||
      ipa > ~0ULL - 0xfffULL || g->Uncertain) return false;
  ipa &= ~(G3_PAGE - 1u);
  f = find_frame(g, ipa);
  if (!f) {
    if (g->Registry->NextGeneration == ~0ULL) return false;
    f = g->Allocate(g->MemoryContext, sizeof(*f));
    if (!f) return false;
    memset(f, 0, sizeof(*f));
    f->Ipa = ipa;
    f->Generation = ++g->Registry->NextGeneration;
    f->Next = g->Registry->Frames;
    if (f->Next) f->Next->Prev = f;
    g->Registry->Frames = f;
    f->HashNext = g->Registry->FrameBuckets[bucket(ipa)];
    g->Registry->FrameBuckets[bucket(ipa)] = f;
  }
  if (f->Mappings == ~0ULL) return false;
  ++f->Mappings;
  return true;
}

void AppleAgxGpuvaG3MappingRelease(APPLE_AGX_GPUVA_G3_GRAPH *g,
    unsigned long long ipa) {
  APPLE_AGX_GPUVA_G3_FRAME *f = find_frame(g, ipa & ~(G3_PAGE - 1u));
  if (!f || !f->Mappings) { g->Uncertain = 1u; return; }
  --f->Mappings;
  release_frame(g, f);
}

static void release_grant(APPLE_AGX_GPUVA_G3_GRAPH *g,
    APPLE_AGX_GPUVA_G3_NODE *backing) {
  if (backing->Frame) {
    --backing->Frame->Grants;
    release_frame(g, backing->Frame);
  }
  unhash_backing(g, backing);
  remove_node(g, &g->Backings, backing);
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
  release_grant(graph, backing);
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
  if (!allocate_slots(graph, root)) {
    graph->Free(graph->MemoryContext, root);
    return false;
  }
  request.Command = AGX_GPUVA_V5_CREATE;
  request.TableIpa = root_ipa;
  request.Flags = paging ? 1u : 0u;
  if (!call(graph, &request)) {
    graph->Free(graph->MemoryContext, root->Slots);
    graph->Free(graph->MemoryContext, root);
    return false;
  }
  root->Ipa = root_ipa;
  root->Level = 0u;
  push_node(&graph->Tables, root);
  graph->RootTable = root;
  graph->RootIpa = root_ipa;
  graph->Created = 1u;
  return true;
}

static bool reachable(APPLE_AGX_GPUVA_G3_GRAPH *graph,
    unsigned long long ipa, unsigned int depth) {
  APPLE_AGX_GPUVA_G3_NODE *edge;
  if (ipa == graph->RootIpa) return true;
  if (depth > 3u) return false;
  for (edge = graph->Parents; edge; edge = edge->Next)
    if (edge->AuxIpa == ipa && reachable(graph, edge->Ipa, depth + 1u))
      return true;
  return false;
}

static bool retire_table(APPLE_AGX_GPUVA_G3_GRAPH *graph,
    APPLE_AGX_GPUVA_G3_NODE *table) {
  AGX_GPUVA_V5_REQUEST request = {0};
  APPLE_AGX_GPUVA_G3_NODE *edge;
  if (table->Ipa == graph->RootIpa) return false;
  /* VidMm frees a subtree by clearing only its top link, so links held by
   * an unreachable (freed) parent are stale: clear them.  A link from a
   * table still reachable from the root is a real conflict. */
  for (;;) {
    for (edge = graph->Parents; edge; edge = edge->Next)
      if (edge->AuxIpa == table->Ipa) break;
    if (!edge) break;
    if (reachable(graph, edge->Ipa, 0u)) return false;
    if (!AppleAgxGpuvaG3GraphUpdateParent(graph, edge->Ipa, edge->Index,
                                          0ULL)) return false;
  }
  if (table->Level == 2u) {
    while ((edge = graph->Leaves) != 0) {
      for (; edge && edge->Ipa != table->Ipa; edge = edge->Next) {}
      if (!edge) break;
      if (!AppleAgxGpuvaG3GraphUpdateLeaf(graph, table->Ipa, edge->Index,
                                          0ULL, false)) return false;
    }
  } else {
    while ((edge = graph->Parents) != 0) {
      for (; edge && edge->Ipa != table->Ipa; edge = edge->Next) {}
      if (!edge) break;
      if (!AppleAgxGpuvaG3GraphUpdateParent(graph, table->Ipa, edge->Index,
                                            0ULL)) return false;
    }
  }
  request.Command = AGX_GPUVA_V5_REVOKE_TABLE;
  request.TableIpa = table->Ipa;
  request.Index = table->Level;
  if (!call(graph, &request)) return false;
  remove_node(graph, &graph->Tables, table);
  return true;
}

bool AppleAgxGpuvaG3GraphRegisterTable(APPLE_AGX_GPUVA_G3_GRAPH *graph,
    unsigned long long ipa, unsigned int level) {
  AGX_GPUVA_V5_REQUEST request = {0};
  APPLE_AGX_GPUVA_G3_NODE *item, *existing;
  if (!graph || !graph->Created || graph->Uncertain || !ipa ||
      (ipa & (G3_PAGE - 1u)) || level > 2u) return false;
  for (existing = graph->Tables; existing; existing = existing->Next)
    if (existing->Ipa == ipa) break;
  if (existing && existing->Level == level) return true;
  /* VidMm reuses a freed page-table page at another level (EXP846 0x10E/0xB).
   * Retire the old table only when no parent still links it: clear its own
   * entries, revoke it, then register the page at the new level. */
  /* Reserve replacement metadata before destructive retirement. */
  item = node(graph);
  if (!item) return false;
  if (!allocate_slots(graph, item)) {
    graph->Free(graph->MemoryContext, item);
    return false;
  }
  if (existing && !retire_table(graph, existing)) {
    graph->Free(graph->MemoryContext, item->Slots);
    graph->Free(graph->MemoryContext, item);
    return false;
  }
  request.Command = AGX_GPUVA_V5_REGISTER_TABLE;
  request.AuxIpa = ipa;
  request.Index = level;
  if (!call(graph, &request)) {
    /* A replaced table is already retired. Retain KMD lifetime records and
     * forbid retry from treating the old shadow as a fresh table. */
    if (existing) graph->Uncertain = 1u;
    graph->Free(graph->MemoryContext, item->Slots);
    graph->Free(graph->MemoryContext, item);
    return false;
  }
  item->Ipa = ipa;
  item->Level = level;
  push_node(&graph->Tables, item);
  return true;
}

bool AppleAgxGpuvaG3GraphBindRoot(APPLE_AGX_GPUVA_G3_GRAPH *graph,
    unsigned long long ipa) {
  AGX_GPUVA_V5_REQUEST request = {0};
  if (!graph || !graph->Created || graph->Uncertain ||
      !find_table(graph, ipa, 0u) || graph->MappingGeneration == ~0ULL) return false;
  if (graph->RootIpa == ipa) return true;
  request.Command = AGX_GPUVA_V5_RELOCATE_ROOT;
  request.TableIpa = ipa;
  if (!call(graph, &request)) return false;
  graph->RootIpa = ipa;
  graph->RootTable = find_table(graph, ipa, 0u);
  ++graph->MappingGeneration;
  return true;
}

bool AppleAgxGpuvaG3GraphUpdateParent(APPLE_AGX_GPUVA_G3_GRAPH *graph,
    unsigned long long table_ipa, unsigned int index,
    unsigned long long child_ipa) {
  AGX_GPUVA_V5_REQUEST request = {0};
  APPLE_AGX_GPUVA_G3_NODE *parent, *edge, *new_edge = 0, *child = 0;
  if (!graph || !graph->Created || graph->Uncertain) return false;
  if (graph->JobInFlight || graph->MappingGeneration == ~0ULL) return false;
  parent = find_table(graph, table_ipa, 0u);
  if (!parent) parent = find_table(graph, table_ipa, 1u);
  if (child_ipa && parent)
    child = find_table(graph, child_ipa, parent->Level + 1u);
  if (!parent || index >= (parent->Level == 0u ? 8u : 2048u) ||
      (child_ipa && !child))
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
    edge->ChildTable = child;
  } else if (child_ipa) {
    new_edge->Ipa = table_ipa;
    new_edge->AuxIpa = child_ipa;
    new_edge->Index = index;
    new_edge->ChildTable = child;
    push_node(&graph->Parents, new_edge);
    parent->Slots[index] = new_edge;
  } else {
    parent->Slots[index] = 0;
    remove_node(graph, &graph->Parents, edge);
  }
  ++graph->MappingGeneration;
  return true;
}

bool AppleAgxGpuvaG3GraphUpdateLeaf(APPLE_AGX_GPUVA_G3_GRAPH *graph,
    unsigned long long table_ipa, unsigned int index,
    unsigned long long guest_ipa, bool writable) {
  return AppleAgxGpuvaG3GraphUpdateLeafBacking(graph, table_ipa, index,
      guest_ipa, writable, AppleAgxGpuvaG3LocalBacking);
}

bool AppleAgxGpuvaG3GraphUpdateLeafBacking(APPLE_AGX_GPUVA_G3_GRAPH *graph,
    unsigned long long table_ipa, unsigned int index,
    unsigned long long guest_ipa, bool writable,
    APPLE_AGX_GPUVA_G3_BACKING_KIND kind) {
  return AppleAgxGpuvaG3GraphTryLeafBacking(graph, table_ipa, index,
      guest_ipa, writable, kind, 0);
}

bool AppleAgxGpuvaG3GraphTryLeafBacking(APPLE_AGX_GPUVA_G3_GRAPH *graph,
    unsigned long long table_ipa, unsigned int index,
    unsigned long long guest_ipa, bool writable,
    APPLE_AGX_GPUVA_G3_BACKING_KIND kind, bool *unavailable) {
  AGX_GPUVA_V5_REQUEST request = {0};
  APPLE_AGX_GPUVA_G3_NODE *leaf, *new_leaf = 0, *backing = 0;
  APPLE_AGX_GPUVA_G3_NODE *old_backing = 0, *table;
  bool new_backing = false;
  if (unavailable) *unavailable = false;
  if (!graph || !graph->Created || graph->Uncertain || graph->JobInFlight ||
      graph->MappingGeneration == ~0ULL ||
      (kind != AppleAgxGpuvaG3LocalBacking && kind != AppleAgxGpuvaG3SystemBacking &&
       kind != AppleAgxGpuvaG3PrivateBacking) ||
      !(table = find_table(graph, table_ipa, 2u)) || index >= 2048u ||
      (guest_ipa && (guest_ipa & (G3_PAGE - 1u)))) return false;
  /* table->Slots[index] is the Leaves edge (table_ipa, index): set on insert,
   * cleared before removal, freed only with the retired table. */
  leaf = slot(table, index);
  if (guest_ipa && leaf && leaf->AuxIpa == guest_ipa &&
      leaf->Writable == (unsigned)writable && leaf->Kind == kind) return true;
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
    if (backing && backing->Kind != kind) {
      if (new_leaf) graph->Free(graph->MemoryContext, new_leaf);
      return false;
    }
    if (!backing) {
      AGX_GPUVA_V5_REQUEST grant = {0};
      backing = node(graph);
      if (!backing) {
        if (new_leaf) graph->Free(graph->MemoryContext, new_leaf);
        return false;
      }
      backing->Ipa = guest_ipa;
      backing->Kind = kind;
      if (kind == AppleAgxGpuvaG3SystemBacking) {
        backing->Frame = find_frame(graph, guest_ipa);
        if (!backing->Frame || !backing->Frame->Mappings ||
            backing->Frame->Grants == ~0ULL) {
          graph->Free(graph->MemoryContext, backing);
          if (new_leaf) graph->Free(graph->MemoryContext, new_leaf);
          return false;
        }
      }
      if (kind == AppleAgxGpuvaG3PrivateBacking && graph->NextGeneration == ~0ULL) {
        graph->Free(graph->MemoryContext, backing);
        if (new_leaf) graph->Free(graph->MemoryContext, new_leaf);
        return false;
      }
      backing->Generation = kind != AppleAgxGpuvaG3PrivateBacking &&
          graph->SharedBackingGeneration ?
          graph->SharedBackingGeneration : ++graph->NextGeneration;
      if (backing->Frame) backing->Generation = backing->Frame->Generation;
      grant.Command = kind != AppleAgxGpuvaG3PrivateBacking &&
          (backing->Frame || graph->SharedBackingGeneration) ?
          AGX_GPUVA_V5_REGISTER_SHARED_BACKING :
          AGX_GPUVA_V5_REGISTER_BACKING;
      grant.AuxIpa = guest_ipa;
      grant.AllocationGeneration = backing->Generation;
      if (!call(graph, &grant)) {
        /* v5 OWNERSHIP=4 / CAPACITY=7 are acknowledged registration
         * refusals, with no grant or leaf store. Do not infer this from a
         * stale LastStatus at any other failure site. */
        if (unavailable && kind == AppleAgxGpuvaG3SystemBacking &&
            !graph->Uncertain &&
            (graph->LastStatus == 4u || graph->LastStatus == 7u))
          *unavailable = true;
        graph->Free(graph->MemoryContext, backing);
        if (new_leaf) graph->Free(graph->MemoryContext, new_leaf);
        return false;
      }
      new_backing = true;
      if (backing->Frame) ++backing->Frame->Grants;
      push_node(&graph->Backings, backing);
      hash_backing(graph, backing);
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
    unsigned int rejected_status = graph->LastStatus;
    if (new_backing) {
      AGX_GPUVA_V5_REQUEST revoke = {0};
      revoke.Command = AGX_GPUVA_V5_REVOKE_BACKING;
      revoke.AuxIpa = guest_ipa;
      revoke.AllocationGeneration = backing->Generation;
      if (graph->Uncertain || !call(graph, &revoke)) graph->Uncertain = 1u;
      else release_grant(graph, backing);
    }
    graph->LastStatus = rejected_status;
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
      leaf->Kind = kind;
      leaf->Generation = backing->Generation;
      leaf->Writable = writable ? 1u : 0u;
      if (old_backing != backing &&
          !revoke_unused_backing(graph, old_backing)) return false;
    } else {
      new_leaf->Ipa = table_ipa;
      new_leaf->AuxIpa = guest_ipa;
      new_leaf->Kind = kind;
      new_leaf->Generation = backing->Generation;
      new_leaf->Index = index;
      new_leaf->Writable = writable ? 1u : 0u;
      push_node(&graph->Leaves, new_leaf);
      table->Slots[index] = new_leaf;
      ++backing->References;
    }
  } else {
    table->Slots[index] = 0;
    remove_node(graph, &graph->Leaves, leaf);
    --backing->References;
    if (!revoke_unused_backing(graph, backing)) return false;
  }
  ++graph->MappingGeneration;
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

const APPLE_AGX_GPUVA_G3_NODE *AppleAgxGpuvaG3GraphLeaf(
    APPLE_AGX_GPUVA_G3_GRAPH *graph, unsigned long long table_ipa,
    unsigned int index) {
  if (!graph || index >= 2048u) return 0;
  return slot(find_table(graph, table_ipa, 2u), index);
}

bool AppleAgxGpuvaG3GraphTranslateVa(APPLE_AGX_GPUVA_G3_GRAPH *graph,
    unsigned long long va, unsigned long long *guest_ipa) {
  APPLE_AGX_GPUVA_G3_NODE *root_edge, *middle_edge, *leaf;
  if (!graph || !graph->Created || graph->Uncertain || !guest_ipa ||
      va >= (1ULL << 39)) return false;
  root_edge = slot(graph->RootTable, (unsigned int)((va >> 36) & 7u));
  if (!root_edge) return false;
  middle_edge = slot(root_edge->ChildTable,
                     (unsigned int)((va >> 25) & 2047u));
  if (!middle_edge) return false;
  leaf = slot(middle_edge->ChildTable,
              (unsigned int)((va >> 14) & 2047u));
  if (!leaf || !leaf->AuxIpa) return false;
  *guest_ipa = leaf->AuxIpa + (va & (G3_PAGE - 1u));
  return true;
}

bool AppleAgxGpuvaG3GraphLeafTableIpa(APPLE_AGX_GPUVA_G3_GRAPH *graph,
    unsigned long long va, unsigned long long *table_ipa) {
  APPLE_AGX_GPUVA_G3_NODE *root, *middle;
  if (!graph || !graph->Created || graph->Uncertain || !table_ipa ||
      va >= (1ULL << 39)) return false;
  root = slot(graph->RootTable, (unsigned int)((va >> 36) & 7u));
  if (!root) return false;
  middle = slot(root->ChildTable, (unsigned int)((va >> 25) & 2047u));
  if (!middle) return false;
  *table_ipa = middle->AuxIpa;
  return true;
}

bool AppleAgxGpuvaG3GraphContainsRange(APPLE_AGX_GPUVA_G3_GRAPH *graph,
    unsigned long long start_va, unsigned int bytes) {
  return AppleAgxGpuvaG3GraphContainsRangeAccess(
      graph, start_va, bytes, false);
}

/* Optional evidence follows the same walk and never influences acceptance. */
static bool missing_range(APPLE_AGX_GPUVA_G3_WALK_FAILURE *failure,
    unsigned long long start, unsigned long long page, unsigned int level,
    unsigned int index, unsigned int reason) {
  if (failure) {
    failure->Level = level;
    failure->Index = index;
    failure->Va = page < start ? start : page;
    failure->ComponentReason = reason;
    failure->Reason = page > (start & ~(G3_PAGE - 1u)) ?
        AppleAgxG3WalkTailShort : reason;
  }
  return false;
}

bool AppleAgxGpuvaG3GraphInspectRangeAccess(
    APPLE_AGX_GPUVA_G3_GRAPH *graph, unsigned long long start_va,
    unsigned int bytes, bool write, APPLE_AGX_GPUVA_G3_WALK_FAILURE *failure) {
  unsigned long long va, end;
  APPLE_AGX_GPUVA_G3_NODE *root_edge, *middle_edge, *leaf;
  if (failure) {
    memset(failure, 0, sizeof(*failure));
    failure->Level = failure->Index = ~0u;
  }
  if (!bytes || start_va >= (1ULL << 39) ||
      bytes > (1ULL << 39) - start_va || !graph || !graph->Created ||
      graph->Uncertain) return false;
  end = start_va + bytes;
  for (va = start_va & ~(G3_PAGE - 1u); va < end; va += G3_PAGE) {
    unsigned int ri = (unsigned int)((va >> 36) & 7u);
    unsigned int mi = (unsigned int)((va >> 25) & 2047u);
    unsigned int li = (unsigned int)((va >> 14) & 2047u);
    root_edge = slot(graph->RootTable, ri);
    if (!root_edge) return missing_range(failure, start_va, va, 0u, ri,
                                         AppleAgxG3WalkNoRoot);
    middle_edge = slot(root_edge->ChildTable, mi);
    if (!middle_edge) return missing_range(failure, start_va, va, 1u, mi,
                                           AppleAgxG3WalkNoTable);
    leaf = slot(middle_edge->ChildTable, li);
    if (!leaf) return missing_range(failure, start_va, va, 2u, li,
                                    AppleAgxG3WalkLeafAbsent);
    if (!leaf->AuxIpa || (write && !leaf->Writable))
      return missing_range(failure, start_va, va, 2u, li,
                           AppleAgxG3WalkLeafNotPublished);
  }
  return true;
}

bool AppleAgxGpuvaG3GraphContainsRangeAccess(
    APPLE_AGX_GPUVA_G3_GRAPH *graph, unsigned long long start_va,
    unsigned int bytes, bool write) {
  return AppleAgxGpuvaG3GraphInspectRangeAccess(graph, start_va, bytes, write, NULL);
}

/* A private leaf is grafted into the actual current shadow root. VidMm may
 * supply the middle table; only the reserved leaf slot belongs to the KMD. */
bool AppleAgxGpuvaG3GraphAttachPrivate(APPLE_AGX_GPUVA_G3_GRAPH *g,
    unsigned long long va, unsigned long long middle, unsigned long long leaf) {
  APPLE_AGX_GPUVA_G3_NODE *root_edge, *leaf_edge;
  unsigned ri=(unsigned)(va>>36), mi=(unsigned)((va>>25)&2047u);
  bool new_leaf;
  if (!g) return false;
  g->AttachFailure =
      !g->Created || g->Uncertain ? 1u :
      va<(1ULL<<25) || va>=(1ULL<<39) || (va&0x1ffffffULL) ? 4u :
      !find_table(g,middle,1u) ? 5u : !find_table(g,leaf,2u) ? 6u : 0u;
  if (g->AttachFailure) return false;
  root_edge=find_edge(g->Parents,g->RootIpa,ri);
  if (root_edge) middle=root_edge->AuxIpa;
  leaf_edge=find_edge(g->Parents,middle,mi);
  /* SetRootPageTable is per context and VOID: another context of this
   * process may be running.  An existing exact link needs no publication,
   * so it does not wait for process quiescence (EXP1053 DWM poison). */
  if (root_edge && leaf_edge && leaf_edge->AuxIpa==leaf) return true;
  if (g->JobInFlight || g->LeaseToken) {
    g->AttachFailure = g->JobInFlight ? 2u : 3u;
    return false;
  }
  if (leaf_edge && leaf_edge->AuxIpa!=leaf) { g->AttachFailure=7u; return false; }
  new_leaf=leaf_edge==0;
  if (!AppleAgxGpuvaG3GraphUpdateParent(g,middle,mi,leaf)) {
    g->AttachFailure=8u;
    return false;
  }
  if (!root_edge && !AppleAgxGpuvaG3GraphUpdateParent(g,g->RootIpa,ri,middle)) {
    g->AttachFailure=9u;
    if (new_leaf && !g->Uncertain &&
        !AppleAgxGpuvaG3GraphUpdateParent(g,middle,mi,0)) g->Uncertain=1u;
    return false;
  }
  return true;
}

/* Root parking must ignore only our own private branch, and must preserve
 * every ordinary mapping as a real nonempty-root conflict. */
bool AppleAgxGpuvaG3GraphCanDetachPrivateRoot(APPLE_AGX_GPUVA_G3_GRAPH *g,
    unsigned long long va, unsigned long long root, unsigned long long leaf) {
  APPLE_AGX_GPUVA_G3_NODE *edge, *private_edge;
  unsigned ri=(unsigned)(va>>36), mi=(unsigned)((va>>25)&2047u);
  if (!g || g->Uncertain || g->JobInFlight || g->LeaseToken ||
      va<(1ULL<<25) || va>=(1ULL<<39) || (va&0x1ffffffULL)) return false;
  private_edge=find_edge(g->Parents,root,ri);
  if (!private_edge) {
    for (edge=g->Parents;edge;edge=edge->Next)
      if (edge->Ipa==root) return false;
    return true;
  }
  for (edge=g->Parents;edge;edge=edge->Next)
    if ((edge->Ipa==root && edge!=private_edge) ||
        (edge->Ipa==private_edge->AuxIpa &&
         (edge->Index!=mi || edge->AuxIpa!=leaf))) return false;
  edge=find_edge(g->Parents,private_edge->AuxIpa,mi);
  if (!edge || edge->AuxIpa!=leaf) return false;
  return true;
}

bool AppleAgxGpuvaG3GraphDetachPrivateRoot(APPLE_AGX_GPUVA_G3_GRAPH *g,
    unsigned long long va, unsigned long long root, unsigned long long leaf) {
  if (!AppleAgxGpuvaG3GraphCanDetachPrivateRoot(g,va,root,leaf)) return false;
  return AppleAgxGpuvaG3GraphUpdateParent(g,root,(unsigned)(va>>36),0);
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
    release_grant(graph, item);
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
      request.TableIpa = item->Ipa;
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
  graph->RootTable = 0;
  return true;
}
