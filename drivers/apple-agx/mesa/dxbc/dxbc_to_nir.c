/*
 * DXBC (shader model 4.x) -> NIR.  See dxbc_to_nir.h and
 * investigation/analysis/D3D-H3-dxbc-nir.md for the contract.
 *
 * SPDX-License-Identifier: MIT
 */
#include "dxbc_to_nir.h"
#include "dxbc_tir.h"
#include "dxbc_tir_gs.h"
#include "dxbc_minmax.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "nir.h"
#include "nir_builder.h"
#include "nir_xfb_info.h"

#define MAX_TEMPS 4096
#define MAX_REGS 32          /* input/output registers per stage */
#define MAX_ITEMPS 64
#define MAX_RESOURCES 128
#define MAX_SAMPLERS 16
#define MAX_CF 64
/* UAV entries: the slots (c->uav_slots of them: 8, or 64 in D3D11.1 mode),
 * then the C47 heap ranges from MAX_UAVS */
#define MAX_UAVS DXBC_NIR_UAV11_SLOTS
#define UAV11_SHADER_BUFFERS 32u /* Gallium PIPE_MAX_SHADER_BUFFERS */
#define UAV11_IMAGES 32u         /* Asahi's 32-bit image_mask (EXP1887) */
#define MAX_TGSM 32
#define MAX_PHASES 64

/* hull shader phases */
enum { HS_PHASE_NONE, HS_PHASE_DECLS, HS_PHASE_CP, HS_PHASE_FORK, HS_PHASE_JOIN };

enum mem_kind { MEM_NONE, MEM_TYPED, MEM_RAW, MEM_STRUCTURED };

/* a tess factor: an outer (gl_TessLevelOuter) or inner level and its index */
#define TESS_FACTOR_INNER 0x80u

struct tex_shape {
   enum glsl_sampler_dim dim;
   bool array;
   unsigned coords;     /* including the array index */
};

enum cf_kind { CF_IF, CF_LOOP, CF_SWITCH };

struct cf_entry {
   enum cf_kind kind;
   nir_if *nif;
   nir_loop *loop;
   bool in_else;
   /* switch */
   nir_def *selector;
   nir_variable *fallthrough;   /* bool: an earlier case body ran on */
   nir_variable *continue_outer;/* bool: 'continue' of the enclosing loop */
   nir_def *any_case;           /* selector equals some case value */
   bool case_open;              /* an if for the current case group is open */
   nir_if *case_if;
};

/* One destination of an output register: the components 'mask' of the
 * register feed variable 'var' starting at its component 'first'. */
struct output_sink {
   nir_variable *var;
   uint8_t mask;
   uint8_t first;
   bool scalar;   /* store one component as a scalar variable */
};

struct ctx {
   nir_builder b;
   const struct dxbc_program *p;
   struct dxbc_nir_error *error;
   bool failed;
   mesa_shader_stage stage;

   nir_def *temps[MAX_TEMPS];
   unsigned num_temps;
   nir_variable *itemps[MAX_ITEMPS];
   unsigned itemp_count[MAX_ITEMPS];
   nir_variable *icb;
   unsigned icb_count;

   /* inputs: a variable per register, or system values per component */
   nir_variable *inputs[MAX_REGS];
   nir_def *input_sysval[MAX_REGS][4];
   uint8_t input_sysval_mask[MAX_REGS];
   unsigned gs_vertices_in;

   /* outputs: a register each, stored to its sinks */
   nir_def *outputs[MAX_REGS];
   struct output_sink sinks[MAX_REGS][4];
   unsigned num_sinks[MAX_REGS];
   nir_def *depth_reg, *mask_reg;
   nir_variable *depth_var, *mask_var;
   unsigned clip_slots, cull_slots;

   struct {
      bool declared;
      uint8_t dim;
      nir_alu_type type;
   } res[MAX_RESOURCES + DXBC_HEAP_RANGE_MAX]; /* C47: heap ranges follow the slots */
   struct {
      bool declared;
      bool comparison;
   } samp[MAX_SAMPLERS + DXBC_HEAP_RANGE_MAX]; /* C47: heap sampler ranges follow the slots */
   /* C34: the register part of an SM5.1 resource/sampler range index, added
    * to the texture instruction being built (texture_/sampler_offset) */
   nir_def *dyn_texture, *dyn_sampler;
   /* C47: the heap range and the register offset in it of the texture
    * instruction being built (a bindless handle in tex_finish) */
   nir_def *heap_offset;
   unsigned heap_range;
   /* C47: likewise for a heap sampler range */
   nir_def *sampler_heap_offset;
   unsigned sampler_heap_range;
   /* C53T (Tier 2 feedback, the _s opcodes): the texture instruction being
    * built returns its residency code (is_sparse) into feedback_code; a
    * _cl opcode's clamp is its min_lod */
   bool feedback;
   nir_def *feedback_code, *min_lod;

   /* shader model 5 memory: u# views, g# groupshared, raw/structured t# */
   struct {
      uint8_t kind;          /* enum mem_kind */
      uint32_t stride;
      nir_variable *image;   /* MEM_TYPED */
      uint8_t dim;
      nir_alu_type type;
   } uav[MAX_UAVS + DXBC_HEAP_RANGE_MAX]; /* C47: heap UAVs follow the slots */
   struct {
      nir_variable *var;     /* uint array */
      uint32_t stride;       /* 0: raw */
   } tgsm[MAX_TGSM];
   uint8_t res_kind[MAX_RESOURCES + DXBC_HEAP_RANGE_MAX];
   uint32_t res_stride[MAX_RESOURCES + DXBC_HEAP_RANGE_MAX];
   unsigned gs_stream;    /* the stream later output declarations belong to */

   /* tessellation: patch constants vpc# (inputs of the domain shader), each
    * component a patch varying (PATCH0 + #) or a tess factor */
   nir_variable *patch_vars[MAX_REGS];
   uint8_t pc_factor_mask[MAX_REGS];
   uint8_t pc_factor[MAX_REGS][4];   /* TESS_FACTOR_INNER | level index */

   /* hull shader: one nir_function per phase, called from main; the
    * patch constants the fork/join phases write live in pc_regs */
   unsigned hs_phase;
   struct {
      nir_function *func;
      unsigned kind;
      unsigned instances;
   } phases[MAX_PHASES];
   unsigned num_phases;
   bool hs_had_cp;
   nir_function_impl *main_impl;
   nir_variable *pc_regs;
   uint8_t pc_generic_mask[MAX_REGS];
   nir_variable *cp_out[MAX_REGS];   /* per-vertex outputs */
   uint8_t cp_out_mask[MAX_REGS];
   unsigned cp_in;                   /* input control point count */
   uint32_t passthrough_registers;   /* a pass-through phase copies these */
   /* UAV registers u0..uav_slots-1; counters at shader buffer counter_base + u#
    * (D3D11.1 mode: 64 and 64, compacted by compact_uav_buffers) */
   unsigned uav_slots, counter_base;
   /* FL 11_1 (dxbc_tir.h): a pixel shader variant's forced sample count (0:
    * off), its covered-sample mask, the offset centroid inputs are evaluated
    * at, and the registers declared centroid */
   unsigned forced_samples;
   bool rov_interlock;       /* the pixel interlock is taken (rasterizer-ordered UAVs) */
   nir_def *forced_mask, *forced_centroid;
   bool forced_centroid_reg[MAX_REGS];
   uint32_t passthrough_positions;   /* of which these hold SV_Position */

   struct cf_entry cf[MAX_CF];
   unsigned cf_depth;
   unsigned loop_depth;   /* NIR loops, switches included */
   bool main_done;        /* the top-level ret of the main program */
};

static void
fail(struct ctx *c, const struct dxbc_instruction *insn, const char *fmt, ...)
{
   va_list args;
   if (c->failed)
      return;
   c->failed = true;
   va_start(args, fmt);
   vsnprintf(c->error->message, sizeof(c->error->message), fmt, args);
   va_end(args);
   c->error->offset = insn ? insn->offset : 0;
   c->error->opcode = insn ? insn->opcode : 0;
}

static nir_def *
zero4(struct ctx *c)
{
   return nir_imm_ivec4(&c->b, 0, 0, 0, 0);
}

static nir_def *
vec4_of(struct ctx *c, nir_def *v)
{
   nir_def *comp[4];
   if (v->num_components == 4)
      return v;
   for (unsigned i = 0; i < 4; ++i)
      comp[i] = nir_channel(&c->b, v, MIN2(i, v->num_components - 1u));
   return nir_vec(&c->b, comp, 4);
}

static nir_def *
replicate4(struct ctx *c, nir_def *scalar)
{
   return nir_replicate(&c->b, nir_channel(&c->b, scalar, 0), 4);
}

/* ---------------------------------------------------------------------
 * Register files
 */

static nir_def *
temp_reg(struct ctx *c, unsigned index)
{
   if (index >= MAX_TEMPS)
      return NULL;
   if (!c->temps[index])
      c->temps[index] = nir_decl_reg(&c->b, 4, 32, 0);
   return c->temps[index];
}

static nir_def *
output_reg(struct ctx *c, unsigned index)
{
   if (index >= MAX_REGS)
      return NULL;
   if (!c->outputs[index])
      c->outputs[index] = nir_decl_reg(&c->b, 4, 32, 0);
   return c->outputs[index];
}

static nir_def *read_src(struct ctx *c, const struct dxbc_instruction *insn,
                         const struct dxbc_operand *o, nir_alu_type type);
static bool tex_shape(unsigned dim, struct tex_shape *s);
static nir_def *load_patch_constant(struct ctx *c, const struct dxbc_instruction *insn,
                                    const struct dxbc_operand *o);
static nir_deref_instr *hs_pc_reg(struct ctx *c, const struct dxbc_instruction *insn,
                                  const struct dxbc_index *index);
static void hs_store_cp_outputs(struct ctx *c);

/* imm + relative register component, as a 32-bit integer */
static nir_def *
index_value(struct ctx *c, const struct dxbc_instruction *insn,
            const struct dxbc_index *index)
{
   nir_def *value = nir_imm_int(&c->b, (int)index->imm);
   if (index->relative) {
      nir_def *rel = read_src(c, insn, index->relative, nir_type_int32);
      value = nir_iadd(&c->b, value, nir_channel(&c->b, rel, 0));
   }
   return value;
}

static bool
index_direct(const struct dxbc_index *index)
{
   return !index->relative;
}

/* C47: an operand of a heap range (only a program whose bindings placed
 * heap ranges has them; SM5.1 operands outside the binding path keep their
 * two-dimensional form) */
static bool
heap_operand(const struct ctx *c, const struct dxbc_operand *o)
{
   return c->p->heap_ranges && o->index_dimension == 2;
}

/* C34: the slots an SRV or sampler declaration covers -- index[1] of a
 * normalized SM5.1 range declaration, one otherwise */
static unsigned
range_slots(const struct ctx *c, const struct dxbc_operand *o)
{
   return c->p->major == 5 && c->p->minor == 1 && o->index_dimension == 1 && o->index[1].imm > 1
             ? (unsigned)o->index[1].imm : 1u;
}

static nir_def *
load_cb(struct ctx *c, const struct dxbc_instruction *insn,
        const struct dxbc_operand *o)
{
   nir_def *slot, *offset;
   if (o->index_dimension != 2 || !index_direct(&o->index[0])) {
      fail(c, insn, "constant buffer operand form");
      return zero4(c);
   }
   slot = nir_imm_int(&c->b, (int)o->index[0].imm);
   offset = nir_ishl_imm(&c->b, index_value(c, insn, &o->index[1]), 4);
   return nir_load_ubo(&c->b, 4, 32, slot, offset, .align_mul = 16,
                       .align_offset = 0, .range_base = 0, .range = ~0u);
}

static nir_def *
load_input(struct ctx *c, const struct dxbc_instruction *insn,
           const struct dxbc_operand *o)
{
   unsigned reg;
   nir_deref_instr *deref;
   if (o->index_dimension == 2) {
      /* geometry shader: v[vertex][register] */
      if (!index_direct(&o->index[1]) || o->index[1].imm >= MAX_REGS ||
          !c->inputs[o->index[1].imm]) {
         fail(c, insn, "geometry input operand");
         return zero4(c);
      }
      reg = (unsigned)o->index[1].imm;
      deref = nir_build_deref_array(&c->b, nir_build_deref_var(&c->b, c->inputs[reg]),
                                    index_value(c, insn, &o->index[0]));
      return nir_load_deref(&c->b, deref);
   }
   if (o->index_dimension != 1 || !index_direct(&o->index[0]) ||
       o->index[0].imm >= MAX_REGS) {
      fail(c, insn, "input operand form");
      return zero4(c);
   }
   reg = (unsigned)o->index[0].imm;
   if (c->input_sysval_mask[reg]) {
      nir_def *comp[4];
      nir_def *base = c->inputs[reg] ?
         nir_load_var(&c->b, c->inputs[reg]) : NULL;
      for (unsigned i = 0; i < 4; ++i) {
         if (c->input_sysval_mask[reg] & (1u << i))
            comp[i] = c->input_sysval[reg][i];
         else
            comp[i] = base ? nir_channel(&c->b, base, i) : nir_imm_int(&c->b, 0);
      }
      return nir_vec(&c->b, comp, 4);
   }
   if (!c->inputs[reg]) {
      fail(c, insn, "undeclared input v%u", reg);
      return zero4(c);
   }
   /* FL 11_1: centroid of the forced pattern */
   if (c->forced_centroid_reg[reg])
      return nir_interp_deref_at_offset(&c->b, 4, 32, &nir_build_deref_var(&c->b, c->inputs[reg])->def,
                                        c->forced_centroid);
   return nir_load_var(&c->b, c->inputs[reg]);
}

static nir_def *
load_file(struct ctx *c, const struct dxbc_instruction *insn,
          const struct dxbc_operand *o)
{
   nir_builder *b = &c->b;
   switch (o->type) {
   case DXBC_OPERAND_TEMP:
      if (o->index_dimension != 1 || !index_direct(&o->index[0]) ||
          !temp_reg(c, (unsigned)o->index[0].imm)) {
         fail(c, insn, "temp operand form");
         return zero4(c);
      }
      return nir_load_reg(b, temp_reg(c, (unsigned)o->index[0].imm));
   case DXBC_OPERAND_INDEXABLE_TEMP: {
      unsigned x = (unsigned)o->index[0].imm;
      if (o->index_dimension != 2 || !index_direct(&o->index[0]) ||
          x >= MAX_ITEMPS || !c->itemps[x]) {
         fail(c, insn, "indexable temp operand");
         return zero4(c);
      }
      return nir_load_deref(b, nir_build_deref_array(b,
         nir_build_deref_var(b, c->itemps[x]), index_value(c, insn, &o->index[1])));
   }
   case DXBC_OPERAND_INPUT:
      return load_input(c, insn, o);
   case DXBC_OPERAND_OUTPUT:
      if (c->stage == MESA_SHADER_TESS_CTRL && c->hs_phase >= HS_PHASE_FORK &&
          o->index_dimension == 1)
         return nir_load_deref(b, hs_pc_reg(c, insn, &o->index[0]));
      if (o->index_dimension == 1 && index_direct(&o->index[0]) &&
          output_reg(c, (unsigned)o->index[0].imm))
         return nir_load_reg(b, output_reg(c, (unsigned)o->index[0].imm));
      fail(c, insn, "output read form");
      return zero4(c);
   case DXBC_OPERAND_IMMEDIATE32:
      if (o->num_components == 1)
         return nir_imm_ivec4(b, (int)o->imm32[0], (int)o->imm32[0],
                              (int)o->imm32[0], (int)o->imm32[0]);
      return nir_imm_ivec4(b, (int)o->imm32[0], (int)o->imm32[1],
                           (int)o->imm32[2], (int)o->imm32[3]);
   case DXBC_OPERAND_CONSTANT_BUFFER:
      return load_cb(c, insn, o);
   case DXBC_OPERAND_IMMEDIATE_CONSTANT_BUFFER:
      if (!c->icb || o->index_dimension != 1) {
         fail(c, insn, "immediate constant buffer operand");
         return zero4(c);
      }
      return nir_load_deref(b, nir_build_deref_array(b,
         nir_build_deref_var(b, c->icb), index_value(c, insn, &o->index[0])));
   case DXBC_OPERAND_INPUT_PRIMITIVEID:
      return replicate4(c, nir_load_primitive_id(b));
   case DXBC_OPERAND_INPUT_THREAD_ID:
      return nir_pad_vector_imm_int(b, nir_load_global_invocation_id(b, 32), 0, 4);
   case DXBC_OPERAND_INPUT_THREAD_GROUP_ID:
      return nir_pad_vector_imm_int(b, nir_load_workgroup_id(b), 0, 4);
   case DXBC_OPERAND_INPUT_THREAD_ID_IN_GROUP:
      return nir_pad_vector_imm_int(b, nir_load_local_invocation_id(b), 0, 4);
   case DXBC_OPERAND_INPUT_THREAD_ID_IN_GROUP_FLATTENED:
      return replicate4(c, nir_load_local_invocation_index(b));
   case DXBC_OPERAND_INPUT_COVERAGE_MASK:
      /* FL 11_1: the coverage of the forced sample pattern */
      return replicate4(c, c->forced_mask ? c->forced_mask : nir_load_sample_mask_in(b));
   case DXBC_OPERAND_INPUT_GS_INSTANCE_ID:
      return replicate4(c, nir_load_invocation_id(b));
   case DXBC_OPERAND_INPUT_CONTROL_POINT:
      return load_input(c, insn, o);
   case DXBC_OPERAND_INPUT_PATCH_CONSTANT:
      if (c->stage == MESA_SHADER_TESS_CTRL) {
         if (o->index_dimension != 1) {
            fail(c, insn, "patch constant operand form");
            return zero4(c);
         }
         return nir_load_deref(b, hs_pc_reg(c, insn, &o->index[0]));
      }
      return load_patch_constant(c, insn, o);
   case DXBC_OPERAND_OUTPUT_CONTROL_POINT_ID:
      return replicate4(c, nir_load_invocation_id(b));
   case DXBC_OPERAND_INPUT_FORK_INSTANCE_ID:
   case DXBC_OPERAND_INPUT_JOIN_INSTANCE_ID:
      return replicate4(c, nir_load_param(b, 0));
   case DXBC_OPERAND_OUTPUT_CONTROL_POINT: {
      unsigned reg = (unsigned)o->index[1].imm;
      if (o->index_dimension != 2 || !index_direct(&o->index[1]) || reg >= MAX_REGS ||
          !c->cp_out[reg]) {
         fail(c, insn, "output control point operand");
         return zero4(c);
      }
      return nir_load_deref(b, nir_build_deref_array(b, nir_build_deref_var(b, c->cp_out[reg]),
                                                     index_value(c, insn, &o->index[0])));
   }
   case DXBC_OPERAND_INPUT_DOMAIN_POINT:
      return nir_pad_vector_imm_int(b, nir_load_tess_coord(b), 0, 4);
   default:
      fail(c, insn, "source operand type %u", o->type);
      return zero4(c);
   }
}

static nir_def *
swizzle4(struct ctx *c, nir_def *v, const struct dxbc_operand *o)
{
   unsigned swz[4];
   if (o->num_components != 4)
      return v;
   for (unsigned i = 0; i < 4; ++i)
      swz[i] = o->swizzle[i];
   if (o->selection == DXBC_SELECT_MASK)
      return v; /* a source in mask mode reads in place */
   return nir_swizzle(&c->b, v, swz, 4);
}

/* A source operand as a 4-component 32-bit value, swizzled, with its
 * modifier applied as the instruction's type reads it. */
static nir_def *
read_src(struct ctx *c, const struct dxbc_instruction *insn,
         const struct dxbc_operand *o, nir_alu_type type)
{
   nir_def *v = swizzle4(c, vec4_of(c, load_file(c, insn, o)), o);
   bool is_float = nir_alu_type_get_base_type(type) == nir_type_float;
   if (o->modifier == DXBC_MOD_ABS || o->modifier == DXBC_MOD_ABSNEG)
      v = is_float ? nir_fabs(&c->b, v) : nir_iabs(&c->b, v);
   if (o->modifier == DXBC_MOD_NEG || o->modifier == DXBC_MOD_ABSNEG)
      v = is_float ? nir_fneg(&c->b, v) : nir_ineg(&c->b, v);
   return v;
}

static void
write_dst(struct ctx *c, const struct dxbc_instruction *insn,
          const struct dxbc_operand *o, nir_def *value, bool saturate)
{
   nir_builder *b = &c->b;
   unsigned mask = o->num_components == 4 ? o->mask :
                   o->num_components == 1 ? 1u : 0xfu;
   value = vec4_of(c, value);
   if (saturate)
      value = nir_fsat(b, value);
   switch (o->type) {
   case DXBC_OPERAND_NULL:
      return;
   case DXBC_OPERAND_TEMP:
      if (o->index_dimension == 1 && index_direct(&o->index[0]) &&
          temp_reg(c, (unsigned)o->index[0].imm)) {
         nir_build_store_reg(b, value, temp_reg(c, (unsigned)o->index[0].imm),
                             .write_mask = mask);
         return;
      }
      break;
   case DXBC_OPERAND_OUTPUT:
      if (c->stage == MESA_SHADER_TESS_CTRL && c->hs_phase >= HS_PHASE_FORK &&
          o->index_dimension == 1) {
         /* patch constants, relative indexing included (dcl_indexrange) */
         nir_store_deref(b, hs_pc_reg(c, insn, &o->index[0]), value, mask);
         return;
      }
      if (o->index_dimension == 1 && index_direct(&o->index[0]) &&
          output_reg(c, (unsigned)o->index[0].imm)) {
         nir_build_store_reg(b, value, output_reg(c, (unsigned)o->index[0].imm),
                             .write_mask = mask);
         return;
      }
      break;
   case DXBC_OPERAND_INDEXABLE_TEMP: {
      unsigned x = (unsigned)o->index[0].imm;
      if (o->index_dimension == 2 && index_direct(&o->index[0]) &&
          x < MAX_ITEMPS && c->itemps[x]) {
         nir_store_deref(b, nir_build_deref_array(b,
            nir_build_deref_var(b, c->itemps[x]), index_value(c, insn, &o->index[1])),
            value, mask);
         return;
      }
      break;
   }
   case DXBC_OPERAND_OUTPUT_DEPTH:
   case DXBC_OPERAND_OUTPUT_DEPTH_GREATER_EQUAL:
   case DXBC_OPERAND_OUTPUT_DEPTH_LESS_EQUAL:
      if (c->depth_reg) {
         nir_build_store_reg(b, value, c->depth_reg, .write_mask = 1);
         return;
      }
      break;
   case DXBC_OPERAND_OUTPUT_COVERAGE_MASK:
      if (c->mask_reg) {
         nir_build_store_reg(b, value, c->mask_reg, .write_mask = 1);
         return;
      }
      break;
   default:
      break;
   }
   fail(c, insn, "destination operand type %u", o->type);
}

/* ---------------------------------------------------------------------
 * Declarations
 */

static nir_variable *
make_var(struct ctx *c, nir_variable_mode mode, const struct glsl_type *type,
         int location, const char *name)
{
   nir_variable *var = nir_variable_create(c->b.shader, mode, type, name);
   var->data.location = location;
   return var;
}

/* Control points link hull to domain shader by register (C37, EXP1834: an
 * SV_Position control point stored at VARYING_SLOT_POS was read by the
 * domain shader's plain "dcl_input vicp[n][r]" at VAR0 + r, so the patch
 * collapsed): hull outputs and domain inputs use VAR0 + register whatever
 * their system value. Only the vertex shader feeding the hull shader
 * writes SV_Position at VARYING_SLOT_POS. */
static int
hs_cp_slot(unsigned reg)
{
   return VARYING_SLOT_VAR0 + (int)reg;
}

static int
generic_slot(struct ctx *c, unsigned reg, bool input)
{
   if (c->stage == MESA_SHADER_VERTEX && input)
      return VERT_ATTRIB_GENERIC0 + (int)reg;
   if (c->stage == MESA_SHADER_FRAGMENT && !input)
      return FRAG_RESULT_DATA0 + (int)reg;
   return VARYING_SLOT_VAR0 + (int)reg;
}

static enum glsl_interp_mode
interp_mode(uint32_t mode, bool *centroid, bool *sample)
{
   *centroid = mode == 3 || mode == 5;
   *sample = mode == 6 || mode == 7;
   switch (mode) {
   case 1: return INTERP_MODE_FLAT;
   case 4: case 5: case 7: return INTERP_MODE_NOPERSPECTIVE;
   default: return INTERP_MODE_SMOOTH;
   }
}

/* the patch-constant register file of a hull shader's fork/join phases */
static nir_variable *
hs_pc_regs(struct ctx *c)
{
   if (!c->pc_regs)
      c->pc_regs = nir_variable_create(c->b.shader, nir_var_shader_temp,
                                       glsl_array_type(glsl_uvec4_type(), MAX_REGS, 0), "pc");
   return c->pc_regs;
}

static nir_deref_instr *
hs_pc_reg(struct ctx *c, const struct dxbc_instruction *insn, const struct dxbc_index *index)
{
   nir_builder *b = &c->b;
   return nir_build_deref_array(b, nir_build_deref_var(b, hs_pc_regs(c)),
                                index_value(c, insn, index));
}

/* the per-vertex output array of control point register 'reg' */
static nir_variable *
hs_cp_output(struct ctx *c, unsigned reg, int slot)
{
   unsigned n = c->b.shader->info.tess.tcs_vertices_out;
   if (!c->cp_out[reg] && n)
      c->cp_out[reg] = make_var(c, nir_var_shader_out,
                                glsl_array_type(glsl_vec4_type(), n, 0), slot, "vocp");
   return c->cp_out[reg];
}

