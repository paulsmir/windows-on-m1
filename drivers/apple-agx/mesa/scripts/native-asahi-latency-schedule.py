#!/usr/bin/env python3
"""RC96: latency-aware choice in Asahi's bottom-up pressure scheduler.

agx_pressure_schedule.c (pinned 9aa1215f) chooses, among the ready
instructions of a block, the one with the best liveness delta. A memory load
frees its destination, so it is chosen as soon as its first use is placed:
every load lands right before its use and agx_insert_waits stalls on it after
one or two instructions (bdb1944a, GravityMark's deferred-lighting kernel:
51 of 83 waits within two instructions of their load).  The latency then has
to be hidden by the other resident threads alone (occupancy 512 there).

agx_pressure_schedule.c: after the pinned pass, a second pass reschedules
each block over the same dependency graph (create_dag: SSA read-after-write,
loads, stores, atomics, barriers and coverage serialized as before) and
changes only which ready node is chosen: a long-latency node (device_load,
texture_sample, texture_load, image_load) that became ready fewer than
`target` placements ago is passed over while another ready node exists whose
choice keeps the live size within the shader's allocator demand.  Every
order it emits is a topological order of the pinned graph.  A block keeps
the new order only if its allocator demand (agx_calc_register_demand,
recomputed for the order) stays within the shader's.

agx_compile.c: the allocator's result, not the demand, sets occupancy (the
same demand can allocate to a different register count), so each non-preamble
function is compiled with the pinned scheduler first and then with targets
16, 8 and 4; the first variant that keeps the pinned result's thread count,
spills, fills and scratch and adds at most 1/32 more instructions is kept,
otherwise the pinned binary and info are restored. A variant whose second
pass reordered nothing is the pinned result itself.

agx_compiler.h: agx_context gains the target and the reordered-block count
(appended, so the layout of the existing fields does not change).

APPLE_AGX_LATENCY_SCHED=0 restores the pinned compiler; =N (1..64) makes N
the first target (default 16). Read once per process.

K1: APPLE_AGX_LOCAL_LOAD_SCHED=1 also spaces shared-memory loads from their
consumers. Default off pending hardware timing: local_load has no scoreboard
but still has a dependent-use latency. This changes only ready-node selection
within the existing dependency graph and retains every post-RA keep guard.
"""

ANCHOR_NODE = '''   /* Instruction this node represents */
   agx_instr *instr;
};'''
NODE = '''   /* Instruction this node represents */
   agx_instr *instr;

   /* RC96: placements done (bottom-up) when this node became ready */
   unsigned windows_ready_at;
   bool windows_ready;
};'''
ANCHOR_CHOOSE = '''static void
pressure_schedule_block(agx_context *ctx, agx_block *block, struct sched_ctx *s)
{'''
ANCHOR_MAIN = '''void
agx_pressure_schedule(agx_context *ctx)
{
   agx_compute_liveness(ctx);
   void *memctx = ralloc_context(ctx);

   agx_foreach_block(ctx, block) {
      struct sched_ctx sctx = {.dag = create_dag(ctx, block, memctx)};
      u_sparse_bitset_init(&sctx.live, ctx->alloc, memctx);

      pressure_schedule_block(ctx, block, &sctx);
   }
'''

LATENCY = r'''/* RC96 (native-asahi-latency-schedule.py): latency-aware second pass.
 * ctx->windows_latency_target: the distance, plus AGX_WIN32_LATENCY_TIGHT to
 * bound the live size by the first pass's demand itself instead of its
 * occupancy step. */
/* K1: opt in shared-load latency scheduling. No change to memory ordering,
 * barriers, or wait insertion. Exact value 1 only, read once per process. */
static bool
agx_win32_local_load_latency(void)
{
   static int enabled = -1;
   if (enabled < 0) {
      const char *env = getenv("APPLE_AGX_LOCAL_LOAD_SCHED");
      enabled = env && env[0] == '1' && !env[1];
   }
   return enabled != 0;
}

static bool
agx_win32_long_latency(const agx_instr *I)
{
   switch (I->op) {
   case AGX_OPCODE_LOCAL_LOAD:
      return agx_win32_local_load_latency();
   case AGX_OPCODE_DEVICE_LOAD:
   case AGX_OPCODE_TEXTURE_SAMPLE:
   case AGX_OPCODE_TEXTURE_LOAD:
   case AGX_OPCODE_IMAGE_LOAD:
      return true;
   default:
      return false;
   }
}

/* The register allocator's width of every SSA value: its definition's
 * 16-bit size rounded up to a power of two, 0 for memory-class values
 * (agx_register_allocate.c agx_calc_register_demand without remat). */
static uint8_t *
agx_win32_value_widths(agx_context *ctx, void *memctx)
{
   uint8_t *width = rzalloc_array(memctx, uint8_t, ctx->alloc);
   agx_foreach_instr_global(ctx, I) {
      agx_foreach_ssa_dest(I, d) {
         if (ra_class_for_index(I->dest[d]) == RA_GPR)
            width[I->dest[d].value] = util_next_power_of_two(agx_index_size_16(I->dest[d]));
      }
   }
   return width;
}

static unsigned
agx_win32_reserved_size(agx_context *ctx)
{
   return ctx->has_spill_pcopy_reserved ? 8 : ctx->any_quad_divergent_shuffle ? 2 : ctx->any_cf ? 1 : 0;
}

/* agx_calc_register_demand restricted to one block in the given order
 * (order[0] first; the instructions after the logical end follow it
 * unchanged). Kills are recomputed for the order from live_out, exactly as
 * agx_compute_liveness would. */
static unsigned
agx_win32_block_demand(agx_context *ctx, agx_block *block, agx_instr **order, unsigned n,
                       const uint8_t *width, struct u_sparse_bitset *live, void *memctx)
{
   unsigned tail = 0;
   agx_foreach_instr_in_block(block, I)
      tail += instr_after_logical_end(I);
   const unsigned total = n + tail;
   agx_instr **all = ralloc_array(memctx, agx_instr *, total);
   memcpy(all, order, n * sizeof(*all));
   unsigned t = n;
   agx_foreach_instr_in_block(block, I) {
      if (instr_after_logical_end(I))
         all[t++] = I;
   }

   /* kill bits per (position, source), walking up from live_out */
   uint32_t *kill = rzalloc_array(memctx, uint32_t, total);
   u_sparse_bitset_dup(live, &block->live_out);
   for (unsigned i = total; i-- > 0;) {
      agx_instr *I = all[i];
      agx_foreach_ssa_dest(I, d)
         u_sparse_bitset_clear(live, I->dest[d].value);
      agx_foreach_ssa_src(I, s) {
         if (s < 32 && !u_sparse_bitset_test(live, I->src[s].value))
            kill[i] |= 1u << s;
         u_sparse_bitset_set(live, I->src[s].value);
      }
   }

   signed demand = agx_win32_reserved_size(ctx);
   U_SPARSE_BITSET_FOREACH_SET(&block->live_in, v)
      demand += width[v];
   signed max_demand = demand, late = 0;
   for (unsigned i = 0; i < total; ++i) {
      agx_instr *I = all[i];
      if (I->op == AGX_OPCODE_PHI)
         continue;
      if (I->op == AGX_OPCODE_PRELOAD)
         max_demand = MAX2(max_demand, (signed)(I->src[0].value + agx_size_align_16(I->src[0].size)));
      else if (I->op == AGX_OPCODE_EXPORT)
         max_demand = MAX2(max_demand, (signed)(I->imm + agx_size_align_16(I->src[0].size)));
      demand -= late;
      late = 0;
      agx_foreach_src(I, s) {
         if (s >= 32 || !(kill[i] & (1u << s)) || I->src[s].type != AGX_INDEX_NORMAL ||
             ra_class_for_index(I->src[s]) != RA_GPR)
            continue;
         bool skip = false;
         for (unsigned b = 0; b < s; ++b)
            skip |= agx_is_equiv(I->src[b], I->src[s]);
         if (!skip)
            late += width[I->src[s].value];
      }
      agx_foreach_ssa_dest(I, d) {
         if (ra_class_for_index(I->dest[d]) != RA_GPR)
            continue;
         const signed real = agx_index_size_16(I->dest[d]);
         const signed pot = util_next_power_of_two(real);
         demand += pot;
         late += pot - real;
      }
      max_demand = MAX2(max_demand, demand);
   }
   return max_demand > 0 ? (unsigned)max_demand : 0u;
}

static unsigned
agx_win32_block_order(agx_block *block, agx_instr **order)
{
   unsigned n = 0;
   agx_foreach_instr_in_block(block, I) {
      if (instr_after_logical_end(I))
         break;
      order[n++] = I;
   }
   return n;
}

/* Bottom-up choice: the pinned heuristic, except that a long-latency node
 * that became ready fewer than `target` placements ago is passed over while
 * another ready node keeps the live size (allocator widths) within budget. */
static struct sched_node *
agx_win32_choose_latency(struct sched_ctx *s, unsigned placed, unsigned target, const uint8_t *width,
                         signed live_size, signed budget)
{
   struct sched_node *fallback = choose_instr(s);
   if (!agx_win32_long_latency(fallback->instr) || placed - fallback->windows_ready_at >= target)
      return fallback;

   struct sched_node *best = NULL;
   signed best_size = INT32_MAX;
   list_for_each_entry(struct sched_node, n, &s->dag->heads, dag.link) {
      agx_instr *I = n->instr;
      if (I->op == AGX_OPCODE_SAMPLE_MASK || I->op == AGX_OPCODE_ZS_EMIT)
         continue;
      if (agx_win32_long_latency(I) && placed - n->windows_ready_at < target)
         continue;
      /* live size above this instruction: its destinations die, its
       * sources become live */
      signed size = live_size;
      agx_foreach_ssa_dest(I, d) {
         if (u_sparse_bitset_test(&s->live, I->dest[d].value))
            size -= width[I->dest[d].value];
      }
      agx_foreach_ssa_src(I, src) {
         bool dupe = false;
         for (unsigned i = 0; i < src; ++i)
            dupe |= agx_is_equiv(I->src[i], I->src[src]);
         if (!dupe && !u_sparse_bitset_test(&s->live, I->src[src].value))
            size += width[I->src[src].value];
      }
      if (size > budget)
         continue;
      if (size < best_size) {
         best = n;
         best_size = size;
      }
   }
   return best ? best : fallback;
}

static signed
agx_win32_live_size(struct u_sparse_bitset *live, const uint8_t *width)
{
   signed size = 0;
   U_SPARSE_BITSET_FOREACH_SET(live, v)
      size += width[v];
   return size;
}

struct agx_win32_budget {
   const uint8_t *width;
   unsigned max_raw; /* live-size budget: the first pass's demand, rounded up to its occupancy step */
   struct u_sparse_bitset scratch;
};

/* Schedule one block for latency; the order is kept only if the block's
 * allocator demand stays within the budget. Whether the result is kept at all is decided by
 * agx_compile.c after register allocation (agx_win32_compile_function). */
static void
agx_win32_latency_schedule_block(agx_context *ctx, agx_block *block, void *memctx,
                                 struct agx_win32_budget *b, unsigned target)
{
   unsigned nr_ins = 0;
   agx_foreach_instr_in_block(block, I) {
      if (instr_after_logical_end(I))
         break;
      nr_ins++;
   }
   if (nr_ins < 2)
      return;

   struct sched_ctx sctx = {.dag = create_dag(ctx, block, memctx)};
   u_sparse_bitset_init(&sctx.live, ctx->alloc, memctx);
   u_sparse_bitset_dup(&sctx.live, &block->live_out);

   agx_instr **order = ralloc_array(memctx, agx_instr *, nr_ins);
   signed live_size = agx_win32_live_size(&sctx.live, b->width);
   unsigned placed = 0;
   while (!list_is_empty(&sctx.dag->heads)) {
      list_for_each_entry(struct sched_node, n, &sctx.dag->heads, dag.link) {
         if (!n->windows_ready) {
            n->windows_ready = true;
            n->windows_ready_at = placed;
         }
      }
      struct sched_node *node =
         agx_win32_choose_latency(&sctx, placed, target, b->width, live_size, (signed)b->max_raw);
      dag_prune_head(sctx.dag, &node->dag);
      agx_liveness_ins_update(&sctx.live, node->instr);
      live_size = agx_win32_live_size(&sctx.live, b->width);
      if (placed == nr_ins)
         return; /* the graph holds more nodes than the block: leave it */
      order[nr_ins - 1 - placed++] = node->instr;
   }
   if (placed != nr_ins)
      return;

   agx_instr **current = ralloc_array(memctx, agx_instr *, nr_ins);
   agx_win32_block_order(block, current);
   if (!memcmp(current, order, nr_ins * sizeof(*order)))
      return;

   if (agx_win32_block_demand(ctx, block, order, nr_ins, b->width, &b->scratch, memctx) > b->max_raw)
      return;
   ctx->windows_latency_blocks++;

   /* apply: reverse program order, each at the block's head */
   for (unsigned i = nr_ins; i-- > 0;) {
      agx_remove_instruction(order[i]);
      list_add(&order[i]->link, &block->instructions);
   }
}

static void
agx_win32_latency_schedule(agx_context *ctx, void *memctx)
{
   const unsigned target = ctx->windows_latency_target & AGX_WIN32_LATENCY_DISTANCE;
   if (!target || ctx->is_preamble)
      return;

   struct agx_win32_budget b = {.width = agx_win32_value_widths(ctx, memctx)};
   u_sparse_bitset_init(&b.scratch, ctx->alloc, memctx);
   agx_foreach_block(ctx, block) {
      unsigned n = 0;
      agx_foreach_instr_in_block(block, I)
         n += !instr_after_logical_end(I);
      agx_instr **order = ralloc_array(memctx, agx_instr *, MAX2(n, 1));
      agx_win32_block_order(block, order);
      b.max_raw = MAX2(b.max_raw, agx_win32_block_demand(ctx, block, order, n, b.width, &b.scratch, memctx));
   }
   /* registers up to the occupancy step of that demand cost no threads
    * (agx_ra rounds the demand up to it); whether the allocation really keeps
    * the occupancy is checked after agx_ra (agx_win32_compile_function) */
   if (!(ctx->windows_latency_target & AGX_WIN32_LATENCY_TIGHT))
      b.max_raw = MAX2(b.max_raw, agx_round_registers(MIN2(b.max_raw, 256u)));

   agx_foreach_block(ctx, block)
      agx_win32_latency_schedule_block(ctx, block, memctx, &b, target);
}

'''

