/*
 * DXBC shader model 4.0-5.1 container and token-stream parser.
 * See dxbc_parse.h.
 *
 * SPDX-License-Identifier: MIT
 */
#include "dxbc_parse.h"

#include <stdlib.h>
#include <string.h>

/* OpcodeToken0 */
#define TOKEN_OPCODE(t) ((t) & 0x7ffu)
#define TOKEN_LENGTH(t) (((t) >> 24) & 0x7fu)
#define TOKEN_EXTENDED(t) (((t) >> 31) & 1u)
#define TOKEN_SATURATE(t) (((t) >> 13) & 1u)
#define TOKEN_TEST_NONZERO(t) (((t) >> 18) & 1u)
#define TOKEN_PRECISE(t) (((t) >> 19) & 0xfu)
#define TOKEN_CUSTOMDATA_CLASS(t) ((t) >> 11)

/* Extended opcode token */
#define EXT_OPCODE_TYPE(t) ((t) & 0x3fu)
#define EXT_SAMPLE_CONTROLS 1u
#define EXT_RESOURCE_DIM 2u
#define EXT_RESOURCE_RETURN_TYPE 3u

/* OperandToken0 */
#define OPERAND_NUM_COMPONENTS(t) ((t) & 3u)
#define OPERAND_SELECTION(t) (((t) >> 2) & 3u)
#define OPERAND_TYPE(t) (((t) >> 12) & 0xffu)
#define OPERAND_INDEX_DIMENSION(t) (((t) >> 20) & 3u)
#define OPERAND_INDEX_REPRESENTATION(t, d) (((t) >> (22u + 3u * (d))) & 7u)
#define OPERAND_EXTENDED(t) (((t) >> 31) & 1u)

/* Extended operand token */
#define EXT_OPERAND_TYPE(t) ((t) & 0x3fu)
#define EXT_OPERAND_MODIFIER 1u
#define EXT_OPERAND_MOD(t) (((t) >> 6) & 0xffu)
#define EXT_OPERAND_MIN_PRECISION(t) (((t) >> 14) & 7u)
#define EXT_OPERAND_NONUNIFORM(t) (((t) >> 17) & 1u)

struct dxbc_opcode_info {
   const char *name;
   uint8_t declaration;
   /* Leading operands of a declaration (the rest of it is payload);
    * ignored for other instructions, whose tokens are all operands. */
   uint8_t dcl_operands;
};

#define OP(name) { name, 0, 0 }
#define DCL(name, n) { name, 1, n }
#define RESERVED { NULL, 0, 0 }