/* the per-vertex input array of control point register 'reg' */
static nir_variable *
hs_cp_input(struct ctx *c, unsigned reg, int slot)
{
   if (!c->inputs[reg])
      c->inputs[reg] = make_var(c, nir_var_shader_in,
                                glsl_array_type(glsl_vec4_type(), c->cp_in ? c->cp_in : 32, 0),
                                slot, "vicp");
   return c->inputs[reg];
}

/* the control point phase's outputs, to gl_out[invocation] */
static void
hs_store_cp_outputs(struct ctx *c)
{
   nir_builder *b = &c->b;
   nir_def *id = NULL;
   for (unsigned reg = 0; reg < MAX_REGS; ++reg) {
      if (!c->cp_out[reg] || !c->outputs[reg] || !c->cp_out_mask[reg])
         continue;
      if (!id)
         id = nir_load_invocation_id(b);
      nir_store_deref(b, nir_build_deref_array(b, nir_build_deref_var(b, c->cp_out[reg]), id),
                      nir_load_reg(b, c->outputs[reg]), c->cp_out_mask[reg]);
   }
}

/* D3D11_SB_NAME_FINAL_*_TESSFACTOR (11..22) -> TESS_FACTOR_INNER | index;
 * 0xff for other names.  Triangle and quad edges and insides map index for
 * index (as Mesa's GL -> DXIL path, dxil_nir_tess.c); isoline density is
 * gl_TessLevelOuter[0] (number of lines), detail [1] (segments per line). */
static unsigned
tess_factor(unsigned name)
{
   switch (name) {
   case 11: return 0;                       /* quad U == 0 edge */
   case 12: return 1;                       /* quad V == 0 edge */
   case 13: return 2;                       /* quad U == 1 edge */
   case 14: return 3;                       /* quad V == 1 edge */
   case 15: return TESS_FACTOR_INNER | 0;   /* quad U inside */
   case 16: return TESS_FACTOR_INNER | 1;   /* quad V inside */
   case 17: return 0;                       /* tri U == 0 edge */
   case 18: return 1;                       /* tri V == 0 edge */
   case 19: return 2;                       /* tri W == 0 edge */
   case 20: return TESS_FACTOR_INNER | 0;   /* tri inside */
   case 21: return 1;                       /* line detail */
   case 22: return 0;                       /* line density */
   default: return 0xffu;
   }
}

/* dcl_input(_siv) vpc#: a domain shader's patch constants */
static void
declare_patch_input(struct ctx *c, const struct dxbc_instruction *insn)
{
   const struct dxbc_operand *o = &insn->operands[0];
   unsigned name = insn->num_payload ? insn->payload[0] : 0;
   unsigned mask = o->num_components == 4 ? o->mask : 1u, reg;
   if (o->index_dimension != 1 || o->index[0].imm >= MAX_REGS) {
      fail(c, insn, "patch constant declaration");
      return;
   }
   reg = (unsigned)o->index[0].imm;
   if (insn->opcode == DXBC_OP_DCL_INPUT_SIV && tess_factor(name) != 0xffu) {
      for (unsigned i = 0; i < 4; ++i)
         if (mask & (1u << i))
            c->pc_factor[reg][i] = (uint8_t)tess_factor(name);
      c->pc_factor_mask[reg] |= (uint8_t)mask;
      return;
   }
   if (insn->opcode != DXBC_OP_DCL_INPUT) {
      fail(c, insn, "patch constant system value %u", name);
      return;
   }
   if (!c->patch_vars[reg]) {
      c->patch_vars[reg] = make_var(c, nir_var_shader_in, glsl_vec4_type(),
                                    VARYING_SLOT_PATCH0 + (int)reg, "vpc");
      c->patch_vars[reg]->data.patch = true;
   }
}

static nir_def *
load_patch_constant(struct ctx *c, const struct dxbc_instruction *insn,
                    const struct dxbc_operand *o)
{
   nir_builder *b = &c->b;
   nir_def *comp[4], *base = NULL, *outer = NULL, *inner = NULL;
   unsigned reg;
   if (o->index_dimension != 1 || !index_direct(&o->index[0]) ||
       o->index[0].imm >= MAX_REGS) {
      fail(c, insn, "patch constant operand form");
      return zero4(c);
   }
   reg = (unsigned)o->index[0].imm;
   if (c->patch_vars[reg])
      base = nir_load_var(b, c->patch_vars[reg]);
   for (unsigned i = 0; i < 4; ++i) {
      if (c->pc_factor_mask[reg] & (1u << i)) {
         unsigned f = c->pc_factor[reg][i];
         if (f & TESS_FACTOR_INNER) {
            if (!inner)
               inner = nir_load_tess_level_inner(b);
            comp[i] = nir_channel(b, inner, f & 1u);
         } else {
            if (!outer)
               outer = nir_load_tess_level_outer(b);
            comp[i] = nir_channel(b, outer, f & 3u);
         }
      } else {
         comp[i] = base ? nir_channel(b, base, i) : nir_imm_int(b, 0);
      }
   }
   return nir_vec(b, comp, 4);
}

static void
declare_input(struct ctx *c, const struct dxbc_instruction *insn)
{
   const struct dxbc_operand *o = &insn->operands[0];
   nir_builder *b = &c->b;
   unsigned name = insn->num_payload ? insn->payload[0] : 0;
   unsigned reg, mask = o->num_components == 4 ? o->mask : 1u;
   bool ps = c->stage == MESA_SHADER_FRAGMENT;

   switch (o->type) {
   case DXBC_OPERAND_INPUT:
      break;
   case DXBC_OPERAND_INPUT_PRIMITIVEID:
   case DXBC_OPERAND_INPUT_COVERAGE_MASK:
   case DXBC_OPERAND_INPUT_GS_INSTANCE_ID:
   case DXBC_OPERAND_INPUT_THREAD_ID:
   case DXBC_OPERAND_INPUT_THREAD_GROUP_ID:
   case DXBC_OPERAND_INPUT_THREAD_ID_IN_GROUP:
   case DXBC_OPERAND_INPUT_THREAD_ID_IN_GROUP_FLATTENED:
   case DXBC_OPERAND_INPUT_DOMAIN_POINT:
   case DXBC_OPERAND_OUTPUT_CONTROL_POINT_ID:
   case DXBC_OPERAND_INPUT_FORK_INSTANCE_ID:
   case DXBC_OPERAND_INPUT_JOIN_INSTANCE_ID:
   case DXBC_OPERAND_OUTPUT_CONTROL_POINT:
      return; /* read through system values or the phase's outputs at use */
   case DXBC_OPERAND_INPUT_PATCH_CONSTANT:
      if (c->stage == MESA_SHADER_TESS_CTRL)
         return; /* a join phase reads the patch-constant registers */
      declare_patch_input(c, insn);
      return;
   case DXBC_OPERAND_INPUT_CONTROL_POINT:
      break;
   default:
      fail(c, insn, "input declaration of operand type %u", o->type);
      return;
   }

   if (o->index_dimension == 2) {
      /* geometry shader input array v[n][reg]; the control points of a
       * hull or domain shader, vicp[n][reg] */
      if (!index_direct(&o->index[1]) || o->index[1].imm >= MAX_REGS) {
         fail(c, insn, "geometry input declaration");
         return;
      }
      reg = (unsigned)o->index[1].imm;
      c->gs_vertices_in = (unsigned)o->index[0].imm;
      if (c->stage == MESA_SHADER_TESS_CTRL && !c->cp_in)
         c->cp_in = c->gs_vertices_in;
      if (!c->inputs[reg]) {
         /* C37: a hull shader reads the vertex shader's SV_Position (named
          * by the declaration or by the caller's signature mask) at
          * VARYING_SLOT_POS; a domain shader reads control points by
          * register (hs_cp_slot) */
         const bool position = (insn->opcode == DXBC_OP_DCL_INPUT_SIV && name == 1) ||
            (c->stage == MESA_SHADER_TESS_CTRL && (c->passthrough_positions & (1u << reg)));
         int slot = c->stage == MESA_SHADER_TESS_EVAL ? hs_cp_slot(reg) :
            position ? VARYING_SLOT_POS : generic_slot(c, reg, true);
         c->inputs[reg] = make_var(c, nir_var_shader_in,
            glsl_array_type(glsl_vec4_type(), c->gs_vertices_in, 0), slot, "vin");
      }
      return;
   }
   if (o->index_dimension != 1 || o->index[0].imm >= MAX_REGS) {
      fail(c, insn, "input declaration form");
      return;
   }
   reg = (unsigned)o->index[0].imm;

   if (insn->opcode == DXBC_OP_DCL_INPUT_SGV || insn->opcode == DXBC_OP_DCL_INPUT_SIV ||
       insn->opcode == DXBC_OP_DCL_INPUT_PS_SGV || insn->opcode == DXBC_OP_DCL_INPUT_PS_SIV) {
      nir_def *value = NULL;
      int slot = -1;
      switch (name) {
      case 1: /* position */
         if (ps) {
            nir_def *fc = nir_load_frag_coord(b);
            for (unsigned i = 0; i < 4; ++i)
               if (mask & (1u << i))
                  c->input_sysval[reg][i] = nir_channel(b, fc, i);
            c->input_sysval_mask[reg] |= (uint8_t)mask;
            return;
         }
         slot = VARYING_SLOT_POS;
         break;
      case 2: slot = VARYING_SLOT_CLIP_DIST0; break;
      case 3: slot = VARYING_SLOT_CULL_DIST0; break;
      case 4: slot = VARYING_SLOT_LAYER; break;
      case 5: slot = VARYING_SLOT_VIEWPORT; break;
      case 6: value = nir_load_vertex_id_zero_base(b); break;
      case 7:
         /* a pixel shader reads the primitive id as a flat varying (the
          * TGSI path's PRIMID input); other stages as a system value */
         if (ps)
            slot = VARYING_SLOT_PRIMITIVE_ID;
         else
            value = nir_load_primitive_id(b);
         break;
      case 8: value = nir_load_instance_id(b); break;
      case 9: value = nir_ineg(b, nir_b2i32(b, nir_load_front_face(b, 1))); break;
      case 10:
         /* SV_SampleIndex runs the pixel shader per sample (tgsi_to_nir's
          * SAMPLEID); Asahi lowers sample intrinsics only then */
         value = nir_load_sample_id(b);
         b->shader->info.fs.uses_sample_shading = true;
         break;
      default:
         fail(c, insn, "input system value %u", name);
         return;
      }
      if (value) {
         for (unsigned i = 0; i < 4; ++i)
            if (mask & (1u << i))
               c->input_sysval[reg][i] = value;
         c->input_sysval_mask[reg] |= (uint8_t)mask;
         return;
      }
      if (!c->inputs[reg] && slot == VARYING_SLOT_PRIMITIVE_ID) {
         /* one 32-bit component: v#.c reads it in its declared component */
         nir_variable *var = make_var(c, nir_var_shader_in, glsl_uint_type(), slot, "primid");
         nir_def *id;
         var->data.interpolation = INTERP_MODE_FLAT;
         id = nir_load_var(b, var);
         for (unsigned i = 0; i < 4; ++i)
            if (mask & (1u << i))
               c->input_sysval[reg][i] = id;
         c->input_sysval_mask[reg] |= (uint8_t)mask;
         return;
      }
      if (!c->inputs[reg]) {
         c->inputs[reg] = make_var(c, nir_var_shader_in, glsl_vec4_type(), slot, "vin");
         if (ps) {
            bool centroid, sample;
            c->inputs[reg]->data.interpolation =
               interp_mode((insn->token0 >> 11) & 0xfu, &centroid, &sample);
            c->inputs[reg]->data.centroid = centroid && !c->forced_samples;
            c->forced_centroid_reg[reg] = centroid && c->forced_samples;
            c->inputs[reg]->data.sample = sample;
            b->shader->info.fs.uses_sample_shading |= sample;
         }
      }
      return;
   }

   if (c->inputs[reg])
      return; /* several declarations of one register (component sets) */
   c->inputs[reg] = make_var(c, nir_var_shader_in, glsl_vec4_type(),
                             generic_slot(c, reg, true), "vin");
   /* A vertex input reads vertex element 'reg' (the frontend places each
    * element at its D3D input register): its io base, which Asahi's vertex
    * prolog maps to an attribute (agx_nir_lower_vs_input_to_prolog), as
    * tgsi_to_nir's driver_location.  EXP1297: without it every input read
    * attribute 0. */
   if (c->stage == MESA_SHADER_VERTEX)
      c->inputs[reg]->data.driver_location = reg;
   if (ps) {
      bool centroid, sample;
      c->inputs[reg]->data.interpolation =
         interp_mode((insn->token0 >> 11) & 0xfu, &centroid, &sample);
      /* FL 11_1: a forced pattern's centroid is an offset (load_input) */
      c->inputs[reg]->data.centroid = centroid && !c->forced_samples;
      c->forced_centroid_reg[reg] = centroid && c->forced_samples;
      c->inputs[reg]->data.sample = sample;
      /* a sample-interpolated input runs the shader per sample (as the
       * GLSL linker and spirv_to_nir mark it) */
      c->b.shader->info.fs.uses_sample_shading |= sample;
   }
}

static void
add_sink(struct ctx *c, unsigned reg, nir_variable *var, unsigned mask,
         unsigned first, bool scalar)
{
   if (c->num_sinks[reg] < 4) {
      struct output_sink *s = &c->sinks[reg][c->num_sinks[reg]++];
      s->var = var;
      s->mask = (uint8_t)mask;
      s->first = (uint8_t)first;
      s->scalar = scalar;
   }
}

static nir_variable *
find_output(struct ctx *c, int location)
{
   nir_foreach_shader_out_variable(var, c->b.shader)
      if (var->data.location == location)
         return var;
   return NULL;
}

static void
declare_output(struct ctx *c, const struct dxbc_instruction *insn)
{
   const struct dxbc_operand *o = &insn->operands[0];
   unsigned name = insn->num_payload ? insn->payload[0] : 0;
   unsigned reg, mask = o->num_components == 4 ? o->mask : 1u;
   nir_variable *var;

   switch (o->type) {
   case DXBC_OPERAND_OUTPUT:
      break;
   case DXBC_OPERAND_OUTPUT_DEPTH:
   case DXBC_OPERAND_OUTPUT_DEPTH_GREATER_EQUAL:
   case DXBC_OPERAND_OUTPUT_DEPTH_LESS_EQUAL:
      if (!c->depth_reg) {
         c->depth_reg = nir_decl_reg(&c->b, 4, 32, 0);
         c->depth_var = make_var(c, nir_var_shader_out, glsl_float_type(),
                                 FRAG_RESULT_DEPTH, "depth");
         c->b.shader->info.fs.depth_layout =
            o->type == DXBC_OPERAND_OUTPUT_DEPTH_GREATER_EQUAL ? FRAG_DEPTH_LAYOUT_GREATER :
            o->type == DXBC_OPERAND_OUTPUT_DEPTH_LESS_EQUAL ? FRAG_DEPTH_LAYOUT_LESS :
            FRAG_DEPTH_LAYOUT_ANY;
      }
      return;
   case DXBC_OPERAND_OUTPUT_COVERAGE_MASK:
      if (!c->mask_reg) {
         c->mask_reg = nir_decl_reg(&c->b, 4, 32, 0);
         c->mask_var = make_var(c, nir_var_shader_out, glsl_uint_type(),
                                FRAG_RESULT_SAMPLE_MASK, "mask");
      }
      return;
   default:
      fail(c, insn, "output declaration of operand type %u", o->type);
      return;
   }
   if (o->index_dimension != 1 || o->index[0].imm >= MAX_REGS) {
      fail(c, insn, "output declaration form");
      return;
   }
   reg = (unsigned)o->index[0].imm;
   if (c->stage == MESA_SHADER_TESS_CTRL && c->hs_phase >= HS_PHASE_FORK) {
      /* patch constants: tess factors by name, the rest patch varyings */
      if (insn->opcode == DXBC_OP_DCL_OUTPUT_SIV && tess_factor(name) != 0xffu) {
         for (unsigned i = 0; i < 4; ++i)
            if (mask & (1u << i))
               c->pc_factor[reg][i] = (uint8_t)tess_factor(name);
         c->pc_factor_mask[reg] |= (uint8_t)mask;
      } else if (insn->opcode == DXBC_OP_DCL_OUTPUT) {
         c->pc_generic_mask[reg] |= (uint8_t)mask;
      } else {
         fail(c, insn, "patch constant output system value %u", name);
      }
      return;
   }
   output_reg(c, reg);
   if (c->stage == MESA_SHADER_TESS_CTRL) {
      /* control points: gl_out[invocation] */
      if (!(insn->opcode == DXBC_OP_DCL_OUTPUT || name == 0 || name == 1) ||
          !hs_cp_output(c, reg, hs_cp_slot(reg))) {
         fail(c, insn, "control point output");
         return;
      }
      c->cp_out_mask[reg] |= (uint8_t)mask;
      return;
   }

   if (insn->opcode == DXBC_OP_DCL_OUTPUT || name == 0) {
      int slot = generic_slot(c, reg, false);
      var = find_output(c, slot);
      if (!var)
         var = make_var(c, nir_var_shader_out, glsl_vec4_type(), slot, "vout");
      var->data.stream = c->gs_stream;
      add_sink(c, reg, var, mask, 0, false);
      return;
   }
   switch (name) {
   case 1: /* position */
      var = make_var(c, nir_var_shader_out, glsl_vec4_type(), VARYING_SLOT_POS, "pos");
      add_sink(c, reg, var, mask, 0, false);
      return;
   case 2: /* clip distance: registers fill CLIP_DIST0 then CLIP_DIST1 */
   case 3: { /* cull distance */
      bool clip = name == 2;
      unsigned *slots = clip ? &c->clip_slots : &c->cull_slots;
      unsigned n = (unsigned)util_bitcount(mask), first = 0;
      int base = clip ? VARYING_SLOT_CLIP_DIST0 : VARYING_SLOT_CULL_DIST0;
      var = make_var(c, nir_var_shader_out, glsl_vec4_type(), base + (int)*slots,
                     clip ? "clip" : "cull");
      (void)first;
      add_sink(c, reg, var, mask, 0, false);
      if (clip)
         c->b.shader->info.clip_distance_array_size += (uint8_t)n;
      else
         c->b.shader->info.cull_distance_array_size += (uint8_t)n;
      ++*slots;
      return;
   }
   case 4: /* render target array index */
      var = make_var(c, nir_var_shader_out, glsl_int_type(), VARYING_SLOT_LAYER, "layer");
      add_sink(c, reg, var, mask, 0, true);
      return;
   case 5: /* viewport array index */
      var = make_var(c, nir_var_shader_out, glsl_int_type(), VARYING_SLOT_VIEWPORT, "viewport");
      add_sink(c, reg, var, mask, 0, true);
      return;
   default:
      fail(c, insn, "output system value %u", name);
      return;
   }
}

static nir_alu_type
return_type(uint32_t token)
{
   switch (token & 0xfu) {
   case DXBC_RETURN_SINT: return nir_type_int32;
   case DXBC_RETURN_UINT: return nir_type_uint32;
   default: return nir_type_float32;
   }
}

/* ROV (D3D12 12_1, SM 5.1): a rasterizer-ordered UAV declaration
 * (D3D11_SB_RASTERIZER_ORDERED_ACCESS) takes the pixel interlock once, ahead
 * of the shader body -- every access follows its declarations. AGX orders
 * from the interlock begin (wait_pix) to the end of the shader, so no end is
 * emitted (as the DXIL path's drop_interlock_end). */
static void
rov_declaration(struct ctx *c, const struct dxbc_instruction *insn)
{
   if (c->stage != MESA_SHADER_FRAGMENT || !(insn->token0 & (1u << 17)) || c->rov_interlock)
      return;
   nir_begin_invocation_interlock(&c->b);
   c->b.shader->info.fs.pixel_interlock_ordered = true;
   c->rov_interlock = true;
}