MAIN_AFTER = '''      pressure_schedule_block(ctx, block, &sctx);
   }

   /* RC96: the kill bits of the first pass are cleared below; the second
    * pass reads live_out and the deltas only. */
   agx_win32_latency_schedule(ctx, memctx);
'''


def once(text, old, new):
    if text.count(old) != 1:
        raise RuntimeError('Ambiguous latency schedule anchor: ' + old[:80])
    return text.replace(old, new, 1)


def project_schedule(text):
    """agx_pressure_schedule.c source -> RC96 projection (the anchors must
    occur exactly once; projecting twice is an error)."""
    if 'agx_win32_latency_schedule' in text:
        raise RuntimeError('agx_pressure_schedule.c already projected')
    text = once(text, ANCHOR_NODE, NODE)
    text = once(text, ANCHOR_CHOOSE, LATENCY + ANCHOR_CHOOSE)
    text = once(text, ANCHOR_MAIN, ANCHOR_MAIN.replace(
        '      pressure_schedule_block(ctx, block, &sctx);\n   }\n', MAIN_AFTER))
    return text


CONTEXT_ANCHOR = '''   /* Promoted constants. These will be appended to the binary at the end. */
   uint16_t rodata[512];
} agx_context;'''
CONTEXT = '''   /* Promoted constants. These will be appended to the binary at the end. */
   uint16_t rodata[512];

   /* RC96 (native-asahi-latency-schedule.py): target distance of the
    * latency-aware second scheduling pass (0: pinned scheduler only) and the
    * blocks it reordered. Appended: existing field offsets are unchanged. */
   unsigned windows_latency_target;
   unsigned windows_latency_blocks;

   /* N6 (class D diagnostic, APPLE_AGX_DIAG_CUT): emit-time marks of the
    * prefix-cut points. Recorded always, read only when the cut is requested;
    * no instruction is added or changed. */
   agx_instr *windows_diag_barrier[2]; /* first top-level threadgroup barriers */
   unsigned windows_diag_barriers;
   agx_block *windows_diag_loop; /* header of the first loop, when top-level */
   bool windows_diag_loop_seen;
   unsigned windows_diag_start; /* binary size at function entry */
   unsigned windows_diag_pending; /* located cut offset from the code start; ~0u: none */
   unsigned windows_diag_offset;  /* code start in the binary */
   unsigned windows_diag_cut;     /* applied cut offset; ~0u: none */
   unsigned windows_diag_code;  /* packed code bytes */
} agx_context;

#define AGX_WIN32_LATENCY_DISTANCE 0xffu
#define AGX_WIN32_LATENCY_TIGHT    0x100u
/* RC122: allocate narrow values packed (agx_register_allocate.c) */
#define AGX_WIN32_RA_PACK          0x200u
/* RC123: reserve pending phi-web registers (agx_register_allocate.c) */
#define AGX_WIN32_RA_WEB           0x400u
/* O1: bound a compute function to one more resident workgroup */
#define AGX_WIN32_RA_GROUPS        0x800u

/* T5: first texture-state register of a promoted constant texture handle.
 * The USC TEXTURE word loads registers [0, min(descriptors, 16)) from the
 * bindful table (AGX_NUM_TEXTURE_STATE_REGS), so 16 is the first register no
 * batch loads; the preamble's tex_state_store owns every register from it. */
#define AGX_WIN32_PROMOTE_TEXTURE_BASE 16u
/* T5: image-class preamble storage in 16-bit units, two per texture state.
 * Encoding limits only: the tex_state_store register immediate has 7 bits
 * (agx_pack.c) and agx_shader_info::texture_state_count is a uint8_t. */
#define AGX_WIN32_TEXTURE_STATE_UNITS  254u'''


def project_compiler_h(text):
    if 'windows_latency_target' in text:
        raise RuntimeError('agx_compiler.h already projected')
    return once(text, CONTEXT_ANCHOR, CONTEXT)