static const struct dxbc_opcode_info opcode_info[DXBC_OP_COUNT] = {
   [DXBC_OP_ADD] = OP("add"), [DXBC_OP_AND] = OP("and"),
   [DXBC_OP_BREAK] = OP("break"), [DXBC_OP_BREAKC] = OP("breakc"),
   [DXBC_OP_CALL] = OP("call"), [DXBC_OP_CALLC] = OP("callc"),
   [DXBC_OP_CASE] = OP("case"), [DXBC_OP_CONTINUE] = OP("continue"),
   [DXBC_OP_CONTINUEC] = OP("continuec"), [DXBC_OP_CUT] = OP("cut"),
   [DXBC_OP_DEFAULT] = OP("default"), [DXBC_OP_DERIV_RTX] = OP("deriv_rtx"),
   [DXBC_OP_DERIV_RTY] = OP("deriv_rty"), [DXBC_OP_DISCARD] = OP("discard"),
   [DXBC_OP_DIV] = OP("div"), [DXBC_OP_DP2] = OP("dp2"),
   [DXBC_OP_DP3] = OP("dp3"), [DXBC_OP_DP4] = OP("dp4"),
   [DXBC_OP_ELSE] = OP("else"), [DXBC_OP_EMIT] = OP("emit"),
   [DXBC_OP_EMITTHENCUT] = OP("emit_then_cut"), [DXBC_OP_ENDIF] = OP("endif"),
   [DXBC_OP_ENDLOOP] = OP("endloop"), [DXBC_OP_ENDSWITCH] = OP("endswitch"),
   [DXBC_OP_EQ] = OP("eq"), [DXBC_OP_EXP] = OP("exp"), [DXBC_OP_FRC] = OP("frc"),
   [DXBC_OP_FTOI] = OP("ftoi"), [DXBC_OP_FTOU] = OP("ftou"),
   [DXBC_OP_GE] = OP("ge"), [DXBC_OP_IADD] = OP("iadd"), [DXBC_OP_IF] = OP("if"),
   [DXBC_OP_IEQ] = OP("ieq"), [DXBC_OP_IGE] = OP("ige"), [DXBC_OP_ILT] = OP("ilt"),
   [DXBC_OP_IMAD] = OP("imad"), [DXBC_OP_IMAX] = OP("imax"),
   [DXBC_OP_IMIN] = OP("imin"), [DXBC_OP_IMUL] = OP("imul"),
   [DXBC_OP_INE] = OP("ine"), [DXBC_OP_INEG] = OP("ineg"),
   [DXBC_OP_ISHL] = OP("ishl"), [DXBC_OP_ISHR] = OP("ishr"),
   [DXBC_OP_ITOF] = OP("itof"), [DXBC_OP_LABEL] = OP("label"),
   [DXBC_OP_LD] = OP("ld"), [DXBC_OP_LD_MS] = OP("ldms"),
   [DXBC_OP_LOG] = OP("log"), [DXBC_OP_LOOP] = OP("loop"), [DXBC_OP_LT] = OP("lt"),
   [DXBC_OP_MAD] = OP("mad"), [DXBC_OP_MIN] = OP("min"), [DXBC_OP_MAX] = OP("max"),
   [DXBC_OP_CUSTOMDATA] = DCL("customdata", 0),
   [DXBC_OP_MOV] = OP("mov"), [DXBC_OP_MOVC] = OP("movc"),
   [DXBC_OP_MUL] = OP("mul"), [DXBC_OP_NE] = OP("ne"), [DXBC_OP_NOP] = OP("nop"),
   [DXBC_OP_NOT] = OP("not"), [DXBC_OP_OR] = OP("or"),
   [DXBC_OP_RESINFO] = OP("resinfo"), [DXBC_OP_RET] = OP("ret"),
   [DXBC_OP_RETC] = OP("retc"), [DXBC_OP_ROUND_NE] = OP("round_ne"),
   [DXBC_OP_ROUND_NI] = OP("round_ni"), [DXBC_OP_ROUND_PI] = OP("round_pi"),
   [DXBC_OP_ROUND_Z] = OP("round_z"), [DXBC_OP_RSQ] = OP("rsq"),
   [DXBC_OP_SAMPLE] = OP("sample"), [DXBC_OP_SAMPLE_C] = OP("sample_c"),
   [DXBC_OP_SAMPLE_C_LZ] = OP("sample_c_lz"), [DXBC_OP_SAMPLE_L] = OP("sample_l"),
   [DXBC_OP_SAMPLE_D] = OP("sample_d"), [DXBC_OP_SAMPLE_B] = OP("sample_b"),
   [DXBC_OP_SQRT] = OP("sqrt"), [DXBC_OP_SWITCH] = OP("switch"),
   [DXBC_OP_SINCOS] = OP("sincos"), [DXBC_OP_UDIV] = OP("udiv"),
   [DXBC_OP_ULT] = OP("ult"), [DXBC_OP_UGE] = OP("uge"),
   [DXBC_OP_UMUL] = OP("umul"), [DXBC_OP_UMAD] = OP("umad"),
   [DXBC_OP_UMAX] = OP("umax"), [DXBC_OP_UMIN] = OP("umin"),
   [DXBC_OP_USHR] = OP("ushr"), [DXBC_OP_UTOF] = OP("utof"),
   [DXBC_OP_XOR] = OP("xor"),
   [DXBC_OP_DCL_RESOURCE] = DCL("dcl_resource", 1),
   [DXBC_OP_DCL_CONSTANT_BUFFER] = DCL("dcl_constantbuffer", 1),
   [DXBC_OP_DCL_SAMPLER] = DCL("dcl_sampler", 1),
   [DXBC_OP_DCL_INDEX_RANGE] = DCL("dcl_indexrange", 1),
   [DXBC_OP_DCL_GS_OUTPUT_PRIMITIVE_TOPOLOGY] = DCL("dcl_outputtopology", 0),
   [DXBC_OP_DCL_GS_INPUT_PRIMITIVE] = DCL("dcl_inputprimitive", 0),
   [DXBC_OP_DCL_MAX_OUTPUT_VERTEX_COUNT] = DCL("dcl_maxout", 0),
   [DXBC_OP_DCL_INPUT] = DCL("dcl_input", 1),
   [DXBC_OP_DCL_INPUT_SGV] = DCL("dcl_input_sgv", 1),
   [DXBC_OP_DCL_INPUT_SIV] = DCL("dcl_input_siv", 1),
   [DXBC_OP_DCL_INPUT_PS] = DCL("dcl_input_ps", 1),
   [DXBC_OP_DCL_INPUT_PS_SGV] = DCL("dcl_input_ps_sgv", 1),
   [DXBC_OP_DCL_INPUT_PS_SIV] = DCL("dcl_input_ps_siv", 1),
   [DXBC_OP_DCL_OUTPUT] = DCL("dcl_output", 1),
   [DXBC_OP_DCL_OUTPUT_SGV] = DCL("dcl_output_sgv", 1),
   [DXBC_OP_DCL_OUTPUT_SIV] = DCL("dcl_output_siv", 1),
   [DXBC_OP_DCL_TEMPS] = DCL("dcl_temps", 0),
   [DXBC_OP_DCL_INDEXABLE_TEMP] = DCL("dcl_indexabletemp", 0),
   [DXBC_OP_DCL_GLOBAL_FLAGS] = DCL("dcl_globalflags", 0),
   [DXBC_OP_RESERVED_10_0] = RESERVED,
   [DXBC_OP_LOD] = OP("lod"), [DXBC_OP_GATHER4] = OP("gather4"),
   [DXBC_OP_SAMPLE_POS] = OP("samplepos"), [DXBC_OP_SAMPLE_INFO] = OP("sampleinfo"),
   [DXBC_OP_RESERVED_10_1] = RESERVED,
   [DXBC_OP_HS_DECLS] = DCL("hs_decls", 0),
   [DXBC_OP_HS_CONTROL_POINT_PHASE] = DCL("hs_control_point_phase", 0),
   [DXBC_OP_HS_FORK_PHASE] = DCL("hs_fork_phase", 0),
   [DXBC_OP_HS_JOIN_PHASE] = DCL("hs_join_phase", 0),
   [DXBC_OP_EMIT_STREAM] = OP("emit_stream"), [DXBC_OP_CUT_STREAM] = OP("cut_stream"),
   [DXBC_OP_EMITTHENCUT_STREAM] = OP("emit_then_cut_stream"),
   [DXBC_OP_INTERFACE_CALL] = OP("fcall"), [DXBC_OP_BUFINFO] = OP("bufinfo"),
   [DXBC_OP_DERIV_RTX_COARSE] = OP("deriv_rtx_coarse"),
   [DXBC_OP_DERIV_RTX_FINE] = OP("deriv_rtx_fine"),
   [DXBC_OP_DERIV_RTY_COARSE] = OP("deriv_rty_coarse"),
   [DXBC_OP_DERIV_RTY_FINE] = OP("deriv_rty_fine"),
   [DXBC_OP_GATHER4_C] = OP("gather4_c"), [DXBC_OP_GATHER4_PO] = OP("gather4_po"),
   [DXBC_OP_GATHER4_PO_C] = OP("gather4_po_c"), [DXBC_OP_RCP] = OP("rcp"),
   [DXBC_OP_F32TOF16] = OP("f32tof16"), [DXBC_OP_F16TOF32] = OP("f16tof32"),
   [DXBC_OP_UADDC] = OP("uaddc"), [DXBC_OP_USUBB] = OP("usubb"),
   [DXBC_OP_COUNTBITS] = OP("countbits"), [DXBC_OP_FIRSTBIT_HI] = OP("firstbit_hi"),
   [DXBC_OP_FIRSTBIT_LO] = OP("firstbit_lo"),
   [DXBC_OP_FIRSTBIT_SHI] = OP("firstbit_shi"), [DXBC_OP_UBFE] = OP("ubfe"),
   [DXBC_OP_IBFE] = OP("ibfe"), [DXBC_OP_BFI] = OP("bfi"),
   [DXBC_OP_BFREV] = OP("bfrev"), [DXBC_OP_SWAPC] = OP("swapc"),
   [DXBC_OP_DCL_STREAM] = DCL("dcl_stream", 1),
   [DXBC_OP_DCL_FUNCTION_BODY] = DCL("dcl_function_body", 0),
   [DXBC_OP_DCL_FUNCTION_TABLE] = DCL("dcl_function_table", 0),
   [DXBC_OP_DCL_INTERFACE] = DCL("dcl_interface", 0),
   [DXBC_OP_DCL_INPUT_CONTROL_POINT_COUNT] = DCL("dcl_input_control_point_count", 0),
   [DXBC_OP_DCL_OUTPUT_CONTROL_POINT_COUNT] = DCL("dcl_output_control_point_count", 0),
   [DXBC_OP_DCL_TESS_DOMAIN] = DCL("dcl_tessellator_domain", 0),
   [DXBC_OP_DCL_TESS_PARTITIONING] = DCL("dcl_tessellator_partitioning", 0),
   [DXBC_OP_DCL_TESS_OUTPUT_PRIMITIVE] = DCL("dcl_tessellator_output_primitive", 0),
   [DXBC_OP_DCL_HS_MAX_TESSFACTOR] = DCL("dcl_hs_max_tessfactor", 0),
   [DXBC_OP_DCL_HS_FORK_PHASE_INSTANCE_COUNT] = DCL("dcl_hs_fork_phase_instance_count", 0),
   [DXBC_OP_DCL_HS_JOIN_PHASE_INSTANCE_COUNT] = DCL("dcl_hs_join_phase_instance_count", 0),
   [DXBC_OP_DCL_THREAD_GROUP] = DCL("dcl_thread_group", 0),
   [DXBC_OP_DCL_UAV_TYPED] = DCL("dcl_uav_typed", 1),
   [DXBC_OP_DCL_UAV_RAW] = DCL("dcl_uav_raw", 1),
   [DXBC_OP_DCL_UAV_STRUCTURED] = DCL("dcl_uav_structured", 1),
   [DXBC_OP_DCL_TGSM_RAW] = DCL("dcl_tgsm_raw", 1),
   [DXBC_OP_DCL_TGSM_STRUCTURED] = DCL("dcl_tgsm_structured", 1),
   [DXBC_OP_DCL_RESOURCE_RAW] = DCL("dcl_resource_raw", 1),
   [DXBC_OP_DCL_RESOURCE_STRUCTURED] = DCL("dcl_resource_structured", 1),
   [DXBC_OP_LD_UAV_TYPED] = OP("ld_uav_typed"),
   [DXBC_OP_STORE_UAV_TYPED] = OP("store_uav_typed"),
   [DXBC_OP_LD_RAW] = OP("ld_raw"), [DXBC_OP_STORE_RAW] = OP("store_raw"),
   [DXBC_OP_LD_STRUCTURED] = OP("ld_structured"),
   [DXBC_OP_STORE_STRUCTURED] = OP("store_structured"),
   [DXBC_OP_ATOMIC_AND] = OP("atomic_and"), [DXBC_OP_ATOMIC_OR] = OP("atomic_or"),
   [DXBC_OP_ATOMIC_XOR] = OP("atomic_xor"),
   [DXBC_OP_ATOMIC_CMP_STORE] = OP("atomic_cmp_store"),
   [DXBC_OP_ATOMIC_IADD] = OP("atomic_iadd"), [DXBC_OP_ATOMIC_IMAX] = OP("atomic_imax"),
   [DXBC_OP_ATOMIC_IMIN] = OP("atomic_imin"), [DXBC_OP_ATOMIC_UMAX] = OP("atomic_umax"),
   [DXBC_OP_ATOMIC_UMIN] = OP("atomic_umin"),
   [DXBC_OP_IMM_ATOMIC_ALLOC] = OP("imm_atomic_alloc"),
   [DXBC_OP_IMM_ATOMIC_CONSUME] = OP("imm_atomic_consume"),
   [DXBC_OP_IMM_ATOMIC_IADD] = OP("imm_atomic_iadd"),
   [DXBC_OP_IMM_ATOMIC_AND] = OP("imm_atomic_and"),
   [DXBC_OP_IMM_ATOMIC_OR] = OP("imm_atomic_or"),
   [DXBC_OP_IMM_ATOMIC_XOR] = OP("imm_atomic_xor"),
   [DXBC_OP_IMM_ATOMIC_EXCH] = OP("imm_atomic_exch"),
   [DXBC_OP_IMM_ATOMIC_CMP_EXCH] = OP("imm_atomic_cmp_exch"),
   [DXBC_OP_IMM_ATOMIC_IMAX] = OP("imm_atomic_imax"),
   [DXBC_OP_IMM_ATOMIC_IMIN] = OP("imm_atomic_imin"),
   [DXBC_OP_IMM_ATOMIC_UMAX] = OP("imm_atomic_umax"),
   [DXBC_OP_IMM_ATOMIC_UMIN] = OP("imm_atomic_umin"),
   [DXBC_OP_SYNC] = OP("sync"), [DXBC_OP_DADD] = OP("dadd"),
   [DXBC_OP_DMAX] = OP("dmax"), [DXBC_OP_DMIN] = OP("dmin"),
   [DXBC_OP_DMUL] = OP("dmul"), [DXBC_OP_DEQ] = OP("deq"), [DXBC_OP_DGE] = OP("dge"),
   [DXBC_OP_DLT] = OP("dlt"), [DXBC_OP_DNE] = OP("dne"), [DXBC_OP_DMOV] = OP("dmov"),
   [DXBC_OP_DMOVC] = OP("dmovc"), [DXBC_OP_DTOF] = OP("dtof"),
   [DXBC_OP_FTOD] = OP("ftod"), [DXBC_OP_EVAL_SNAPPED] = OP("eval_snapped"),
   [DXBC_OP_EVAL_SAMPLE_INDEX] = OP("eval_sample_index"),
   [DXBC_OP_EVAL_CENTROID] = OP("eval_centroid"),
   [DXBC_OP_DCL_GS_INSTANCE_COUNT] = DCL("dcl_gsinstances", 0),
   [DXBC_OP_ABORT] = OP("abort"), [DXBC_OP_DEBUG_BREAK] = OP("debug_break"),
   [DXBC_OP_RESERVED_11_0] = RESERVED,
   [DXBC_OP_DDIV] = OP("ddiv"), [DXBC_OP_DFMA] = OP("dfma"),
   [DXBC_OP_DRCP] = OP("drcp"), [DXBC_OP_MSAD] = OP("msad"),
   [DXBC_OP_DTOI] = OP("dtoi"), [DXBC_OP_DTOU] = OP("dtou"),
   [DXBC_OP_ITOD] = OP("itod"), [DXBC_OP_UTOD] = OP("utod"),
   [DXBC_OP_RESERVED_11_1] = RESERVED,
   [DXBC_OP_GATHER4_FEEDBACK] = OP("gather4_s"),
   [DXBC_OP_GATHER4_C_FEEDBACK] = OP("gather4_c_s"),
   [DXBC_OP_GATHER4_PO_FEEDBACK] = OP("gather4_po_s"),
   [DXBC_OP_GATHER4_PO_C_FEEDBACK] = OP("gather4_po_c_s"),
   [DXBC_OP_LD_FEEDBACK] = OP("ld_s"), [DXBC_OP_LD_MS_FEEDBACK] = OP("ldms_s"),
   [DXBC_OP_LD_UAV_TYPED_FEEDBACK] = OP("ld_uav_typed_s"),
   [DXBC_OP_LD_RAW_FEEDBACK] = OP("ld_raw_s"),
   [DXBC_OP_LD_STRUCTURED_FEEDBACK] = OP("ld_structured_s"),
   [DXBC_OP_SAMPLE_L_FEEDBACK] = OP("sample_l_s"),
   [DXBC_OP_SAMPLE_C_LZ_FEEDBACK] = OP("sample_c_lz_s"),
   [DXBC_OP_SAMPLE_CLAMP_FEEDBACK] = OP("sample_cl_s"),
   [DXBC_OP_SAMPLE_B_CLAMP_FEEDBACK] = OP("sample_b_cl_s"),
   [DXBC_OP_SAMPLE_D_CLAMP_FEEDBACK] = OP("sample_d_cl_s"),
   [DXBC_OP_SAMPLE_C_CLAMP_FEEDBACK] = OP("sample_c_cl_s"),
   [DXBC_OP_CHECK_ACCESS_FULLY_MAPPED] = OP("check_access_fully_mapped"),
};