static void
declare(struct ctx *c, const struct dxbc_instruction *insn)
{
   const struct dxbc_operand *o = insn->num_operands ? &insn->operands[0] : NULL;
   nir_builder *b = &c->b;
   switch (insn->opcode) {
   case DXBC_OP_DCL_GLOBAL_FLAGS:
      if (insn->token0 & (1u << 13)) /* forceEarlyDepthStencil */
         b->shader->info.fs.early_fragment_tests = true;
      return;
   case DXBC_OP_DCL_TEMPS:
      c->num_temps = insn->num_payload ? insn->payload[0] : 0;
      if (c->num_temps > MAX_TEMPS)
         fail(c, insn, "%u temps", c->num_temps);
      return;
   case DXBC_OP_DCL_INDEXABLE_TEMP: {
      unsigned x = insn->num_payload >= 3 ? insn->payload[0] : MAX_ITEMPS;
      if (x >= MAX_ITEMPS || !insn->payload[1]) {
         fail(c, insn, "indexable temp declaration");
         return;
      }
      c->itemp_count[x] = insn->payload[1];
      c->itemps[x] = nir_local_variable_create(b->impl,
         glsl_array_type(glsl_uvec4_type(), insn->payload[1], 0), "x");
      return;
   }
   case DXBC_OP_CUSTOMDATA:
      if ((insn->token0 >> 11) == DXBC_CUSTOMDATA_IMMEDIATE_CONSTANT_BUFFER) {
         unsigned n = insn->num_payload / 4;
         if (!n || c->icb) {
            fail(c, insn, "immediate constant buffer");
            return;
         }
         c->icb_count = n;
         c->icb = nir_variable_create(b->shader, nir_var_shader_temp,
            glsl_array_type(glsl_uvec4_type(), n, 0), "icb");
         for (unsigned i = 0; i < n; ++i)
            nir_store_deref(b, nir_build_deref_array_imm(b,
               nir_build_deref_var(b, c->icb), i),
               nir_imm_ivec4(b, (int)insn->payload[4 * i], (int)insn->payload[4 * i + 1],
                             (int)insn->payload[4 * i + 2], (int)insn->payload[4 * i + 3]),
               0xf);
      }
      return; /* comments, debug info */
   case DXBC_OP_DCL_CONSTANT_BUFFER:
      if (!o || o->index_dimension != 2 || o->index[0].imm >= 15) {
         fail(c, insn, "constant buffer declaration");
         return;
      }
      b->shader->info.num_ubos = MAX2(b->shader->info.num_ubos, (uint8_t)(o->index[0].imm + 1));
      return;
   case DXBC_OP_DCL_SAMPLER:
      if (o && heap_operand(c, o)) {
         /* C47: a heap sampler range */
         if (o->index[0].imm >= DXBC_HEAP_RANGE_MAX) {
            fail(c, insn, "heap sampler declaration");
            return;
         }
         c->samp[MAX_SAMPLERS + o->index[0].imm].declared = true;
         c->samp[MAX_SAMPLERS + o->index[0].imm].comparison = ((insn->token0 >> 11) & 0xfu) == 1;
         return;
      }
      if (!o || o->index[0].imm >= MAX_SAMPLERS) {
         fail(c, insn, "sampler declaration");
         return;
      }
      /* C34: an SM5.1 range covers index[1] consecutive slots */
      for (unsigned k = 0, n = range_slots(c, o); k < n; ++k) {
         if (o->index[0].imm + k >= MAX_SAMPLERS) {
            fail(c, insn, "sampler range declaration");
            return;
         }
         c->samp[o->index[0].imm + k].declared = true;
         BITSET_SET(b->shader->info.samplers_used, (unsigned)o->index[0].imm + k);
         c->samp[o->index[0].imm + k].comparison = ((insn->token0 >> 11) & 0xfu) == 1;
      }
      return;
   case DXBC_OP_DCL_RESOURCE:
      if (o && heap_operand(c, o)) {
         /* C47: a heap range (dxbc_prepare_static_bindings) */
         if (o->index[0].imm >= DXBC_HEAP_RANGE_MAX || !insn->num_payload) {
            fail(c, insn, "heap range declaration");
            return;
         }
         const unsigned t = MAX_RESOURCES + (unsigned)o->index[0].imm;
         c->res[t].declared = true;
         c->res[t].dim = (uint8_t)((insn->token0 >> 11) & 0x1fu);
         c->res[t].type = return_type(insn->payload[0]);
         return;
      }
      if (!o || o->index[0].imm >= MAX_RESOURCES || !insn->num_payload) {
         fail(c, insn, "resource declaration");
         return;
      }
      for (unsigned k = 0, n = range_slots(c, o); k < n; ++k) {
         const unsigned t = (unsigned)o->index[0].imm + k;
         if (t >= MAX_RESOURCES) {
            fail(c, insn, "resource range declaration");
            return;
         }
         c->res[t].declared = true;
         BITSET_SET(b->shader->info.textures_used, t);
         c->res[t].dim = (uint8_t)((insn->token0 >> 11) & 0x1fu);
         c->res[t].type = return_type(insn->payload[0]);
      }
      return;
   case DXBC_OP_DCL_INPUT:
   case DXBC_OP_DCL_INPUT_SGV:
   case DXBC_OP_DCL_INPUT_SIV:
   case DXBC_OP_DCL_INPUT_PS:
   case DXBC_OP_DCL_INPUT_PS_SGV:
   case DXBC_OP_DCL_INPUT_PS_SIV:
      declare_input(c, insn);
      return;
   case DXBC_OP_DCL_OUTPUT:
   case DXBC_OP_DCL_OUTPUT_SGV:
   case DXBC_OP_DCL_OUTPUT_SIV:
      declare_output(c, insn);
      return;
   case DXBC_OP_DCL_INDEX_RANGE:
      return; /* indexing is resolved per access */
   case DXBC_OP_DCL_GS_INPUT_PRIMITIVE: {
      static const uint8_t prim[] = {
         [1] = MESA_PRIM_POINTS, [2] = MESA_PRIM_LINES, [3] = MESA_PRIM_TRIANGLES,
         [6] = MESA_PRIM_LINES_ADJACENCY, [7] = MESA_PRIM_TRIANGLES_ADJACENCY };
      static const uint8_t vertices[] = { [1] = 1, [2] = 2, [3] = 3, [6] = 4, [7] = 6 };
      unsigned p = (insn->token0 >> 11) & 0x3fu;
      if (p >= ARRAY_SIZE(prim) || !vertices[p]) {
         fail(c, insn, "geometry input primitive %u", p);
         return;
      }
      b->shader->info.gs.input_primitive = prim[p];
      b->shader->info.gs.vertices_in = vertices[p];
      return;
   }
   case DXBC_OP_DCL_GS_OUTPUT_PRIMITIVE_TOPOLOGY: {
      unsigned t = (insn->token0 >> 11) & 0x7fu;
      b->shader->info.gs.output_primitive =
         t == 1 ? MESA_PRIM_POINTS : (t == 2 || t == 3) ? MESA_PRIM_LINE_STRIP :
         MESA_PRIM_TRIANGLE_STRIP;
      return;
   }
   case DXBC_OP_DCL_MAX_OUTPUT_VERTEX_COUNT:
      b->shader->info.gs.vertices_out = insn->num_payload ? insn->payload[0] : 0;
      return;
   case DXBC_OP_DCL_GS_INSTANCE_COUNT:
      b->shader->info.gs.invocations = insn->num_payload ? insn->payload[0] : 1;
      return;
   case DXBC_OP_DCL_STREAM:
      if (!o || o->type != DXBC_OPERAND_STREAM || o->index[0].imm >= 4) {
         fail(c, insn, "stream declaration");
         return;
      }
      c->gs_stream = (unsigned)o->index[0].imm;
      b->shader->info.gs.active_stream_mask |= (uint8_t)(1u << c->gs_stream);
      return;
   case DXBC_OP_DCL_INPUT_CONTROL_POINT_COUNT:
      c->cp_in = (insn->token0 >> 11) & 0x3fu;
      return;
   case DXBC_OP_DCL_HS_FORK_PHASE_INSTANCE_COUNT:
   case DXBC_OP_DCL_HS_JOIN_PHASE_INSTANCE_COUNT:
      if (!c->num_phases || !insn->num_payload || !insn->payload[0] || insn->payload[0] > 128) {
         fail(c, insn, "phase instance count");
         return;
      }
      c->phases[c->num_phases - 1].instances = insn->payload[0];
      return;
   case DXBC_OP_DCL_OUTPUT_CONTROL_POINT_COUNT:
      b->shader->info.tess.tcs_vertices_out = (uint8_t)((insn->token0 >> 11) & 0x3fu);
      return;
   case DXBC_OP_DCL_TESS_DOMAIN: {
      static const uint8_t prim[] = {[1] = TESS_PRIMITIVE_ISOLINES,
                                     [2] = TESS_PRIMITIVE_TRIANGLES,
                                     [3] = TESS_PRIMITIVE_QUADS};
      unsigned d = (insn->token0 >> 11) & 3u;
      if (!d) {
         fail(c, insn, "tessellator domain %u", d);
         return;
      }
      b->shader->info.tess._primitive_mode = prim[d];
      return;
   }
   case DXBC_OP_DCL_TESS_PARTITIONING: {
      /* pow2 rounds the factors up to powers of two before an integer
       * tessellation; not handled yet */
      unsigned p = (insn->token0 >> 11) & 7u;
      if (p == 1)
         b->shader->info.tess.spacing = TESS_SPACING_EQUAL;
      else if (p == 3)
         b->shader->info.tess.spacing = TESS_SPACING_FRACTIONAL_ODD;
      else if (p == 4)
         b->shader->info.tess.spacing = TESS_SPACING_FRACTIONAL_EVEN;
      else
         fail(c, insn, "tessellator partitioning %u", p);
      return;
   }
   case DXBC_OP_DCL_TESS_OUTPUT_PRIMITIVE: {
      unsigned p = (insn->token0 >> 11) & 7u;
      if (p < 1 || p > 4) {
         fail(c, insn, "tessellator output primitive %u", p);
         return;
      }
      b->shader->info.tess.point_mode = p == 1;
      /* GL's tessellation domain has a lower-left origin, D3D's an upper-left
       * one: a D3D triangle_cw (3) output is GL ccw (Mesa d3d12_compiler.cpp
       * inverts the same way). EXP1858-1860: with ccw = triangle_ccw both
       * windings culled opposite to WARP under back-face culling. */
      b->shader->info.tess.ccw = p == 3;
      return;
   }
   case DXBC_OP_DCL_HS_MAX_TESSFACTOR:
      return; /* a hint: fxc writes the clamp into the code */
   case DXBC_OP_DCL_THREAD_GROUP:
      if (insn->num_payload < 3 || !insn->payload[0] || !insn->payload[1] || !insn->payload[2]) {
         fail(c, insn, "thread group declaration");
         return;
      }
      for (unsigned i = 0; i < 3; ++i)
         b->shader->info.workgroup_size[i] = (uint16_t)insn->payload[i];
      return;
   case DXBC_OP_DCL_TGSM_RAW:
   case DXBC_OP_DCL_TGSM_STRUCTURED: {
      unsigned g = o ? (unsigned)o->index[0].imm : MAX_TGSM;
      bool structured = insn->opcode == DXBC_OP_DCL_TGSM_STRUCTURED;
      uint64_t bytes = !insn->num_payload ? 0 : structured ?
         (insn->num_payload >= 2 ? (uint64_t)insn->payload[0] * insn->payload[1] : 0) :
         insn->payload[0];
      if (g >= MAX_TGSM || !bytes || (bytes & 3) || bytes > 32768 || c->tgsm[g].var) {
         fail(c, insn, "groupshared declaration");
         return;
      }
      c->tgsm[g].stride = structured ? insn->payload[0] : 0;
      c->tgsm[g].var = nir_variable_create(b->shader, nir_var_mem_shared,
         glsl_array_type(glsl_uint_type(), (unsigned)(bytes / 4), 4), "g");
      return;
   }
   case DXBC_OP_DCL_UAV_TYPED: {
      unsigned u = o ? (unsigned)o->index[0].imm : MAX_UAVS;
      rov_declaration(c, insn);
      struct tex_shape shape;
      nir_alu_type type;
      const bool heap = o && heap_operand(c, o); /* C47 */
      if (heap && u < DXBC_HEAP_RANGE_MAX && insn->num_payload &&
          tex_shape((insn->token0 >> 11) & 0x1fu, &shape) && shape.dim != GLSL_SAMPLER_DIM_MS &&
          shape.dim != GLSL_SAMPLER_DIM_CUBE) {
         c->uav[MAX_UAVS + u].kind = MEM_TYPED;
         c->uav[MAX_UAVS + u].dim = (uint8_t)((insn->token0 >> 11) & 0x1fu);
         c->uav[MAX_UAVS + u].type = return_type(insn->payload[0]);
         return;
      }
      if (heap || u >= c->uav_slots || !insn->num_payload ||
          !tex_shape((insn->token0 >> 11) & 0x1fu, &shape) || shape.dim == GLSL_SAMPLER_DIM_MS ||
          shape.dim == GLSL_SAMPLER_DIM_CUBE) {
         fail(c, insn, "typed UAV declaration");
         return;
      }
      type = return_type(insn->payload[0]);
      c->uav[u].kind = MEM_TYPED;
      c->uav[u].dim = (uint8_t)((insn->token0 >> 11) & 0x1fu);
      c->uav[u].type = type;
      c->uav[u].image = nir_variable_create(b->shader, nir_var_image,
         glsl_image_type(shape.dim, shape.array, nir_get_glsl_base_type_for_nir_type(type)), "u");
      c->uav[u].image->data.binding = u;
      c->uav[u].image->data.image.format = PIPE_FORMAT_NONE;
      BITSET_SET(b->shader->info.images_used, u);
      b->shader->info.num_images = MAX2(b->shader->info.num_images, (uint8_t)(u + 1));
      return;
   }
   case DXBC_OP_DCL_UAV_RAW:
   case DXBC_OP_DCL_UAV_STRUCTURED: {
      unsigned u = o ? (unsigned)o->index[0].imm : MAX_UAVS;
      bool structured = insn->opcode == DXBC_OP_DCL_UAV_STRUCTURED;
      const bool heap = o && heap_operand(c, o); /* C47: a buffer image in the heap */
      rov_declaration(c, insn);
      if (u >= (heap ? DXBC_HEAP_RANGE_MAX : c->uav_slots) ||
          (structured && (!insn->num_payload || !insn->payload[0] || (insn->payload[0] & 3)))) {
         fail(c, insn, "buffer UAV declaration");
         return;
      }
      if (heap)
         u += MAX_UAVS;
      c->uav[u].kind = structured ? MEM_STRUCTURED : MEM_RAW;
      c->uav[u].stride = structured ? insn->payload[0] : 0;
      if (!heap)
         b->shader->info.num_ssbos = MAX2(b->shader->info.num_ssbos, (uint8_t)(u + 1));
      return;
   }
   case DXBC_OP_DCL_RESOURCE_RAW:
   case DXBC_OP_DCL_RESOURCE_STRUCTURED: {
      unsigned t = o ? (unsigned)o->index[0].imm : MAX_RESOURCES;
      bool structured = insn->opcode == DXBC_OP_DCL_RESOURCE_STRUCTURED;
      const bool heap = o && heap_operand(c, o); /* C47: an R32_UINT buffer texture in the heap */
      if (t >= (heap ? DXBC_HEAP_RANGE_MAX : MAX_RESOURCES) ||
          (structured && (!insn->num_payload || !insn->payload[0] || (insn->payload[0] & 3)))) {
         fail(c, insn, "buffer SRV declaration");
         return;
      }
      if (heap) {
         t += MAX_RESOURCES;
         c->res[t].declared = true;
         c->res[t].dim = DXBC_DIM_BUFFER;
         c->res[t].type = nir_type_uint32;
         c->res_kind[t] = structured ? MEM_STRUCTURED : MEM_RAW;
         c->res_stride[t] = structured ? insn->payload[0] : 0;
         return;
      }
      /* read as a 32-bit texel buffer view (one dword per texel) */
      for (unsigned k = 0, n = range_slots(c, o); k < n; ++k, ++t) {
         if (t >= MAX_RESOURCES) {
            fail(c, insn, "buffer SRV range declaration");
            return;
         }
         c->res[t].declared = true;
         c->res[t].dim = DXBC_DIM_BUFFER;
         c->res[t].type = nir_type_uint32;
         c->res_kind[t] = structured ? MEM_STRUCTURED : MEM_RAW;
         c->res_stride[t] = structured ? insn->payload[0] : 0;
         BITSET_SET(b->shader->info.textures_used, t);
         BITSET_SET(b->shader->info.textures_used_by_txf, t);
      }
      return;
   }
   default:
      fail(c, insn, "declaration %s", dxbc_opcode_name(insn->opcode));
      return;
   }
}

/* ---------------------------------------------------------------------
 * Output stores (end of program, before returns and emits)
 */

static void
store_outputs(struct ctx *c)
{
   nir_builder *b = &c->b;
   for (unsigned reg = 0; reg < MAX_REGS; ++reg) {
      nir_def *value;
      if (!c->outputs[reg] || !c->num_sinks[reg])
         continue;
      value = nir_load_reg(b, c->outputs[reg]);
      for (unsigned s = 0; s < c->num_sinks[reg]; ++s) {
         const struct output_sink *sink = &c->sinks[reg][s];
         if (sink->scalar) {
            nir_store_var(b, sink->var,
                          nir_channel(b, value, (unsigned)ffs(sink->mask) - 1), 1);
         } else {
            nir_store_var(b, sink->var, value, sink->mask);
         }
      }
   }
   if (c->depth_reg)
      nir_store_var(b, c->depth_var, nir_channel(b, nir_load_reg(b, c->depth_reg), 0), 1);
   if (c->mask_reg)
      nir_store_var(b, c->mask_var, nir_channel(b, nir_load_reg(b, c->mask_reg), 0), 1);
}

/* ---------------------------------------------------------------------
 * ALU
 */

static nir_def *
truth(struct ctx *c, nir_def *cond)
{
   return nir_bcsel(&c->b, cond, nir_imm_int(&c->b, -1), nir_imm_int(&c->b, 0));
}

static nir_def *
nonzero(struct ctx *c, nir_def *v)
{
   return nir_ine_imm(&c->b, nir_channel(&c->b, v, 0), 0);
}

#define SRC(i, t) read_src(c, insn, &insn->operands[(i)], (t))
#define F nir_type_float32
#define I nir_type_int32
#define U nir_type_uint32

static bool
emit_alu(struct ctx *c, const struct dxbc_instruction *insn)
{
   nir_builder *b = &c->b;
   nir_def *r = NULL, *s0, *s1, *s2;
   bool sat = insn->saturate;

   switch (insn->opcode) {
   /* float */
   case DXBC_OP_ADD: r = nir_fadd(b, SRC(1, F), SRC(2, F)); break;
   case DXBC_OP_MUL: r = nir_fmul(b, SRC(1, F), SRC(2, F)); break;
   case DXBC_OP_MAD: r = nir_ffma(b, SRC(1, F), SRC(2, F), SRC(3, F)); break;
   case DXBC_OP_DIV: r = nir_fdiv(b, SRC(1, F), SRC(2, F)); break;
   case DXBC_OP_MIN: r = nir_fmin(b, SRC(1, F), SRC(2, F)); break;
   case DXBC_OP_MAX: r = nir_fmax(b, SRC(1, F), SRC(2, F)); break;
   case DXBC_OP_DP2:
      r = replicate4(c, nir_fdot2(b, nir_trim_vector(b, SRC(1, F), 2),
                                  nir_trim_vector(b, SRC(2, F), 2)));
      break;
   case DXBC_OP_DP3:
      r = replicate4(c, nir_fdot3(b, nir_trim_vector(b, SRC(1, F), 3),
                                  nir_trim_vector(b, SRC(2, F), 3)));
      break;
   case DXBC_OP_DP4: r = replicate4(c, nir_fdot4(b, SRC(1, F), SRC(2, F))); break;
   case DXBC_OP_EXP: r = nir_fexp2(b, SRC(1, F)); break;
   case DXBC_OP_LOG: r = nir_flog2(b, SRC(1, F)); break;
   case DXBC_OP_FRC: r = nir_ffract(b, SRC(1, F)); break;
   case DXBC_OP_RSQ: r = nir_frsq(b, SRC(1, F)); break;
   case DXBC_OP_RCP: r = nir_frcp(b, SRC(1, F)); break;
   case DXBC_OP_SQRT: r = nir_fsqrt(b, SRC(1, F)); break;
   case DXBC_OP_ROUND_NE: r = nir_fround_even(b, SRC(1, F)); break;
   case DXBC_OP_ROUND_NI: r = nir_ffloor(b, SRC(1, F)); break;
   case DXBC_OP_ROUND_PI: r = nir_fceil(b, SRC(1, F)); break;
   case DXBC_OP_ROUND_Z: r = nir_ftrunc(b, SRC(1, F)); break;
   case DXBC_OP_MOV: r = SRC(1, insn->saturate ? F : U); break;
   /* SM 4 leaves the derivative precision to the implementation (TGSI
    * DDX/DDY -> generic ddx/ddy); SM 5 names it */
   case DXBC_OP_DERIV_RTX: r = nir_ddx(b, SRC(1, F)); break;
   case DXBC_OP_DERIV_RTY: r = nir_ddy(b, SRC(1, F)); break;
   case DXBC_OP_DERIV_RTX_COARSE: r = nir_ddx_coarse(b, SRC(1, F)); break;
   case DXBC_OP_DERIV_RTY_COARSE: r = nir_ddy_coarse(b, SRC(1, F)); break;
   case DXBC_OP_DERIV_RTX_FINE: r = nir_ddx_fine(b, SRC(1, F)); break;
   case DXBC_OP_DERIV_RTY_FINE: r = nir_ddy_fine(b, SRC(1, F)); break;
   /* comparisons: ~0 or 0 */
   case DXBC_OP_EQ: r = truth(c, nir_feq(b, SRC(1, F), SRC(2, F))); break;
   case DXBC_OP_NE: r = truth(c, nir_fneu(b, SRC(1, F), SRC(2, F))); break;
   case DXBC_OP_LT: r = truth(c, nir_flt(b, SRC(1, F), SRC(2, F))); break;
   case DXBC_OP_GE: r = truth(c, nir_fge(b, SRC(1, F), SRC(2, F))); break;
   case DXBC_OP_IEQ: r = truth(c, nir_ieq(b, SRC(1, I), SRC(2, I))); break;
   case DXBC_OP_INE: r = truth(c, nir_ine(b, SRC(1, I), SRC(2, I))); break;
   case DXBC_OP_ILT: r = truth(c, nir_ilt(b, SRC(1, I), SRC(2, I))); break;
   case DXBC_OP_IGE: r = truth(c, nir_ige(b, SRC(1, I), SRC(2, I))); break;
   case DXBC_OP_ULT: r = truth(c, nir_ult(b, SRC(1, U), SRC(2, U))); break;
   case DXBC_OP_UGE: r = truth(c, nir_uge(b, SRC(1, U), SRC(2, U))); break;
   /* integer */
   case DXBC_OP_IADD: r = nir_iadd(b, SRC(1, I), SRC(2, I)); break;
   case DXBC_OP_IMAD: r = nir_iadd(b, nir_imul(b, SRC(1, I), SRC(2, I)), SRC(3, I)); break;
   case DXBC_OP_UMAD: r = nir_iadd(b, nir_imul(b, SRC(1, U), SRC(2, U)), SRC(3, U)); break;
   case DXBC_OP_IMAX: r = nir_imax(b, SRC(1, I), SRC(2, I)); break;
   case DXBC_OP_IMIN: r = nir_imin(b, SRC(1, I), SRC(2, I)); break;
   case DXBC_OP_UMAX: r = nir_umax(b, SRC(1, U), SRC(2, U)); break;
   case DXBC_OP_UMIN: r = nir_umin(b, SRC(1, U), SRC(2, U)); break;
   case DXBC_OP_INEG: r = nir_ineg(b, SRC(1, I)); break;
   case DXBC_OP_NOT: r = nir_inot(b, SRC(1, U)); break;
   case DXBC_OP_AND: r = nir_iand(b, SRC(1, U), SRC(2, U)); break;
   case DXBC_OP_OR: r = nir_ior(b, SRC(1, U), SRC(2, U)); break;
   case DXBC_OP_XOR: r = nir_ixor(b, SRC(1, U), SRC(2, U)); break;
   case DXBC_OP_ISHL: r = nir_ishl(b, SRC(1, I), SRC(2, U)); break;
   case DXBC_OP_ISHR: r = nir_ishr(b, SRC(1, I), SRC(2, U)); break;
   case DXBC_OP_USHR: r = nir_ushr(b, SRC(1, U), SRC(2, U)); break;
   case DXBC_OP_ITOF: r = nir_i2f32(b, SRC(1, I)); break;
   case DXBC_OP_UTOF: r = nir_u2f32(b, SRC(1, U)); break;
   case DXBC_OP_FTOI: r = nir_f2i32(b, SRC(1, F)); break;
   case DXBC_OP_FTOU: r = nir_f2u32(b, SRC(1, F)); break;
   case DXBC_OP_MOVC:
      s0 = SRC(1, U);
      s1 = SRC(2, U);
      s2 = SRC(3, U);
      r = nir_bcsel(b, nir_ine_imm(b, s0, 0), s1, s2);
      break;
   case DXBC_OP_IMUL: case DXBC_OP_UMUL: {
      bool sign = insn->opcode == DXBC_OP_IMUL;
      s0 = SRC(2, sign ? I : U);
      s1 = SRC(3, sign ? I : U);
      write_dst(c, insn, &insn->operands[0],
                sign ? nir_imul_high(b, s0, s1) : nir_umul_high(b, s0, s1), false);
      write_dst(c, insn, &insn->operands[1], nir_imul(b, s0, s1), false);
      return true;
   }
   case DXBC_OP_UDIV:
      s0 = SRC(2, U);
      s1 = SRC(3, U);
      /* D3D: division by zero gives 0xffffffff for both results */
      write_dst(c, insn, &insn->operands[0],
                nir_bcsel(b, nir_ieq_imm(b, s1, 0), nir_imm_int(b, -1), nir_udiv(b, s0, s1)), false);
      write_dst(c, insn, &insn->operands[1],
                nir_bcsel(b, nir_ieq_imm(b, s1, 0), nir_imm_int(b, -1), nir_umod(b, s0, s1)), false);
      return true;
   /* shader model 5 bit operations */
   case DXBC_OP_COUNTBITS: r = nir_bit_count(b, SRC(1, U)); break;
   case DXBC_OP_FIRSTBIT_LO: r = nir_find_lsb(b, SRC(1, U)); break;
   case DXBC_OP_FIRSTBIT_HI:
   case DXBC_OP_FIRSTBIT_SHI: {
      /* D3D counts from the most significant bit; NIR from the least */
      nir_def *msb = insn->opcode == DXBC_OP_FIRSTBIT_HI ?
         nir_ufind_msb(b, SRC(1, U)) : nir_ifind_msb(b, SRC(1, I));
      r = nir_bcsel(b, nir_ilt_imm(b, msb, 0), nir_imm_int(b, -1), nir_isub_imm(b, 31, msb));
      break;
   }
   /* EXP1821: Asahi neither lowers nor emits nir_op_ubfe/ibfe/bfm/bfi (no
    * has_bfe/has_bfm/has_bfi; agx_compile "Unhandled ALU op"). These are the
    * D3D11 functional-spec definitions in shifts and masks: width and offset
    * are the low five bits, width 0 extracts 0, an extract reaching bit 31
    * is a plain shift, and the insert mask is truncated to 32 bits. */
   case DXBC_OP_UBFE:
   case DXBC_OP_IBFE: {
      nir_def *width = nir_iand_imm(b, SRC(1, U), 31), *offset = nir_iand_imm(b, SRC(2, U), 31);
      nir_def *value = SRC(3, U), *end = nir_iadd(b, width, offset);
      const unsigned n = value->num_components;
      const bool sign = insn->opcode == DXBC_OP_IBFE;
      nir_def *left = nir_ishl(b, value, nir_isub_imm(b, 32, end));
      nir_def *right = nir_isub_imm(b, 32, width);
      nir_def *inside = sign ? nir_ishr(b, left, right) : nir_ushr(b, left, right);
      nir_def *tail = sign ? nir_ishr(b, value, offset) : nir_ushr(b, value, offset);
      r = nir_bcsel(b, nir_ieq(b, width, nir_imm_zero(b, n, 32)), nir_imm_zero(b, n, 32),
                    nir_bcsel(b, nir_ult(b, end, nir_replicate(b, nir_imm_int(b, 32), n)), inside, tail));
      break;
   }
   case DXBC_OP_BFI: {
      nir_def *width = nir_iand_imm(b, SRC(1, U), 31), *offset = nir_iand_imm(b, SRC(2, U), 31);
      const unsigned n = width->num_components;
      nir_def *ones = nir_iadd_imm(b, nir_ishl(b, nir_replicate(b, nir_imm_int(b, 1), n), width), -1);
      nir_def *mask = nir_ishl(b, ones, offset);
      r = nir_ior(b, nir_iand(b, nir_ishl(b, SRC(3, U), offset), mask), nir_iand(b, SRC(4, U), nir_inot(b, mask)));
      break;
   }
   case DXBC_OP_BFREV: r = nir_bitfield_reverse(b, SRC(1, U)); break;
   case DXBC_OP_F32TOF16: {
      nir_def *v = SRC(1, F), *comp[4];
      for (unsigned i = 0; i < 4; ++i)
         comp[i] = nir_pack_half_2x16_split(b, nir_channel(b, v, i), nir_imm_float(b, 0));
      r = nir_vec(b, comp, 4);
      break;
   }
   case DXBC_OP_F16TOF32: {
      nir_def *v = SRC(1, U), *comp[4];
      for (unsigned i = 0; i < 4; ++i)
         comp[i] = nir_channel(b, nir_unpack_half_2x16(b, nir_channel(b, v, i)), 0);
      r = nir_vec(b, comp, 4);
      break;
   }
   case DXBC_OP_MSAD: {
      /* sum of |ref byte - src byte| over the reference's nonzero bytes */
      nir_def *ref = SRC(1, U), *src = SRC(2, U), *acc = SRC(3, U);
      for (unsigned i = 0; i < 4; ++i) {
         nir_def *rb = nir_ubfe_imm(b, ref, 8 * i, 8);
         nir_def *sb = nir_ubfe_imm(b, src, 8 * i, 8);
         nir_def *d = nir_iabs(b, nir_isub(b, rb, sb));
         acc = nir_iadd(b, acc, nir_bcsel(b, nir_ieq_imm(b, rb, 0), nir_imm_int(b, 0), d));
      }
      r = acc;
      break;
   }
   case DXBC_OP_UADDC: case DXBC_OP_USUBB: {
      bool add = insn->opcode == DXBC_OP_UADDC;
      s0 = SRC(2, U);
      s1 = SRC(3, U);
      write_dst(c, insn, &insn->operands[0], add ? nir_iadd(b, s0, s1) : nir_isub(b, s0, s1), false);
      write_dst(c, insn, &insn->operands[1], add ? nir_uadd_carry(b, s0, s1) : nir_usub_borrow(b, s0, s1), false);
      return true;
   }
   case DXBC_OP_SWAPC:
      s0 = nir_ine_imm(b, SRC(2, U), 0);
      s1 = SRC(3, U);
      s2 = SRC(4, U);
      write_dst(c, insn, &insn->operands[0], nir_bcsel(b, s0, s2, s1), false);
      write_dst(c, insn, &insn->operands[1], nir_bcsel(b, s0, s1, s2), false);
      return true;
   case DXBC_OP_SINCOS:
      s0 = SRC(2, F);
      write_dst(c, insn, &insn->operands[0], nir_fsin(b, s0), sat);
      write_dst(c, insn, &insn->operands[1], nir_fcos(b, s0), sat);
      return true;
   default:
      return false;
   }
   write_dst(c, insn, &insn->operands[0], r, sat);
   return true;
}

