/* Print a DXBC program as the Microsoft assembler lists it (mnemonic and
 * operand registers; immediates as l<count>), so that
 * test_dxbc_parse.py can compare every decoded instruction and operand with
 * fxc's own listing of the same object.  Declarations print their mnemonic
 * only.  usage: dxbc_parse_fixture <object.dxbc> */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "dxbc_parse.h"

static const char comp[] = "xyzw";

static void print_operand(const struct dxbc_program *p,
                          const struct dxbc_instruction *insn,
                          const struct dxbc_operand *o, char *out, size_t n);

static void
print_index(const struct dxbc_program *p, const struct dxbc_index *index,
            char *out, size_t n)
{
   char rel[128];
   if (index->relative) {
      print_operand(p, NULL, index->relative, rel, sizeof(rel));
      snprintf(out, n, "%s + %llu", rel, (unsigned long long)index->imm);
   } else {
      snprintf(out, n, "%llu", (unsigned long long)index->imm);
   }
}

/* name<imm> for an immediate first index, name[<relative>] otherwise */
static size_t
print_named(const struct dxbc_program *p, const char *name,
            const struct dxbc_operand *o, unsigned first, char *out, size_t n)
{
   char buf[160];
   size_t len;
   if (o->index_dimension <= first)
      return (size_t)snprintf(out, n, "%s", name);
   if (!o->index[first].relative)
      len = (size_t)snprintf(out, n, "%s%llu", name,
                             (unsigned long long)o->index[first].imm);
   else {
      print_index(p, &o->index[first], buf, sizeof(buf));
      len = (size_t)snprintf(out, n, "%s[%s]", name, buf);
   }
   for (unsigned d = first + 1; d < o->index_dimension && len < n; ++d) {
      print_index(p, &o->index[d], buf, sizeof(buf));
      len += (size_t)snprintf(out + len, n - len, "[%s]", buf);
   }
   return len;
}

/* name[<i>][<j>]... with every index bracketed */
static size_t
print_bracketed(const struct dxbc_program *p, const char *name,
                const struct dxbc_operand *o, char *out, size_t n)
{
   char buf[160];
   size_t len = (size_t)snprintf(out, n, "%s", name);
   for (unsigned d = 0; d < o->index_dimension && len < n; ++d) {
      print_index(p, &o->index[d], buf, sizeof(buf));
      len += (size_t)snprintf(out + len, n - len, "[%s]", buf);
   }
   return len;
}

