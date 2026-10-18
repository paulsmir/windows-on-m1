/*
 * DXBC shader model 4.0-5.1 container and token-stream parser.
 *
 * The D3D10/11 user-mode DDI hands a driver the tokenized program (the
 * SHDR/SHEX chunk body); D3D12 and offline tools hand the whole DXBC
 * container.  This parser decodes either into instructions whose operands,
 * extended opcode tokens and declaration payloads are fully decoded, and
 * proves that every instruction consumes exactly the DWORDs its length
 * token states.  It knows nothing about any graphics API or compiler, so a
 * translator (DXBC -> NIR) and tools can share it.
 *
 * Token layouts and numeric values follow Microsoft's tokenized program
 * format specification (d3d10/d3d11/d3d12TokenizedProgramFormat.hpp in the
 * Windows SDK); the names here are this project's own.
 *
 * SPDX-License-Identifier: MIT
 */
#ifndef DXBC_PARSE_H
#define DXBC_PARSE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

enum dxbc_program_type {
   DXBC_PROGRAM_PIXEL = 0,
   DXBC_PROGRAM_VERTEX = 1,
   DXBC_PROGRAM_GEOMETRY = 2,
   DXBC_PROGRAM_HULL = 3,
   DXBC_PROGRAM_DOMAIN = 4,
   DXBC_PROGRAM_COMPUTE = 5,
};