/* ---------------------------------------------------------------------
 * Control flow
 */

static struct cf_entry *
cf_push(struct ctx *c, const struct dxbc_instruction *insn, enum cf_kind kind)
{
   struct cf_entry *e;
   if (c->cf_depth == MAX_CF) {
      fail(c, insn, "control flow nesting");
      return NULL;
   }
   e = &c->cf[c->cf_depth++];
   memset(e, 0, sizeof(*e));
   e->kind = kind;
   return e;
}

static struct cf_entry *
cf_top(struct ctx *c, const struct dxbc_instruction *insn, enum cf_kind kind)
{
   if (!c->cf_depth || c->cf[c->cf_depth - 1].kind != kind) {
      fail(c, insn, "unbalanced control flow");
      return NULL;
   }
   return &c->cf[c->cf_depth - 1];
}

/* the innermost loop or switch */
static struct cf_entry *
cf_breakable(struct ctx *c)
{
   for (unsigned i = c->cf_depth; i-- > 0;)
      if (c->cf[i].kind != CF_IF)
         return &c->cf[i];
   return NULL;
}

/* A jump ends its NIR block; DXBC may place code after it, so every jump
 * sits in its own if (true) and later instructions start a new block. */
static void
jump(struct ctx *c, nir_jump_type type)
{
   nir_if *nif = nir_push_if(&c->b, nir_imm_true(&c->b));
   nir_jump(&c->b, type);
   nir_pop_if(&c->b, nif);
}

static void
emit_break(struct ctx *c, const struct dxbc_instruction *insn)
{
   if (!cf_breakable(c)) {
      fail(c, insn, "break outside a loop or switch");
      return;
   }
   jump(c, nir_jump_break);
}

static void
emit_continue(struct ctx *c, const struct dxbc_instruction *insn)
{
   /* inside a switch, flag the enclosing loop and leave the switch loop */
   for (unsigned i = c->cf_depth; i-- > 0;) {
      if (c->cf[i].kind == CF_LOOP) {
         jump(c, nir_jump_continue);
         return;
      }
      if (c->cf[i].kind == CF_SWITCH) {
         if (!c->cf[i].continue_outer)
            break;
         nir_store_var(&c->b, c->cf[i].continue_outer, nir_imm_true(&c->b), 1);
         jump(c, nir_jump_break);
         return;
      }
   }
   fail(c, insn, "continue outside a loop");
}

static void
emit_return(struct ctx *c)
{
   if (c->stage == MESA_SHADER_TESS_CTRL) {
      /* a return ends the phase (its function) */
      if (c->hs_phase == HS_PHASE_CP)
         hs_store_cp_outputs(c);
   } else if (c->stage != MESA_SHADER_GEOMETRY) {
      store_outputs(c);
   }
   jump(c, nir_jump_return);
}

static bool
inside_loop(struct ctx *c)
{
   for (unsigned i = 0; i < c->cf_depth; ++i)
      if (c->cf[i].kind == CF_LOOP)
         return true;
   return false;
}

/* case values of the switch starting after instruction 'start', to the
 * matching endswitch (nested switches skipped) */
static nir_def *
switch_any_case(struct ctx *c, unsigned start, nir_def *selector)
{
   nir_def *any = nir_imm_false(&c->b);
   unsigned depth = 0;
   for (unsigned i = start + 1; i < c->p->num_instructions; ++i) {
      const struct dxbc_instruction *insn = &c->p->instructions[i];
      if (insn->opcode == DXBC_OP_SWITCH) {
         ++depth;
      } else if (insn->opcode == DXBC_OP_ENDSWITCH) {
         if (!depth--)
            break;
      } else if (insn->opcode == DXBC_OP_CASE && !depth) {
         nir_def *v = nir_channel(&c->b, read_src(c, insn, &insn->operands[0], U), 0);
         any = nir_ior(&c->b, any, nir_ieq(&c->b, selector, v));
      }
   }
   return any;
}

static void
close_case(struct ctx *c, struct cf_entry *e)
{
   if (e->case_open) {
      nir_pop_if(&c->b, e->case_if);
      e->case_open = false;
   }
}

/* A run of consecutive case/default labels opens one case group. */
static void
open_case(struct ctx *c, unsigned index, struct cf_entry *e)
{
   nir_builder *b = &c->b;
   nir_def *cond = nir_load_var(b, e->fallthrough);
   unsigned i = index;
   for (; i < c->p->num_instructions; ++i) {
      const struct dxbc_instruction *insn = &c->p->instructions[i];
      if (insn->opcode == DXBC_OP_CASE) {
         nir_def *v = nir_channel(b, read_src(c, insn, &insn->operands[0], U), 0);
         cond = nir_ior(b, cond, nir_ieq(b, e->selector, v));
      } else if (insn->opcode == DXBC_OP_DEFAULT) {
         cond = nir_ior(b, cond, nir_inot(b, e->any_case));
      } else {
         break;
      }
   }
   e->case_if = nir_push_if(b, cond);
   e->case_open = true;
   nir_store_var(b, e->fallthrough, nir_imm_true(b), 1);
}

static bool
emit_flow(struct ctx *c, unsigned index)
{
   const struct dxbc_instruction *insn = &c->p->instructions[index];
   nir_builder *b = &c->b;
   struct cf_entry *e;
   nir_def *cond;

   switch (insn->opcode) {
   case DXBC_OP_IF:
      cond = nonzero(c, SRC(0, U));
      if (!insn->test_nonzero)
         cond = nir_inot(b, cond);
      if ((e = cf_push(c, insn, CF_IF)))
         e->nif = nir_push_if(b, cond);
      return true;
   case DXBC_OP_ELSE:
      if ((e = cf_top(c, insn, CF_IF))) {
         nir_push_else(b, e->nif);
         e->in_else = true;
      }
      return true;
   case DXBC_OP_ENDIF:
      if ((e = cf_top(c, insn, CF_IF))) {
         nir_pop_if(b, e->nif);
         --c->cf_depth;
      }
      return true;
   case DXBC_OP_LOOP:
      /* a NIR continue jumps to the loop's continue construct (as in
       * tgsi_to_nir); nir_lower_continue_constructs removes it later */
      if ((e = cf_push(c, insn, CF_LOOP))) {
         e->loop = nir_push_loop(b);
         nir_loop_add_continue_construct(e->loop);
      }
      return true;
   case DXBC_OP_ENDLOOP:
      if ((e = cf_top(c, insn, CF_LOOP))) {
         nir_pop_loop(b, e->loop);
         --c->cf_depth;
      }
      return true;
   case DXBC_OP_BREAK:
      emit_break(c, insn);
      return true;
   case DXBC_OP_CONTINUE:
      emit_continue(c, insn);
      return true;
   case DXBC_OP_BREAKC:
   case DXBC_OP_CONTINUEC:
   case DXBC_OP_RETC: {
      nir_if *nif;
      cond = nonzero(c, SRC(0, U));
      if (!insn->test_nonzero)
         cond = nir_inot(b, cond);
      nif = nir_push_if(b, cond);
      if (insn->opcode == DXBC_OP_BREAKC)
         emit_break(c, insn);
      else if (insn->opcode == DXBC_OP_CONTINUEC)
         emit_continue(c, insn);
      else
         emit_return(c);
      nir_pop_if(b, nif);
      return true;
   }
   case DXBC_OP_RET:
      if (!c->cf_depth) {
         c->main_done = true; /* outputs are stored after the loop */
         return true;
      }
      emit_return(c);
      return true;
   case DXBC_OP_DISCARD:
      cond = nonzero(c, SRC(0, U));
      if (!insn->test_nonzero)
         cond = nir_inot(b, cond);
      nir_demote_if(b, cond);
      return true;
   case DXBC_OP_SWITCH:
      if ((e = cf_push(c, insn, CF_SWITCH))) {
         e->selector = nir_channel(b, SRC(0, U), 0);
         e->fallthrough = nir_local_variable_create(b->impl, glsl_bool_type(), "fallthrough");
         nir_store_var(b, e->fallthrough, nir_imm_false(b), 1);
         if (inside_loop(c)) {
            e->continue_outer = nir_local_variable_create(b->impl, glsl_bool_type(), "cont");
            nir_store_var(b, e->continue_outer, nir_imm_false(b), 1);
         }
         e->any_case = switch_any_case(c, index, e->selector);
         e->loop = nir_push_loop(b);
      }
      return true;
   case DXBC_OP_CASE:
   case DXBC_OP_DEFAULT:
      if ((e = cf_top(c, insn, CF_SWITCH))) {
         /* only the first label of a run opens the group */
         const struct dxbc_instruction *prev = index ? &c->p->instructions[index - 1] : NULL;
         if (prev && (prev->opcode == DXBC_OP_CASE || prev->opcode == DXBC_OP_DEFAULT) &&
             e->case_open)
            return true;
         close_case(c, e);
         open_case(c, index, e);
      }
      return true;
   case DXBC_OP_ENDSWITCH:
      if ((e = cf_top(c, insn, CF_SWITCH))) {
         nir_variable *cont = e->continue_outer;
         close_case(c, e);
         jump(c, nir_jump_break);
         nir_pop_loop(b, e->loop);
         --c->cf_depth;
         if (cont) {
            nir_if *nif = nir_push_if(b, nir_load_var(b, cont));
            emit_continue(c, insn);
            nir_pop_if(b, nif);
         }
      }
      return true;
   default:
      return false;
   }
}

/* ---------------------------------------------------------------------
 * Resources
 */

static bool
tex_shape(unsigned dim, struct tex_shape *s)
{
   memset(s, 0, sizeof(*s));
   switch (dim) {
   case DXBC_DIM_BUFFER: s->dim = GLSL_SAMPLER_DIM_BUF; s->coords = 1; return true;
   case DXBC_DIM_TEXTURE1D: s->dim = GLSL_SAMPLER_DIM_1D; s->coords = 1; return true;
   case DXBC_DIM_TEXTURE1DARRAY: s->dim = GLSL_SAMPLER_DIM_1D; s->array = true; s->coords = 2; return true;
   case DXBC_DIM_TEXTURE2D: s->dim = GLSL_SAMPLER_DIM_2D; s->coords = 2; return true;
   case DXBC_DIM_TEXTURE2DARRAY: s->dim = GLSL_SAMPLER_DIM_2D; s->array = true; s->coords = 3; return true;
   case DXBC_DIM_TEXTURE2DMS: s->dim = GLSL_SAMPLER_DIM_MS; s->coords = 2; return true;
   case DXBC_DIM_TEXTURE2DMSARRAY: s->dim = GLSL_SAMPLER_DIM_MS; s->array = true; s->coords = 3; return true;
   case DXBC_DIM_TEXTURE3D: s->dim = GLSL_SAMPLER_DIM_3D; s->coords = 3; return true;
   case DXBC_DIM_TEXTURECUBE: s->dim = GLSL_SAMPLER_DIM_CUBE; s->coords = 3; return true;
   case DXBC_DIM_TEXTURECUBEARRAY: s->dim = GLSL_SAMPLER_DIM_CUBE; s->array = true; s->coords = 4; return true;
   default: return false;
   }
}

/* C34: the register part of an SM5.1 range index (the slot's immediate
 * part is the texture/sampler index) */
static nir_def *
range_register(struct ctx *c, const struct dxbc_instruction *insn, const struct dxbc_operand *o)
{
   if (index_direct(&o->index[0]))
      return NULL;
   return nir_channel(&c->b, read_src(c, insn, o->index[0].relative, nir_type_int32), 0);
}

static unsigned
resource_index(struct ctx *c, const struct dxbc_instruction *insn,
               const struct dxbc_operand *o)
{
   if (o->type == DXBC_OPERAND_RESOURCE && heap_operand(c, o)) {
      /* C47: [heap range][register offset] */
      const unsigned t = MAX_RESOURCES + (unsigned)o->index[0].imm;
      if (o->index[0].imm >= DXBC_HEAP_RANGE_MAX || !c->res[t].declared || c->heap_offset ||
          c->dyn_texture) {
         fail(c, insn, "heap resource operand");
         return 0;
      }
      c->heap_offset = index_value(c, insn, &o->index[1]);
      c->heap_range = (unsigned)o->index[0].imm;
      return t;
   }
   if (o->type != DXBC_OPERAND_RESOURCE || o->index_dimension != 1 ||
       o->index[0].imm >= MAX_RESOURCES || !c->res[o->index[0].imm].declared ||
       (!index_direct(&o->index[0]) && c->dyn_texture)) {
      fail(c, insn, "resource operand");
      return 0;
   }
   c->dyn_texture = range_register(c, insn, o);
   return (unsigned)o->index[0].imm;
}

static unsigned
sampler_index(struct ctx *c, const struct dxbc_instruction *insn,
              const struct dxbc_operand *o)
{
   if (o->type == DXBC_OPERAND_SAMPLER && heap_operand(c, o)) {
      /* C47: [heap sampler range][register offset] */
      const unsigned s = MAX_SAMPLERS + (unsigned)o->index[0].imm;
      if (o->index[0].imm >= DXBC_HEAP_RANGE_MAX || !c->samp[s].declared || c->sampler_heap_offset || c->dyn_sampler) {
         fail(c, insn, "heap sampler operand");
         return 0;
      }
      c->sampler_heap_offset = index_value(c, insn, &o->index[1]);
      c->sampler_heap_range = (unsigned)o->index[0].imm;
      return s;
   }
   if (o->type != DXBC_OPERAND_SAMPLER || o->index_dimension != 1 ||
       o->index[0].imm >= MAX_SAMPLERS || (!index_direct(&o->index[0]) && c->dyn_sampler)) {
      fail(c, insn, "sampler operand");
      return 0;
   }
   c->dyn_sampler = range_register(c, insn, o);
   return (unsigned)o->index[0].imm;
}

/* C47: the heap index of heap range h's first register (constant buffer
 * DXBC_HEAP_TABLE_CB, vec4 per range) */
static nir_def *
heap_first(struct ctx *c, unsigned h)
{
   return nir_channel(&c->b, nir_load_ubo(&c->b, 4, 32, nir_imm_int(&c->b, DXBC_HEAP_TABLE_CB),
                                          nir_imm_int(&c->b, (int)(h * 16u)), .align_mul = 16,
                                          .align_offset = 0, .range_base = 0, .range = ~0u), 0);
}

/* C47: the bindless handle of a heap descriptor's texture (or, pbe, its PBE)
 * descriptor; the native state pairs the marked handle with the heap */
static nir_def *
heap_handle_at(struct ctx *c, nir_def *index, unsigned delta)
{
   nir_def *bytes = nir_iadd_imm(&c->b, nir_imul_imm(&c->b, index, DXBC_HEAP_DESCRIPTOR_STRIDE),
                                 DXBC_HEAP_DESCRIPTOR_AGX_OFFSET + delta);
   return nir_bindless_image_agx(&c->b, bytes, .desc_set = DXBC_HEAP_HANDLE_MARK);
}

static nir_def *
heap_handle(struct ctx *c, nir_def *index, bool pbe)
{
   return heap_handle_at(c, index, pbe ? DXBC_HEAP_DESCRIPTOR_PBE_DELTA : 0);
}

/* C47: the counter of a heap structured UAV (its R32_UINT word's PBE) */
static nir_def *
uav_heap_counter(struct ctx *c, const struct dxbc_instruction *insn, const struct dxbc_operand *o)
{
   return heap_handle_at(c, nir_iadd(&c->b, heap_first(c, (unsigned)o->index[0].imm), index_value(c, insn, &o->index[1])),
                         DXBC_HEAP_DESCRIPTOR_COUNTER_DELTA);
}


/* C47: a UAV operand's entry -- its slot, or MAX_UAVS + heap range for a heap
 * UAV ([range][register offset], dxbc_prepare_static_bindings) -- without
 * emitting code; MAX_UAVS + DXBC_HEAP_RANGE_MAX when malformed */
static unsigned
uav_entry(const struct ctx *c, const struct dxbc_operand *o)
{
   const unsigned none = MAX_UAVS + DXBC_HEAP_RANGE_MAX;
   if (o->type != DXBC_OPERAND_UNORDERED_ACCESS_VIEW || !index_direct(&o->index[0]))
      return none;
   if (heap_operand(c, o))
      return o->index[0].imm < DXBC_HEAP_RANGE_MAX && c->uav[MAX_UAVS + o->index[0].imm].kind
                ? MAX_UAVS + (unsigned)o->index[0].imm : none;
   return o->index[0].imm < c->uav_slots ? (unsigned)o->index[0].imm : none;
}

/* C47: a heap UAV's descriptor handle (texture for reads and size queries,
 * PBE for writes and atomics) */
static nir_def *
uav_heap_handle(struct ctx *c, const struct dxbc_instruction *insn, const struct dxbc_operand *o, bool pbe)
{
   return heap_handle(c, nir_iadd(&c->b, heap_first(c, (unsigned)o->index[0].imm), index_value(c, insn, &o->index[1])),
                      pbe);
}

static nir_tex_instr *
tex_begin(struct ctx *c, nir_texop op, unsigned t, unsigned s, bool sampler,
          unsigned num_srcs, const struct tex_shape *shape, nir_alu_type type)
{
   /* C34: room for texture_offset/sampler_offset (tex_finish); C47: or
    * texture/sampler handles and a sampler bias; C53T: and a min_lod */
   nir_tex_instr *tex = nir_tex_instr_create(c->b.shader, num_srcs + 4);
   tex->op = op;
   tex->sampler_dim = shape->dim;
   tex->is_array = shape->array;
   tex->texture_index = t;
   tex->sampler_index = sampler ? s : 0;
   tex->dest_type = type;
   tex->coord_components = 0;
   (void)s;
   return tex;
}

/* C47: a heap sampler: its AGX sampler handle and MipLODBias from the map
 * in DXBC_HEAP_TABLE_CB; the bias applies to implicit-LOD sampling (tex
 * becomes txb, txb adds it) and to gradients (scaled by 2^bias) */
static unsigned
heap_sampler(struct ctx *c, nir_tex_instr *tex, unsigned num_srcs, nir_def **minmax)
{
   nir_builder *b = &c->b;
   nir_def *range = nir_load_ubo(b, 4, 32, nir_imm_int(b, DXBC_HEAP_TABLE_CB),
                                 nir_imm_int(b, (int)(c->sampler_heap_range * 16u)), .align_mul = 16,
                                 .align_offset = 0, .range_base = 0, .range = ~0u);
   nir_def *entry = nir_isub(b, nir_iadd(b, nir_channel(b, range, 0), c->sampler_heap_offset), nir_channel(b, range, 1));
   nir_def *word = nir_load_ubo(b, 1, 32, nir_imm_int(b, DXBC_HEAP_TABLE_CB),
                                nir_iadd_imm(b, nir_ishl_imm(b, entry, 2), DXBC_HEAP_SAMPLER_MAP_OFFSET),
                                .align_mul = 4, .align_offset = 0, .range_base = 0, .range = ~0u);
   nir_def *bias = nir_f2f32(b, nir_u2u16(b, nir_ushr_imm(b, word, 16))); /* fp16 bits */
   /* C65: handle bits 0-10; the min/max bits 11-15 select the reduction
    * emulation after the sample (dxbc_minmax.h) */
   tex->src[num_srcs++] = nir_tex_src_for_ssa(nir_tex_src_sampler_handle,
                                              nir_u2u16(b, nir_iand_imm(b, word, DXBC_SAMPLER_HANDLE_MASK)));
   *minmax = nir_iand_imm(b, nir_ushr_imm(b, word, DXBC_SAMPLER_MINMAX_SHIFT), 31);
   tex->sampler_index = DXBC_HEAP_SAMPLER_INDEX;
   if (tex->op == nir_texop_tex) {
      tex->op = nir_texop_txb;
      tex->src[num_srcs++] = nir_tex_src_for_ssa(nir_tex_src_bias, bias);
   } else if (tex->op == nir_texop_txb) {
      const int i = nir_tex_instr_src_index(tex, nir_tex_src_bias);
      if (i >= 0) {
         nir_def *sum = nir_fadd(b, tex->src[i].src.ssa, bias);
         tex->src[i] = nir_tex_src_for_ssa(nir_tex_src_bias, sum);
      }
   } else if (tex->op == nir_texop_txd) {
      nir_def *scale = nir_fexp2(b, bias);
      for (unsigned i = 0; i < num_srcs; ++i)
         if (tex->src[i].src_type == nir_tex_src_ddx || tex->src[i].src_type == nir_tex_src_ddy)
            tex->src[i] = nir_tex_src_for_ssa(tex->src[i].src_type, nir_fmul(b, tex->src[i].src.ssa, scale));
   }
   return num_srcs;
}

static nir_def *
tex_finish(struct ctx *c, nir_tex_instr *tex, unsigned num_srcs, unsigned components)
{
   /* C34: dynamically indexed SM5.1 ranges, in the two slots tex_begin
    * reserved */
   if (c->dyn_texture)
      tex->src[num_srcs++] = nir_tex_src_for_ssa(nir_tex_src_texture_offset, c->dyn_texture);
   if (c->heap_offset) {
      /* C47: heap index = the range's first heap index (constant buffer
       * DXBC_HEAP_TABLE_CB, vec4 per range) + the register offset; the
       * descriptor's byte offset in the heap mirror is a bindless handle
       * the native state pairs with the mirror's address */
      nir_def *handle = heap_handle(c, nir_iadd(&c->b, heap_first(c, c->heap_range), c->heap_offset), false);
      tex->src[num_srcs++] = nir_tex_src_for_ssa(nir_tex_src_texture_handle, handle);
      tex->texture_index = 0;
   }
   if (c->dyn_sampler)
      tex->src[num_srcs++] = nir_tex_src_for_ssa(nir_tex_src_sampler_offset, c->dyn_sampler);
   nir_def *minmax = NULL;
   if (c->sampler_heap_offset)
      num_srcs = heap_sampler(c, tex, num_srcs, &minmax);
   if (c->min_lod) {
      /* C53T: the _cl clamp. AGX takes a min_lod with implicit LOD and
       * gradients; an explicit LOD is clamped here */
      const int l = nir_tex_instr_src_index(tex, nir_tex_src_lod);
      if (tex->op == nir_texop_txl && l >= 0)
         tex->src[l] = nir_tex_src_for_ssa(nir_tex_src_lod, nir_fmax(&c->b, tex->src[l].src.ssa, c->min_lod));
      else if (tex->op == nir_texop_tex || tex->op == nir_texop_txb || tex->op == nir_texop_txd)
         tex->src[num_srcs++] = nir_tex_src_for_ssa(nir_tex_src_min_lod, c->min_lod);
   }
   /* C53T: the residency code follows the result */
   const bool sparse = c->feedback && tex->op != nir_texop_txs && tex->op != nir_texop_query_levels &&
                       tex->op != nir_texop_lod;
   tex->is_sparse = sparse;
   c->dyn_texture = c->dyn_sampler = c->heap_offset = c->sampler_heap_offset = c->min_lod = NULL;
   tex->num_srcs = num_srcs;
   nir_def_init(&tex->instr, &tex->def, components + (sparse ? 1 : 0), 32);
   nir_builder_instr_insert(&c->b, &tex->instr);
   if (sparse) {
      c->feedback_code = nir_channel(&c->b, &tex->def, components);
      return nir_trim_vector(&c->b, &tex->def, components);
   }
   /* C65: a heap sampler's min/max reduction, when the caller asked for it */
   nir_def *reduced = minmax && c->p->minmax ? dxbc_minmax_wrap(&c->b, tex, minmax) : NULL;
   return reduced ? reduced : &tex->def;
}

static nir_def *
offset_src(struct ctx *c, const struct dxbc_instruction *insn, unsigned n)
{
   return nir_trim_vector(&c->b, nir_imm_ivec4(&c->b, insn->texel_offset[0],
                          insn->texel_offset[1], insn->texel_offset[2], 0), n);
}

static bool
has_offset(const struct dxbc_instruction *insn)
{
   return insn->texel_offset[0] || insn->texel_offset[1] || insn->texel_offset[2];
}

static unsigned
offset_components(const struct tex_shape *s)
{
   return s->dim == GLSL_SAMPLER_DIM_3D ? 3 : s->dim == GLSL_SAMPLER_DIM_1D ? 1 : 2;
}

/* the result swizzled by the resource operand */
static nir_def *
resource_swizzle(struct ctx *c, nir_def *v, const struct dxbc_operand *res)
{
   return swizzle4(c, vec4_of(c, v), res);
}