const char *
dxbc_opcode_name(uint32_t opcode)
{
   return opcode < DXBC_OP_COUNT ? opcode_info[opcode].name : NULL;
}

bool
dxbc_opcode_is_declaration(uint32_t opcode)
{
   return opcode < DXBC_OP_COUNT && opcode_info[opcode].declaration;
}

struct parse_state {
   const uint32_t *tokens;
   uint32_t end;              /* DWORD bound of the current instruction */
   uint32_t pos;
   struct dxbc_program *program;
   unsigned relative_capacity;
};

static enum dxbc_status
read_token(struct parse_state *s, uint32_t *value)
{
   if (s->pos >= s->end)
      return DXBC_ERROR_TRUNCATED;
   *value = s->tokens[s->pos++];
   return DXBC_OK;
}

static enum dxbc_status parse_operand(struct parse_state *s,
                                      struct dxbc_operand *operand,
                                      unsigned depth);

static enum dxbc_status
alloc_relative(struct parse_state *s, struct dxbc_operand **out)
{
   /* Relative operands live in one growable array; their pointers are
    * fixed up when it moves, so store indices until the end. */
   struct dxbc_program *p = s->program;
   if (p->num_relative == s->relative_capacity) {
      unsigned capacity = s->relative_capacity ? s->relative_capacity * 2 : 16;
      struct dxbc_operand *grown =
         realloc(p->relative, capacity * sizeof(*grown));
      if (!grown)
         return DXBC_ERROR_NO_MEMORY;
      p->relative = grown;
      s->relative_capacity = capacity;
   }
   memset(&p->relative[p->num_relative], 0, sizeof(p->relative[0]));
   /* index + 1 so that NULL still means "no relative part" */
   *out = (struct dxbc_operand *)(uintptr_t)(p->num_relative + 1);
   ++p->num_relative;
   return DXBC_OK;
}