/* Opcode numbers (OpcodeToken0 [10:00]). */
enum dxbc_opcode {
   DXBC_OP_ADD = 0, DXBC_OP_AND, DXBC_OP_BREAK, DXBC_OP_BREAKC, DXBC_OP_CALL,
   DXBC_OP_CALLC, DXBC_OP_CASE, DXBC_OP_CONTINUE, DXBC_OP_CONTINUEC,
   DXBC_OP_CUT, DXBC_OP_DEFAULT, DXBC_OP_DERIV_RTX, DXBC_OP_DERIV_RTY,
   DXBC_OP_DISCARD, DXBC_OP_DIV, DXBC_OP_DP2, DXBC_OP_DP3, DXBC_OP_DP4,
   DXBC_OP_ELSE, DXBC_OP_EMIT, DXBC_OP_EMITTHENCUT, DXBC_OP_ENDIF,
   DXBC_OP_ENDLOOP, DXBC_OP_ENDSWITCH, DXBC_OP_EQ, DXBC_OP_EXP, DXBC_OP_FRC,
   DXBC_OP_FTOI, DXBC_OP_FTOU, DXBC_OP_GE, DXBC_OP_IADD, DXBC_OP_IF,
   DXBC_OP_IEQ, DXBC_OP_IGE, DXBC_OP_ILT, DXBC_OP_IMAD, DXBC_OP_IMAX,
   DXBC_OP_IMIN, DXBC_OP_IMUL, DXBC_OP_INE, DXBC_OP_INEG, DXBC_OP_ISHL,
   DXBC_OP_ISHR, DXBC_OP_ITOF, DXBC_OP_LABEL, DXBC_OP_LD, DXBC_OP_LD_MS,
   DXBC_OP_LOG, DXBC_OP_LOOP, DXBC_OP_LT, DXBC_OP_MAD, DXBC_OP_MIN,
   DXBC_OP_MAX, DXBC_OP_CUSTOMDATA, DXBC_OP_MOV, DXBC_OP_MOVC, DXBC_OP_MUL,
   DXBC_OP_NE, DXBC_OP_NOP, DXBC_OP_NOT, DXBC_OP_OR, DXBC_OP_RESINFO,
   DXBC_OP_RET, DXBC_OP_RETC, DXBC_OP_ROUND_NE, DXBC_OP_ROUND_NI,
   DXBC_OP_ROUND_PI, DXBC_OP_ROUND_Z, DXBC_OP_RSQ, DXBC_OP_SAMPLE,
   DXBC_OP_SAMPLE_C, DXBC_OP_SAMPLE_C_LZ, DXBC_OP_SAMPLE_L, DXBC_OP_SAMPLE_D,
   DXBC_OP_SAMPLE_B, DXBC_OP_SQRT, DXBC_OP_SWITCH, DXBC_OP_SINCOS,
   DXBC_OP_UDIV, DXBC_OP_ULT, DXBC_OP_UGE, DXBC_OP_UMUL, DXBC_OP_UMAD,
   DXBC_OP_UMAX, DXBC_OP_UMIN, DXBC_OP_USHR, DXBC_OP_UTOF, DXBC_OP_XOR,
   DXBC_OP_DCL_RESOURCE, DXBC_OP_DCL_CONSTANT_BUFFER, DXBC_OP_DCL_SAMPLER,
   DXBC_OP_DCL_INDEX_RANGE, DXBC_OP_DCL_GS_OUTPUT_PRIMITIVE_TOPOLOGY,
   DXBC_OP_DCL_GS_INPUT_PRIMITIVE, DXBC_OP_DCL_MAX_OUTPUT_VERTEX_COUNT,
   DXBC_OP_DCL_INPUT, DXBC_OP_DCL_INPUT_SGV, DXBC_OP_DCL_INPUT_SIV,
   DXBC_OP_DCL_INPUT_PS, DXBC_OP_DCL_INPUT_PS_SGV, DXBC_OP_DCL_INPUT_PS_SIV,
   DXBC_OP_DCL_OUTPUT, DXBC_OP_DCL_OUTPUT_SGV, DXBC_OP_DCL_OUTPUT_SIV,
   DXBC_OP_DCL_TEMPS, DXBC_OP_DCL_INDEXABLE_TEMP, DXBC_OP_DCL_GLOBAL_FLAGS,
   DXBC_OP_RESERVED_10_0,
   /* shader model 4.1 */
   DXBC_OP_LOD, DXBC_OP_GATHER4, DXBC_OP_SAMPLE_POS, DXBC_OP_SAMPLE_INFO,
   DXBC_OP_RESERVED_10_1,
   /* shader model 5.0 */
   DXBC_OP_HS_DECLS, DXBC_OP_HS_CONTROL_POINT_PHASE, DXBC_OP_HS_FORK_PHASE,
   DXBC_OP_HS_JOIN_PHASE, DXBC_OP_EMIT_STREAM, DXBC_OP_CUT_STREAM,
   DXBC_OP_EMITTHENCUT_STREAM, DXBC_OP_INTERFACE_CALL, DXBC_OP_BUFINFO,
   DXBC_OP_DERIV_RTX_COARSE, DXBC_OP_DERIV_RTX_FINE, DXBC_OP_DERIV_RTY_COARSE,
   DXBC_OP_DERIV_RTY_FINE, DXBC_OP_GATHER4_C, DXBC_OP_GATHER4_PO,
   DXBC_OP_GATHER4_PO_C, DXBC_OP_RCP, DXBC_OP_F32TOF16, DXBC_OP_F16TOF32,
   DXBC_OP_UADDC, DXBC_OP_USUBB, DXBC_OP_COUNTBITS, DXBC_OP_FIRSTBIT_HI,
   DXBC_OP_FIRSTBIT_LO, DXBC_OP_FIRSTBIT_SHI, DXBC_OP_UBFE, DXBC_OP_IBFE,
   DXBC_OP_BFI, DXBC_OP_BFREV, DXBC_OP_SWAPC, DXBC_OP_DCL_STREAM,
   DXBC_OP_DCL_FUNCTION_BODY, DXBC_OP_DCL_FUNCTION_TABLE,
   DXBC_OP_DCL_INTERFACE, DXBC_OP_DCL_INPUT_CONTROL_POINT_COUNT,
   DXBC_OP_DCL_OUTPUT_CONTROL_POINT_COUNT, DXBC_OP_DCL_TESS_DOMAIN,
   DXBC_OP_DCL_TESS_PARTITIONING, DXBC_OP_DCL_TESS_OUTPUT_PRIMITIVE,
   DXBC_OP_DCL_HS_MAX_TESSFACTOR, DXBC_OP_DCL_HS_FORK_PHASE_INSTANCE_COUNT,
   DXBC_OP_DCL_HS_JOIN_PHASE_INSTANCE_COUNT, DXBC_OP_DCL_THREAD_GROUP,
   DXBC_OP_DCL_UAV_TYPED, DXBC_OP_DCL_UAV_RAW, DXBC_OP_DCL_UAV_STRUCTURED,
   DXBC_OP_DCL_TGSM_RAW, DXBC_OP_DCL_TGSM_STRUCTURED, DXBC_OP_DCL_RESOURCE_RAW,
   DXBC_OP_DCL_RESOURCE_STRUCTURED, DXBC_OP_LD_UAV_TYPED,
   DXBC_OP_STORE_UAV_TYPED, DXBC_OP_LD_RAW, DXBC_OP_STORE_RAW,
   DXBC_OP_LD_STRUCTURED, DXBC_OP_STORE_STRUCTURED, DXBC_OP_ATOMIC_AND,
   DXBC_OP_ATOMIC_OR, DXBC_OP_ATOMIC_XOR, DXBC_OP_ATOMIC_CMP_STORE,
   DXBC_OP_ATOMIC_IADD, DXBC_OP_ATOMIC_IMAX, DXBC_OP_ATOMIC_IMIN,
   DXBC_OP_ATOMIC_UMAX, DXBC_OP_ATOMIC_UMIN, DXBC_OP_IMM_ATOMIC_ALLOC,
   DXBC_OP_IMM_ATOMIC_CONSUME, DXBC_OP_IMM_ATOMIC_IADD, DXBC_OP_IMM_ATOMIC_AND,
   DXBC_OP_IMM_ATOMIC_OR, DXBC_OP_IMM_ATOMIC_XOR, DXBC_OP_IMM_ATOMIC_EXCH,
   DXBC_OP_IMM_ATOMIC_CMP_EXCH, DXBC_OP_IMM_ATOMIC_IMAX,
   DXBC_OP_IMM_ATOMIC_IMIN, DXBC_OP_IMM_ATOMIC_UMAX, DXBC_OP_IMM_ATOMIC_UMIN,
   DXBC_OP_SYNC, DXBC_OP_DADD, DXBC_OP_DMAX, DXBC_OP_DMIN, DXBC_OP_DMUL,
   DXBC_OP_DEQ, DXBC_OP_DGE, DXBC_OP_DLT, DXBC_OP_DNE, DXBC_OP_DMOV,
   DXBC_OP_DMOVC, DXBC_OP_DTOF, DXBC_OP_FTOD, DXBC_OP_EVAL_SNAPPED,
   DXBC_OP_EVAL_SAMPLE_INDEX, DXBC_OP_EVAL_CENTROID,
   DXBC_OP_DCL_GS_INSTANCE_COUNT, DXBC_OP_ABORT, DXBC_OP_DEBUG_BREAK,
   DXBC_OP_RESERVED_11_0,
   /* D3D 11.1 */
   DXBC_OP_DDIV, DXBC_OP_DFMA, DXBC_OP_DRCP, DXBC_OP_MSAD, DXBC_OP_DTOI,
   DXBC_OP_DTOU, DXBC_OP_ITOD, DXBC_OP_UTOD,
   DXBC_OP_RESERVED_11_1,
   /* WDDM 1.3 (tiled resources) */
   DXBC_OP_GATHER4_FEEDBACK, DXBC_OP_GATHER4_C_FEEDBACK,
   DXBC_OP_GATHER4_PO_FEEDBACK, DXBC_OP_GATHER4_PO_C_FEEDBACK,
   DXBC_OP_LD_FEEDBACK, DXBC_OP_LD_MS_FEEDBACK, DXBC_OP_LD_UAV_TYPED_FEEDBACK,
   DXBC_OP_LD_RAW_FEEDBACK, DXBC_OP_LD_STRUCTURED_FEEDBACK,
   DXBC_OP_SAMPLE_L_FEEDBACK, DXBC_OP_SAMPLE_C_LZ_FEEDBACK,
   DXBC_OP_SAMPLE_CLAMP_FEEDBACK, DXBC_OP_SAMPLE_B_CLAMP_FEEDBACK,
   DXBC_OP_SAMPLE_D_CLAMP_FEEDBACK, DXBC_OP_SAMPLE_C_CLAMP_FEEDBACK,
   DXBC_OP_CHECK_ACCESS_FULLY_MAPPED,
   DXBC_OP_COUNT
};