# N6 (class D diagnostic): prefix-cut section profiler. Opt-in
# APPLE_AGX_DIAG_CUT=<hash8>:<E|B1|B2|L> ends one compute kernel at a chosen
# point so that the difference of two cut arms is the GPU time of the section
# between the points. hash8 = low 32 bits of the D3D shader hash (the split
# tag), given to the compiler by the D3D12 owner around create_compute_state
# (agx_win32_diag_cut_begin/end); "any" instead of the hash cuts every compute
# main function (offline fixture only). Points: E = first instruction of the
# main function; B<n> = first instruction after the n-th threadgroup barrier
# emitted at nesting 0; L = header block of the first loop when that loop is
# top-level. No instruction is inserted: the compile, its statistics and the
# variant selection are those of the uncut build; after agx_pack_binary the two
# bytes at the cut (an instruction boundary: the pack offset of a block) are
# overwritten with `stop`. A point that does not exist leaves the binary
# unchanged and reports offset ~0u. Absent variable: nothing happens.
DIAG_CUT = r'''#ifdef _MSC_VER
#define AGX_WIN32_DIAG_TLS __declspec(thread)
#else
#define AGX_WIN32_DIAG_TLS _Thread_local
#endif

struct agx_win32_diag_cut_entry {
   uint32_t fnv, bytes, cut, code;
};

static AGX_WIN32_DIAG_TLS uint32_t agx_win32_diag_cut_tag;
static AGX_WIN32_DIAG_TLS int agx_win32_diag_cut_tagged;
static AGX_WIN32_DIAG_TLS struct agx_win32_diag_cut_entry agx_win32_diag_cut_entries[16];
static AGX_WIN32_DIAG_TLS unsigned agx_win32_diag_cut_count;
static AGX_WIN32_DIAG_TLS int agx_win32_diag_cut_selected;

/* The requested point for the function being compiled on this thread:
 * 0 E, 1 B1, 2 B2, 3 L; -1 none. The variable is read once per process. */
static int
agx_win32_diag_cut_point(void)
{
   static int parsed = -1, point = -1, any = 0;
   static uint32_t hash = 0;
   if (parsed < 0) {
      const char *env = getenv("APPLE_AGX_DIAG_CUT");
      const char *colon = env ? strchr(env, ':') : NULL;
      int p = -1, a = 0;
      uint32_t h = 0;
      if (colon) {
         const char *name = colon + 1;
         if (!strcmp(name, "E"))
            p = 0;
         else if (!strcmp(name, "B1"))
            p = 1;
         else if (!strcmp(name, "B2"))
            p = 2;
         else if (!strcmp(name, "L"))
            p = 3;
         if (colon - env == 3 && !strncmp(env, "any", 3)) {
            a = 1;
         } else if (colon - env == 8) {
            for (unsigned i = 0; i < 8 && p >= 0; ++i) {
               const char c = env[i];
               const int digit = c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : -1;
               if (digit < 0)
                  p = -1;
               else
                  h = (h << 4) | (uint32_t)digit;
            }
         } else {
            p = -1;
         }
      }
      hash = h;
      any = a;
      point = p;
      parsed = 1;
   }
   if (point < 0)
      return -1;
   if (!any && (!agx_win32_diag_cut_tagged || agx_win32_diag_cut_tag != hash))
      return -1;
   return point;
}

static bool
agx_win32_diag_cut_applies(const agx_context *ctx)
{
   return ctx->stage == MESA_SHADER_COMPUTE && !ctx->is_preamble && agx_win32_diag_cut_point() >= 0;
}

/* The cut offset from the start of the packed code, ~0u when the point does
 * not exist. Offsets are block offsets of a trial pack into a scratch buffer;
 * a cut inside a block splits the block for that trial only. */
static unsigned
agx_win32_diag_cut_locate(agx_context *ctx)
{
   const int point = agx_win32_diag_cut_point();
   agx_block *cut = NULL, *split_from = NULL, *tail = NULL;
   if (point == 0) {
      return 0;
   } else if (point == 3) {
      agx_foreach_block(ctx, block) {
         if (block == ctx->windows_diag_loop)
            cut = block;
      }
   } else {
      const unsigned n = (unsigned)point - 1u;
      const agx_instr *mark = n < ctx->windows_diag_barriers ? ctx->windows_diag_barrier[n] : NULL;
      agx_foreach_block(ctx, block) {
         agx_foreach_instr_in_block(block, I) {
            if (mark && I == mark && !cut && !split_from) {
               if (I->link.next != &block->instructions)
                  split_from = block;
               else if (block->link.next != &ctx->blocks)
                  cut = agx_next_block(block);
            }
         }
      }
      if (split_from) {
         tail = rzalloc(ctx, agx_block);
         list_inithead(&tail->instructions);
         util_dynarray_init(&tail->predecessors, tail);
         struct list_head *item = ((agx_instr *)mark)->link.next;
         while (item != &split_from->instructions) {
            struct list_head *next = item->next;
            list_del(item);
            list_addtail(item, &tail->instructions);
            item = next;
         }
         list_add(&tail->link, &split_from->link);
         cut = tail;
      }
   }
   if (!cut)
      return ~0u;
   struct util_dynarray trial;
   util_dynarray_init(&trial, ctx);
   agx_pack_binary(ctx, &trial);
   const unsigned offset = (unsigned)cut->offset;
   util_dynarray_fini(&trial);
   if (tail) {
      struct list_head *item = tail->instructions.next;
      while (item != &tail->instructions) {
         struct list_head *next = item->next;
         list_del(item);
         list_addtail(item, &split_from->instructions);
         item = next;
      }
      list_del(&tail->link);
   }
   return offset;
}

/* After the function's own checks of the packed code (statistics, the debug
 * build's disassembly of every instruction): the bytes after a cut inside a
 * longer instruction are no longer an instruction stream. */
static void
agx_win32_diag_cut_apply(agx_context *ctx, struct util_dynarray *binary)
{
   const uint16_t stop = (uint16_t)agx_opcodes_info[AGX_OPCODE_STOP].encoding.exact;
   const unsigned cut = ctx->windows_diag_pending;
   if (cut == ~0u || cut + sizeof(stop) > ctx->windows_diag_code)
      return;
   memcpy((uint8_t *)binary->data + ctx->windows_diag_offset + cut, &stop, sizeof(stop));
   ctx->windows_diag_cut = cut;
}

static uint32_t
agx_win32_diag_cut_fnv(const uint8_t *data, unsigned bytes)
{
   uint32_t fnv = 2166136261u;
   for (unsigned i = 0; i < bytes; ++i)
      fnv = (fnv ^ data[i]) * 16777619u;
   return fnv;
}

/* One entry per compiled candidate of the function; the last one is the
 * result unless agx_win32_compile_function restores an earlier candidate. */
static void
agx_win32_diag_cut_record(agx_context *ctx, struct util_dynarray *binary)
{
   if (!agx_win32_diag_cut_applies(ctx))
      return;
   agx_win32_diag_cut_apply(ctx, binary);
   if (agx_win32_diag_cut_count == ARRAY_SIZE(agx_win32_diag_cut_entries)) {
      agx_win32_diag_cut_selected = -1;
      return;
   }
   struct agx_win32_diag_cut_entry *entry = &agx_win32_diag_cut_entries[agx_win32_diag_cut_count];
   entry->bytes = binary->size - ctx->windows_diag_start;
   entry->fnv = agx_win32_diag_cut_fnv((const uint8_t *)binary->data + ctx->windows_diag_start, entry->bytes);
   entry->cut = ctx->windows_diag_cut;
   entry->code = ctx->windows_diag_code;
   agx_win32_diag_cut_selected = (int)agx_win32_diag_cut_count++;
}

static void
agx_win32_diag_cut_select(const uint8_t *data, unsigned bytes)
{
   if (!agx_win32_diag_cut_count)
      return;
   const uint32_t fnv = agx_win32_diag_cut_fnv(data, bytes);
   agx_win32_diag_cut_selected = -1;
   for (unsigned i = agx_win32_diag_cut_count; i-- > 0;) {
      if (agx_win32_diag_cut_entries[i].bytes == bytes && agx_win32_diag_cut_entries[i].fnv == fnv) {
         agx_win32_diag_cut_selected = (int)i;
         break;
      }
   }
}

/* D3D12 owner interface (agx_d3d12_windows.cpp), around create_compute_state. */
void agx_win32_diag_cut_begin(uint32_t tag);
int agx_win32_diag_cut_end(uint32_t out[4]);

void
agx_win32_diag_cut_begin(uint32_t tag)
{
   agx_win32_diag_cut_tag = tag;
   agx_win32_diag_cut_tagged = 1;
   agx_win32_diag_cut_count = 0;
   agx_win32_diag_cut_selected = -1;
}

/* 0: no cut requested for this kernel (nothing to report). Otherwise out =
 * {point, cut offset from the code start or ~0u, code bytes, applications}:
 * applications is 1 when the kept binary carries the cut. */
int
agx_win32_diag_cut_end(uint32_t out[4])
{
   const int point = agx_win32_diag_cut_point();
   const int selected = agx_win32_diag_cut_selected;
   const unsigned count = agx_win32_diag_cut_count;
   agx_win32_diag_cut_tagged = 0;
   agx_win32_diag_cut_count = 0;
   agx_win32_diag_cut_selected = -1;
   if (point < 0)
      return 0;
   out[0] = (uint32_t)point;
   out[1] = ~0u;
   out[2] = 0;
   out[3] = 0;
   if (selected >= 0 && (unsigned)selected < count) {
      out[1] = agx_win32_diag_cut_entries[selected].cut;
      out[2] = agx_win32_diag_cut_entries[selected].code;
      out[3] = out[1] != ~0u;
   }
   return 1;
}

'''
DIAG_CUT_MARKS = (
    ('''         agx_threadgroup_barrier(b);
''', '''         agx_instr *windows_diag_barrier = agx_threadgroup_barrier(b);
         /* N6: top-level barriers are prefix-cut points */
         if (b->shader->total_nesting == 0 && !b->shader->is_preamble && b->shader->windows_diag_barriers < 2)
            b->shader->windows_diag_barrier[b->shader->windows_diag_barriers++] = windows_diag_barrier;
'''),
    ('''   loop_header->loop_header = true;
''', '''   loop_header->loop_header = true;
   /* N6: the first loop, when top-level, is a prefix-cut point */
   if (!ctx->windows_diag_loop_seen) {
      ctx->windows_diag_loop_seen = true;
      if (ctx->total_nesting == 1)
         ctx->windows_diag_loop = loop_header;
   }
'''),
    ('''   agx_pack_binary(ctx, binary);
''', '''   const unsigned windows_diag_cut = agx_win32_diag_cut_applies(ctx) ? agx_win32_diag_cut_locate(ctx) : ~0u;
   agx_pack_binary(ctx, binary);
   ctx->windows_diag_pending = windows_diag_cut;
   ctx->windows_diag_offset = offset;
   ctx->windows_diag_code = binary->size - offset;
'''),
)