static enum dxbc_status
parse_index(struct parse_state *s, struct dxbc_index *index,
            unsigned representation, unsigned depth)
{
   enum dxbc_status status = DXBC_OK;
   uint32_t lo, hi;
   index->representation = (uint8_t)representation;
   switch (representation) {
   case DXBC_INDEX_IMM32:
   case DXBC_INDEX_IMM32_PLUS_RELATIVE:
      status = read_token(s, &lo);
      index->imm = lo;
      break;
   case DXBC_INDEX_IMM64:
   case DXBC_INDEX_IMM64_PLUS_RELATIVE:
      /* HI32 then LO32 */
      if ((status = read_token(s, &hi)) == DXBC_OK &&
          (status = read_token(s, &lo)) == DXBC_OK)
         index->imm = ((uint64_t)hi << 32) | lo;
      break;
   case DXBC_INDEX_RELATIVE:
      index->imm = 0;
      break;
   default:
      return DXBC_ERROR_OPERAND;
   }
   if (status != DXBC_OK)
      return status;
   if (representation == DXBC_INDEX_RELATIVE ||
       representation == DXBC_INDEX_IMM32_PLUS_RELATIVE ||
       representation == DXBC_INDEX_IMM64_PLUS_RELATIVE) {
      struct dxbc_operand *slot;
      unsigned which;
      if (depth >= 2)
         return DXBC_ERROR_OPERAND;
      if ((status = alloc_relative(s, &slot)) != DXBC_OK)
         return status;
      which = (unsigned)(uintptr_t)slot - 1;
      {
         struct dxbc_operand rel;
         memset(&rel, 0, sizeof(rel));
         status = parse_operand(s, &rel, depth + 1);
         if (status != DXBC_OK)
            return status;
         s->program->relative[which] = rel;
      }
      index->relative = slot;
   }
   return DXBC_OK;
}

