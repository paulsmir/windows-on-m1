/* dxil_agx_cost_fixture --production: the Gallium Asahi steps the fixture's
 * default path replaces by stand-ins, called the way agx_state.c calls them
 * for a D3D12 owner's compute shader.  tools/agx_shader_cost.py build()
 * compiles this file with the pinned agx_nir_lower_bindings.c and the
 * projected agx_nir_lower_sysvals.c (build-native-asahi-state.py, D3D12 C47)
 * and links the three objects into the fixture. */
#include "agx_state.h"
#include "nir_serialize.h"

/* agx_shader_initialize: NIR_PASS(_, nir, agx_nir_lower_bindings,
 * &so->uses_bindless_samplers) */
void
agx_cost_production_bindings(nir_shader *nir)
{
   bool uses_bindless_samplers = false;
   NIR_PASS(_, nir, agx_nir_lower_bindings, &uses_bindless_samplers);
}

/* agx_shader_initialize serializes the NIR (so->serialized_nir) and
 * agx_compile_variant compiles the deserialized copy: the round trip
 * renumbers the definitions the backend sees */
nir_shader *
agx_cost_production_round_trip(nir_shader *nir)
{
   struct blob blob;
   blob_init(&blob);
   nir_serialize(&blob, nir, true);
   struct blob_reader reader;
   blob_reader_init(&reader, blob.data, blob.size);
   nir_shader *copy = nir_deserialize(NULL, &agx_nir_options, &reader);
   blob_finish(&blob);
   ralloc_free(nir);
   return copy;
}

/* agx_compile_nir (not an internal kernel, not secondary): the sysval
 * lowering and the uniform layout; returns agx_shader_key::reserved_preamble */
unsigned
agx_cost_production_sysvals(nir_shader *nir)
{
   struct agx_compiled_shader *compiled = CALLOC_STRUCT(agx_compiled_shader);
   unsigned reserved_preamble = 0;
   compiled->stage = nir->info.stage;
   NIR_PASS(_, nir, agx_nir_lower_sysvals, nir->info.stage, true);
   NIR_PASS(_, nir, agx_nir_layout_uniforms, compiled, &reserved_preamble);
   FREE(compiled);
   return reserved_preamble;
}