static bool
emit_sample(struct ctx *c, const struct dxbc_instruction *insn)
{
   nir_builder *b = &c->b;
   const struct dxbc_operand *res = &insn->operands[2];
   unsigned t = resource_index(c, insn, res), s = sampler_index(c, insn, &insn->operands[3]);
   struct tex_shape shape;
   nir_tex_instr *tex;
   unsigned n = 0;
   nir_texop op = nir_texop_tex;
   bool shadow = false;
   nir_def *coord;

   if (c->failed || !tex_shape(c->res[t].dim, &shape)) {
      fail(c, insn, "sample resource dimension");
      return true;
   }
   switch (insn->opcode) {
   case DXBC_OP_SAMPLE: op = c->stage == MESA_SHADER_FRAGMENT ? nir_texop_tex : nir_texop_txl; break;
   case DXBC_OP_SAMPLE_B: op = nir_texop_txb; break;
   case DXBC_OP_SAMPLE_L: op = nir_texop_txl; break;
   case DXBC_OP_SAMPLE_D: op = nir_texop_txd; break;
   case DXBC_OP_SAMPLE_C: op = c->stage == MESA_SHADER_FRAGMENT ? nir_texop_tex : nir_texop_txl;
      shadow = true; break;
   case DXBC_OP_SAMPLE_C_LZ: op = nir_texop_txl; shadow = true; break;
   default: return false;
   }
   tex = tex_begin(c, op, t, s, true, 6, &shape, shadow ? nir_type_float32 : c->res[t].type);
   tex->is_shadow = shadow;
   tex->is_new_style_shadow = shadow;
   coord = nir_trim_vector(b, SRC(1, F), shape.coords);
   tex->coord_components = shape.coords;
   tex->src[n++] = nir_tex_src_for_ssa(nir_tex_src_coord, coord);
   if (insn->opcode == DXBC_OP_SAMPLE_B)
      tex->src[n++] = nir_tex_src_for_ssa(nir_tex_src_bias, nir_channel(b, SRC(4, F), 0));
   if (insn->opcode == DXBC_OP_SAMPLE_L)
      tex->src[n++] = nir_tex_src_for_ssa(nir_tex_src_lod, nir_channel(b, SRC(4, F), 0));
   if (op == nir_texop_txl && insn->opcode != DXBC_OP_SAMPLE_L)
      tex->src[n++] = nir_tex_src_for_ssa(nir_tex_src_lod, nir_imm_float(b, 0));
   if (insn->opcode == DXBC_OP_SAMPLE_D) {
      unsigned d = shape.dim == GLSL_SAMPLER_DIM_1D ? 1 : shape.dim == GLSL_SAMPLER_DIM_2D ? 2 : 3;
      tex->src[n++] = nir_tex_src_for_ssa(nir_tex_src_ddx, nir_trim_vector(b, SRC(4, F), d));
      tex->src[n++] = nir_tex_src_for_ssa(nir_tex_src_ddy, nir_trim_vector(b, SRC(5, F), d));
   }
   if (shadow)
      tex->src[n++] = nir_tex_src_for_ssa(nir_tex_src_comparator, nir_channel(b, SRC(4, F), 0));
   if (has_offset(insn))
      tex->src[n++] = nir_tex_src_for_ssa(nir_tex_src_offset,
                                          offset_src(c, insn, offset_components(&shape)));
   if (shadow) {
      nir_def *v = tex_finish(c, tex, n, 1);
      write_dst(c, insn, &insn->operands[0], replicate4(c, v), insn->saturate);
   } else {
      write_dst(c, insn, &insn->operands[0],
                resource_swizzle(c, tex_finish(c, tex, n, 4), res), insn->saturate);
   }
   return true;
}

static void
emit_ld(struct ctx *c, const struct dxbc_instruction *insn)
{
   nir_builder *b = &c->b;
   const struct dxbc_operand *res = &insn->operands[2];
   unsigned t = resource_index(c, insn, res), n = 0;
   struct tex_shape shape;
   nir_tex_instr *tex;
   nir_def *address;
   bool ms = insn->opcode == DXBC_OP_LD_MS;

   if (c->failed || !tex_shape(c->res[t].dim, &shape)) {
      fail(c, insn, "ld resource dimension");
      return;
   }
   BITSET_SET(b->shader->info.textures_used_by_txf, t);
   tex = tex_begin(c, ms ? nir_texop_txf_ms : nir_texop_txf, t, 0, false, 4,
                   &shape, c->res[t].type);
   address = SRC(1, I);
   tex->coord_components = shape.coords;
   tex->src[n++] = nir_tex_src_for_ssa(nir_tex_src_coord, nir_trim_vector(b, address, shape.coords));
   if (ms)
      tex->src[n++] = nir_tex_src_for_ssa(nir_tex_src_ms_index, nir_channel(b, SRC(3, I), 0));
   else if (shape.dim != GLSL_SAMPLER_DIM_BUF)
      tex->src[n++] = nir_tex_src_for_ssa(nir_tex_src_lod, nir_channel(b, address, 3));
   if (has_offset(insn))
      tex->src[n++] = nir_tex_src_for_ssa(nir_tex_src_offset,
                                          offset_src(c, insn, offset_components(&shape)));
   write_dst(c, insn, &insn->operands[0], resource_swizzle(c, tex_finish(c, tex, n, 4), res),
             insn->saturate);
}

static nir_def *uav_image(struct ctx *c, const struct dxbc_instruction *insn,
                          const struct dxbc_operand *o, struct tex_shape *shape, bool pbe, bool *bindless);

/* resinfo's return type: float, rcpfloat (dimensions only) or uint */
static nir_def *
resinfo_convert(struct ctx *c, nir_def *value, unsigned mode)
{
   nir_builder *b = &c->b;
   if (mode != 2) {
      nir_def *f = nir_u2f32(b, value);
      if (mode == 1) {
         nir_def *r[4];
         for (unsigned i = 0; i < 4; ++i)
            r[i] = i < 3 ? nir_frcp(b, nir_channel(b, f, i)) : nir_channel(b, f, i);
         f = nir_vec(b, r, 4);
      }
      value = f;
   }
   return value;
}

/* resinfo on a typed UAV texture: the view's single mip level, so the
 * mip count is 1 and any other srcMipLevel reads as unbound (zero sizes) */
static void
emit_resinfo_uav(struct ctx *c, const struct dxbc_instruction *insn, unsigned mode)
{
   nir_builder *b = &c->b;
   const struct dxbc_operand *uav = &insn->operands[2];
   struct tex_shape shape;
   bool bindless;
   nir_def *image = uav_image(c, insn, uav, &shape, false, &bindless), *size, *in_range, *comp[4];
   if (!image)
      return;
   if (shape.dim == GLSL_SAMPLER_DIM_BUF) {
      fail(c, insn, "resinfo on a buffer UAV");
      return;
   }
   size = bindless ? nir_bindless_image_size(b, shape.coords, 32, image, nir_imm_int(b, 0),
                                             .image_dim = shape.dim, .image_array = shape.array)
                   : nir_image_size(b, shape.coords, 32, image, nir_imm_int(b, 0),
                                    .image_dim = shape.dim, .image_array = shape.array);
   in_range = nir_ieq_imm(b, nir_channel(b, SRC(1, U), 0), 0);
   for (unsigned i = 0; i < 3; ++i)
      comp[i] = i < shape.coords ? nir_bcsel(b, in_range, nir_channel(b, size, i), nir_imm_int(b, 0))
                                 : nir_imm_int(b, 0);
   comp[3] = nir_imm_int(b, 1);
   write_dst(c, insn, &insn->operands[0],
             swizzle4(c, resinfo_convert(c, nir_vec(b, comp, 4), mode), uav), insn->saturate);
}

static void
emit_resinfo(struct ctx *c, const struct dxbc_instruction *insn)
{
   nir_builder *b = &c->b;
   const struct dxbc_operand *res = &insn->operands[2];
   unsigned t, mode = (insn->token0 >> 11) & 3u;
   struct tex_shape shape;
   nir_tex_instr *tex;
   nir_def *lod, *size, *levels, *comp[4], *value;
   unsigned dims;

   if (res->type == DXBC_OPERAND_UNORDERED_ACCESS_VIEW) {
      emit_resinfo_uav(c, insn, mode);
      return;
   }
   t = resource_index(c, insn, res);
   if (c->failed || !tex_shape(c->res[t].dim, &shape)) {
      fail(c, insn, "resinfo resource dimension");
      return;
   }
   lod = nir_channel(b, SRC(1, U), 0);
   dims = shape.coords - (shape.dim == GLSL_SAMPLER_DIM_CUBE ? 1 : 0);
   if (shape.dim == GLSL_SAMPLER_DIM_CUBE)
      dims = shape.array ? 3 : 2;
   tex = tex_begin(c, nir_texop_txs, t, 0, false, 1, &shape, nir_type_uint32);
   tex->src[0] = nir_tex_src_for_ssa(nir_tex_src_lod, lod);
   size = tex_finish(c, tex, 1, dims);
   tex = tex_begin(c, nir_texop_query_levels, t, 0, false, 0, &shape, nir_type_uint32);
   levels = tex_finish(c, tex, 0, 1);
   for (unsigned i = 0; i < 3; ++i)
      comp[i] = i < dims ? nir_channel(b, size, i) : nir_imm_int(b, 0);
   comp[3] = levels;
   value = resinfo_convert(c, nir_vec(b, comp, 4), mode);
   write_dst(c, insn, &insn->operands[0], resource_swizzle(c, value, res), insn->saturate);
}

/* gather4 dst, address, t, s; gather4_c adds the reference; the _po forms
 * take a programmable offset operand after the address */
static void
emit_gather(struct ctx *c, const struct dxbc_instruction *insn)
{
   nir_builder *b = &c->b;
   bool po = insn->opcode == DXBC_OP_GATHER4_PO || insn->opcode == DXBC_OP_GATHER4_PO_C;
   bool cmp = insn->opcode == DXBC_OP_GATHER4_C || insn->opcode == DXBC_OP_GATHER4_PO_C;
   unsigned r = po ? 3 : 2;
   const struct dxbc_operand *res = &insn->operands[r], *samp = &insn->operands[r + 1];
   unsigned t = resource_index(c, insn, res), s = sampler_index(c, insn, samp), n = 0;
   struct tex_shape shape;
   nir_tex_instr *tex;

   if (c->failed || !tex_shape(c->res[t].dim, &shape)) {
      fail(c, insn, "gather resource dimension");
      return;
   }
   tex = tex_begin(c, nir_texop_tg4, t, s, true, 4, &shape, cmp ? nir_type_float32 : c->res[t].type);
   tex->component = cmp ? 0 : samp->swizzle[0];
   /* a comparison gather returns four results (no new-style scalar) */
   tex->is_shadow = cmp;
   tex->coord_components = shape.coords;
   tex->src[n++] = nir_tex_src_for_ssa(nir_tex_src_coord, nir_trim_vector(b, SRC(1, F), shape.coords));
   if (cmp)
      tex->src[n++] = nir_tex_src_for_ssa(nir_tex_src_comparator, nir_channel(b, SRC(r + 2, F), 0));
   if (po)
      tex->src[n++] = nir_tex_src_for_ssa(nir_tex_src_offset, nir_trim_vector(b, SRC(2, I), 2));
   else if (has_offset(insn))
      tex->src[n++] = nir_tex_src_for_ssa(nir_tex_src_offset, offset_src(c, insn, 2));
   write_dst(c, insn, &insn->operands[0], resource_swizzle(c, tex_finish(c, tex, n, 4), res),
             insn->saturate);
}

static void emit_mem_load(struct ctx *c, const struct dxbc_instruction *insn);
static void emit_typed_load(struct ctx *c, const struct dxbc_instruction *insn);

/* C53T (Tier 2): the _s opcodes are their base opcode with a status
 * operand (operands[1], after the result) and, for the _cl forms, a
 * trailing LOD clamp. The status is the access's residency code (AGX: bit 0
 * clear = resident), which only check_access_fully_mapped interprets.
 * Buffer and typed UAV loads return no residency: their status is
 * "resident" (Asahi's raw RGB32 loads do the same). */
static void
emit_feedback(struct ctx *c, const struct dxbc_instruction *insn)
{
   static const struct { uint32_t op, base, clamp; } forms[] = {
      {DXBC_OP_SAMPLE_L_FEEDBACK, DXBC_OP_SAMPLE_L, 0}, {DXBC_OP_SAMPLE_C_LZ_FEEDBACK, DXBC_OP_SAMPLE_C_LZ, 0},
      {DXBC_OP_SAMPLE_CLAMP_FEEDBACK, DXBC_OP_SAMPLE, 4}, {DXBC_OP_SAMPLE_B_CLAMP_FEEDBACK, DXBC_OP_SAMPLE_B, 5},
      {DXBC_OP_SAMPLE_D_CLAMP_FEEDBACK, DXBC_OP_SAMPLE_D, 6}, {DXBC_OP_SAMPLE_C_CLAMP_FEEDBACK, DXBC_OP_SAMPLE_C, 5},
      {DXBC_OP_LD_FEEDBACK, DXBC_OP_LD, 0}, {DXBC_OP_LD_MS_FEEDBACK, DXBC_OP_LD_MS, 0},
      {DXBC_OP_GATHER4_FEEDBACK, DXBC_OP_GATHER4, 0}, {DXBC_OP_GATHER4_C_FEEDBACK, DXBC_OP_GATHER4_C, 0},
      {DXBC_OP_GATHER4_PO_FEEDBACK, DXBC_OP_GATHER4_PO, 0}, {DXBC_OP_GATHER4_PO_C_FEEDBACK, DXBC_OP_GATHER4_PO_C, 0},
      {DXBC_OP_LD_UAV_TYPED_FEEDBACK, DXBC_OP_LD_UAV_TYPED, 0}, {DXBC_OP_LD_RAW_FEEDBACK, DXBC_OP_LD_RAW, 0},
      {DXBC_OP_LD_STRUCTURED_FEEDBACK, DXBC_OP_LD_STRUCTURED, 0},
   };
   unsigned f = 0;
   while (f < ARRAY_SIZE(forms) && forms[f].op != insn->opcode)
      ++f;
   if (f == ARRAY_SIZE(forms) || insn->num_operands < 3) {
      fail(c, insn, "feedback opcode");
      return;
   }
   struct dxbc_instruction base = *insn;
   base.opcode = forms[f].base;
   memmove(&base.operands[1], &base.operands[2], (base.num_operands - 2) * sizeof(base.operands[0]));
   --base.num_operands;
   if (forms[f].clamp && forms[f].clamp < base.num_operands) {
      c->min_lod = nir_channel(&c->b, read_src(c, &base, &base.operands[forms[f].clamp], nir_type_float32), 0);
      --base.num_operands;
   }
   c->feedback = true;
   c->feedback_code = NULL;
   switch (base.opcode) {
   case DXBC_OP_SAMPLE: case DXBC_OP_SAMPLE_B: case DXBC_OP_SAMPLE_L:
   case DXBC_OP_SAMPLE_D: case DXBC_OP_SAMPLE_C: case DXBC_OP_SAMPLE_C_LZ:
      emit_sample(c, &base);
      break;
   case DXBC_OP_LD: case DXBC_OP_LD_MS:
      emit_ld(c, &base);
      break;
   case DXBC_OP_GATHER4: case DXBC_OP_GATHER4_C: case DXBC_OP_GATHER4_PO: case DXBC_OP_GATHER4_PO_C:
      emit_gather(c, &base);
      break;
   case DXBC_OP_LD_UAV_TYPED:
      c->feedback = false;
      emit_typed_load(c, &base);
      break;
   default:
      c->feedback = false;
      emit_mem_load(c, &base);
      break;
   }
   c->feedback = false;
   c->min_lod = NULL;
   nir_def *code = c->feedback_code ? c->feedback_code : nir_imm_int(&c->b, 0);
   c->feedback_code = NULL;
   write_dst(c, insn, &insn->operands[1], replicate4(c, code), false);
}

/* C53T: check_access_fully_mapped dst, status: ~0 when the access the
 * status came from touched only mapped tiles */
static void
emit_check_access(struct ctx *c, const struct dxbc_instruction *insn)
{
   nir_builder *b = &c->b;
   nir_def *resident = nir_is_sparse_texels_resident(b, 1, nir_channel(b, SRC(1, U), 0));
   write_dst(c, insn, &insn->operands[0], replicate4(c, nir_bcsel(b, resident, nir_imm_int(b, ~0), nir_imm_int(b, 0))),
             false);
}

static void
emit_lod(struct ctx *c, const struct dxbc_instruction *insn)
{
   nir_builder *b = &c->b;
   const struct dxbc_operand *res = &insn->operands[2];
   unsigned t = resource_index(c, insn, res), s = sampler_index(c, insn, &insn->operands[3]);
   struct tex_shape shape;
   nir_tex_instr *tex;
   nir_def *lod, *comp[4];

   if (c->failed || !tex_shape(c->res[t].dim, &shape)) {
      fail(c, insn, "lod resource dimension");
      return;
   }
   tex = tex_begin(c, nir_texop_lod, t, s, true, 1, &shape, nir_type_float32);
   tex->is_array = false; /* the lod query ignores the array index */
   tex->coord_components = shape.coords - (shape.array ? 1 : 0);
   tex->src[0] = nir_tex_src_for_ssa(nir_tex_src_coord,
                                     nir_trim_vector(b, SRC(1, F), tex->coord_components));
   lod = tex_finish(c, tex, 1, 2);
   /* D3D: x clamped, y unclamped, z and w undefined (0) */
   comp[0] = nir_channel(b, lod, 0);
   comp[1] = nir_channel(b, lod, 1);
   comp[2] = comp[3] = nir_imm_float(b, 0);
   write_dst(c, insn, &insn->operands[0], resource_swizzle(c, nir_vec(b, comp, 4), res),
             insn->saturate);
}

static nir_def *
resource_samples(struct ctx *c, const struct dxbc_instruction *insn,
                 const struct dxbc_operand *res)
{
   unsigned t = resource_index(c, insn, res);
   struct tex_shape shape;
   nir_tex_instr *tex;
   if (c->failed || !tex_shape(c->res[t].dim, &shape) ||
       shape.dim != GLSL_SAMPLER_DIM_MS) {
      fail(c, insn, "sample query on a single-sampled resource");
      return nir_imm_int(&c->b, 1);
   }
   tex = tex_begin(c, nir_texop_texture_samples, t, 0, false, 0, &shape, nir_type_uint32);
   return tex_finish(c, tex, 0, 1);
}

/* FL 11_1 forced sample count, shared with the D3D12 path (dxbc_tir_gs.h) */
nir_def *
dxbc_tir_sample_offset(nir_builder *b, unsigned samples, nir_def *index)
{
   const int8_t (*pattern)[2] = dxbc_tir_pattern(samples);
   nir_def *x = nir_imm_float(b, 0), *y = nir_imm_float(b, 0);
   for (unsigned s = 0; pattern && s < samples; ++s) {
      nir_def *hit = nir_ieq_imm(b, index, s);
      x = nir_bcsel(b, hit, nir_imm_float(b, pattern[s][0] / 16.0f), x);
      y = nir_bcsel(b, hit, nir_imm_float(b, pattern[s][1] / 16.0f), y);
   }
   return nir_vec2(b, x, y);
}

void
dxbc_tir_coverage(nir_builder *b, unsigned samples, nir_def **mask_out, nir_def **centroid_out)
{
   const int8_t (*pattern)[2] = dxbc_tir_pattern(samples);
   nir_def *edge[DXBC_TIR_EDGE_SLOTS], *a[3], *bb[3], *centre[3], *mask = nir_imm_int(b, 0);
   for (unsigned i = 0; i < DXBC_TIR_EDGE_SLOTS; ++i) {
      nir_variable *var = nir_variable_create(b->shader, nir_var_shader_in, glsl_ivec4_type(), "tir_edge");
      var->data.location = VARYING_SLOT_TEX0 + i;
      var->data.interpolation = INTERP_MODE_FLAT;
      edge[i] = nir_load_var(b, var);
   }
   nir_def *fc = nir_load_frag_coord(b);
   nir_def *cx = nir_iadd_imm(b, nir_imul_imm(b, nir_f2i32(b, nir_ffloor(b, nir_channel(b, fc, 0))),
                                              DXBC_TIR_SUBPIXEL), DXBC_TIR_SUBPIXEL / 2);
   nir_def *cy = nir_iadd_imm(b, nir_imul_imm(b, nir_f2i32(b, nir_ffloor(b, nir_channel(b, fc, 1))),
                                              DXBC_TIR_SUBPIXEL), DXBC_TIR_SUBPIXEL / 2);
   for (unsigned e = 0; e < 3; ++e) {
      a[e] = nir_channel(b, edge[e], 2);
      bb[e] = nir_channel(b, edge[e], 3);
      centre[e] = nir_iadd(b,
         nir_iadd(b, nir_imul(b, nir_i2i64(b, a[e]), nir_i2i64(b, nir_isub(b, cx, nir_channel(b, edge[e], 0)))),
                     nir_imul(b, nir_i2i64(b, bb[e]), nir_i2i64(b, nir_isub(b, cy, nir_channel(b, edge[e], 1))))),
         nir_i2i64(b, nir_channel(b, edge[3], e)));
   }
   for (unsigned s = 0; pattern && s < samples; ++s) {
      nir_def *inside = nir_imm_true(b);
      for (unsigned e = 0; e < 3; ++e) {
         /* |A|, |B| < 2^23 and |offset| <= 128: the step fits 32 bits */
         nir_def *step = nir_iadd(b, nir_imul_imm(b, a[e], pattern[s][0] * (DXBC_TIR_SUBPIXEL / 16)),
                                  nir_imul_imm(b, bb[e], pattern[s][1] * (DXBC_TIR_SUBPIXEL / 16)));
         inside = nir_iand(b, inside, nir_ige_imm(b, nir_iadd(b, centre[e], nir_i2i64(b, step)), 0));
      }
      mask = nir_ior(b, mask, nir_bcsel(b, inside, nir_imm_int(b, 1 << s), nir_imm_int(b, 0)));
   }
   nir_demote_if(b, nir_ieq_imm(b, mask, 0));
   nir_def *full = nir_ieq_imm(b, mask, (int)((1u << samples) - 1u));
   *mask_out = mask;
   *centroid_out = nir_bcsel(b, full, nir_imm_vec2(b, 0, 0), dxbc_tir_sample_offset(b, samples, nir_find_lsb(b, mask)));
}

/* Conservative rasterization (D3D12 Tier 1) of any fragment shader behind
 * the expansion GS: a pixel runs when the original triangle touches its
 * square (dxbc_tir_touched: each edge at the outermost corner, bounding
 * boxes), so the GS's vertex extension at sharp angles adds no pixel;
 * SV_Coverage is the full single sample. */
bool
dxbc_cr_lower_fs(nir_shader *nir)
{
   if (nir->info.stage != MESA_SHADER_FRAGMENT)
      return false;
   nir_function_impl *impl = nir_shader_get_entrypoint(nir);
   nir_builder bb = nir_builder_at(nir_before_impl(impl));
   nir_builder *b = &bb;
   nir_def *edge[DXBC_TIR_EDGE_SLOTS];
   for (unsigned i = 0; i < DXBC_TIR_EDGE_SLOTS; ++i) {
      nir_variable *var = nir_variable_create(nir, nir_var_shader_in, glsl_ivec4_type(), "tir_edge");
      var->data.location = VARYING_SLOT_TEX0 + i;
      var->data.interpolation = INTERP_MODE_FLAT;
      edge[i] = nir_load_var(b, var);
   }
   nir_def *fc = nir_load_frag_coord(b);
   const int half = DXBC_TIR_SUBPIXEL / 2;
   nir_def *cx = nir_iadd_imm(b, nir_imul_imm(b, nir_f2i32(b, nir_ffloor(b, nir_channel(b, fc, 0))), DXBC_TIR_SUBPIXEL), half);
   nir_def *cy = nir_iadd_imm(b, nir_imul_imm(b, nir_f2i32(b, nir_ffloor(b, nir_channel(b, fc, 1))), DXBC_TIR_SUBPIXEL), half);
   nir_def *touched = nir_imm_true(b);
   nir_def *minx = NULL, *maxx = NULL, *miny = NULL, *maxy = NULL;
   for (unsigned e = 0; e < 3; ++e) {
      nir_def *x = nir_channel(b, edge[e], 0), *y = nir_channel(b, edge[e], 1);
      nir_def *a = nir_channel(b, edge[e], 2), *bv = nir_channel(b, edge[e], 3);
      nir_def *centre = nir_iadd(b, nir_imul(b, nir_i2i64(b, a), nir_i2i64(b, nir_isub(b, cx, x))),
                                 nir_imul(b, nir_i2i64(b, bv), nir_i2i64(b, nir_isub(b, cy, y))));
      nir_def *reach = nir_imul_imm(b, nir_i2i64(b, nir_iadd(b, nir_iabs(b, a), nir_iabs(b, bv))), half);
      touched = nir_iand(b, touched, nir_ige_imm(b, nir_iadd(b, centre, reach), 0));
      minx = minx ? nir_imin(b, minx, x) : x;
      maxx = maxx ? nir_imax(b, maxx, x) : x;
      miny = miny ? nir_imin(b, miny, y) : y;
      maxy = maxy ? nir_imax(b, maxy, y) : y;
   }
   touched = nir_iand(b, touched, nir_iand(b, nir_ilt(b, minx, nir_iadd_imm(b, cx, half)),
                                               nir_ilt(b, nir_iadd_imm(b, cx, -half), maxx)));
   touched = nir_iand(b, touched, nir_iand(b, nir_ilt(b, miny, nir_iadd_imm(b, cy, half)),
                                               nir_ilt(b, nir_iadd_imm(b, cy, -half), maxy)));
   nir_demote_if(b, nir_inot(b, touched));
   /* SV_Coverage stays the hardware's: the GS expands by a whole pixel
    * (dxbc_tir_gs_key.full_pixel), so a touched pixel has every sample
    * covered, with the SampleMask applied as without conservative mode */
   nir_progress(true, impl, nir_metadata_none);
   return true;
}

/* The forced-sample-count variant of any fragment shader still holding its
 * input variables (the DXIL path; dxbc_tir_gs.h): the coverage prologue,
 * SV_Coverage from the forced pattern, centroid and pull-model evaluation on
 * that pattern. */