static enum dxbc_status
parse_operand(struct parse_state *s, struct dxbc_operand *operand,
              unsigned depth)
{
   enum dxbc_status status;
   uint32_t token, ext, c;
   unsigned d, n;

   if ((status = read_token(s, &token)) != DXBC_OK)
      return status;
   operand->type = (uint8_t)OPERAND_TYPE(token);
   switch (OPERAND_NUM_COMPONENTS(token)) {
   case 0: operand->num_components = 0; break;
   case 1: operand->num_components = 1; break;
   case 2: operand->num_components = 4; break;
   default: return DXBC_ERROR_OPERAND; /* N-component: undefined */
   }
   for (c = 0; c < 4; ++c)
      operand->swizzle[c] = (uint8_t)c;
   operand->mask = operand->num_components == 1 ? 1 : 0;
   if (operand->num_components == 4) {
      operand->selection = (uint8_t)OPERAND_SELECTION(token);
      switch (operand->selection) {
      case DXBC_SELECT_MASK:
         operand->mask = (uint8_t)((token >> 4) & 0xfu);
         break;
      case DXBC_SELECT_SWIZZLE:
         for (c = 0; c < 4; ++c)
            operand->swizzle[c] = (uint8_t)((token >> (4 + 2 * c)) & 3u);
         break;
      case DXBC_SELECT_ONE:
         for (c = 0; c < 4; ++c)
            operand->swizzle[c] = (uint8_t)((token >> 4) & 3u);
         break;
      default:
         return DXBC_ERROR_OPERAND;
      }
   }
   operand->index_dimension = (uint8_t)OPERAND_INDEX_DIMENSION(token);

   ext = token;
   while (OPERAND_EXTENDED(ext)) {
      if ((status = read_token(s, &ext)) != DXBC_OK)
         return status;
      if (EXT_OPERAND_TYPE(ext) == EXT_OPERAND_MODIFIER) {
         operand->modifier = (uint8_t)EXT_OPERAND_MOD(ext);
         operand->min_precision = (uint8_t)EXT_OPERAND_MIN_PRECISION(ext);
         operand->nonuniform = EXT_OPERAND_NONUNIFORM(ext) != 0;
         if (operand->modifier > DXBC_MOD_ABSNEG)
            return DXBC_ERROR_OPERAND;
      } else if (EXT_OPERAND_TYPE(ext) != 0) {
         return DXBC_ERROR_OPERAND;
      }
   }

   if (operand->type == DXBC_OPERAND_IMMEDIATE32) {
      n = operand->num_components;
      if (n != 1 && n != 4)
         return DXBC_ERROR_OPERAND;
      for (c = 0; c < n; ++c)
         if ((status = read_token(s, &operand->imm32[c])) != DXBC_OK)
            return status;
   } else if (operand->type == DXBC_OPERAND_IMMEDIATE64) {
      /* one double (scalar) or two (a 4-component vector holds 2) */
      n = operand->num_components == 1 ? 1 :
          operand->num_components == 4 ? 2 : 0;
      if (!n)
         return DXBC_ERROR_OPERAND;
      for (c = 0; c < n; ++c) {
         uint32_t lo, hi;
         if ((status = read_token(s, &lo)) != DXBC_OK ||
             (status = read_token(s, &hi)) != DXBC_OK)
            return status;
         operand->imm64[c] = ((uint64_t)hi << 32) | lo;
      }
   }

   for (d = 0; d < operand->index_dimension; ++d) {
      status = parse_index(s, &operand->index[d],
                           OPERAND_INDEX_REPRESENTATION(token, d), depth);
      if (status != DXBC_OK)
         return status;
   }
   return DXBC_OK;
}