enum dxbc_operand_type {
   DXBC_OPERAND_TEMP = 0,
   DXBC_OPERAND_INPUT = 1,
   DXBC_OPERAND_OUTPUT = 2,
   DXBC_OPERAND_INDEXABLE_TEMP = 3,
   DXBC_OPERAND_IMMEDIATE32 = 4,
   DXBC_OPERAND_IMMEDIATE64 = 5,
   DXBC_OPERAND_SAMPLER = 6,
   DXBC_OPERAND_RESOURCE = 7,
   DXBC_OPERAND_CONSTANT_BUFFER = 8,
   DXBC_OPERAND_IMMEDIATE_CONSTANT_BUFFER = 9,
   DXBC_OPERAND_LABEL = 10,
   DXBC_OPERAND_INPUT_PRIMITIVEID = 11,
   DXBC_OPERAND_OUTPUT_DEPTH = 12,
   DXBC_OPERAND_NULL = 13,
   DXBC_OPERAND_RASTERIZER = 14,
   DXBC_OPERAND_OUTPUT_COVERAGE_MASK = 15,
   DXBC_OPERAND_STREAM = 16,
   DXBC_OPERAND_FUNCTION_BODY = 17,
   DXBC_OPERAND_FUNCTION_TABLE = 18,
   DXBC_OPERAND_INTERFACE = 19,
   DXBC_OPERAND_FUNCTION_INPUT = 20,
   DXBC_OPERAND_FUNCTION_OUTPUT = 21,
   DXBC_OPERAND_OUTPUT_CONTROL_POINT_ID = 22,
   DXBC_OPERAND_INPUT_FORK_INSTANCE_ID = 23,
   DXBC_OPERAND_INPUT_JOIN_INSTANCE_ID = 24,
   DXBC_OPERAND_INPUT_CONTROL_POINT = 25,
   DXBC_OPERAND_OUTPUT_CONTROL_POINT = 26,
   DXBC_OPERAND_INPUT_PATCH_CONSTANT = 27,
   DXBC_OPERAND_INPUT_DOMAIN_POINT = 28,
   DXBC_OPERAND_THIS_POINTER = 29,
   DXBC_OPERAND_UNORDERED_ACCESS_VIEW = 30,
   DXBC_OPERAND_THREAD_GROUP_SHARED_MEMORY = 31,
   DXBC_OPERAND_INPUT_THREAD_ID = 32,
   DXBC_OPERAND_INPUT_THREAD_GROUP_ID = 33,
   DXBC_OPERAND_INPUT_THREAD_ID_IN_GROUP = 34,
   DXBC_OPERAND_INPUT_COVERAGE_MASK = 35,
   DXBC_OPERAND_INPUT_THREAD_ID_IN_GROUP_FLATTENED = 36,
   DXBC_OPERAND_INPUT_GS_INSTANCE_ID = 37,
   DXBC_OPERAND_OUTPUT_DEPTH_GREATER_EQUAL = 38,
   DXBC_OPERAND_OUTPUT_DEPTH_LESS_EQUAL = 39,
   DXBC_OPERAND_CYCLE_COUNTER = 40,
   DXBC_OPERAND_OUTPUT_STENCIL_REF = 41,
   DXBC_OPERAND_INNER_COVERAGE = 42,
   DXBC_OPERAND_COUNT
};