bool
dxbc_tir_lower_fs(nir_shader *nir, unsigned samples)
{
   if (nir->info.stage != MESA_SHADER_FRAGMENT || !dxbc_tir_pattern(samples))
      return false;
   nir_function_impl *impl = nir_shader_get_entrypoint(nir);
   nir_builder b = nir_builder_at(nir_before_impl(impl));
   nir_def *mask, *centroid;
   dxbc_tir_coverage(&b, samples, &mask, &centroid);
   nir_foreach_block(block, impl) {
      nir_foreach_instr_safe(instr, block) {
         if (instr->type != nir_instr_type_intrinsic)
            continue;
         nir_intrinsic_instr *intr = nir_instr_as_intrinsic(instr);
         nir_def *value = NULL;
         b.cursor = nir_before_instr(instr);
         switch (intr->intrinsic) {
         case nir_intrinsic_load_sample_mask_in:
            value = mask;
            break;
         case nir_intrinsic_interp_deref_at_sample:
            value = nir_interp_deref_at_offset(&b, intr->def.num_components, intr->def.bit_size,
                                               intr->src[0].ssa,
                                               dxbc_tir_sample_offset(&b, samples, intr->src[1].ssa));
            break;
         case nir_intrinsic_interp_deref_at_centroid:
            value = nir_interp_deref_at_offset(&b, intr->def.num_components, intr->def.bit_size,
                                               intr->src[0].ssa, centroid);
            break;
         case nir_intrinsic_load_deref: {
            nir_deref_instr *deref = nir_src_as_deref(intr->src[0]);
            nir_variable *var = nir_deref_instr_get_variable(deref);
            if (!var || var->data.mode != nir_var_shader_in || !var->data.centroid)
               continue;
            value = nir_interp_deref_at_offset(&b, intr->def.num_components, intr->def.bit_size,
                                               &deref->def, centroid);
            break;
         }
         default:
            continue;
         }
         nir_def_replace(&intr->def, value);
      }
   }
   nir_foreach_variable_with_modes(var, nir, nir_var_shader_in)
      var->data.centroid = false;
   nir_progress(true, impl, nir_metadata_none);
   return true;
}

/* FL 11_1: sample 'index' of the forced pattern */
static nir_def *
tir_sample_offset(struct ctx *c, nir_def *index)
{
   return dxbc_tir_sample_offset(&c->b, c->forced_samples, index);
}

/* FL 11_1 forced sample count: the coverage prologue (dxbc_tir_coverage) */
static void
tir_coverage(struct ctx *c)
{
   dxbc_tir_coverage(&c->b, c->forced_samples, &c->forced_mask, &c->forced_centroid);
}

/* sampleinfo: the sample count, as float or (_uint) integer, in x */
static void
emit_sampleinfo(struct ctx *c, const struct dxbc_instruction *insn)
{
   nir_builder *b = &c->b;
   const struct dxbc_operand *res = &insn->operands[1];
   nir_def *count, *comp[4];
   if (res->type == DXBC_OPERAND_RASTERIZER && !c->forced_samples) {
      fail(c, insn, "sampleinfo of the rasterizer");
      return;
   }
   /* FL 11_1: the rasterizer's count is the forced one */
   count = res->type == DXBC_OPERAND_RASTERIZER ? nir_imm_int(b, (int)c->forced_samples) :
                                                  resource_samples(c, insn, res);
   if (((insn->token0 >> 11) & 3u) != 1)
      count = nir_u2f32(b, count);
   comp[0] = count;
   comp[1] = comp[2] = comp[3] = nir_imm_int(b, 0);
   write_dst(c, insn, &insn->operands[0], swizzle4(c, nir_vec(b, comp, 4), res),
             insn->saturate);
}

/* samplepos: the standard D3D sample positions (AGX uses the same
 * patterns), in 1/16 pixel from the centre; 0 outside the count */
static void
emit_samplepos(struct ctx *c, const struct dxbc_instruction *insn)
{
   nir_builder *b = &c->b;
   static const int8_t pos2[2][2] = { { 4, 4 }, { -4, -4 } };
   static const int8_t pos4[4][2] = { { -2, -6 }, { 6, -2 }, { -6, 2 }, { 2, 6 } };
   const struct dxbc_operand *res = &insn->operands[1];
   nir_def *count, *index, *x = nir_imm_float(b, 0), *y = nir_imm_float(b, 0), *comp[4];
   if (res->type == DXBC_OPERAND_RASTERIZER && !c->forced_samples) {
      fail(c, insn, "samplepos of the rasterizer");
      return;
   }
   index = nir_channel(b, SRC(2, U), 0);
   if (res->type == DXBC_OPERAND_RASTERIZER) {
      /* FL 11_1: the forced pattern */
      nir_def *offset = tir_sample_offset(c, index);
      comp[0] = nir_channel(b, offset, 0);
      comp[1] = nir_channel(b, offset, 1);
      comp[2] = comp[3] = nir_imm_float(b, 0);
      write_dst(c, insn, &insn->operands[0], swizzle4(c, nir_vec(b, comp, 4), res), insn->saturate);
      return;
   }
   count = resource_samples(c, insn, res);
   for (unsigned i = 0; i < 2; ++i) {
      nir_def *hit = nir_iand(b, nir_ieq_imm(b, count, 2), nir_ieq_imm(b, index, i));
      x = nir_bcsel(b, hit, nir_imm_float(b, pos2[i][0] / 16.0f), x);
      y = nir_bcsel(b, hit, nir_imm_float(b, pos2[i][1] / 16.0f), y);
   }
   for (unsigned i = 0; i < 4; ++i) {
      nir_def *hit = nir_iand(b, nir_ieq_imm(b, count, 4), nir_ieq_imm(b, index, i));
      x = nir_bcsel(b, hit, nir_imm_float(b, pos4[i][0] / 16.0f), x);
      y = nir_bcsel(b, hit, nir_imm_float(b, pos4[i][1] / 16.0f), y);
   }
   comp[0] = x;
   comp[1] = y;
   comp[2] = comp[3] = nir_imm_float(b, 0);
   write_dst(c, insn, &insn->operands[0], swizzle4(c, nir_vec(b, comp, 4), res),
             insn->saturate);
}

/* ---------------------------------------------------------------------
 * Shader model 5 memory: UAVs (u#), groupshared memory (g#), raw and
 * structured SRVs (t#)
 */

static nir_def *
mem_dword_index(struct ctx *c, nir_def *byte_address)
{
   return nir_ushr_imm(&c->b, byte_address, 2);
}

/* the byte address of an access: raw buffers take address.x, structured
 * ones element address.x at byte address.y */
static nir_def *
mem_address(struct ctx *c, const struct dxbc_instruction *insn,
            const struct dxbc_operand *target, nir_def *address)
{
   uint32_t stride = 0;
   unsigned index = (unsigned)target->index[0].imm;
   switch (target->type) {
   case DXBC_OPERAND_UNORDERED_ACCESS_VIEW:
      index = uav_entry(c, target);
      if (index >= MAX_UAVS + DXBC_HEAP_RANGE_MAX || c->uav[index].kind < MEM_RAW) {
         fail(c, insn, "buffer UAV u%u", index);
         return nir_imm_int(&c->b, 0);
      }
      stride = c->uav[index].stride;
      break;
   case DXBC_OPERAND_THREAD_GROUP_SHARED_MEMORY:
      if (index >= MAX_TGSM || !c->tgsm[index].var) {
         fail(c, insn, "groupshared g%u", index);
         return nir_imm_int(&c->b, 0);
      }
      stride = c->tgsm[index].stride;
      break;
   case DXBC_OPERAND_RESOURCE:
      if (heap_operand(c, target))
         index = target->index[0].imm < DXBC_HEAP_RANGE_MAX ? MAX_RESOURCES + (unsigned)target->index[0].imm
                                                              : MAX_RESOURCES + DXBC_HEAP_RANGE_MAX;
      if (index >= MAX_RESOURCES + DXBC_HEAP_RANGE_MAX || c->res_kind[index] < MEM_RAW) {
         fail(c, insn, "buffer SRV t%u", index);
         return nir_imm_int(&c->b, 0);
      }
      stride = c->res_stride[index];
      break;
   default:
      fail(c, insn, "memory operand type %u", target->type);
      return nir_imm_int(&c->b, 0);
   }
   if (!stride)
      return nir_channel(&c->b, address, 0);
   return nir_iadd(&c->b, nir_imul_imm(&c->b, nir_channel(&c->b, address, 0), stride),
                   nir_channel(&c->b, address, 1));
}

static nir_def *
mem_load(struct ctx *c, const struct dxbc_instruction *insn,
         const struct dxbc_operand *target, nir_def *byte_address, unsigned n)
{
   nir_builder *b = &c->b;
   unsigned index = (unsigned)target->index[0].imm;
   nir_def *comp[4];
   if (c->failed)
      return nir_imm_zero(b, n, 32);
   if (target->type == DXBC_OPERAND_UNORDERED_ACCESS_VIEW && heap_operand(c, target)) {
      /* C47: a heap buffer UAV is an R32_UINT buffer image */
      nir_def *handle = uav_heap_handle(c, insn, target, false);
      for (unsigned i = 0; i < n; ++i) {
         nir_def *coord = nir_vec4(b, nir_iadd_imm(b, mem_dword_index(c, byte_address), i), nir_imm_int(b, 0),
                                   nir_imm_int(b, 0), nir_imm_int(b, 0));
         comp[i] = nir_channel(b, nir_bindless_image_load(b, 4, 32, handle, coord, nir_imm_int(b, 0), nir_imm_int(b, 0),
                                                          .image_dim = GLSL_SAMPLER_DIM_BUF, .dest_type = nir_type_uint32,
                                                          .format = PIPE_FORMAT_R32_UINT), 0);
      }
      return nir_vec(b, comp, n);
   }
   if (target->type == DXBC_OPERAND_UNORDERED_ACCESS_VIEW)
      return nir_load_ssbo(b, n, 32, nir_imm_int(b, (int)index), byte_address,
                           .align_mul = 4, .align_offset = 0);
   const bool heap = target->type == DXBC_OPERAND_RESOURCE && heap_operand(c, target);
   if (heap)
      index = MAX_RESOURCES + (unsigned)target->index[0].imm; /* C47: mem_address validated it */
   for (unsigned i = 0; i < n; ++i) {
      nir_def *dword = nir_iadd_imm(b, mem_dword_index(c, byte_address), i);
      if (heap) {
         c->heap_offset = index_value(c, insn, &target->index[1]);
         c->heap_range = (unsigned)target->index[0].imm;
      }
      if (target->type == DXBC_OPERAND_THREAD_GROUP_SHARED_MEMORY) {
         comp[i] = nir_load_deref(b, nir_build_deref_array(b,
            nir_build_deref_var(b, c->tgsm[index].var), dword));
      } else {
         struct tex_shape shape;
         nir_tex_instr *tex;
         tex_shape(DXBC_DIM_BUFFER, &shape);
         tex = tex_begin(c, nir_texop_txf, index, 0, false, 1, &shape, nir_type_uint32);
         tex->coord_components = 1;
         tex->src[0] = nir_tex_src_for_ssa(nir_tex_src_coord, dword);
         comp[i] = nir_channel(b, tex_finish(c, tex, 1, 4), 0);
      }
   }
   return nir_vec(b, comp, n);
}

static void
mem_store(struct ctx *c, const struct dxbc_instruction *insn,
          const struct dxbc_operand *target, nir_def *byte_address, nir_def *value,
          unsigned n)
{
   nir_builder *b = &c->b;
   unsigned index = (unsigned)target->index[0].imm;
   if (c->failed)
      return;
   if (target->type == DXBC_OPERAND_UNORDERED_ACCESS_VIEW && heap_operand(c, target)) {
      /* C47: a heap buffer UAV is an R32_UINT buffer image */
      nir_def *handle = uav_heap_handle(c, insn, target, true);
      for (unsigned i = 0; i < n; ++i) {
         nir_def *coord = nir_vec4(b, nir_iadd_imm(b, mem_dword_index(c, byte_address), i), nir_imm_int(b, 0),
                                   nir_imm_int(b, 0), nir_imm_int(b, 0));
         nir_def *texel = nir_vec4(b, nir_channel(b, value, i), nir_imm_int(b, 0), nir_imm_int(b, 0), nir_imm_int(b, 0));
         nir_bindless_image_store(b, handle, coord, nir_imm_int(b, 0), texel, nir_imm_int(b, 0),
                                  .image_dim = GLSL_SAMPLER_DIM_BUF, .src_type = nir_type_uint32,
                                  .format = PIPE_FORMAT_R32_UINT);
      }
      return;
   }
   if (target->type == DXBC_OPERAND_UNORDERED_ACCESS_VIEW) {
      nir_store_ssbo(b, nir_trim_vector(b, value, n), nir_imm_int(b, (int)index), byte_address,
                     .write_mask = BITFIELD_MASK(n), .align_mul = 4, .align_offset = 0);
      return;
   }
   if (target->type != DXBC_OPERAND_THREAD_GROUP_SHARED_MEMORY) {
      fail(c, insn, "store to operand type %u", target->type);
      return;
   }
   for (unsigned i = 0; i < n; ++i)
      nir_store_deref(b, nir_build_deref_array(b, nir_build_deref_var(b, c->tgsm[index].var),
                      nir_iadd_imm(b, mem_dword_index(c, byte_address), i)),
                      nir_channel(b, value, i), 1);
}

/* ld_raw / ld_structured: dst.c = memory[address + 4 * src.swizzle[c]] */
static void
emit_mem_load(struct ctx *c, const struct dxbc_instruction *insn)
{
   bool structured = insn->opcode == DXBC_OP_LD_STRUCTURED;
   const struct dxbc_operand *dst = &insn->operands[0];
   const struct dxbc_operand *src = &insn->operands[structured ? 3 : 2];
   nir_def *address, *byte_address, *v;
   unsigned mask = dst->num_components == 4 ? dst->mask : 1u, n = 1;
   for (unsigned i = 0; i < 4; ++i)
      if (mask & (1u << i))
         n = MAX2(n, (unsigned)src->swizzle[i] + 1u);
   if (structured) {
      address = nir_vec2(&c->b, nir_channel(&c->b, SRC(1, U), 0), nir_channel(&c->b, SRC(2, U), 0));
   } else {
      address = SRC(1, U);
   }
   byte_address = mem_address(c, insn, src, address);
   v = nir_pad_vector_imm_int(&c->b, mem_load(c, insn, src, byte_address, n), 0, 4);
   write_dst(c, insn, dst, swizzle4(c, v, src), false);
}

/* store_raw / store_structured: the destination mask (.x .. .xyzw) names
 * the dwords written from the value's first components */
static void
emit_mem_store(struct ctx *c, const struct dxbc_instruction *insn)
{
   bool structured = insn->opcode == DXBC_OP_STORE_STRUCTURED;
   const struct dxbc_operand *dst = &insn->operands[0];
   nir_def *address, *value;
   unsigned mask = dst->num_components == 4 ? dst->mask : 1u;
   unsigned n = (unsigned)util_last_bit(mask);
   if (mask != BITFIELD_MASK(n)) {
      fail(c, insn, "non-contiguous store mask");
      return;
   }
   address = structured ?
      nir_vec2(&c->b, nir_channel(&c->b, SRC(1, U), 0), nir_channel(&c->b, SRC(2, U), 0)) :
      SRC(1, U);
   value = SRC(structured ? 3 : 2, U);
   mem_store(c, insn, dst, mem_address(c, insn, dst, address), value, n);
}

/* the image of a typed UAV operand: its slot, or (C47, *bindless) a heap
 * descriptor handle -- the PBE one when pbe (writes and atomics) */
static nir_def *
uav_image(struct ctx *c, const struct dxbc_instruction *insn, const struct dxbc_operand *o,
          struct tex_shape *shape, bool pbe, bool *bindless)
{
   unsigned u = uav_entry(c, o);
   *bindless = false;
   if (u >= MAX_UAVS + DXBC_HEAP_RANGE_MAX || c->uav[u].kind != MEM_TYPED) {
      fail(c, insn, "typed UAV operand");
      return NULL;
   }
   tex_shape(c->uav[u].dim, shape);
   if (u >= MAX_UAVS) {
      *bindless = true;
      return uav_heap_handle(c, insn, o, pbe);
   }
   /* EXP1304: the image's slot, not a deref: Asahi's binding lowering
    * (agx_nir_lower_bindings) takes the indexed image_* intrinsics only,
    * and an image_deref_* reached the backend ("Unhandled intrinsic"). */
   return nir_imm_int(&c->b, (int)u);
}

static void
emit_typed_load(struct ctx *c, const struct dxbc_instruction *insn)
{
   struct tex_shape shape;
   const struct dxbc_operand *uav = &insn->operands[2];
   bool bindless;
   nir_def *image = uav_image(c, insn, uav, &shape, false, &bindless);
   nir_def *v;
   if (!image)
      return;
   const nir_alu_type type = c->uav[uav_entry(c, uav)].type;
   v = bindless ? nir_bindless_image_load(&c->b, 4, 32, image, SRC(1, U), nir_imm_int(&c->b, 0), nir_imm_int(&c->b, 0),
                                          .image_dim = shape.dim, .image_array = shape.array, .dest_type = type)
                : nir_image_load(&c->b, 4, 32, image, SRC(1, U), nir_imm_int(&c->b, 0),
                            nir_imm_int(&c->b, 0), .image_dim = shape.dim,
                            .image_array = shape.array,
                            .dest_type = type);
   write_dst(c, insn, &insn->operands[0], swizzle4(c, v, uav), insn->saturate);
}

static void
emit_typed_store(struct ctx *c, const struct dxbc_instruction *insn)
{
   struct tex_shape shape;
   const struct dxbc_operand *uav = &insn->operands[0];
   bool bindless;
   nir_def *image = uav_image(c, insn, uav, &shape, true, &bindless);
   if (!image)
      return;
   const nir_alu_type type = c->uav[uav_entry(c, uav)].type;
   if (bindless)
      nir_bindless_image_store(&c->b, image, SRC(1, U), nir_imm_int(&c->b, 0), SRC(2, U), nir_imm_int(&c->b, 0),
                               .image_dim = shape.dim, .image_array = shape.array, .src_type = type);
   else
      nir_image_store(&c->b, image, SRC(1, U), nir_imm_int(&c->b, 0), SRC(2, U),
                         nir_imm_int(&c->b, 0), .image_dim = shape.dim,
                         .image_array = shape.array,
                         .src_type = type);
}

static nir_atomic_op
atomic_op(uint32_t opcode)
{
   switch (opcode) {
   case DXBC_OP_ATOMIC_AND: case DXBC_OP_IMM_ATOMIC_AND: return nir_atomic_op_iand;
   case DXBC_OP_ATOMIC_OR: case DXBC_OP_IMM_ATOMIC_OR: return nir_atomic_op_ior;
   case DXBC_OP_ATOMIC_XOR: case DXBC_OP_IMM_ATOMIC_XOR: return nir_atomic_op_ixor;
   case DXBC_OP_ATOMIC_IADD: case DXBC_OP_IMM_ATOMIC_IADD: return nir_atomic_op_iadd;
   case DXBC_OP_ATOMIC_IMAX: case DXBC_OP_IMM_ATOMIC_IMAX: return nir_atomic_op_imax;
   case DXBC_OP_ATOMIC_IMIN: case DXBC_OP_IMM_ATOMIC_IMIN: return nir_atomic_op_imin;
   case DXBC_OP_ATOMIC_UMAX: case DXBC_OP_IMM_ATOMIC_UMAX: return nir_atomic_op_umax;
   case DXBC_OP_ATOMIC_UMIN: case DXBC_OP_IMM_ATOMIC_UMIN: return nir_atomic_op_umin;
   case DXBC_OP_IMM_ATOMIC_EXCH: return nir_atomic_op_xchg;
   default: return nir_atomic_op_cmpxchg;
   }
}

/* atomic_* target, address, value[, value]; imm_atomic_* result, target,
 * address, value[, value] (cmp forms: compare, then value) */
static void
emit_atomic(struct ctx *c, const struct dxbc_instruction *insn)
{
   nir_builder *b = &c->b;
   bool imm = insn->opcode >= DXBC_OP_IMM_ATOMIC_ALLOC && insn->opcode <= DXBC_OP_IMM_ATOMIC_UMIN;
   bool swap = insn->opcode == DXBC_OP_ATOMIC_CMP_STORE || insn->opcode == DXBC_OP_IMM_ATOMIC_CMP_EXCH;
   unsigned first = imm ? 1 : 0;
   const struct dxbc_operand *target = &insn->operands[first];
   nir_atomic_op op = atomic_op(insn->opcode);
   nir_def *address = SRC(first + 1, U), *data, *compare = NULL, *result = NULL;
   unsigned index = (unsigned)target->index[0].imm;

   if (swap) {
      compare = nir_channel(b, SRC(first + 2, U), 0);
      data = nir_channel(b, SRC(first + 3, U), 0);
   } else {
      data = nir_channel(b, SRC(first + 2, U), 0);
   }
   if (target->type == DXBC_OPERAND_UNORDERED_ACCESS_VIEW)
      index = uav_entry(c, target);
   if (target->type == DXBC_OPERAND_UNORDERED_ACCESS_VIEW && index < MAX_UAVS + DXBC_HEAP_RANGE_MAX &&
       c->uav[index].kind == MEM_TYPED) {
      struct tex_shape shape;
      bool bindless;
      nir_def *image = uav_image(c, insn, target, &shape, true, &bindless);
      if (!image)
         return;
      if (bindless)
         result = swap ?
            nir_bindless_image_atomic_swap(b, 32, image, address, nir_imm_int(b, 0), compare, data,
                                           .image_dim = shape.dim, .image_array = shape.array,
                                           .atomic_op = op, .format = PIPE_FORMAT_R32_UINT) :
            nir_bindless_image_atomic(b, 32, image, address, nir_imm_int(b, 0), data,
                                      .image_dim = shape.dim, .image_array = shape.array,
                                      .atomic_op = op, .format = PIPE_FORMAT_R32_UINT);
      else
      result = swap ?
         nir_image_atomic_swap(b, 32, image, address, nir_imm_int(b, 0), compare, data,
                                     .image_dim = shape.dim, .image_array = shape.array,
                                     .atomic_op = op, .format = PIPE_FORMAT_R32_UINT) :
         nir_image_atomic(b, 32, image, address, nir_imm_int(b, 0), data,
                                .image_dim = shape.dim, .image_array = shape.array,
                                .atomic_op = op, .format = PIPE_FORMAT_R32_UINT);
   } else {
      nir_def *byte_address = mem_address(c, insn, target, address);
      if (c->failed)
         return;
      if (target->type == DXBC_OPERAND_UNORDERED_ACCESS_VIEW && heap_operand(c, target)) {
         /* C47: a heap buffer UAV is an R32_UINT buffer image */
         nir_def *image = uav_heap_handle(c, insn, target, true);
         nir_def *coord = nir_vec4(b, mem_dword_index(c, byte_address), nir_imm_int(b, 0), nir_imm_int(b, 0),
                                   nir_imm_int(b, 0));
         result = swap ?
            nir_bindless_image_atomic_swap(b, 32, image, coord, nir_imm_int(b, 0), compare, data,
                                           .image_dim = GLSL_SAMPLER_DIM_BUF, .atomic_op = op,
                                           .format = PIPE_FORMAT_R32_UINT) :
            nir_bindless_image_atomic(b, 32, image, coord, nir_imm_int(b, 0), data,
                                      .image_dim = GLSL_SAMPLER_DIM_BUF, .atomic_op = op,
                                      .format = PIPE_FORMAT_R32_UINT);
      } else if (target->type == DXBC_OPERAND_UNORDERED_ACCESS_VIEW) {
         nir_def *slot = nir_imm_int(b, (int)index);
         result = swap ? nir_ssbo_atomic_swap(b, 32, slot, byte_address, compare, data, .atomic_op = op) :
                         nir_ssbo_atomic(b, 32, slot, byte_address, data, .atomic_op = op);
      } else if (target->type == DXBC_OPERAND_THREAD_GROUP_SHARED_MEMORY) {
         nir_deref_instr *d = nir_build_deref_array(b, nir_build_deref_var(b, c->tgsm[index].var),
                                                    mem_dword_index(c, byte_address));
         result = swap ? nir_deref_atomic_swap(b, 32, &d->def, compare, data, .atomic_op = op) :
                         nir_deref_atomic(b, 32, &d->def, data, .atomic_op = op);
      } else {
         fail(c, insn, "atomic on operand type %u", target->type);
         return;
      }
   }
   if (imm)
      write_dst(c, insn, &insn->operands[0], replicate4(c, result), false);
}

/* imm_atomic_alloc / imm_atomic_consume: the UAV's hidden counter
 * (shader buffer counter_base + u#); alloc returns the old count, consume the
 * new one */
static void
emit_counter(struct ctx *c, const struct dxbc_instruction *insn)
{
   nir_builder *b = &c->b;
   const struct dxbc_operand *uav = &insn->operands[1];
   unsigned u = (unsigned)uav->index[0].imm;
   bool alloc = insn->opcode == DXBC_OP_IMM_ATOMIC_ALLOC;
   nir_def *slot, *old;
   if (uav->type == DXBC_OPERAND_UNORDERED_ACCESS_VIEW && heap_operand(c, uav)) {
      /* C47: a heap UAV's counter word, an R32_UINT buffer image */
      const unsigned entry = uav_entry(c, uav);
      if (entry >= MAX_UAVS + DXBC_HEAP_RANGE_MAX || c->uav[entry].kind != MEM_STRUCTURED) {
         fail(c, insn, "counter of a non-structured UAV");
         return;
      }
      old = nir_bindless_image_atomic(b, 32, uav_heap_counter(c, insn, uav), nir_imm_ivec4(b, 0, 0, 0, 0),
                                      nir_imm_int(b, 0), nir_imm_int(b, alloc ? 1 : -1),
                                      .image_dim = GLSL_SAMPLER_DIM_BUF, .atomic_op = nir_atomic_op_iadd,
                                      .format = PIPE_FORMAT_R32_UINT);
      write_dst(c, insn, &insn->operands[0], replicate4(c, alloc ? old : nir_iadd_imm(b, old, -1)), false);
      return;
   }
   if (uav->type != DXBC_OPERAND_UNORDERED_ACCESS_VIEW || u >= c->uav_slots ||
       c->uav[u].kind != MEM_STRUCTURED) {
      fail(c, insn, "counter of a non-structured UAV");
      return;
   }
   slot = nir_imm_int(b, (int)(c->counter_base + u));
   b->shader->info.num_ssbos = MAX2(b->shader->info.num_ssbos, (uint8_t)(c->counter_base + u + 1));
   old = nir_ssbo_atomic(b, 32, slot, nir_imm_int(b, 0), nir_imm_int(b, alloc ? 1 : -1),
                         .atomic_op = nir_atomic_op_iadd);
   write_dst(c, insn, &insn->operands[0], replicate4(c, alloc ? old : nir_iadd_imm(b, old, -1)),
             false);
}