static void
fixup_relative(struct dxbc_program *p, struct dxbc_operand *operand)
{
   for (unsigned d = 0; d < 3; ++d) {
      uintptr_t slot = (uintptr_t)operand->index[d].relative;
      if (slot)
         operand->index[d].relative = &p->relative[slot - 1];
   }
}

static enum dxbc_status
parse_instruction(struct parse_state *s, uint32_t start, uint32_t limit,
                  struct dxbc_instruction *insn)
{
   enum dxbc_status status;
   uint32_t token = s->tokens[start], ext, length, opcode;
   const struct dxbc_opcode_info *info;

   memset(insn, 0, sizeof(*insn));
   opcode = TOKEN_OPCODE(token);
   insn->opcode = opcode;
   insn->token0 = token;
   insn->offset = start;
   if (opcode >= DXBC_OP_COUNT || !opcode_info[opcode].name)
      return DXBC_ERROR_OPCODE;
   info = &opcode_info[opcode];

   if (opcode == DXBC_OP_CUSTOMDATA) {
      /* OpcodeToken0, total length DWORD, data */
      if (start + 1 >= limit)
         return DXBC_ERROR_TRUNCATED;
      length = s->tokens[start + 1];
      if (length < 2 || length > limit - start)
         return DXBC_ERROR_TRUNCATED;
      insn->length = length;
      insn->payload = &s->tokens[start + 2];
      insn->num_payload = length - 2;
      return DXBC_OK;
   }