enum dxbc_selection {
   DXBC_SELECT_MASK = 0,    /* destination write mask */
   DXBC_SELECT_SWIZZLE = 1,
   DXBC_SELECT_ONE = 2,     /* a single component */
};

enum dxbc_index_representation {
   DXBC_INDEX_IMM32 = 0,
   DXBC_INDEX_IMM64 = 1,
   DXBC_INDEX_RELATIVE = 2,
   DXBC_INDEX_IMM32_PLUS_RELATIVE = 3,
   DXBC_INDEX_IMM64_PLUS_RELATIVE = 4,
};

enum dxbc_modifier {
   DXBC_MOD_NONE = 0,
   DXBC_MOD_NEG = 1,
   DXBC_MOD_ABS = 2,
   DXBC_MOD_ABSNEG = 3,
};

enum dxbc_resource_dimension {
   DXBC_DIM_UNKNOWN = 0,
   DXBC_DIM_BUFFER = 1,
   DXBC_DIM_TEXTURE1D = 2,
   DXBC_DIM_TEXTURE2D = 3,
   DXBC_DIM_TEXTURE2DMS = 4,
   DXBC_DIM_TEXTURE3D = 5,
   DXBC_DIM_TEXTURECUBE = 6,
   DXBC_DIM_TEXTURE1DARRAY = 7,
   DXBC_DIM_TEXTURE2DARRAY = 8,
   DXBC_DIM_TEXTURE2DMSARRAY = 9,
   DXBC_DIM_TEXTURECUBEARRAY = 10,
   DXBC_DIM_RAW_BUFFER = 11,
   DXBC_DIM_STRUCTURED_BUFFER = 12,
};