static void
emit_sync(struct ctx *c, const struct dxbc_instruction *insn)
{
   uint32_t flags = insn->token0 & 0x7800u;
   bool threads = flags & 0x800u, tgsm = flags & 0x1000u;
   bool uav_group = flags & 0x2000u, uav_global = flags & 0x4000u;
   nir_variable_mode modes = (tgsm ? nir_var_mem_shared : 0) |
      ((uav_group || uav_global) ? (nir_var_mem_ssbo | nir_var_image) : 0);
   nir_barrier(&c->b, .execution_scope = threads ? SCOPE_WORKGROUP : SCOPE_NONE,
               .memory_scope = uav_global ? SCOPE_DEVICE : SCOPE_WORKGROUP,
               .memory_semantics = modes ? NIR_MEMORY_ACQ_REL : 0,
               .memory_modes = modes);
}

/* bufinfo: the element count of a buffer resource or UAV (bytes for raw) */
static void
emit_bufinfo(struct ctx *c, const struct dxbc_instruction *insn)
{
   nir_builder *b = &c->b;
   const struct dxbc_operand *o = &insn->operands[1];
   unsigned index = (unsigned)o->index[0].imm;
   nir_def *count = NULL;
   if (o->type == DXBC_OPERAND_UNORDERED_ACCESS_VIEW)
      index = uav_entry(c, o);
   if (o->type == DXBC_OPERAND_UNORDERED_ACCESS_VIEW && index < MAX_UAVS + DXBC_HEAP_RANGE_MAX &&
       c->uav[index].kind >= MEM_RAW) {
      /* C47: a heap buffer UAV's R32_UINT texels are its dwords */
      count = index >= MAX_UAVS
         ? nir_imul_imm(b, nir_bindless_image_size(b, 1, 32, uav_heap_handle(c, insn, o, false), nir_imm_int(b, 0),
                                                   .image_dim = GLSL_SAMPLER_DIM_BUF), 4)
         : nir_get_ssbo_size(b, 32, nir_imm_int(b, (int)index));
      if (c->uav[index].kind == MEM_STRUCTURED)
         count = nir_udiv_imm(b, count, c->uav[index].stride);
   } else if (o->type == DXBC_OPERAND_RESOURCE &&
              (heap_operand(c, o) ? o->index[0].imm < DXBC_HEAP_RANGE_MAX : index < MAX_RESOURCES) &&
              c->res[heap_operand(c, o) ? MAX_RESOURCES + (unsigned)o->index[0].imm : index].declared &&
              c->res[heap_operand(c, o) ? MAX_RESOURCES + (unsigned)o->index[0].imm : index].dim == DXBC_DIM_BUFFER) {
      struct tex_shape shape;
      nir_tex_instr *tex;
      if (heap_operand(c, o)) {
         /* C47: a heap buffer SRV */
         index = MAX_RESOURCES + (unsigned)o->index[0].imm;
         c->heap_offset = index_value(c, insn, &o->index[1]);
         c->heap_range = (unsigned)o->index[0].imm;
      }
      tex_shape(DXBC_DIM_BUFFER, &shape);
      tex = tex_begin(c, nir_texop_txs, index, 0, false, 1, &shape, nir_type_uint32);
      tex->src[0] = nir_tex_src_for_ssa(nir_tex_src_lod, nir_imm_int(b, 0));
      count = tex_finish(c, tex, 1, 1);
      if (c->res_kind[index] == MEM_RAW)
         count = nir_imul_imm(b, count, 4);
      else if (c->res_kind[index] == MEM_STRUCTURED)
         count = nir_udiv_imm(b, nir_imul_imm(b, count, 4), c->res_stride[index]);
   } else {
      fail(c, insn, "bufinfo operand");
      return;
   }
   write_dst(c, insn, &insn->operands[0], replicate4(c, count), false);
}

/* eval_sample_index / eval_centroid / eval_snapped: re-interpolate a pixel
 * shader input register (D3D snapped offsets are in 1/16 pixel) */
static void
emit_eval(struct ctx *c, const struct dxbc_instruction *insn)
{
   nir_builder *b = &c->b;
   const struct dxbc_operand *src = &insn->operands[1];
   unsigned reg = (unsigned)src->index[0].imm;
   nir_deref_instr *deref;
   nir_def *v;
   if (c->stage != MESA_SHADER_FRAGMENT || src->type != DXBC_OPERAND_INPUT ||
       src->index_dimension != 1 || !index_direct(&src->index[0]) || reg >= MAX_REGS ||
       !c->inputs[reg] || c->input_sysval_mask[reg] ||
       c->inputs[reg]->data.interpolation == INTERP_MODE_FLAT) {
      fail(c, insn, "attribute evaluation operand");
      return;
   }
   deref = nir_build_deref_var(b, c->inputs[reg]);
   switch (insn->opcode) {
   case DXBC_OP_EVAL_SAMPLE_INDEX:
      /* FL 11_1: pull-model evaluation uses the forced pattern */
      v = c->forced_samples ?
          nir_interp_deref_at_offset(b, 4, 32, &deref->def,
                                     tir_sample_offset(c, nir_channel(b, SRC(2, U), 0))) :
          nir_interp_deref_at_sample(b, 4, 32, &deref->def, nir_channel(b, SRC(2, U), 0));
      break;
   case DXBC_OP_EVAL_CENTROID:
      v = c->forced_samples ? nir_interp_deref_at_offset(b, 4, 32, &deref->def, c->forced_centroid) :
                              nir_interp_deref_at_centroid(b, 4, 32, &deref->def);
      break;
   default: {
      nir_def *offset = nir_i2f32(b, nir_trim_vector(b, SRC(2, I), 2));
      v = nir_interp_deref_at_offset(b, 4, 32, &deref->def, nir_fmul_imm(b, offset, 1.0 / 16.0));
      break;
   }
   }
   write_dst(c, insn, &insn->operands[0], swizzle4(c, v, src), insn->saturate);
}

/* ---------------------------------------------------------------------
 * Driver
 */

static void
emit_instruction(struct ctx *c, unsigned index)
{
   const struct dxbc_instruction *insn = &c->p->instructions[index];
   nir_builder *b = &c->b;

   if (dxbc_opcode_is_declaration(insn->opcode)) {
      declare(c, insn);
      return;
   }
   if (emit_flow(c, index) || emit_alu(c, insn))
      return;
   switch (insn->opcode) {
   case DXBC_OP_NOP:
      return;
   case DXBC_OP_SAMPLE: case DXBC_OP_SAMPLE_B: case DXBC_OP_SAMPLE_L:
   case DXBC_OP_SAMPLE_D: case DXBC_OP_SAMPLE_C: case DXBC_OP_SAMPLE_C_LZ:
      emit_sample(c, insn);
      return;
   case DXBC_OP_LD: case DXBC_OP_LD_MS:
      emit_ld(c, insn);
      return;
   case DXBC_OP_RESINFO:
      emit_resinfo(c, insn);
      return;
   case DXBC_OP_GATHER4: case DXBC_OP_GATHER4_C:
   case DXBC_OP_GATHER4_PO: case DXBC_OP_GATHER4_PO_C:
      emit_gather(c, insn);
      return;
   case DXBC_OP_LOD:
      emit_lod(c, insn);
      return;
   case DXBC_OP_GATHER4_FEEDBACK: case DXBC_OP_GATHER4_C_FEEDBACK: case DXBC_OP_GATHER4_PO_FEEDBACK:
   case DXBC_OP_GATHER4_PO_C_FEEDBACK: case DXBC_OP_LD_FEEDBACK: case DXBC_OP_LD_MS_FEEDBACK:
   case DXBC_OP_LD_UAV_TYPED_FEEDBACK: case DXBC_OP_LD_RAW_FEEDBACK: case DXBC_OP_LD_STRUCTURED_FEEDBACK:
   case DXBC_OP_SAMPLE_L_FEEDBACK: case DXBC_OP_SAMPLE_C_LZ_FEEDBACK: case DXBC_OP_SAMPLE_CLAMP_FEEDBACK:
   case DXBC_OP_SAMPLE_B_CLAMP_FEEDBACK: case DXBC_OP_SAMPLE_D_CLAMP_FEEDBACK: case DXBC_OP_SAMPLE_C_CLAMP_FEEDBACK:
      emit_feedback(c, insn);
      return;
   case DXBC_OP_CHECK_ACCESS_FULLY_MAPPED:
      emit_check_access(c, insn);
      return;
   case DXBC_OP_SAMPLE_INFO:
      emit_sampleinfo(c, insn);
      return;
   case DXBC_OP_SAMPLE_POS:
      emit_samplepos(c, insn);
      return;
   case DXBC_OP_EMIT:
   case DXBC_OP_EMITTHENCUT:
   case DXBC_OP_EMIT_STREAM:
   case DXBC_OP_EMITTHENCUT_STREAM: {
      unsigned stream = 0;
      if (insn->opcode == DXBC_OP_EMIT_STREAM || insn->opcode == DXBC_OP_EMITTHENCUT_STREAM)
         stream = (unsigned)insn->operands[0].index[0].imm & 3u;
      store_outputs(c);
      nir_emit_vertex(b, .stream_id = stream);
      if (insn->opcode == DXBC_OP_EMITTHENCUT || insn->opcode == DXBC_OP_EMITTHENCUT_STREAM)
         nir_end_primitive(b, .stream_id = stream);
      return;
   }
   case DXBC_OP_CUT:
   case DXBC_OP_CUT_STREAM: {
      unsigned stream = insn->opcode == DXBC_OP_CUT_STREAM ?
         (unsigned)insn->operands[0].index[0].imm & 3u : 0;
      nir_end_primitive(b, .stream_id = stream);
      return;
   }
   case DXBC_OP_LD_RAW: case DXBC_OP_LD_STRUCTURED:
      emit_mem_load(c, insn);
      return;
   case DXBC_OP_STORE_RAW: case DXBC_OP_STORE_STRUCTURED:
      emit_mem_store(c, insn);
      return;
   case DXBC_OP_LD_UAV_TYPED:
      emit_typed_load(c, insn);
      return;
   case DXBC_OP_STORE_UAV_TYPED:
      emit_typed_store(c, insn);
      return;
   case DXBC_OP_ATOMIC_AND: case DXBC_OP_ATOMIC_OR: case DXBC_OP_ATOMIC_XOR:
   case DXBC_OP_ATOMIC_CMP_STORE: case DXBC_OP_ATOMIC_IADD: case DXBC_OP_ATOMIC_IMAX:
   case DXBC_OP_ATOMIC_IMIN: case DXBC_OP_ATOMIC_UMAX: case DXBC_OP_ATOMIC_UMIN:
   case DXBC_OP_IMM_ATOMIC_IADD: case DXBC_OP_IMM_ATOMIC_AND: case DXBC_OP_IMM_ATOMIC_OR:
   case DXBC_OP_IMM_ATOMIC_XOR: case DXBC_OP_IMM_ATOMIC_EXCH: case DXBC_OP_IMM_ATOMIC_CMP_EXCH:
   case DXBC_OP_IMM_ATOMIC_IMAX: case DXBC_OP_IMM_ATOMIC_IMIN: case DXBC_OP_IMM_ATOMIC_UMAX:
   case DXBC_OP_IMM_ATOMIC_UMIN:
      emit_atomic(c, insn);
      return;
   case DXBC_OP_SYNC:
      emit_sync(c, insn);
      return;
   case DXBC_OP_IMM_ATOMIC_ALLOC: case DXBC_OP_IMM_ATOMIC_CONSUME:
      emit_counter(c, insn);
      return;
   case DXBC_OP_BUFINFO:
      emit_bufinfo(c, insn);
      return;
   case DXBC_OP_EVAL_SAMPLE_INDEX: case DXBC_OP_EVAL_CENTROID: case DXBC_OP_EVAL_SNAPPED:
      emit_eval(c, insn);
      return;
   default:
      fail(c, insn, "instruction %s", dxbc_opcode_name(insn->opcode));
      return;
   }
}

static void
hs_end_phase(struct ctx *c)
{
   if (c->hs_phase == HS_PHASE_CP)
      hs_store_cp_outputs(c);
}

/* pass-through control points: the input registers of the signature */
static void
hs_passthrough_vars(struct ctx *c)
{
   for (unsigned reg = 0; reg < MAX_REGS; ++reg) {
      int slot;
      if (!(c->passthrough_registers & (1u << reg)))
         continue;
      slot = (c->passthrough_positions & (1u << reg)) ? VARYING_SLOT_POS : hs_cp_slot(reg);
      if (!hs_cp_input(c, reg, slot) || !hs_cp_output(c, reg, hs_cp_slot(reg)))
         fail(c, NULL, "pass-through control point register %u", reg);
      c->cp_out_mask[reg] = 0xfu;
   }
}

static void
hs_begin_phase(struct ctx *c, const struct dxbc_instruction *insn, unsigned kind)
{
   static const char *const names[] = {[HS_PHASE_CP] = "control_point_phase",
                                       [HS_PHASE_FORK] = "fork_phase",
                                       [HS_PHASE_JOIN] = "join_phase"};
   nir_function *f;
   hs_end_phase(c);
   if (c->failed)
      return;
   if (c->cf_depth || c->num_phases >= MAX_PHASES ||
       (kind == HS_PHASE_CP && (c->hs_had_cp || c->num_phases))) {
      fail(c, insn, "hull shader phase order");
      return;
   }
   if (!c->b.shader->info.tess.tcs_vertices_out) {
      fail(c, insn, "hull shader without an output control point count");
      return;
   }
   if (kind != HS_PHASE_CP && !c->hs_had_cp && !c->num_phases)
      hs_passthrough_vars(c);
   f = nir_function_create(c->b.shader, names[kind]);
   if (kind != HS_PHASE_CP) {
      f->num_params = 1;
      f->params = rzalloc_array(c->b.shader, nir_parameter, 1);
      f->params[0].num_components = 1;
      f->params[0].bit_size = 32;
   }
   c->b = nir_builder_at(nir_after_impl(nir_function_impl_create(f)));
   c->phases[c->num_phases].func = f;
   c->phases[c->num_phases].kind = kind;
   c->phases[c->num_phases].instances = 1;
   ++c->num_phases;
   c->hs_had_cp |= kind == HS_PHASE_CP;
   c->hs_phase = kind;
   /* registers and indexable temporaries are phase-local */
   memset(c->temps, 0, sizeof(c->temps));
   memset(c->outputs, 0, sizeof(c->outputs));
   memset(c->itemps, 0, sizeof(c->itemps));
   c->num_temps = 0;
   c->main_done = false;
}

/* gl_TessLevelOuter/Inner and the patch varyings from the patch-constant
 * registers (invocation 0, after every fork/join phase) */
static void
hs_store_patch_constants(struct ctx *c)
{
   nir_builder *b = &c->b;
   nir_variable *outer = NULL, *inner = NULL;
   for (unsigned reg = 0; reg < MAX_REGS; ++reg) {
      nir_def *v;
      if (!c->pc_generic_mask[reg] && !c->pc_factor_mask[reg])
         continue;
      v = nir_load_deref(b, nir_build_deref_array_imm(b, nir_build_deref_var(b, hs_pc_regs(c)), reg));
      if (c->pc_generic_mask[reg]) {
         nir_variable *var = make_var(c, nir_var_shader_out, glsl_vec4_type(),
                                      VARYING_SLOT_PATCH0 + (int)reg, "pc");
         var->data.patch = true;
         nir_store_var(b, var, v, c->pc_generic_mask[reg]);
      }
      for (unsigned i = 0; i < 4; ++i) {
         unsigned f = c->pc_factor[reg][i];
         nir_variable **level;
         if (!(c->pc_factor_mask[reg] & (1u << i)))
            continue;
         level = (f & TESS_FACTOR_INNER) ? &inner : &outer;
         if (!*level) {
            bool in = level == &inner;
            *level = make_var(c, nir_var_shader_out,
                              glsl_array_type(glsl_float_type(), in ? 2 : 4, 0),
                              in ? VARYING_SLOT_TESS_LEVEL_INNER : VARYING_SLOT_TESS_LEVEL_OUTER,
                              in ? "inner" : "outer");
            (*level)->data.patch = true;
            (*level)->data.compact = true;
         }
         nir_store_deref(b, nir_build_deref_array_imm(b, nir_build_deref_var(b, *level),
                                                      f & ~TESS_FACTOR_INNER),
                         nir_channel(b, v, i), 1);
      }
   }
}

/* main: the control point phase in every invocation (or the pass-through
 * copy), a barrier, then invocation 0 runs every fork/join phase instance
 * and writes the patch constants */
static void
hs_build_main(struct ctx *c)
{
   nir_builder *b = &c->b;
   *b = nir_builder_at(nir_after_impl(c->main_impl));
   if (c->hs_had_cp) {
      nir_build_call(b, c->phases[0].func, 0, NULL);
   } else {
      nir_def *id = nir_load_invocation_id(b);
      if (!c->num_phases)
         hs_passthrough_vars(c);
      for (unsigned reg = 0; reg < MAX_REGS; ++reg)
         if (c->passthrough_registers & (1u << reg))
            nir_store_deref(b, nir_build_deref_array(b, nir_build_deref_var(b, c->cp_out[reg]), id),
                            nir_load_deref(b, nir_build_deref_array(b,
                               nir_build_deref_var(b, c->inputs[reg]), id)), 0xfu);
   }
   nir_barrier(b, .execution_scope = SCOPE_WORKGROUP, .memory_scope = SCOPE_WORKGROUP,
               .memory_semantics = NIR_MEMORY_ACQ_REL, .memory_modes = nir_var_shader_out);
   nir_push_if(b, nir_ieq_imm(b, nir_load_invocation_id(b), 0));
   for (unsigned p = 0; p < c->num_phases; ++p) {
      if (c->phases[p].kind == HS_PHASE_CP)
         continue;
      for (unsigned i = 0; i < c->phases[p].instances; ++i) {
         nir_def *arg = nir_imm_int(b, (int)i);
         nir_build_call(b, c->phases[p].func, 1, &arg);
      }
   }
   hs_store_patch_constants(c);
   nir_pop_if(b, NULL);
}
/* Stream output: every captured register feeds a vec4 output variable at a
 * generic or position location (add_sink in declare_output); the xfb info
 * names that location and component range, as the projection does for the
 * TGSI path from TGSI semantics (the order of the D3D declaration kept). */
static bool
build_xfb(struct ctx *c, const struct dxbc_nir_xfb *xfb)
{
   nir_shader *nir = c->b.shader;
   nir_xfb_info *info;

   if (!xfb->num_outputs || xfb->num_outputs > DXBC_NIR_MAX_XFB_OUTPUTS ||
       (c->stage != MESA_SHADER_VERTEX && c->stage != MESA_SHADER_GEOMETRY)) {
      fail(c, NULL, "stream output of stage %u", c->stage);
      return false;
   }
   info = rzalloc_size(nir, nir_xfb_info_size(xfb->num_outputs));
   if (!info)
      return false;
   for (unsigned i = 0; i < xfb->num_outputs; ++i) {
      const struct dxbc_nir_xfb_output *o = &xfb->outputs[i];
      const struct output_sink *sink = NULL;
      unsigned want = o->num_components && o->num_components <= 4 ?
         ((1u << o->num_components) - 1u) << o->start_component : 0u;
      if (o->reg < MAX_REGS && want && o->start_component + o->num_components <= 4)
         for (unsigned s = 0; s < c->num_sinks[o->reg] && !sink; ++s) {
            const struct output_sink *k = &c->sinks[o->reg][s];
            int loc = k->var->data.location;
            if (!k->scalar && !k->first && (k->mask & want) == want &&
                k->var->data.stream == 0 &&
                (loc == VARYING_SLOT_POS ||
                 (loc >= VARYING_SLOT_VAR0 && loc < VARYING_SLOT_VAR0 + MAX_REGS)))
               sink = k;
         }
      if (!sink || o->stream || o->buffer >= DXBC_NIR_MAX_XFB_BUFFERS) {
         fail(c, NULL, "stream output %u (register %u)", i, o->reg);
         return false;
      }
      nir_xfb_output_info *out = &info->outputs[i];
      out->buffer = o->buffer;
      out->offset = o->dst_offset * 4u;
      out->location = (uint8_t)sink->var->data.location;
      out->component_offset = o->start_component;
      out->component_mask = (uint8_t)want;
      info->buffers_written |= (uint8_t)BITFIELD_BIT(o->buffer);
      info->buffers[o->buffer].varying_count++;
   }
   info->streams_written = BITFIELD_BIT(0);
   for (unsigned buffer = 0; buffer < DXBC_NIR_MAX_XFB_BUFFERS; ++buffer) {
      info->buffer_to_stream[buffer] = 0;
      if (info->buffers_written & BITFIELD_BIT(buffer)) {
         info->buffers[buffer].stride = (uint16_t)(xfb->stride[buffer] * 4u);
         nir->info.xfb_stride[buffer] = (uint8_t)xfb->stride[buffer];
      }
   }
   info->output_count = (uint16_t)xfb->num_outputs;
   nir->xfb_info = info;
   nir->info.has_transform_feedback_varyings = true;
   return true;
}

uint32_t
dxbc_nir_tess_info(const struct nir_shader *nir)
{
   return (nir->info.tess.ccw ? 1u : 0u) | (nir->info.tess.point_mode ? 2u : 0u) |
          ((uint32_t)nir->info.tess._primitive_mode << 8) | ((uint32_t)nir->info.tess.spacing << 16);
}

/* D3D11.1 mode: buffer UAV u# (shader buffer u#) and its counter (shader
 * buffer DXBC_NIR_UAV11_SLOTS + u#) move to consecutive shader buffers --
 * the buffer views in register order, then the counters -- since Gallium has
 * 32 (PIPE_MAX_SHADER_BUFFERS) for 64 registers; typed views (image u#) move
 * to consecutive images likewise: Asahi binds 32 (its 32-bit image_mask,
 * EXP1887, although it reports PIPE_MAX_SHADER_IMAGES). SM 5.0 indexes UAVs
 * by immediates only, so every index is constant. */
static bool
compact_uav_buffers(const struct ctx *c, nir_shader *nir, struct dxbc_nir_uav_slots *slots)
{
   uint8_t remap[2 * DXBC_NIR_UAV11_SLOTS];
   unsigned next = 0, images = 0;
   memset(slots, 0, sizeof(*slots));
   memset(slots->buffer, DXBC_NIR_UAV_UNUSED, sizeof(slots->buffer));
   memset(slots->counter, DXBC_NIR_UAV_UNUSED, sizeof(slots->counter));
   memset(slots->image, DXBC_NIR_UAV_UNUSED, sizeof(slots->image));
   memset(remap, DXBC_NIR_UAV_UNUSED, sizeof(remap));
   for (unsigned u = 0; u < DXBC_NIR_UAV11_SLOTS; ++u) {
      if (c->uav[u].kind == MEM_TYPED) {
         slots->typed |= 1ull << u;
         slots->image[u] = (uint8_t)images++;
      } else if (c->uav[u].kind >= MEM_RAW) {
         remap[u] = slots->buffer[u] = (uint8_t)next++;
      }
   }
   if (images > UAV11_IMAGES)
      return false;
   /* counters: the structured views an alloc/consume names */
   nir_foreach_function_impl(impl, nir)
      nir_foreach_block(block, impl)
         nir_foreach_instr(instr, block) {
            if (instr->type != nir_instr_type_intrinsic ||
                nir_instr_as_intrinsic(instr)->intrinsic != nir_intrinsic_ssbo_atomic)
               continue;
            nir_intrinsic_instr *intr = nir_instr_as_intrinsic(instr);
            if (!nir_src_is_const(intr->src[0]))
               return false;
            unsigned slot = nir_src_as_uint(intr->src[0]);
            if (slot >= DXBC_NIR_UAV11_SLOTS && slot < 2 * DXBC_NIR_UAV11_SLOTS)
               slots->counter[slot - DXBC_NIR_UAV11_SLOTS] = 0;
         }
   for (unsigned u = 0; u < DXBC_NIR_UAV11_SLOTS; ++u)
      if (slots->counter[u] != DXBC_NIR_UAV_UNUSED)
         remap[DXBC_NIR_UAV11_SLOTS + u] = slots->counter[u] = (uint8_t)next++;
   if (next > UAV11_SHADER_BUFFERS)
      return false;
   nir_foreach_function_impl(impl, nir) {
      nir_builder b = nir_builder_create(impl);
      nir_foreach_block(block, impl)
         nir_foreach_instr(instr, block) {
            if (instr->type != nir_instr_type_intrinsic)
               continue;
            nir_intrinsic_instr *intr = nir_instr_as_intrinsic(instr);
            unsigned source;
            bool image = false;
            switch (intr->intrinsic) {
            case nir_intrinsic_store_ssbo: source = 1; break;
            case nir_intrinsic_load_ssbo:
            case nir_intrinsic_ssbo_atomic:
            case nir_intrinsic_ssbo_atomic_swap:
            case nir_intrinsic_get_ssbo_size: source = 0; break;
            case nir_intrinsic_image_load:
            case nir_intrinsic_image_sparse_load:
            case nir_intrinsic_image_store:
            case nir_intrinsic_image_atomic:
            case nir_intrinsic_image_atomic_swap:
            case nir_intrinsic_image_size:
            case nir_intrinsic_image_levels:
            case nir_intrinsic_image_samples: source = 0; image = true; break;
            default: continue;
            }
            if (!nir_src_is_const(intr->src[source]))
               return false;
            const unsigned slot = nir_src_as_uint(intr->src[source]);
            const unsigned to = image ? (slot < DXBC_NIR_UAV11_SLOTS ? slots->image[slot] : DXBC_NIR_UAV_UNUSED) :
                                slot < ARRAY_SIZE(remap) ? remap[slot] : DXBC_NIR_UAV_UNUSED;
            if (to == DXBC_NIR_UAV_UNUSED)
               return false;
            b.cursor = nir_before_instr(instr);
            nir_src_rewrite(&intr->src[source], nir_imm_int(&b, (int)to));
         }
      nir_progress(true, impl, nir_metadata_control_flow);
   }
   /* the image variables (Asahi recounts num_images from them) */
   BITSET_ZERO(nir->info.images_used);
   nir_foreach_variable_with_modes(var, nir, nir_var_image) {
      const unsigned u = var->data.binding;
      if (u >= DXBC_NIR_UAV11_SLOTS || slots->image[u] == DXBC_NIR_UAV_UNUSED)
         return false;
      var->data.binding = slots->image[u];
      BITSET_SET(nir->info.images_used, slots->image[u]);
   }
   nir->info.num_images = (uint8_t)images;
   nir->info.num_ssbos = (uint8_t)next;
   slots->buffers = next;
   slots->images = images;
   return true;
}