   length = TOKEN_LENGTH(token);
   s->pos = start + 1;
   s->end = limit;
   if (info->declaration && TOKEN_EXTENDED(token)) {
      /* A declaration whose length does not fit in 7 bits sets bit 31 and
       * carries it in the next DWORD (function tables, interfaces). */
      if ((status = read_token(s, &length)) != DXBC_OK)
         return status;
   }
   if (length == 0)
      return DXBC_ERROR_LENGTH;
   if (length > limit - start)
      return DXBC_ERROR_TRUNCATED;
   insn->length = length;
   s->end = start + length;

   insn->saturate = TOKEN_SATURATE(token) != 0;
   insn->test_nonzero = TOKEN_TEST_NONZERO(token) != 0;
   insn->precise_mask = (uint8_t)TOKEN_PRECISE(token);

   /* Extended opcode tokens (not on declarations, whose bit 31 is
    * reserved for an extended length). */
   ext = token;
   while (!info->declaration && TOKEN_EXTENDED(ext)) {
      if ((status = read_token(s, &ext)) != DXBC_OK)
         return status;
      switch (EXT_OPCODE_TYPE(ext)) {
      case EXT_SAMPLE_CONTROLS:
         for (unsigned c = 0; c < 3; ++c) {
            int v = (int)((ext >> (9 + 4 * c)) & 0xfu);
            insn->texel_offset[c] = (int8_t)(v >= 8 ? v - 16 : v);
         }
         break;
      case EXT_RESOURCE_DIM:
         insn->resource_dimension = (uint8_t)((ext >> 6) & 0x1fu);
         insn->structure_stride = (ext >> 11) & 0xfffu;
         break;
      case EXT_RESOURCE_RETURN_TYPE:
         for (unsigned c = 0; c < 4; ++c)
            insn->return_type[c] = (uint8_t)((ext >> (6 + 4 * c)) & 0xfu);
         break;
      default:
         return DXBC_ERROR_OPCODE;
      }
   }

   if (info->declaration) {
      unsigned want = info->dcl_operands;
      for (unsigned i = 0; i < want; ++i) {
         status = parse_operand(s, &insn->operands[insn->num_operands], 0);
         if (status != DXBC_OK)
            return status;
         ++insn->num_operands;
      }
      insn->payload = &s->tokens[s->pos];
      insn->num_payload = s->end - s->pos;
      s->pos = s->end;
      return DXBC_OK;
   }

   if (opcode == DXBC_OP_INTERFACE_CALL) {
      /* function index DWORD, then the interface operand */
      if (s->pos >= s->end)
         return DXBC_ERROR_TRUNCATED;
      insn->payload = &s->tokens[s->pos];
      insn->num_payload = 1;
      ++s->pos;
   }