enum dxbc_return_type {
   DXBC_RETURN_UNORM = 1,
   DXBC_RETURN_SNORM = 2,
   DXBC_RETURN_SINT = 3,
   DXBC_RETURN_UINT = 4,
   DXBC_RETURN_FLOAT = 5,
   DXBC_RETURN_MIXED = 6,
   DXBC_RETURN_DOUBLE = 7,
   DXBC_RETURN_CONTINUED = 8,
   DXBC_RETURN_UNUSED = 9,
};

enum dxbc_customdata_class {
   DXBC_CUSTOMDATA_COMMENT = 0,
   DXBC_CUSTOMDATA_DEBUGINFO = 1,
   DXBC_CUSTOMDATA_OPAQUE = 2,
   DXBC_CUSTOMDATA_IMMEDIATE_CONSTANT_BUFFER = 3,
   DXBC_CUSTOMDATA_SHADER_MESSAGE = 4,
   DXBC_CUSTOMDATA_CLIP_PLANE_CONSTANT_MAPPINGS = 5,
};

struct dxbc_operand;

struct dxbc_index {
   uint8_t representation;   /* enum dxbc_index_representation */
   uint64_t imm;             /* immediate part (0 for a purely relative one) */
   struct dxbc_operand *relative; /* register supplying the relative part */
};

struct dxbc_operand {
   uint8_t type;             /* enum dxbc_operand_type */
   uint8_t num_components;   /* 0, 1 or 4 (N-component operands refused) */
   uint8_t selection;        /* enum dxbc_selection, 4-component operands */
   uint8_t mask;             /* DXBC_SELECT_MASK: write mask bits */
   uint8_t swizzle[4];       /* component read for x, y, z, w */
   uint8_t index_dimension;  /* 0..3 */
   uint8_t modifier;         /* enum dxbc_modifier */
   uint8_t min_precision;
   bool nonuniform;          /* shader model 5.1 */
   struct dxbc_index index[3];
   uint32_t imm32[4];        /* DXBC_OPERAND_IMMEDIATE32 */
   uint64_t imm64[2];        /* DXBC_OPERAND_IMMEDIATE64 (one or two doubles) */
};