FUNCTION_ANCHOR = '''static unsigned
agx_compile_function_nir(nir_shader *nir, nir_function_impl *impl,
                         struct agx_shader_key *key,
                         struct util_dynarray *binary,
                         struct agx_shader_info *out)
{'''
FUNCTION = DIAG_CUT + '''static unsigned
agx_compile_function_nir(nir_shader *nir, nir_function_impl *impl,
                         struct agx_shader_key *key,
                         struct util_dynarray *binary,
                         struct agx_shader_info *out,
                         unsigned windows_latency_target,
                         unsigned *windows_latency_blocks)
{'''
CTX_ANCHOR = '''   ctx->allocated_vec = _mesa_hash_table_u64_create(ctx);
'''
CTX = '''   ctx->allocated_vec = _mesa_hash_table_u64_create(ctx);
   ctx->windows_latency_target = windows_latency_target;
   ctx->windows_diag_start = binary->size;
   ctx->windows_diag_cut = ~0u;
   ctx->windows_diag_pending = ~0u;
'''
RETURN_ANCHOR = '''   ralloc_free(ctx);
   return offset;
}
'''
RETURN = '''   if (windows_latency_blocks)
      *windows_latency_blocks = ctx->windows_latency_blocks;
   agx_win32_diag_cut_record(ctx, binary);
   ralloc_free(ctx);
   return offset;
}

/* RC96 (native-asahi-latency-schedule.py) */
static unsigned
agx_win32_latency_first_target(void)
{
   static int target = -1;
   if (target < 0) {
      const char *env = getenv("APPLE_AGX_LATENCY_SCHED");
      int value = 16;
      if (env && env[0]) {
         value = atoi(env);
         if (value < 0)
            value = 0;
         if (value > 64)
            value = 64;
      }
      target = value;
   }
   return (unsigned)target;
}

/* RC122 (native-asahi-latency-schedule.py): APPLE_AGX_RA_PACK=0 never tries
 * the packed allocation; otherwise each function also compiles with it and
 * keeps it only when agx_win32_pack_keeps accepts it. */
static bool
agx_win32_ra_pack_enabled(void)
{
   static int enabled = -1;
   if (enabled < 0) {
      const char *env = getenv("APPLE_AGX_RA_PACK");
      enabled = !(env && env[0] == '0' && !env[1]);
   }
   return enabled != 0;
}

/* O1: APPLE_AGX_RA_GROUPS=1 tries the one-more-workgroup bound for compute
 * functions and keeps it at most 1/16 more instructions; =2 keeps it at any
 * instruction cost (measurement). Default off pending hardware. Read once. */
static unsigned
agx_win32_ra_groups_mode(void)
{
   static int mode = -1;
   if (mode < 0) {
      const char *env = getenv("APPLE_AGX_RA_GROUPS");
      mode = env && (env[0] == '1' || env[0] == '2') && !env[1] ? env[0] - '0' : 0;
   }
   return (unsigned)mode;
}

/* RC123: APPLE_AGX_RA_WEB=0 disables phi-web reservation. Read once. */
static unsigned
agx_win32_ra_web_bit(void)
{
   static int enabled = -1;
   if (enabled < 0) {
      const char *env = getenv("APPLE_AGX_RA_WEB");
      enabled = !(env && env[0] == '0' && !env[1]);
   }
   return enabled ? AGX_WIN32_RA_WEB : 0u;
}

static bool
agx_win32_latency_keeps(const struct agx_shader_info *variant, const struct agx_shader_info *pinned)
{
   const struct agx2_stats *v = &variant->stats, *p = &pinned->stats;
   return v->threads >= p->threads && v->spills <= p->spills && v->fills <= p->fills &&
          v->scratch <= p->scratch && variant->scratch_size <= pinned->scratch_size &&
          variant->preamble_scratch_size <= pinned->preamble_scratch_size &&
          v->instrs <= p->instrs + p->instrs / 32;
}

/* The pinned function first; then the latency ladder, keeping the first
 * variant that agx_win32_latency_keeps accepts (a variant whose second pass
 * reordered no block is the pinned compile itself). The
 * function's output is its binary bytes and *out, so restoring both restores
 * the pinned compile. */
/* RC122: whether the packed allocation of the same variant is kept: fewer
 * instructions, and no occupancy, spill, fill or scratch cost. */
static bool
agx_win32_pack_keeps(const struct agx_shader_info *packed, const struct agx_shader_info *chosen)
{
   const struct agx2_stats *v = &packed->stats, *c = &chosen->stats;
   return v->threads >= c->threads && v->spills <= c->spills && v->fills <= c->fills &&
          v->scratch <= c->scratch && packed->scratch_size <= chosen->scratch_size &&
          packed->preamble_scratch_size <= chosen->preamble_scratch_size &&
          v->instrs < c->instrs;
}

static unsigned
agx_win32_compile_function(nir_shader *nir, nir_function_impl *impl, struct agx_shader_key *key,
                           struct util_dynarray *binary, struct agx_shader_info *out)
{
   const unsigned first = agx_win32_latency_first_target();
   if (!first || impl->function->is_preamble)
      return agx_compile_function_nir(nir, impl, key, binary, out, 0, NULL);

   const struct agx_shader_info before = *out;
   const unsigned start = binary->size;
   unsigned offset = agx_compile_function_nir(nir, impl, key, binary, out, 0, NULL);
   struct agx_shader_info pinned = *out;
   unsigned bytes = binary->size - start;
   void *code = malloc(bytes ? bytes : 1);
   if (!code)
      return offset;
   memcpy(code, (uint8_t *)binary->data + start, bytes);

   /* RC123: the pinned schedule with phi-web reservation (same spills and
    * fills: RA only). Kept as the baseline when strictly smaller. */
   /* RC123 (EXP2074): only a function whose pinned allocation fills the
    * register file reserves; below that, phi copies are rare and the
    * reservation only widened small kernels (97ae4b02/e5ffd902 79 -> 87
    * halfregs, C4 +0.25 ms). */
   const unsigned web = pinned.stats.gprs >= 255u ? agx_win32_ra_web_bit() : 0u;
   unsigned base_target = 0;
   if (web) {
      binary->size = start;
      *out = before;
      const unsigned webbed = agx_compile_function_nir(nir, impl, key, binary, out, web, NULL);
      if (agx_win32_pack_keeps(out, &pinned)) {
         const unsigned webbed_bytes = binary->size - start;
         void *kept = realloc(code, webbed_bytes ? webbed_bytes : 1);
         if (kept) {
            code = kept;
            bytes = webbed_bytes;
            memcpy(code, (uint8_t *)binary->data + start, bytes);
            pinned = *out;
            offset = webbed;
            base_target = web;
         }
      }
   }

   /* the full distance within the occupancy step of the pinned demand, then
    * shorter distances within the pinned demand itself */
   const unsigned ladder[] = {first, MAX2(first / 2, 1u) | AGX_WIN32_LATENCY_TIGHT,
                              MAX2(first / 4, 1u) | AGX_WIN32_LATENCY_TIGHT};
   unsigned chosen_target = base_target;
   struct agx_shader_info chosen = pinned;
   bool kept_variant = false;
   for (unsigned i = 0; i < ARRAY_SIZE(ladder) && !kept_variant; ++i) {
      /* RC123: each step with phi-web reservation first; when that variant
       * is rejected (reservation can widen the footprint and cost
       * occupancy), the same step without it (EXP2073 lighting kernel) */
      for (unsigned w = 0; w < (web ? 2u : 1u) && !kept_variant; ++w) {
         binary->size = start;
         *out = before;
         unsigned blocks = 0;
         const unsigned target = ladder[i] | (w ? 0u : web);
         const unsigned variant = agx_compile_function_nir(nir, impl, key, binary, out, target, &blocks);
         if (!blocks)
            break; /* the pinned compile itself */
         if (agx_win32_latency_keeps(out, &pinned)) {
            /* keep this variant as the one to beat */
            kept_variant = true;
            chosen_target = target;
            chosen = *out;
            offset = variant;
            bytes = binary->size - start;
            void *kept = realloc(code, bytes ? bytes : 1);
            if (!kept)
               break;
            code = kept;
            memcpy(code, (uint8_t *)binary->data + start, bytes);
         }
      }
   }

   /* O1 (opt-in APPLE_AGX_RA_GROUPS=1): the chosen variant bounded to the
    * register count of one more resident workgroup (agx_ra spills to it);
    * kept only when it really runs more threads (and, at =1, costs at
    * most 1/16 more instructions). */
   const unsigned groups_mode = agx_win32_ra_groups_mode();
   if (groups_mode && nir->info.stage == MESA_SHADER_COMPUTE) {
      binary->size = start;
      *out = before;
      unsigned blocks = 0;
      const unsigned grouped = agx_compile_function_nir(nir, impl, key, binary, out,
                                                        chosen_target | AGX_WIN32_RA_GROUPS, &blocks);
      if (out->stats.threads > chosen.stats.threads &&
          (groups_mode == 2 || out->stats.instrs <= chosen.stats.instrs + chosen.stats.instrs / 16)) {
         chosen_target |= AGX_WIN32_RA_GROUPS;
         chosen = *out;
         offset = grouped;
         bytes = binary->size - start;
         void *kept = realloc(code, bytes ? bytes : 1);
         if (kept) {
            code = kept;
            memcpy(code, (uint8_t *)binary->data + start, bytes);
         } else {
            /* the copy of the chosen code is still the old one: keep both
             * consistent by restoring the pinned result below */
            free(code);
            binary->size = start;
            *out = before;
            return agx_compile_function_nir(nir, impl, key, binary, out, 0, NULL);
         }
      }
   }

   /* RC122: the same variant with packed narrow allocations */
   if (agx_win32_ra_pack_enabled()) {
      binary->size = start;
      *out = before;
      unsigned blocks = 0;
      const unsigned packed = agx_compile_function_nir(nir, impl, key, binary, out,
                                                       chosen_target | AGX_WIN32_RA_PACK, &blocks);
      if (agx_win32_pack_keeps(out, &chosen)) {
         free(code);
         return packed;
      }
   }

   binary->size = start;
   memcpy(util_dynarray_grow_bytes(binary, 1, bytes), code, bytes);
   agx_win32_diag_cut_select((const uint8_t *)binary->data + start, bytes);
   *out = chosen;
   free(code);
   return offset;
}
'''
CALL_ANCHOR = '''      unsigned offset =
         agx_compile_function_nir(nir, impl, key, &binary, &out->info);'''
CALL = '''      unsigned offset =
         agx_win32_compile_function(nir, impl, key, &binary, &out->info);'''