   while (s->pos < s->end) {
      /* fxc pads samplepos (ps_4_1) with one zero DWORD inside the
       * instruction length; a zero token is no valid operand (a temp
       * register with no index), so zero tail DWORDs are padding. */
      bool padding = true;
      for (uint32_t i = s->pos; i < s->end; ++i)
         padding = padding && s->tokens[i] == 0;
      if (padding && insn->num_operands) {
         insn->padding = s->end - s->pos;
         s->pos = s->end;
         break;
      }
      if (insn->num_operands == DXBC_MAX_OPERANDS)
         return DXBC_ERROR_LIMIT;
      status = parse_operand(s, &insn->operands[insn->num_operands], 0);
      if (status != DXBC_OK)
         return status == DXBC_ERROR_TRUNCATED ? DXBC_ERROR_LENGTH : status;
      ++insn->num_operands;
   }
   return s->pos == s->end ? DXBC_OK : DXBC_ERROR_LENGTH;
}

enum dxbc_status
dxbc_parse_program(const uint32_t *tokens, size_t dwords,
                   struct dxbc_program *program, struct dxbc_error *error)
{
   struct parse_state s;
   enum dxbc_status status = DXBC_OK;
   unsigned capacity = 0;
   uint32_t pos, length;

   memset(program, 0, sizeof(*program));
   if (error)
      memset(error, 0, sizeof(*error));
   if (!tokens || dwords < 2)
      return DXBC_ERROR_TRUNCATED;
   program->type = (tokens[0] >> 16) & 0xffffu;
   program->major = (tokens[0] >> 4) & 0xfu;
   program->minor = tokens[0] & 0xfu;
   length = tokens[1];
   if (program->type > DXBC_PROGRAM_COMPUTE || program->major < 4 ||
       program->major > 5)
      return DXBC_ERROR_VERSION;
   if (length < 2 || length > dwords)
      return DXBC_ERROR_TRUNCATED;
   program->length = length;

   memset(&s, 0, sizeof(s));
   s.tokens = tokens;
   s.program = program;
   for (pos = 2; pos < length;) {
      struct dxbc_instruction *insn;
      if (program->num_instructions == capacity) {
         unsigned grown_capacity = capacity ? capacity * 2 : 64;
         struct dxbc_instruction *grown = realloc(program->instructions,
            grown_capacity * sizeof(*grown));
         if (!grown) {
            status = DXBC_ERROR_NO_MEMORY;
            break;
         }
         program->instructions = grown;
         capacity = grown_capacity;
      }
      insn = &program->instructions[program->num_instructions];
      status = parse_instruction(&s, pos, length, insn);
      if (status != DXBC_OK) {
         if (error) {
            error->status = status;
            error->offset = pos;
            error->opcode = TOKEN_OPCODE(tokens[pos]);
         }
         break;
      }
      ++program->num_instructions;
      pos += insn->length;
   }
   if (status != DXBC_OK) {
      dxbc_program_free(program);
      return status;
   }
   for (unsigned i = 0; i < program->num_instructions; ++i)
      for (unsigned o = 0; o < program->instructions[i].num_operands; ++o)
         fixup_relative(program, &program->instructions[i].operands[o]);
   for (unsigned r = 0; r < program->num_relative; ++r)
      fixup_relative(program, &program->relative[r]);
   return DXBC_OK;
}

void
dxbc_program_free(struct dxbc_program *program)
{
   if (!program)
      return;
   free(program->instructions);
   free(program->relative);
   memset(program, 0, sizeof(*program));
}

static uint32_t
read_le32(const uint8_t *p)
{
   return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) |
          ((uint32_t)p[3] << 24);
}

bool
dxbc_container_find_chunk(const void *container, size_t bytes,
                          uint32_t fourcc, const void **chunk,
                          uint32_t *chunk_bytes)
{
   /* 'DXBC', 16-byte digest, version (1), total size, chunk count, then
    * one offset per chunk; each chunk is fourcc, size, data. */
   const uint8_t *base = container;
   uint32_t total, count;
   if (!base || bytes < 32 || read_le32(base) != DXBC_FOURCC('D', 'X', 'B', 'C'))
      return false;
   total = read_le32(base + 24);
   count = read_le32(base + 28);
   if (total > bytes || total < 32 || count > (total - 32) / 4)
      return false;
   for (uint32_t i = 0; i < count; ++i) {
      uint32_t offset = read_le32(base + 32 + 4 * i), size;
      if (offset > total || total - offset < 8)
         return false;
      size = read_le32(base + offset + 4);
      if (size > total - offset - 8)
         return false;
      if (read_le32(base + offset) == fourcc) {
         *chunk = base + offset + 8;
         *chunk_bytes = size;
         return true;
      }
   }
   return false;
}