static void
print_operand(const struct dxbc_program *p, const struct dxbc_instruction *insn,
              const struct dxbc_operand *o, char *out, size_t n)
{
   char body[256];
   size_t len = 0;
   bool sm51 = p->major == 5 && p->minor == 1;
   switch (o->type) {
   case DXBC_OPERAND_TEMP: print_named(p, "r", o, 0, body, sizeof(body)); break;
   case DXBC_OPERAND_INPUT:
      if (o->index_dimension == 2)
         print_bracketed(p, "v", o, body, sizeof(body));
      else
         print_named(p, "v", o, 0, body, sizeof(body));
      break;
   case DXBC_OPERAND_OUTPUT: print_named(p, "o", o, 0, body, sizeof(body)); break;
   case DXBC_OPERAND_INDEXABLE_TEMP: print_named(p, "x", o, 0, body, sizeof(body)); break;
   case DXBC_OPERAND_IMMEDIATE32:
      snprintf(out, n, "l%u", o->num_components);
      return;
   case DXBC_OPERAND_IMMEDIATE64:
      snprintf(out, n, "d%u", o->num_components == 4 ? 2u : 1u);
      return;
   case DXBC_OPERAND_SAMPLER: print_named(p, sm51 ? "S" : "s", o, 0, body, sizeof(body)); break;
   case DXBC_OPERAND_RESOURCE: print_named(p, sm51 ? "T" : "t", o, 0, body, sizeof(body)); break;
   case DXBC_OPERAND_CONSTANT_BUFFER: print_named(p, sm51 ? "CB" : "cb", o, 0, body, sizeof(body)); break;
   case DXBC_OPERAND_IMMEDIATE_CONSTANT_BUFFER: print_bracketed(p, "icb", o, body, sizeof(body)); break;
   case DXBC_OPERAND_LABEL: print_named(p, "l", o, 0, body, sizeof(body)); break;
   case DXBC_OPERAND_INPUT_PRIMITIVEID: snprintf(body, sizeof(body), "vPrim"); break;
   case DXBC_OPERAND_OUTPUT_DEPTH: snprintf(body, sizeof(body), "oDepth"); break;
   case DXBC_OPERAND_NULL: snprintf(body, sizeof(body), "null"); break;
   case DXBC_OPERAND_RASTERIZER: snprintf(body, sizeof(body), "rasterizer"); break;
   case DXBC_OPERAND_OUTPUT_COVERAGE_MASK: snprintf(body, sizeof(body), "oMask"); break;
   case DXBC_OPERAND_STREAM: print_named(p, "m", o, 0, body, sizeof(body)); break;
   case DXBC_OPERAND_FUNCTION_BODY: print_named(p, "fb", o, 0, body, sizeof(body)); break;
   case DXBC_OPERAND_FUNCTION_TABLE: print_named(p, "ft", o, 0, body, sizeof(body)); break;
   case DXBC_OPERAND_INTERFACE:
      len = print_named(p, "fp", o, 0, body, sizeof(body));
      if (insn && insn->opcode == DXBC_OP_INTERFACE_CALL && insn->num_payload)
         snprintf(body + len, sizeof(body) - len, "[%u]", insn->payload[0]);
      break;
   case DXBC_OPERAND_FUNCTION_INPUT: print_named(p, "fi", o, 0, body, sizeof(body)); break;
   case DXBC_OPERAND_FUNCTION_OUTPUT: print_named(p, "fo", o, 0, body, sizeof(body)); break;
   case DXBC_OPERAND_OUTPUT_CONTROL_POINT_ID: snprintf(body, sizeof(body), "vOutputControlPointID"); break;
   case DXBC_OPERAND_INPUT_FORK_INSTANCE_ID: snprintf(body, sizeof(body), "vForkInstanceID"); break;
   case DXBC_OPERAND_INPUT_JOIN_INSTANCE_ID: snprintf(body, sizeof(body), "vJoinInstanceID"); break;
   case DXBC_OPERAND_INPUT_CONTROL_POINT: print_bracketed(p, "vicp", o, body, sizeof(body)); break;
   case DXBC_OPERAND_OUTPUT_CONTROL_POINT: print_bracketed(p, "vocp", o, body, sizeof(body)); break;
   case DXBC_OPERAND_INPUT_PATCH_CONSTANT: print_named(p, "vpc", o, 0, body, sizeof(body)); break;
   case DXBC_OPERAND_INPUT_DOMAIN_POINT: snprintf(body, sizeof(body), "vDomain"); break;
   case DXBC_OPERAND_THIS_POINTER: print_bracketed(p, "this", o, body, sizeof(body)); break;
   case DXBC_OPERAND_UNORDERED_ACCESS_VIEW: print_named(p, sm51 ? "U" : "u", o, 0, body, sizeof(body)); break;
   case DXBC_OPERAND_THREAD_GROUP_SHARED_MEMORY: print_named(p, "g", o, 0, body, sizeof(body)); break;
   case DXBC_OPERAND_INPUT_THREAD_ID: snprintf(body, sizeof(body), "vThreadID"); break;
   case DXBC_OPERAND_INPUT_THREAD_GROUP_ID: snprintf(body, sizeof(body), "vThreadGroupID"); break;
   case DXBC_OPERAND_INPUT_THREAD_ID_IN_GROUP: snprintf(body, sizeof(body), "vThreadIDInGroup"); break;
   case DXBC_OPERAND_INPUT_COVERAGE_MASK: snprintf(body, sizeof(body), "vCoverage"); break;
   case DXBC_OPERAND_INPUT_THREAD_ID_IN_GROUP_FLATTENED: snprintf(body, sizeof(body), "vThreadIDInGroupFlattened"); break;
   case DXBC_OPERAND_INPUT_GS_INSTANCE_ID: snprintf(body, sizeof(body), "vGSInstanceID"); break;
   case DXBC_OPERAND_OUTPUT_DEPTH_GREATER_EQUAL: snprintf(body, sizeof(body), "oDepthGE"); break;
   case DXBC_OPERAND_OUTPUT_DEPTH_LESS_EQUAL: snprintf(body, sizeof(body), "oDepthLE"); break;
   case DXBC_OPERAND_CYCLE_COUNTER: snprintf(body, sizeof(body), "vCycleCounter"); break;
   case DXBC_OPERAND_OUTPUT_STENCIL_REF: snprintf(body, sizeof(body), "oStencilRef"); break;
   case DXBC_OPERAND_INNER_COVERAGE: snprintf(body, sizeof(body), "vInnerCoverage"); break;
   default: snprintf(body, sizeof(body), "?%u", o->type); break;
   }
   len = strlen(body);
   if (o->num_components == 4 && len + 6 < sizeof(body)) {
      body[len++] = '.';
      if (o->selection == DXBC_SELECT_MASK) {
         for (unsigned c = 0; c < 4; ++c)
            if (o->mask & (1u << c))
               body[len++] = comp[c];
      } else if (o->selection == DXBC_SELECT_SWIZZLE) {
         for (unsigned c = 0; c < 4; ++c)
            body[len++] = comp[o->swizzle[c]];
      } else {
         body[len++] = comp[o->swizzle[0]];
      }
      if (body[len - 1] == '.')
         --len;
      body[len] = 0;
   }
   if (o->nonuniform)
      snprintf(body + len, sizeof(body) - len, " { nonuniform }");
   switch (o->modifier) {
   case DXBC_MOD_NEG: snprintf(out, n, "-%s", body); break;
   case DXBC_MOD_ABS: snprintf(out, n, "|%s|", body); break;
   case DXBC_MOD_ABSNEG: snprintf(out, n, "-|%s|", body); break;
   default: snprintf(out, n, "%s", body); break;
   }
}