# K2: the pinned vectorizer already handles shared aliasing and barriers, and
# the AGX emitter supports dense local-load masks. K3 separately opts in dense
# 32-bit stores: the emitter does not handle arbitrary write masks. Independent of
# K1's scheduling experiment (EXP2080 found no timing gain from load spacing).
SHARED_VECTORIZE = r'''/* K2/K3: independent compute shared-load/store options.
 * No holes, barrier changes or shader-specific admission. Read once per process. */
static bool
agx_win32_shared_load_vectorize(void)
{
   static int enabled = -1;
   if (enabled < 0) {
      /* Default on since EXP2110/2112 (+3% GravityMark, suite 119/0);
       * exactly "0" restores scalar shared loads. */
      const char *env = getenv("APPLE_AGX_SHARED_LOAD_VECTORIZE");
      enabled = !(env && env[0] == '0' && !env[1]);
   }
   return enabled != 0;
}

static bool
agx_win32_shared_store_vectorize(void)
{
   static int enabled = -1;
   if (enabled < 0) {
      const char *env = getenv("APPLE_AGX_SHARED_STORE_VECTORIZE");
      enabled = env && env[0] == '1' && !env[1];
   }
   return enabled != 0;
}

static bool
agx_win32_mem_vectorize_cb(unsigned align_mul, unsigned align_offset,
                          unsigned bit_size, unsigned num_components,
                          int64_t hole_size, nir_intrinsic_instr *low,
                          nir_intrinsic_instr *high, void *data)
{
   if (low->intrinsic == nir_intrinsic_store_shared ||
       high->intrinsic == nir_intrinsic_store_shared) {
      /* Full masks and no gap imply a dense union, including overlaps. NIR
       * preserves the last writer and checks intervening reads/atomics and
       * barriers. Keep the emitter's dense-mask assumption valid without
       * introducing bit-size reinterpretation or sparse writes here. */
      if (!agx_win32_shared_store_vectorize() ||
          low->intrinsic != nir_intrinsic_store_shared ||
          high->intrinsic != nir_intrinsic_store_shared ||
          bit_size != 32 || nir_src_bit_size(low->src[0]) != 32 ||
          nir_src_bit_size(high->src[0]) != 32 ||
          nir_intrinsic_write_mask(low) != BITFIELD_MASK(low->num_components) ||
          nir_intrinsic_write_mask(high) != BITFIELD_MASK(high->num_components))
         return false;
   }
   if ((low->intrinsic == nir_intrinsic_load_shared ||
        high->intrinsic == nir_intrinsic_load_shared) &&
       !agx_win32_shared_load_vectorize())
      return false;
   return agx_mem_vectorize_cb(align_mul, align_offset, bit_size,
                               num_components, hole_size, low, high, data);
}

'''


# RC124 (EXP2140): GravityMark's full-screen PS 428487da needs 296 halfregs
# against 256 and spills 152 values. At the peak about 27 of ~190 live 32-bit
# values are interpolated inputs loaded at the top (nir_opt_sink places a load
# at the dominator of all uses) and used again thousands of instructions later.
# An interpolated input is a pure function of its barycentric and the
# varying coefficients, so it can be issued again where it is used: for a use
# in another block more than AGX_WIN32_REMAT_DISTANCE instructions after the
# def, clone the barycentric and the load at that block's start, or, for a use
# inside a loop that does not contain the def, at the end of the block before
# the outermost such loop (the input is loop invariant). Offline (EXP2139
# capture): 296 -> 258 halfregs demand, 152 -> 29 spills, 7335 -> 6597
# instructions; the other 240 GravityMark shaders are unchanged. Runs after
# the last CSE so the clones stay. APPLE_AGX_REMAT_INPUTS=2 also re-issues
# global loads (load_agx/load_constant_agx after agx_nir_lower_address) whose
# whole address tree is pure (constants, ALU, preamble uniforms, varying
# coefficients), only in shaders without stores, atomics or barriers: no write
# in the draw can change the value. Offline: 296 -> 246 halfregs, spills 0,
# 7335 -> 6421 instructions; the other 30 captured pixel shaders unchanged.
# Default level 2 (EXP2142: +0.9 % HD, suite 119/0); APPLE_AGX_REMAT_INPUTS=0
# restores the pre-RC124 compiler, =1 inputs only.
REMAT = r'''/* RC124: fragment interpolated-input rematerialization (opt-in). */
#define AGX_WIN32_REMAT_DISTANCE 256
#define AGX_WIN32_REMAT_BUDGET 16

/* 1: interpolated inputs; 2 (default since EXP2142): also loads of
 * read-only global memory; exactly "0" disables, "1" keeps inputs only. */
static unsigned
agx_win32_remat_level(void)
{
   static int level = -1;
   if (level < 0) {
      const char *env = getenv("APPLE_AGX_REMAT_INPUTS");
      level = env && env[0] >= '0' && env[0] <= '2' && !env[1] ? env[0] - '0' : 2;
      /* APPLE_AGX_LATENCY_SCHED=0 stays the pinned compiler */
      const char *pinned = getenv("APPLE_AGX_LATENCY_SCHED");
      if (pinned && pinned[0] == '0' && !pinned[1])
         level = 0;
   }
   return (unsigned)level;
}

/* The block a clone for a use in `at` goes to: `at` itself, or the block
 * before the outermost loop around `at` that does not contain `def_block`
 * (every rematerialized value is loop invariant, and the def dominates it). */
static nir_block *
agx_win32_remat_target(nir_block *def_block, nir_block *at)
{
   nir_cf_node *outer = NULL;
   for (nir_cf_node *node = at->cf_node.parent; node; node = node->parent) {
      if (node->type != nir_cf_node_loop)
         continue;
      bool contains_def = false;
      for (nir_cf_node *d = def_block->cf_node.parent; d; d = d->parent)
         contains_def |= d == node;
      if (contains_def)
         break;
      outer = node;
   }
   return outer ? nir_cf_node_as_block(nir_cf_node_prev(outer)) : at;
}

/* A value is pure when it and everything it is computed from can be
 * evaluated again anywhere it dominates with the same result: constants,
 * ALU, preamble uniforms, barycentrics, varying coefficients and pixel
 * interpolation, and (level 2, shaders without memory writes) global loads.
 * At most `budget` non-constant instructions. */
static bool
agx_win32_remat_pure(nir_def *def, bool loads, unsigned *budget)
{
   nir_instr *instr = nir_def_instr(def);
   if (instr->type == nir_instr_type_load_const)
      return true;
   if (!*budget)
      return false;
   --*budget;
   if (instr->type == nir_instr_type_alu) {
      nir_alu_instr *alu = nir_instr_as_alu(instr);
      for (unsigned i = 0; i < nir_op_infos[alu->op].num_inputs; ++i) {
         if (!agx_win32_remat_pure(alu->src[i].src.ssa, loads, budget))
            return false;
      }
      return true;
   }
   if (instr->type != nir_instr_type_intrinsic)
      return false;
   nir_intrinsic_instr *intr = nir_instr_as_intrinsic(instr);
   switch (intr->intrinsic) {
   case nir_intrinsic_load_preamble:
   case nir_intrinsic_load_barycentric_pixel:
   case nir_intrinsic_load_coefficients_agx:
   case nir_intrinsic_load_interpolated_input:
      break;
   case nir_intrinsic_load_global:
   case nir_intrinsic_load_global_constant:
   case nir_intrinsic_load_agx:
   case nir_intrinsic_load_constant_agx:
      if (!loads)
         return false;
      break;
   default:
      return false;
   }
   for (unsigned i = 0; i < nir_intrinsic_infos[intr->intrinsic].num_srcs; ++i) {
      if (!agx_win32_remat_pure(intr->src[i].ssa, loads, budget))
         return false;
   }
   return true;
}

struct agx_win32_remat_block {
   struct hash_table *clones; /* original def -> clone in this block */
   nir_instr *last;           /* clones go after the previous clone */
};

static nir_def *
agx_win32_remat_clone(nir_shader *nir, struct agx_win32_remat_block *state,
                      nir_cursor first, nir_def *def)
{
   struct hash_entry *entry = _mesa_hash_table_search(state->clones, def);
   if (entry)
      return entry->data;
   nir_instr *instr = nir_def_instr(def);
   nir_def *srcs[NIR_MAX_VEC_COMPONENTS * 4];
   unsigned count = 0;
   if (instr->type == nir_instr_type_alu) {
      nir_alu_instr *alu = nir_instr_as_alu(instr);
      for (unsigned i = 0; i < nir_op_infos[alu->op].num_inputs; ++i)
         srcs[count++] = agx_win32_remat_clone(nir, state, first, alu->src[i].src.ssa);
   } else if (instr->type == nir_instr_type_intrinsic) {
      nir_intrinsic_instr *intr = nir_instr_as_intrinsic(instr);
      for (unsigned i = 0; i < nir_intrinsic_infos[intr->intrinsic].num_srcs; ++i)
         srcs[count++] = agx_win32_remat_clone(nir, state, first, intr->src[i].ssa);
   }
   nir_instr *copy = nir_instr_clone(nir, instr);
   nir_instr_insert(state->last ? nir_after_instr(state->last) : first, copy);
   state->last = copy;
   if (copy->type == nir_instr_type_alu) {
      for (unsigned i = 0; i < count; ++i)
         nir_src_rewrite(&nir_instr_as_alu(copy)->src[i].src, srcs[i]);
   } else if (copy->type == nir_instr_type_intrinsic) {
      for (unsigned i = 0; i < count; ++i)
         nir_src_rewrite(&nir_instr_as_intrinsic(copy)->src[i], srcs[i]);
   }
   nir_def *result = nir_instr_def(copy);
   _mesa_hash_table_insert(state->clones, def, result);
   return result;
}

static bool
agx_win32_remat_inputs(nir_shader *nir)
{
   const unsigned level = agx_win32_remat_level();
   if (nir->info.stage != MESA_SHADER_FRAGMENT || !level)
      return false;
   bool progress = false;
   nir_foreach_function_impl(impl, nir) {
      bool read_only = level >= 2;
      nir_foreach_block(block, impl) {
         nir_foreach_instr(instr, block) {
            if (instr->type != nir_instr_type_intrinsic)
               continue;
            /* conservative: any store, atomic or barrier (agx_nir_lower_address
             * has already turned global stores into store_agx) */
            nir_intrinsic_instr *intr = nir_instr_as_intrinsic(instr);
            const char *name = nir_intrinsic_infos[intr->intrinsic].name;
            if (nir_intrinsic_writes_external_memory(intr) || !strncmp(name, "store_", 6) ||
                strstr(name, "atomic") || strstr(name, "barrier"))
               read_only = false;
         }
      }
      void *mem = ralloc_context(NULL);
      /* keyed by block, | 1 for clones at a loop predecessor's end */
      struct hash_table *blocks = _mesa_pointer_hash_table_create(mem);
      nir_index_instrs(impl);
      nir_foreach_block(block, impl) {
         nir_foreach_instr(instr, block) {
            if (instr->type != nir_instr_type_intrinsic || !instr->index)
               continue;
            nir_intrinsic_instr *root = nir_instr_as_intrinsic(instr);
            if (root->intrinsic != nir_intrinsic_load_interpolated_input &&
                root->intrinsic != nir_intrinsic_load_global &&
                root->intrinsic != nir_intrinsic_load_global_constant &&
                root->intrinsic != nir_intrinsic_load_agx &&
                root->intrinsic != nir_intrinsic_load_constant_agx)
               continue;
            unsigned budget = AGX_WIN32_REMAT_BUDGET;
            if (!agx_win32_remat_pure(&root->def, read_only, &budget))
               continue;
            nir_foreach_use_safe(src, &root->def) {
               if (nir_src_is_if(src))
                  continue;
               nir_instr *user = nir_src_use_instr(src);
               if (user->type == nir_instr_type_phi || !user->index ||
                   user->index < instr->index + AGX_WIN32_REMAT_DISTANCE)
                  continue;
               nir_block *at = agx_win32_remat_target(block, user->block);
               if (at == block)
                  continue;
               /* a use block gets its clones at its start, a loop's
                * predecessor block at its end */
               const bool start = at == user->block;
               void *key = (void *)((uintptr_t)at | (start ? 0 : 1));
               struct hash_entry *entry = _mesa_hash_table_search(blocks, key);
               struct agx_win32_remat_block *state;
               if (entry) {
                  state = entry->data;
               } else {
                  state = rzalloc(mem, struct agx_win32_remat_block);
                  state->clones = _mesa_pointer_hash_table_create(mem);
                  _mesa_hash_table_insert(blocks, key, state);
               }
               nir_def *clone = agx_win32_remat_clone(nir, state,
                  start ? nir_after_phis(at) : nir_after_block(at), &root->def);
               nir_src_rewrite(src, clone);
               progress = true;
            }
         }
      }
      ralloc_free(mem);
      nir_progress(progress, impl, nir_metadata_control_flow);
   }
   return progress;
}

'''
REMAT_PASS_ANCHOR = '''   NIR_PASS(_, nir, nir_opt_move, move_all);
   NIR_PASS(progress, nir, nir_lower_all_phis_to_scalar);'''