#define DXBC_MAX_OPERANDS 8

struct dxbc_instruction {
   uint32_t opcode;          /* enum dxbc_opcode */
   uint32_t token0;          /* raw OpcodeToken0: opcode-specific controls */
   uint32_t length;          /* DWORDs, all tokens included */
   uint32_t offset;          /* DWORD offset in the token stream */
   bool saturate;
   bool test_nonzero;        /* if/breakc/... _nz (else _z) */
   uint8_t precise_mask;
   /* extended opcode tokens */
   int8_t texel_offset[3];
   uint8_t resource_dimension; /* RESOURCE_DIM extended token */
   uint32_t structure_stride;
   uint8_t return_type[4];   /* RESOURCE_RETURN_TYPE extended token */
   unsigned num_operands;
   struct dxbc_operand operands[DXBC_MAX_OPERANDS];
   /* DWORDs after the operands (declaration payloads) or, for
    * DXBC_OP_INTERFACE_CALL, the function index before its operand; for
    * custom data the data DWORDs. */
   unsigned num_payload;
   const uint32_t *payload;
   unsigned padding;         /* zero DWORDs after the operands */
};

struct dxbc_program {
   uint32_t type;            /* enum dxbc_program_type */
   uint32_t major, minor;
   uint32_t length;          /* DWORDs, version and length tokens included */
   unsigned num_instructions;
   struct dxbc_instruction *instructions;
   /* every relative-index operand, owned by the program */
   unsigned num_relative;
   struct dxbc_operand *relative;
};

enum dxbc_status {
   DXBC_OK = 0,
   DXBC_ERROR_TRUNCATED,     /* a token runs past its instruction or program */
   DXBC_ERROR_LENGTH,        /* an instruction does not consume its length */
   DXBC_ERROR_OPCODE,        /* unknown or reserved opcode */
   DXBC_ERROR_OPERAND,       /* malformed operand token */
   DXBC_ERROR_LIMIT,         /* more operands than DXBC_MAX_OPERANDS */
   DXBC_ERROR_VERSION,       /* unsupported program type or model */
   DXBC_ERROR_NO_MEMORY,
   DXBC_ERROR_CONTAINER,     /* malformed DXBC container */
};

struct dxbc_error {
   enum dxbc_status status;
   uint32_t offset;          /* DWORD offset of the failing instruction */
   uint32_t opcode;
};

/* Parse a tokenized program (version token first).  'dwords' bounds the
 * buffer; the program's own length token must fit in it. */
enum dxbc_status dxbc_parse_program(const uint32_t *tokens, size_t dwords,
                                    struct dxbc_program *program,
                                    struct dxbc_error *error);

void dxbc_program_free(struct dxbc_program *program);

/* Find a chunk ('SHDR', 'SHEX', 'ISGN', ...) in a DXBC container.  Returns
 * false when the container is malformed or the chunk is absent. */
bool dxbc_container_find_chunk(const void *container, size_t bytes,
                               uint32_t fourcc, const void **chunk,
                               uint32_t *chunk_bytes);

#define DXBC_FOURCC(a, b, c, d) \
   ((uint32_t)(uint8_t)(a) | ((uint32_t)(uint8_t)(b) << 8) | \
    ((uint32_t)(uint8_t)(c) << 16) | ((uint32_t)(uint8_t)(d) << 24))

/* Mnemonic of an opcode as the Microsoft assembler prints its base form
 * (no _sat, test, return-type or resource suffixes). */
const char *dxbc_opcode_name(uint32_t opcode);
bool dxbc_opcode_is_declaration(uint32_t opcode);

#ifdef __cplusplus
}
#endif

#endif /* DXBC_PARSE_H */
