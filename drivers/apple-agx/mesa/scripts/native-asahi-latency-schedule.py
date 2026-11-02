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
static bool
agx_win32_long_latency(const agx_instr *I)
{
   switch (I->op) {
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
} agx_context;

#define AGX_WIN32_LATENCY_DISTANCE 0xffu
#define AGX_WIN32_LATENCY_TIGHT    0x100u
/* RC122: allocate narrow values packed (agx_register_allocate.c) */
#define AGX_WIN32_RA_PACK          0x200u
/* RC123: reserve pending phi-web registers (agx_register_allocate.c) */
#define AGX_WIN32_RA_WEB           0x400u'''


def project_compiler_h(text):
    if 'windows_latency_target' in text:
        raise RuntimeError('agx_compiler.h already projected')
    return once(text, CONTEXT_ANCHOR, CONTEXT)


FUNCTION_ANCHOR = '''static unsigned
agx_compile_function_nir(nir_shader *nir, nir_function_impl *impl,
                         struct agx_shader_key *key,
                         struct util_dynarray *binary,
                         struct agx_shader_info *out)
{'''
FUNCTION = '''static unsigned
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
'''
RETURN_ANCHOR = '''   ralloc_free(ctx);
   return offset;
}
'''
RETURN = '''   if (windows_latency_blocks)
      *windows_latency_blocks = ctx->windows_latency_blocks;
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
   *out = chosen;
   free(code);
   return offset;
}
'''
CALL_ANCHOR = '''      unsigned offset =
         agx_compile_function_nir(nir, impl, key, &binary, &out->info);'''
CALL = '''      unsigned offset =
         agx_win32_compile_function(nir, impl, key, &binary, &out->info);'''


def project_compile(text):
    if 'agx_win32_compile_function' in text:
        raise RuntimeError('agx_compile.c already projected')
    text = once(text, FUNCTION_ANCHOR, FUNCTION)
    text = once(text, CTX_ANCHOR, CTX)
    text = once(text, RETURN_ANCHOR, RETURN)
    text = once(text, CALL_ANCHOR, CALL)
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


def project_ra(text):
    if 'RC122' in text:
        raise RuntimeError('agx_register_allocate.c already projected')
    text = once(text, RA_ANCHOR, RA)
    text = once(text, RA_WEB_FIELD_ANCHOR, RA_WEB_FIELD)
    text = once(text, RA_WEB_BLOCK_ANCHOR, RA_WEB_BLOCK)
    text = once(text, RA_WEB_FIND_ANCHOR, RA_WEB_FIND)
    return text


FILES = {
    'src/asahi/compiler/agx_compiler.h': project_compiler_h,
    'src/asahi/compiler/agx_pressure_schedule.c': project_schedule,
    'src/asahi/compiler/agx_compile.c': project_compile,
    'src/asahi/compiler/agx_register_allocate.c': project_ra,
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
    """Project agx_compiler.h, agx_pressure_schedule.c and agx_compile.c of
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