REMAT_PASS = '''   NIR_PASS(_, nir, nir_opt_move, move_all);
   /* RC124 (native-asahi-latency-schedule.py): after the last CSE */
   NIR_PASS(_, nir, agx_win32_remat_inputs);
   NIR_PASS(progress, nir, nir_lower_all_phis_to_scalar);'''


# T5 (r17-shader-rows, 2026-11-10): texture-state promotion of constant texture
# handles. The gallium rule keeps a texture bindful only for table index < 16
# without a descriptor crawl; every other texture is sampled bindless, and the
# pinned compiler promotes a bindless handle to a texture-state register only
# with agx_shader_key::promote_textures, which the gallium driver never sets.
# GravityMark's lighting kernel (806ee7b1bdb1944a) then samples the shadow map
# bindless 16 times inside its filter loops (29 of 50 samples), the planet PS
# (428487dae22a5191) 75 of 79; Apple's binaries load the same handles once into
# texture state in the preamble and keep only the dynamic handles bindless.
#
# In this compiler projection promote_textures means the D3D12 form (the only
# setter in the projected driver is agx_compile_nir,
# native-asahi-batch-lifecycle.py project_texture_promotion, under
# APPLE_AGX_D3D_PROMOTE_TEX=1):
#  - gate: only a shader that samples a constant handle inside a loop is
#    promoted (144a46deb6ed2427, 255 GPRs, constant handles only outside
#    loops, grew by 137 instructions and 12 spills in the ungated r17
#    prototype); every other shader takes the unpromoted path and compiles
#    byte-identical to the key bit being clear;
#  - merge: bindless_image_agx definitions with one constant offset and heap
#    become one definition at the function start, so one table entry takes
#    one register (unmerged: 26 states for the lighting kernel, merged 3);
#  - registers: the image-class cursor starts at AGX_WIN32_PROMOTE_TEXTURE_BASE
#    registers, after the bindful ones; agx_shader_info::texture_state_count
#    returns the end cursor in 16-bit units (two per register), 0 when nothing
#    was promoted; the driver emits max(bindful descriptors, end / 2);
#  - samplers are never promoted (the sampler-state registers are bindful).
# Without the key bit nothing here runs and the image-class limit is not
# reached (the cursor of an unpromoted compile is 1000 units).
PROMOTE_TEXTURES = r'''/* T5 (native-asahi-latency-schedule.py): texture-state promotion of constant
 * texture handles for agx_shader_key::promote_textures. */
static bool
agx_win32_constant_texture_handle(nir_def *def)
{
   nir_intrinsic_instr *intr = nir_def_as_intrinsic_or_null(def);

   /* a heap uniform from 32 up cannot be encoded in tex_state_store
    * (agx_nir_opt_preamble.c preamble_class) */
   return intr && intr->intrinsic == nir_intrinsic_bindless_image_agx &&
          nir_intrinsic_desc_set(intr) < 32 && nir_src_is_const(intr->src[0]);
}

static bool
agx_win32_loop_samples_constant_handle(struct exec_list *list, bool in_loop)
{
   foreach_list_typed(nir_cf_node, node, node, list) {
      switch (node->type) {
      case nir_cf_node_block:
         if (!in_loop)
            break;
         nir_foreach_instr(instr, nir_cf_node_as_block(node)) {
            if (instr->type != nir_instr_type_tex)
               continue;
            nir_tex_instr *tex = nir_instr_as_tex(instr);
            const int handle =
               nir_tex_instr_src_index(tex, nir_tex_src_texture_handle);
            if (handle >= 0 && !nir_tex_instr_is_query(tex) &&
                agx_win32_constant_texture_handle(tex->src[handle].src.ssa))
               return true;
         }
         break;
      case nir_cf_node_if: {
         nir_if *nif = nir_cf_node_as_if(node);
         if (agx_win32_loop_samples_constant_handle(&nif->then_list, in_loop) ||
             agx_win32_loop_samples_constant_handle(&nif->else_list, in_loop))
            return true;
         break;
      }
      case nir_cf_node_loop: {
         nir_loop *loop = nir_cf_node_as_loop(node);
         if (agx_win32_loop_samples_constant_handle(&loop->body, true) ||
             agx_win32_loop_samples_constant_handle(&loop->continue_list, true))
            return true;
         break;
      }
      default:
         break;
      }
   }
   return false;
}

/* One definition per (heap, constant offset), at the function start: it
 * dominates every use, and nir_opt_preamble sees one candidate per table
 * entry instead of one per sibling block. Entries past the table stay as
 * they are (still promotable, one register each). */
static bool
agx_win32_merge_constant_texture_handles(nir_shader *nir)
{
   struct agx_win32_handle {
      uint32_t offset, heap;
      nir_def *def;
   } seen[128];
   unsigned count = 0;
   bool progress = false;
   nir_function_impl *impl = nir_shader_get_entrypoint(nir);
   nir_builder b = nir_builder_at(nir_before_impl(impl));

   nir_foreach_block(block, impl) {
      nir_foreach_instr_safe(instr, block) {
         if (instr->type != nir_instr_type_intrinsic)
            continue;
         nir_intrinsic_instr *intr = nir_instr_as_intrinsic(instr);
         if (!agx_win32_constant_texture_handle(&intr->def))
            continue;

         const uint32_t offset = (uint32_t)nir_src_as_uint(intr->src[0]);
         const uint32_t heap = (uint32_t)nir_intrinsic_desc_set(intr);
         unsigned slot = 0;
         while (slot < count &&
                (seen[slot].offset != offset || seen[slot].heap != heap))
            ++slot;
         if (slot == count) {
            if (count == ARRAY_SIZE(seen))
               continue;
            /* the builder stays at the function start, before this block */
            seen[count].offset = offset;
            seen[count].heap = heap;
            seen[count].def = nir_bindless_image_agx(
               &b, nir_imm_intN_t(&b, offset, intr->src[0].ssa->bit_size),
               .desc_set = heap);
            ++count;
         }
         nir_def_replace(&intr->def, seen[slot].def);
         progress = true;
      }
   }
   return nir_progress(progress, impl, nir_metadata_control_flow);
}

/* The gate; a promoted shader gets its handles merged. */
static bool
agx_win32_promote_texture_states(nir_shader *nir)
{
   nir_function_impl *impl = nir_shader_get_entrypoint(nir);

   if (!agx_win32_loop_samples_constant_handle(&impl->body, false))
      return false;

   NIR_PASS(_, nir, agx_win32_merge_constant_texture_handles);
   return true;
}

'''
PROMOTE_SIZES_ANCHOR = '''      unsigned sizes[] = {
         *preamble_size,
'''
PROMOTE_SIZES = '''      /* T5: a shader without a constant-handle sample in a loop compiles as
       * if promote_textures were clear */
      if (ts_count && !agx_win32_promote_texture_states(nir))
         ts_count = NULL;

''' + PROMOTE_SIZES_ANCHOR
PROMOTE_CALL_ANCHOR = '''   agx_optimize_nir(
      nir, key->dev.soft_fault, key->secondary ? NULL : &info->push_count,
      (key->secondary || !key->promote_textures) ? NULL
                                                 : &info->texture_state_count,
      (key->secondary || !key->promote_textures) ? NULL
                                                 : &info->sampler_state_count);
'''
PROMOTE_CALL = '''   /* T5: promoted texture states start after the bindful registers; the
    * count returns the end of the promoted range in 16-bit units and 0 when
    * nothing was promoted. Samplers stay bindful. */
   const bool windows_promote = !key->secondary && key->promote_textures;
   const uint8_t windows_promote_base = (uint8_t)(2u * AGX_WIN32_PROMOTE_TEXTURE_BASE);
   if (windows_promote)
      info->texture_state_count = windows_promote_base;
   agx_optimize_nir(
      nir, key->dev.soft_fault, key->secondary ? NULL : &info->push_count,
      windows_promote ? &info->texture_state_count : NULL, NULL);
   if (windows_promote && info->texture_state_count == windows_promote_base)
      info->texture_state_count = 0;
'''