static struct nir_shader *
translate(const struct dxbc_program *program, const struct nir_shader_compiler_options *options,
          uint32_t passthrough_registers, uint32_t passthrough_positions,
          const struct dxbc_nir_xfb *xfb, struct dxbc_nir_uav_slots *uav64,
          unsigned forced_samples, struct dxbc_nir_error *error)
{
   static struct dxbc_nir_error ignored;
   struct ctx *c;
   mesa_shader_stage stage;
   nir_shader *nir;

   if (!error)
      error = &ignored;
   memset(error, 0, sizeof(*error));
   switch (program->type) {
   case DXBC_PROGRAM_VERTEX: stage = MESA_SHADER_VERTEX; break;
   case DXBC_PROGRAM_PIXEL: stage = MESA_SHADER_FRAGMENT; break;
   case DXBC_PROGRAM_GEOMETRY: stage = MESA_SHADER_GEOMETRY; break;
   case DXBC_PROGRAM_COMPUTE: stage = MESA_SHADER_COMPUTE; break;
   case DXBC_PROGRAM_DOMAIN: stage = MESA_SHADER_TESS_EVAL; break;
   case DXBC_PROGRAM_HULL: stage = MESA_SHADER_TESS_CTRL; break;
   default:
      snprintf(error->message, sizeof(error->message), "program type %u", program->type);
      return NULL;
   }
   c = calloc(1, sizeof(*c));
   if (!c)
      return NULL;
   c->p = program;
   c->error = error;
   c->passthrough_registers = passthrough_registers;
   c->passthrough_positions = passthrough_positions;
   c->uav_slots = c->counter_base = uav64 ? DXBC_NIR_UAV11_SLOTS : DXBC_NIR_MAX_UAVS;
   c->forced_samples = stage == MESA_SHADER_FRAGMENT && dxbc_tir_pattern(forced_samples) ? forced_samples : 0;
   c->stage = stage;
   c->b = nir_builder_init_simple_shader(stage, options, "dxbc");
   nir = c->b.shader;
   /* D3D binds each stage separately (tgsi_to_nir sets the same) */
   nir->info.separate_shader = true;
   nir->info.internal = false;
   if (stage == MESA_SHADER_FRAGMENT) {
      /* D3D: upper-left origin, pixel centres at half integers; colour
       * outputs carry raw bits for integer targets (the TGSI path's
       * properties and tgsi_to_nir's untyped outputs) */
      nir->info.fs.origin_upper_left = true;
      nir->info.fs.pixel_center_integer = false;
      nir->info.fs.untyped_color_outputs = true;
   }
   if (stage == MESA_SHADER_GEOMETRY) {
      nir->info.gs.invocations = 1;
      nir->info.gs.active_stream_mask = 1;
   }
   if (c->forced_samples)
      tir_coverage(c);

   if (stage == MESA_SHADER_TESS_CTRL)
      c->main_impl = c->b.impl;
   for (unsigned i = 0; i < program->num_instructions && !c->failed; ++i) {
      const struct dxbc_instruction *insn = &program->instructions[i];
      if (stage == MESA_SHADER_TESS_CTRL) {
         if (insn->opcode == DXBC_OP_HS_DECLS) {
            c->hs_phase = HS_PHASE_DECLS;
            continue;
         }
         if (insn->opcode == DXBC_OP_HS_CONTROL_POINT_PHASE ||
             insn->opcode == DXBC_OP_HS_FORK_PHASE || insn->opcode == DXBC_OP_HS_JOIN_PHASE) {
            hs_begin_phase(c, insn, insn->opcode == DXBC_OP_HS_CONTROL_POINT_PHASE ? HS_PHASE_CP :
                                    insn->opcode == DXBC_OP_HS_FORK_PHASE ? HS_PHASE_FORK :
                                    HS_PHASE_JOIN);
            continue;
         }
         if (c->main_done)
            continue; /* after a phase's top-level ret */
      } else if (c->main_done) {
         break;
      }
      if (insn->opcode == DXBC_OP_LABEL || insn->opcode == DXBC_OP_CALL ||
          insn->opcode == DXBC_OP_CALLC || insn->opcode == DXBC_OP_INTERFACE_CALL) {
         fail(c, insn, "subroutines");
         break;
      }
      c->dyn_texture = c->dyn_sampler = c->heap_offset = c->sampler_heap_offset = NULL;
      emit_instruction(c, i);
      if (!c->failed && (c->dyn_texture || c->dyn_sampler || c->heap_offset || c->sampler_heap_offset))
         fail(c, insn, "dynamically indexed resource outside a texture instruction");
   }
   if (!c->failed && c->cf_depth)
      fail(c, NULL, "unterminated control flow");
   if (!c->failed && stage == MESA_SHADER_TESS_CTRL) {
      hs_end_phase(c);
      if (!c->failed)
         hs_build_main(c);
   } else if (!c->failed && stage != MESA_SHADER_GEOMETRY) {
      store_outputs(c);
   }
   if (!c->failed && xfb)
      build_xfb(c, xfb);

   if (c->failed) {
      ralloc_free(nir);
      free(c);
      return NULL;
   }
   nir->info.num_textures = 0;
   for (unsigned i = 0; i < MAX_RESOURCES; ++i)
      if (c->res[i].declared)
         nir->info.num_textures = i + 1;
   /* EXP1841: Asahi clamps bindless image indices (every image query) to
    * info.num_images - 1, which nir_shader_gather_info recounts from the
    * image variables; slots without a typed UAV (raw/structured UAVs, gaps)
    * get placeholders so the count spans the highest typed slot */
   for (unsigned u = 0; !uav64 && u < BITSET_LAST_BIT(nir->info.images_used) && u < c->uav_slots; ++u) {
      if (c->uav[u].image)
         continue;
      nir_variable *gap = nir_variable_create(nir, nir_var_image,
         glsl_image_type(GLSL_SAMPLER_DIM_2D, false, GLSL_TYPE_FLOAT), "u_unused");
      gap->data.binding = u;
      gap->data.image.format = PIPE_FORMAT_NONE;
   }
   if (uav64 && !compact_uav_buffers(c, nir, uav64)) {
      snprintf(error->message, sizeof(error->message), "more than %u buffer UAVs and counters",
               UAV11_SHADER_BUFFERS);
      ralloc_free(nir);
      free(c);
      return NULL;
   }
   free(c);
   return nir;
}

struct nir_shader *
dxbc_to_nir(const struct dxbc_program *program,
            const struct nir_shader_compiler_options *options,
            struct dxbc_nir_error *error)
{
   return translate(program, options, 0, 0, NULL, NULL, 0, error);
}

struct nir_shader *
dxbc_to_nir_hs(const struct dxbc_program *program,
               const struct nir_shader_compiler_options *options,
               uint32_t passthrough_registers, uint32_t passthrough_positions,
               struct dxbc_nir_error *error)
{
   return translate(program, options, passthrough_registers, passthrough_positions, NULL, NULL, 0, error);
}

void
dxbc_nir_lower_fragment_position(nir_shader *nir)
{
   NIR_PASS(_, nir, nir_lower_wpos_center);
}

void
dxbc_nir_finalize(nir_shader *nir)
{
   bool progress;

   NIR_PASS(_, nir, nir_lower_continue_constructs);
   NIR_PASS(_, nir, nir_lower_returns);
   NIR_PASS(_, nir, nir_lower_vars_to_ssa);
   NIR_PASS(_, nir, nir_lower_reg_intrinsics_to_ssa);
   if (nir->info.stage == MESA_SHADER_TESS_CTRL) {
      /* hull shader phases: one function each, called from main */
      NIR_PASS(_, nir, nir_inline_functions);
      nir_remove_non_entrypoints(nir);
      NIR_PASS(_, nir, nir_lower_vars_to_ssa);
   }
   NIR_PASS(_, nir, nir_lower_global_vars_to_local);
   NIR_PASS(_, nir, nir_split_var_copies);
   NIR_PASS(_, nir, nir_lower_var_copies);
   NIR_PASS(_, nir, nir_lower_system_values);
   NIR_PASS(_, nir, nir_lower_compute_system_values, NULL);
   if (nir->info.stage != MESA_SHADER_COMPUTE && nir->info.stage != MESA_SHADER_TESS_CTRL) {
      /* outputs written once, at the end of the shader (and before each
       * geometry emit), after the returns are lowered: AGX turns fragment
       * outputs into exports that must end the shader (EXP1294; Honeykrisp
       * lowers every stage's outputs the same way, hk_shader.c) */
      NIR_PASS(_, nir, nir_lower_io_vars_to_temporaries, nir_shader_get_entrypoint(nir),
               nir_var_shader_out);
      NIR_PASS(_, nir, nir_lower_global_vars_to_local);
      NIR_PASS(_, nir, nir_split_var_copies);
      NIR_PASS(_, nir, nir_lower_var_copies);
   }
   if (nir->info.stage == MESA_SHADER_COMPUTE) {
      /* groupshared memory gets explicit offsets and the threadgroup size
       * (shared_size) here, in the frontend, as st and rusticl do it: the
       * AGX backend allocates shared_size and has no deref lowering for it */
      NIR_PASS(_, nir, nir_lower_vars_to_explicit_types, nir_var_mem_shared,
               glsl_get_natural_size_align_bytes);
      NIR_PASS(_, nir, nir_lower_explicit_io, nir_var_mem_shared,
               nir_address_format_32bit_offset);
   }
   do {
      progress = false;
      NIR_PASS(progress, nir, nir_lower_vars_to_ssa);
      NIR_PASS(progress, nir, nir_opt_copy_prop_vars);
      NIR_PASS(progress, nir, nir_opt_dead_write_vars);
      NIR_PASS(progress, nir, nir_opt_copy_prop);
      NIR_PASS(progress, nir, nir_opt_remove_phis);
      NIR_PASS(progress, nir, nir_opt_dce);
      NIR_PASS(progress, nir, nir_opt_dead_cf);
      NIR_PASS(progress, nir, nir_opt_cse);
      NIR_PASS(progress, nir, nir_opt_algebraic);
      NIR_PASS(progress, nir, nir_opt_constant_folding);
   } while (progress);
   /* RC74: in a compute shader, an indexable temp (DXBC x#, a DXIL alloca
    * array) still indexed by a run-time value is the AGX stack (scratch)
    * when it holds more than DXBC_NIR_SCRATCH_ARRAY_BYTES. agx_preprocess_nir
    * moves only arrays of more than 256 bytes there and turns smaller ones
    * into if/else trees, one leaf per element on every access, with the
    * whole array in registers: GravityMark's uint[32] x3 and uint[64] kernels
    * (EXP1969)
    * compile to 255 GPRs (occupancy 384) and 2-4x the instructions. Same
    * lowering and size rules as agx_preprocess_nir, a lower threshold.
    * APPLE_AGX_D3D_SCRATCH_ARRAYS=0 keeps Asahi's rule alone, for a hardware
    * comparison. */
   const char *scratch_arrays = getenv("APPLE_AGX_D3D_SCRATCH_ARRAYS");
   if (nir->info.stage == MESA_SHADER_COMPUTE && (!scratch_arrays || strcmp(scratch_arrays, "0")))
      NIR_PASS(_, nir, nir_lower_vars_to_scratch, DXBC_NIR_SCRATCH_ARRAY_BYTES, glsl_get_natural_size_align_bytes,
               glsl_get_word_size_align_bytes);
   nir_shader_gather_info(nir, nir_shader_get_entrypoint(nir));
   nir_validate_shader(nir, "dxbc_nir_finalize");
}

void
dxbc_nir_compute_info(const nir_shader *nir, unsigned block[3], unsigned *shared_bytes)
{
   for (unsigned i = 0; i < 3; ++i)
      block[i] = nir->info.workgroup_size[i];
   *shared_bytes = nir->info.shared_size;
}

struct nir_shader *
dxbc_translate_tokens(const uint32_t *tokens,
                      const struct nir_shader_compiler_options *options,
                      struct dxbc_nir_error *error)
{
   return dxbc_translate_tokens_hs(tokens, options, 0, 0, error);
}

static struct nir_shader *
translate_tokens(const uint32_t *tokens, const struct nir_shader_compiler_options *options,
                 uint32_t passthrough_registers, uint32_t passthrough_positions,
                 const struct dxbc_nir_xfb *xfb, struct dxbc_nir_uav_slots *uav64,
                 unsigned forced_samples, struct dxbc_nir_error *error);

struct nir_shader *
dxbc_translate_tokens_hs(const uint32_t *tokens,
                         const struct nir_shader_compiler_options *options,
                         uint32_t passthrough_registers, uint32_t passthrough_positions,
                         struct dxbc_nir_error *error)
{
   return translate_tokens(tokens, options, passthrough_registers, passthrough_positions,
                           NULL, NULL, 0, error);
}

struct nir_shader *
dxbc_translate_tokens_uav64(const uint32_t *tokens,
                            const struct nir_shader_compiler_options *options,
                            uint32_t passthrough_registers, uint32_t passthrough_positions,
                            struct dxbc_nir_uav_slots *slots, struct dxbc_nir_error *error)
{
   static struct dxbc_nir_error ignored;
   if (!slots) {
      if (!error)
         error = &ignored;
      memset(error, 0, sizeof(*error));
      snprintf(error->message, sizeof(error->message), "no UAV slot map");
      return NULL;
   }
   return translate_tokens(tokens, options, passthrough_registers, passthrough_positions,
                           NULL, slots, 0, error);
}

struct nir_shader *
dxbc_translate_tokens_tir(const uint32_t *tokens,
                          const struct nir_shader_compiler_options *options,
                          struct dxbc_nir_uav_slots *slots, unsigned forced_samples,
                          struct dxbc_nir_error *error)
{
   static struct dxbc_nir_error ignored;
   if (!slots || !dxbc_tir_pattern(forced_samples)) {
      if (!error)
         error = &ignored;
      memset(error, 0, sizeof(*error));
      snprintf(error->message, sizeof(error->message), "forced sample count %u", forced_samples);
      return NULL;
   }
   struct nir_shader *nir = translate_tokens(tokens, options, 0, 0, NULL, slots, forced_samples, error);
   if (nir && nir->info.stage != MESA_SHADER_FRAGMENT) {
      ralloc_free(nir);
      snprintf(error->message, sizeof(error->message), "forced sample count outside a pixel shader");
      return NULL;
   }
   return nir;
}

struct nir_shader *
dxbc_translate_tokens_xfb(const uint32_t *tokens,
                          const struct nir_shader_compiler_options *options,
                          const struct dxbc_nir_xfb *xfb,
                          struct dxbc_nir_error *error)
{
   static struct dxbc_nir_error ignored;
   if (!xfb) {
      if (!error)
         error = &ignored;
      memset(error, 0, sizeof(*error));
      snprintf(error->message, sizeof(error->message), "no stream output");
      return NULL;
   }
   return translate_tokens(tokens, options, 0, 0, xfb, NULL, 0, error);
}

static struct nir_shader *
translate_tokens(const uint32_t *tokens, const struct nir_shader_compiler_options *options,
                 uint32_t passthrough_registers, uint32_t passthrough_positions,
                 const struct dxbc_nir_xfb *xfb, struct dxbc_nir_uav_slots *uav64,
                 unsigned forced_samples, struct dxbc_nir_error *error)
{
   static struct dxbc_nir_error ignored;
   struct dxbc_program program;
   struct dxbc_error perr;
   enum dxbc_status status;
   nir_shader *nir;

   if (!error)
      error = &ignored;
   memset(error, 0, sizeof(*error));
   if (!tokens || !options) {
      error->opcode = 0x10000u | DXBC_ERROR_TRUNCATED;
      return NULL;
   }
   status = dxbc_parse_program(tokens, tokens[1], &program, &perr);
   if (status != DXBC_OK) {
      snprintf(error->message, sizeof(error->message), "parse status %d", status);
      error->opcode = 0x10000u | (uint32_t)status;
      error->offset = perr.offset;
      return NULL;
   }
   nir = translate(&program, options, passthrough_registers, passthrough_positions, xfb, uav64,
                   forced_samples, error);
   dxbc_program_free(&program);
   if (nir)
      dxbc_nir_finalize(nir);
   return nir;
}

struct nir_shader *
dxbc_translate_tokens_bindings(const uint32_t *tokens,
   const struct nir_shader_compiler_options *options,
   const uint32_t limits[DXBC_BINDING_KIND_COUNT], struct dxbc_binding_map *map,
   struct dxbc_nir_error *error)
{
   return dxbc_translate_tokens_bindings_hs(tokens, options, limits, map, 0, 0, error);
}

struct nir_shader *
dxbc_translate_tokens_bindings_hs(const uint32_t *tokens,
   const struct nir_shader_compiler_options *options,
   const uint32_t limits[DXBC_BINDING_KIND_COUNT], struct dxbc_binding_map *map,
   uint32_t passthrough_registers, uint32_t passthrough_positions,
   struct dxbc_nir_error *error)
{
   return dxbc_translate_tokens_bindings_xfb(tokens, options, limits, map, passthrough_registers,
                                             passthrough_positions, NULL, error);
}

struct nir_shader *
dxbc_translate_tokens_bindings_xfb(const uint32_t *tokens,
   const struct nir_shader_compiler_options *options,
   const uint32_t limits[DXBC_BINDING_KIND_COUNT], struct dxbc_binding_map *map,
   uint32_t passthrough_registers, uint32_t passthrough_positions,
   const struct dxbc_nir_xfb *xfb, struct dxbc_nir_error *error)
{
   struct dxbc_nir_error ignored;
   struct dxbc_program program;
   struct dxbc_error perr = {0};
   enum dxbc_status status;
   if (!error)
      error = &ignored;
   memset(error, 0, sizeof(*error));
   if (map)
      memset(map, 0, sizeof(*map));
   if (!tokens || !options || !limits || !map) {
      error->opcode = 0x10000u | DXBC_ERROR_OPERAND;
      snprintf(error->message, sizeof(error->message), "invalid binding translation arguments");
      return NULL;
   }
   status = dxbc_parse_program(tokens, tokens[1], &program, &perr);
   if (status == DXBC_OK) {
      status = dxbc_prepare_static_bindings(&program, limits, map, &perr);
      if (status == DXBC_OK) {
         nir_shader *nir = translate(&program, options, passthrough_registers,
                                     passthrough_positions, xfb, NULL, 0, error);
         dxbc_program_free(&program);
         if (nir)
            dxbc_nir_finalize(nir);
         else
            memset(map, 0, sizeof(*map));
         return nir;
      }
      dxbc_program_free(&program);
   }
   snprintf(error->message, sizeof(error->message), "static binding parse status %d", status);
   error->opcode = 0x10000u | (uint32_t)status;
   error->offset = perr.offset;
   return NULL;
}

/* C34: a dynamically indexed SM5.1 range (texture_offset) declares the
 * consecutive used slots from its base; a static access of another
 * dimension or the base of another dynamic range ends the run (static
 * accesses and dynamic bases are gathered first). */
static uint8_t
srv_dimension(const nir_tex_instr *tex)
{
   return tex->sampler_dim == GLSL_SAMPLER_DIM_1D ? (tex->is_array ? DXBC_SRV_1D_ARRAY : DXBC_SRV_1D) :
          tex->sampler_dim == GLSL_SAMPLER_DIM_3D ? DXBC_SRV_3D :
          tex->sampler_dim != GLSL_SAMPLER_DIM_CUBE ? DXBC_SRV_OTHER :
          tex->is_array ? DXBC_SRV_CUBE_ARRAY : DXBC_SRV_CUBE;
}

bool dxbc_nir_srv_dimensions(const struct nir_shader *nir,
                             uint8_t dimensions[DXBC_RESOURCE_SLOTS])
{
   if (!dimensions) return false;
   memset(dimensions, 0, DXBC_RESOURCE_SLOTS);
   if (!nir) return false;
   BITSET_DECLARE(dynamic_bases, DXBC_RESOURCE_SLOTS) = {0};
   for (unsigned pass = 0; pass < 2; ++pass) {
      nir_foreach_function_impl(impl, nir) {
         nir_foreach_block(block, impl) {
            nir_foreach_instr(instr, block) {
               if (instr->type != nir_instr_type_tex) continue;
               const nir_tex_instr *tex = nir_instr_as_tex(instr);
               bool dynamic = false;
               /* C47: heap-range accesses use no slot */
               if (nir_tex_instr_src_index(tex, nir_tex_src_texture_handle) >= 0) continue;
               if (tex->texture_index >= DXBC_RESOURCE_SLOTS) goto fail;
               for (unsigned i = 0; i < tex->num_srcs; ++i) {
                  switch (tex->src[i].src_type) {
                  case nir_tex_src_texture_deref:
                     goto fail;
                  case nir_tex_src_texture_offset:
                     dynamic = true;
                     break;
                  default: break;
                  }
               }
               if (dynamic && pass == 0) BITSET_SET(dynamic_bases, tex->texture_index);
               if (dynamic != (pass == 1)) continue;
               const uint8_t dimension = srv_dimension(tex);
               uint8_t *slot = &dimensions[tex->texture_index];
               if (*slot != DXBC_SRV_UNUSED && *slot != dimension) goto fail;
               *slot = dimension;
               for (unsigned t = tex->texture_index + 1; dynamic && t < DXBC_RESOURCE_SLOTS &&
                    BITSET_TEST(nir->info.textures_used, t) && !BITSET_TEST(dynamic_bases, t) &&
                    (dimensions[t] == DXBC_SRV_UNUSED || dimensions[t] == dimension); ++t)
                  dimensions[t] = dimension;
            }
         }
      }
   }
   return true;
fail:
   memset(dimensions, 0, DXBC_RESOURCE_SLOTS);
   return false;
}

/* C23: retain only the new 1D/non-1D distinction; existing other UAV
 * dimensions keep their prior owner policy. Gather before native consumes NIR. */
bool dxbc_nir_uav_dimensions(const struct nir_shader *nir,
                            uint8_t dimensions[DXBC_UAV_DIMENSION_SLOTS])
{
   if (!dimensions) return false;
   memset(dimensions, 0, DXBC_UAV_DIMENSION_SLOTS);
   if (!nir) return false;
   nir_foreach_function_impl(impl, nir) {
      nir_foreach_block(block, impl) {
         nir_foreach_instr(instr, block) {
            if (instr->type != nir_instr_type_intrinsic) continue;
            const nir_intrinsic_instr *intr = nir_instr_as_intrinsic(instr);
            unsigned source = 0;
            bool image = false;
            switch (intr->intrinsic) {
            case nir_intrinsic_image_load:
            case nir_intrinsic_image_sparse_load:
            case nir_intrinsic_image_store:
            case nir_intrinsic_image_atomic:
            case nir_intrinsic_image_atomic_swap:
            case nir_intrinsic_image_size:
            case nir_intrinsic_image_levels:
            case nir_intrinsic_image_samples:
               image = true;
               break;
            case nir_intrinsic_store_ssbo: source = 1; break;
            case nir_intrinsic_load_ssbo:
            case nir_intrinsic_ssbo_atomic:
            case nir_intrinsic_ssbo_atomic_swap:
            case nir_intrinsic_get_ssbo_size:
               break;
            case nir_intrinsic_bindless_image_load:
            case nir_intrinsic_bindless_image_sparse_load:
            case nir_intrinsic_bindless_image_store:
            case nir_intrinsic_bindless_image_atomic:
            case nir_intrinsic_bindless_image_atomic_swap:
            case nir_intrinsic_bindless_image_size:
               continue; /* C47: heap UAVs use no slot */
            default:
               /* Deref/bindless/unhandled image forms cannot establish a
                * direct slot; do not silently label them unused. */
               if (nir_intrinsic_has_image_dim(intr)) goto fail;
               continue;
            }
            if (!nir_src_is_const(intr->src[source])) goto fail;
            unsigned slot = nir_src_as_uint(intr->src[source]);
            if (!image && slot >= DXBC_NIR_UAV_COUNTER_SLOT &&
                slot < DXBC_NIR_UAV_COUNTER_SLOT + DXBC_UAV_DIMENSION_SLOTS)
               slot -= DXBC_NIR_UAV_COUNTER_SLOT;
            if (slot >= DXBC_UAV_DIMENSION_SLOTS) goto fail;
            uint8_t dimension = image && nir_intrinsic_image_dim(intr) == GLSL_SAMPLER_DIM_1D ?
               (nir_intrinsic_image_array(intr) ? DXBC_UAV_1D_ARRAY : DXBC_UAV_1D) : DXBC_UAV_OTHER;
            if (dimensions[slot] && dimensions[slot] != dimension) goto fail;
            dimensions[slot] = dimension;
         }
      }
   }
   return true;
fail:
   memset(dimensions, 0, DXBC_UAV_DIMENSION_SLOTS);
   return false;
}