/* Every truncation and single-bit mutation of the program must parse or be
 * refused without reading outside the buffer (run under ASan/UBSan). The
 * mutated copy is exactly 'dwords' long so any overread is caught. */
static int
mutate(const uint32_t *tokens, uint32_t dwords)
{
   static const unsigned bits[] = { 0, 1, 2, 4, 11, 12, 13, 20, 21, 22, 24, 25, 28, 30, 31 };
   unsigned ok = 0, refused = 0;
   for (uint32_t len = 0; len <= dwords; ++len) {
      uint32_t *copy = malloc((len ? len : 1) * sizeof(uint32_t));
      struct dxbc_program program;
      memcpy(copy, tokens, len * sizeof(uint32_t));
      if (len >= 2 && copy[1] > len)
         copy[1] = len; /* a truncated program states its new length */
      if (dxbc_parse_program(copy, len, &program, NULL) == DXBC_OK) ++ok; else ++refused;
      dxbc_program_free(&program);
      free(copy);
   }
   for (uint32_t pos = 2; pos < dwords; ++pos) {
      for (unsigned b = 0; b < sizeof(bits) / sizeof(bits[0]); ++b) {
         uint32_t *copy = malloc(dwords * sizeof(uint32_t));
         struct dxbc_program program;
         memcpy(copy, tokens, dwords * sizeof(uint32_t));
         copy[pos] ^= 1u << bits[b];
         if (dxbc_parse_program(copy, dwords, &program, NULL) == DXBC_OK) ++ok; else ++refused;
         dxbc_program_free(&program);
         free(copy);
      }
   }
   printf("MUTATED ok %u refused %u\n", ok, refused);
   return 0;
}

int
main(int argc, char **argv)
{
   FILE *f;
   long size;
   void *bytes;
   const void *chunk;
   uint32_t chunk_bytes;
   struct dxbc_program program;
   struct dxbc_error error;
   enum dxbc_status status;
   bool mutating = argc == 3 && !strcmp(argv[1], "--mutate");

   if (mutating) {
      argv[1] = argv[2];
      argc = 2;
   }
   if (argc != 2 || !(f = fopen(argv[1], "rb")))
      return 2;
   fseek(f, 0, SEEK_END);
   size = ftell(f);
   fseek(f, 0, SEEK_SET);
   bytes = malloc((size_t)size);
   if (!bytes || fread(bytes, 1, (size_t)size, f) != (size_t)size)
      return 2;
   fclose(f);
   if (!dxbc_container_find_chunk(bytes, (size_t)size, DXBC_FOURCC('S', 'H', 'E', 'X'),
                                  &chunk, &chunk_bytes) &&
       !dxbc_container_find_chunk(bytes, (size_t)size, DXBC_FOURCC('S', 'H', 'D', 'R'),
                                  &chunk, &chunk_bytes)) {
      printf("NO_SHADER_CHUNK\n");
      return 1;
   }
   if (mutating) {
      uint32_t *tokens = malloc(chunk_bytes);
      int rc;
      memcpy(tokens, chunk, chunk_bytes);
      rc = mutate(tokens, chunk_bytes / 4);
      free(tokens);
      free(bytes);
      return rc;
   }
   status = dxbc_parse_program(chunk, chunk_bytes / 4, &program, &error);
   if (status != DXBC_OK) {
      printf("PARSE_ERROR status %d offset %u opcode %u\n", status,
             error.offset, error.opcode);
      return 1;
   }
   for (unsigned i = 0; i < program.num_instructions; ++i) {
      const struct dxbc_instruction *insn = &program.instructions[i];
      const char *name = dxbc_opcode_name(insn->opcode);
      if (insn->opcode == DXBC_OP_CUSTOMDATA) {
         unsigned klass = insn->token0 >> 11;
         if (klass == DXBC_CUSTOMDATA_IMMEDIATE_CONSTANT_BUFFER)
            printf("dcl_immediateconstantbuffer\n");
         continue; /* other custom data is not listed */
      }
      if (dxbc_opcode_is_declaration(insn->opcode)) {
         printf("%s\n", name);
         continue;
      }
      printf("%s", name);
      for (unsigned o = 0; o < insn->num_operands; ++o) {
         char text[512];
         print_operand(&program, insn, &insn->operands[o], text, sizeof(text));
         printf("%s%s", o ? ", " : " ", text);
      }
      printf("\n");
   }
   dxbc_program_free(&program);
   free(bytes);
   return 0;
}