def project_compile(text):
    if 'agx_win32_compile_function' in text:
        raise RuntimeError('agx_compile.c already projected')
    text = once(text, FUNCTION_ANCHOR, FUNCTION)
    text = once(text, CTX_ANCHOR, CTX)
    text = once(text, RETURN_ANCHOR, RETURN)
    text = once(text, CALL_ANCHOR, CALL)
    for anchor, new in DIAG_CUT_MARKS:
        text = once(text, anchor, new)
    text = once(text, 'static void\nagx_optimize_nir(',
                SHARED_VECTORIZE + REMAT + PROMOTE_TEXTURES + 'static void\nagx_optimize_nir(')
    text = once(text, REMAT_PASS_ANCHOR, REMAT_PASS)
    text = once(text, PROMOTE_SIZES_ANCHOR, PROMOTE_SIZES)
    text = once(text, PROMOTE_CALL_ANCHOR, PROMOTE_CALL)
    text = once(text, '''               .modes = nir_var_mem_global | nir_var_mem_constant |
                        nir_var_shader_temp,
               .callback = agx_mem_vectorize_cb,''',
                '''               .modes = nir_var_mem_global | nir_var_mem_constant |
                        nir_var_shader_temp |
                        ((nir->info.stage == MESA_SHADER_COMPUTE &&
                          (agx_win32_shared_load_vectorize() ||
                           agx_win32_shared_store_vectorize())) ? nir_var_mem_shared : 0),
               .callback = agx_win32_mem_vectorize_cb,''')
    return text


# rc122 (EXP2067/2069): in GravityMark's full-screen PS (255 halfregs) the
# allocator spends 1914 xor (swap triples) and ~575 mov on live-range splits.
# Most runs sit right before a vector destination (device_load xyzw,
# texture_sample, iterproj): find_regs_simple is first-fit from register 0, so
# 2-halfreg scalars land in fully free aligned blocks, and a 4- or 8-halfreg
# vector then finds no aligned hole and evicts (assign_regs_by_copying).
# Apple's largest fragment shader: 220 mov/mov_imm, no swaps. For a
# destination narrower than 8 halfregs, first take a free aligned slot inside
# an 8-halfreg block that is already partly used, keeping whole blocks for
# vectors; otherwise first fit as before. Each function is compiled with and
# without packing (agx_win32_compile_function) and the packed binary is kept
# only with fewer instructions at no occupancy, spill or scratch cost.
# APPLE_AGX_RA_PACK=0 never tries it; APPLE_AGX_LATENCY_SCHED=0 is the pinned
# compiler.
RA_ANCHOR = '''static bool
find_regs_simple(struct ra_ctx *rctx, enum ra_class cls, unsigned count,
                 unsigned align, unsigned *out)
{
   for (unsigned reg = 0; reg + count <= rctx->bound[cls]; reg += align) {'''
RA = '''#include <stdlib.h>

static bool
find_regs_simple(struct ra_ctx *rctx, enum ra_class cls, unsigned count,
                 unsigned align, unsigned *out)
{
   /* RC122 (native-asahi-latency-schedule.py): pack narrow values into
    * partly used 8-halfreg blocks so aligned vector holes survive. */
   if ((rctx->shader->windows_latency_target & AGX_WIN32_RA_PACK) && cls == RA_GPR && count < 8) {
      const unsigned size = 8;
      for (unsigned block = 0; block + size <= rctx->bound[cls]; block += size) {
         if (!BITSET_TEST_COUNT(rctx->used_regs[cls], block, size))
            continue; /* a whole free block: keep it for a vector */
         for (unsigned reg = block; reg + count <= block + size; reg += align) {
            if (!BITSET_TEST_COUNT(rctx->used_regs[cls], reg, count)) {
               *out = reg;
               return true;
            }
         }
      }
   }
   for (unsigned reg = 0; reg + count <= rctx->bound[cls]; reg += align) {'''


# RC123: phi-web reservation. A phi web (a phi and its sources) takes the
# register of its first allocated member; a later member defined in another
# branch gets that register only if it is still free there. Under full
# pressure, first-fit has usually handed it to an unrelated temporary, so the
# member lands elsewhere and the phi becomes a parallel copy at the block end,
# where crossed assignments become swaps (3 xor each). EXP2072 + the rc123
# fixture: the full-screen PS's post-RA growth (7066 -> 9991 instructions) is
# 1323 phi-copy instructions, 806 swaps. Each block now first keeps ordinary
# values off the registers of assigned webs whose members are not yet
# defined, within the registers already in use (no footprint growth), falling
# back to plain first fit. Only non-pinned variants (AGX_WIN32_RA_WEB, set on
# the latency ladder) reserve, so the keep rule still guards occupancy and
# APPLE_AGX_LATENCY_SCHED=0 stays the pinned compiler. APPLE_AGX_RA_WEB=0
# disables it.
RA_WEB_FIELD_ANCHOR = '''   struct phi_web_node *phi_web;
'''
RA_WEB_FIELD = RA_WEB_FIELD_ANCHOR + '''   /* RC123: registers of assigned phi webs with undefined members */
   BITSET_WORD *win32_web_reserved;
'''
RA_WEB_BLOCK_ANCHOR = '''   reserve_live_in(rctx);
'''
RA_WEB_BLOCK = RA_WEB_BLOCK_ANCHOR + '''
   /* RC123 (native-asahi-latency-schedule.py): phi-web reservation */
   BITSET_DECLARE(win32_web_reserved, AGX_NUM_REGS) = {0};
   rctx->win32_web_reserved = NULL;
   if (rctx->shader->windows_latency_target & AGX_WIN32_RA_WEB) {
      agx_foreach_block(rctx->shader, web_block) {
         agx_foreach_phi_in_block(web_block, phi) {
            agx_foreach_ssa_src(phi, s) {
               const unsigned v = phi->src[s].value;
               if (phi->src[s].memory || BITSET_TEST(rctx->visited, v) || rctx->classes[v] != RA_GPR)
                  continue;
               const struct phi_web_node *node = &rctx->phi_web[phi_web_find(rctx->phi_web, v)];
               if (node->assigned && node->reg + rctx->ncomps[v] <= AGX_NUM_REGS)
                  BITSET_SET_COUNT(win32_web_reserved, node->reg, rctx->ncomps[v]);
            }
         }
      }
      rctx->win32_web_reserved = win32_web_reserved;
   }
'''
RA_WEB_FIND_ANCHOR = '''   if ((rctx->shader->windows_latency_target & AGX_WIN32_RA_PACK) && cls == RA_GPR && count < 8) {'''
RA_WEB_FIND = '''   /* RC123: keep registers promised to pending phi webs free if possible,
    * within the registers already used (no footprint, so no occupancy cost) */
   if (rctx->win32_web_reserved && cls == RA_GPR) {
      const unsigned limit = MIN2(rctx->bound[cls], *rctx->count[cls]);
      for (unsigned reg = 0; reg + count <= limit; reg += align) {
         if (!BITSET_TEST_COUNT(rctx->used_regs[cls], reg, count) &&
             !BITSET_TEST_COUNT(rctx->win32_web_reserved, reg, count)) {
            *out = reg;
            return true;
         }
      }
   }
''' + RA_WEB_FIND_ANCHOR


# O1 (C4 analysis 2026-11-05): a compute function's register bound is the
# occupancy of one workgroup at most; the allocator never targets the tier
# that holds one more whole workgroup. GravityMark's a826 family (256-thread
# groups, 111-127 halfregs, 1.48 ms/frame) sits in tiers 112/128 (896/832
# threads), i.e. 3 resident groups, where 104 halfregs (1024 threads) hold 4.
# With AGX_WIN32_RA_GROUPS the bound becomes the largest register count that
# keeps one more whole group resident, and agx_spill spills down to it.
RA_GROUPS_ANCHOR = '''   bool spilling = (effective_demand > max_possible_regs);
'''
RA_GROUPS = RA_GROUPS_ANCHOR + '''   /* O1 (native-asahi-latency-schedule.py): one more resident workgroup */
   if ((ctx->windows_latency_target & AGX_WIN32_RA_GROUPS) && !spilling &&
       !force_spilling && mesa_shader_stage_is_compute(ctx->stage) &&
       !ctx->nir->info.workgroup_size_variable && ctx->key->has_scratch &&
       !ctx->key->is_helper) {
      const unsigned w = ctx->nir->info.workgroup_size[0] *
                         ctx->nir->info.workgroup_size[1] *
                         ctx->nir->info.workgroup_size[2];
      const unsigned groups = w ? agx_occupancy_for_register_count(effective_demand).max_threads / w : 0;
      if (w >= 32 && groups && (groups + 1) * w <= 1024) {
         const unsigned bound = agx_max_registers_for_occupancy((groups + 1) * w);
         if (bound < effective_demand && bound >= 64) {
            max_possible_regs = bound;
            spilling = true;
         }
      }
   }
'''


def project_ra(text):
    if 'RC122' in text:
        raise RuntimeError('agx_register_allocate.c already projected')
    text = once(text, RA_GROUPS_ANCHOR, RA_GROUPS)
    text = once(text, RA_ANCHOR, RA)
    text = once(text, RA_WEB_FIELD_ANCHOR, RA_WEB_FIELD)
    text = once(text, RA_WEB_BLOCK_ANCHOR, RA_WEB_BLOCK)
    text = once(text, RA_WEB_FIND_ANCHOR, RA_WEB_FIND)
    return text


WAITS_ANCHOR = r'''void
agx_insert_waits(agx_context *ctx)
{
   agx_foreach_block(ctx, block) {
      if (agx_compiler_debug & AGX_DBG_WAIT)
         agx_insert_waits_trivial(ctx, block);
      else
         agx_insert_waits_local(ctx, block);
   }
}'''

# W1 (G1/EXP2122): agx_insert_waits_local drains every scoreboard slot at the
# end of each non-exit block ("This should be optimized"), so no load stays in
# flight across a branch or loop edge. Opt-in APPLE_AGX_WAIT_GLOBAL=1 carries
# the slot state (pending registers and message count) across blocks: the
# entry state of a block is the union over its predecessors, iterated to a
# fixed point; waits are inserted only before a read or overwrite of a
# pending register, before a barrier, on slot overflow, as in the local pass.
# No fixed point within the bound -> the local pass for the whole shader.
WAITS = r'''/* W1: opt-in cross-block scoreboard state (APPLE_AGX_WAIT_GLOBAL=1). */
static bool
agx_win32_wait_global(void)
{
   static int enabled = -1;
   if (enabled < 0) {
      const char *env = getenv("APPLE_AGX_WAIT_GLOBAL");
      enabled = env && env[0] == '1' && !env[1];
   }
   return enabled != 0;
}

static void
agx_win32_wait_merge(struct slot *into, const struct slot *from)
{
   for (unsigned slot = 0; slot < 2; ++slot) {
      BITSET_OR(into[slot].writes, into[slot].writes, from[slot].writes);
      if (from[slot].nr_pending > into[slot].nr_pending)
         into[slot].nr_pending = from[slot].nr_pending;
   }
}

/* The local pass's per-instruction rules from a given entry state; inserts
 * waits only when insert is set, never drains at the block end. */
static void
agx_win32_wait_block(agx_context *ctx, agx_block *block, struct slot *slots,
                     bool insert)
{
   agx_foreach_instr_in_block_safe(block, I) {
      uint8_t wait_mask = 0;

      agx_foreach_src(I, s) {
         if (I->src[s].type != AGX_INDEX_REGISTER)
            continue;
         unsigned nr_read = agx_index_size_16(I->src[s]);
         for (unsigned slot = 0; slot < 2; ++slot) {
            if (BITSET_TEST_COUNT(slots[slot].writes, I->src[s].value, nr_read))
               wait_mask |= BITSET_BIT(slot);
         }
      }

      agx_foreach_dest(I, d) {
         if (I->dest[d].type != AGX_INDEX_REGISTER)
            continue;
         unsigned nr_writes = agx_index_size_16(I->dest[d]);
         for (unsigned slot = 0; slot < 2; ++slot) {
            if (BITSET_TEST_COUNT(slots[slot].writes, I->dest[d].value,
                                  nr_writes))
               wait_mask |= BITSET_BIT(slot);
         }
      }

      if (I->op == AGX_OPCODE_THREADGROUP_BARRIER ||
          I->op == AGX_OPCODE_MEMORY_BARRIER) {
         for (unsigned slot = 0; slot < 2; ++slot) {
            if (slots[slot].nr_pending)
               wait_mask |= BITSET_BIT(slot);
         }
      }

      if (instr_is_async(I)) {
         for (unsigned slot = 0; slot < 2; ++slot) {
            if (slots[slot].nr_pending == 0 ||
                agx_win32_wait_single_slot()) {
               I->scoreboard = slot;
               break;
            }
         }
      }

      if (instr_is_async(I) &&
          slots[I->scoreboard].nr_pending >= AGX_MAX_PENDING)
         wait_mask |= BITSET_BIT(I->scoreboard);

      u_foreach_bit(slot, wait_mask) {
         if (insert) {
            agx_builder b = agx_init_builder(ctx, agx_before_instr(I));
            agx_wait(&b, slot);
         }
         BITSET_ZERO(slots[slot].writes);
         slots[slot].nr_pending = 0;
      }

      if (instr_is_async(I)) {
         agx_foreach_dest(I, d) {
            if (agx_is_null(I->dest[d]))
               continue;
            assert(I->dest[d].type == AGX_INDEX_REGISTER);
            BITSET_SET_COUNT(slots[I->scoreboard].writes, I->dest[d].value,
                             agx_index_size_16(I->dest[d]));
         }
         slots[I->scoreboard].nr_pending++;
      }
   }
}

static bool
agx_win32_insert_waits_global(agx_context *ctx)
{
   unsigned n = ctx->num_blocks;
   struct slot (*in)[2] = calloc(n ? n : 1, sizeof(*in));
   struct slot (*out)[2] = calloc(n ? n : 1, sizeof(*out));
   bool changed = true;
   if (!in || !out) {
      free(in);
      free(out);
      return false;
   }

   for (unsigned iter = 0; changed && iter < 64; ++iter) {
      agx_block *layout_prev = NULL;
      changed = false;
      agx_foreach_block(ctx, block) {
         struct slot state[2];
         memset(state, 0, sizeof(state));
         agx_foreach_predecessor(block, pred)
            agx_win32_wait_merge(state, out[(*pred)->index]);
         /* EXP2123 (C19A-D): AGX control flow is execution-masked, so the
          * block laid out before this one also runs into it (then -> else
          * with no CFG edge); its pending writes are live here too. */
         if (layout_prev)
            agx_win32_wait_merge(state, out[layout_prev->index]);
         layout_prev = block;
         memcpy(in[block->index], state, sizeof(state));
         agx_win32_wait_block(ctx, block, state, false);
         if (memcmp(state, out[block->index], sizeof(state))) {
            memcpy(out[block->index], state, sizeof(state));
            changed = true;
         }
      }
   }

   if (!changed) {
      agx_foreach_block(ctx, block) {
         struct slot state[2];
         memcpy(state, in[block->index], sizeof(state));
         agx_win32_wait_block(ctx, block, state, true);
      }
   }
   free(in);
   free(out);
   return !changed;
}

void
agx_insert_waits(agx_context *ctx)
{
   if (!(agx_compiler_debug & AGX_DBG_WAIT) && agx_win32_wait_global() &&
       agx_win32_insert_waits_global(ctx))
      return;

   agx_foreach_block(ctx, block) {
      if (agx_compiler_debug & AGX_DBG_WAIT)
         agx_insert_waits_trivial(ctx, block);
      else
         agx_insert_waits_local(ctx, block);
   }
}'''


# SS1 (R10, night 2026-11-10): the upstream slot choice is "first slot with
# nr_pending == 0", so two loads issued back to back land in slots 0 and 1 and
# their consumers pay two waits. Apple's compiler issues several loads on
# slot 0 and one wait (metal-oracle m208: four device loads, one wait).
# Opt-in APPLE_AGX_WAIT_SINGLE_SLOT=1 queues every asynchronous op on slot 0;
# every hazard rule (read or overwrite of a pending register, barriers, the
# AGX_MAX_PENDING overflow wait) is unchanged. Variable absent or any other
# value: the slot choice is the upstream one.
SINGLE_SLOT_ANCHOR = '#define AGX_MAX_PENDING (8)\n'
SINGLE_SLOT = r'''#define AGX_MAX_PENDING (8)

/* SS1: opt-in single scoreboard slot (APPLE_AGX_WAIT_SINGLE_SLOT=1). */
static bool
agx_win32_wait_single_slot(void)
{
   static int enabled = -1;
   if (enabled < 0) {
      const char *env = getenv("APPLE_AGX_WAIT_SINGLE_SLOT");
      enabled = env && env[0] == '1' && !env[1];
   }
   return enabled != 0;
}
'''
LOCAL_SLOT_ANCHOR = '            if (slots[slot].nr_pending == 0) {\n'
LOCAL_SLOT = ('            if (slots[slot].nr_pending == 0 ||\n'
              '                agx_win32_wait_single_slot()) {\n')


def project_waits(text):
    if '#include <stdlib.h>' not in text:
        text = text.replace('#include "agx_builder.h"', '#include <stdlib.h>\n#include <string.h>\n#include "agx_builder.h"', 1)
    text = once(text, LOCAL_SLOT_ANCHOR, LOCAL_SLOT)
    text = once(text, SINGLE_SLOT_ANCHOR, SINGLE_SLOT)
    return once(text, WAITS_ANCHOR, WAITS)


PREAMBLE_ANCHOR = '''   /* We have at least 32 texture state registers. TODO: check for more? */
   .preamble_storage_size[nir_preamble_class_image] = 32,
'''
PREAMBLE = '''   /* T5 (native-asahi-latency-schedule.py): the pinned limit of 32 units is
    * the 16 bindful registers; promoted handles start after them
    * (AGX_WIN32_PROMOTE_TEXTURE_BASE), up to the encodable register range. */
   .preamble_storage_size[nir_preamble_class_image] = AGX_WIN32_TEXTURE_STATE_UNITS,
'''


def project_preamble(text):
    """agx_nir_opt_preamble.c: T5 image-class storage (an unpromoted compile
    passes a cursor of 1000 units and is past either limit)."""
    return once(text, PREAMBLE_ANCHOR, PREAMBLE)


FILES = {
    'src/asahi/compiler/agx_compiler.h': project_compiler_h,
    'src/asahi/compiler/agx_pressure_schedule.c': project_schedule,
    'src/asahi/compiler/agx_compile.c': project_compile,
    'src/asahi/compiler/agx_register_allocate.c': project_ra,
    'src/asahi/compiler/agx_insert_waits.c': project_waits,
    'src/asahi/compiler/agx_nir_opt_preamble.c': project_preamble,
}


def project_bytes(function, data):
    """Apply a projection to file bytes, keeping CRLF line endings (the
    builder's compiler evidence may carry them)."""
    text = data.decode()
    crlf = '\r\n' in text
    if crlf:
        text = text.replace('\r\n', '\n')
    text = function(text)
    if crlf:
        text = text.replace('\n', '\r\n')
    return text.encode()


def project_directory(directory):
    """Project every FILES entry (agx_compiler.h and the compiler units) of
    one directory in place (build-asahi-runtime-closure.py: the
    compiler-evidence directory, where every compiler unit is compiled beside
    the one agx_compiler.h). Returns {name: (before_sha256, after_sha256)}."""
    import hashlib
    hashes = {}
    for relative, function in FILES.items():
        target = directory / relative.rsplit('/', 1)[1]
        before = target.read_bytes()
        after = project_bytes(function, before)
        target.write_bytes(after)
        hashes[target.name] = (hashlib.sha256(before).hexdigest(), hashlib.sha256(after).hexdigest())
    return hashes
