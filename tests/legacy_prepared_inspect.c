/* Original real NIR/GLSL lifetime+equivalence fixture, no core/provider. MIT */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "nir.h"
#include "nir_serialize.h"
#include "dxbc_to_nir.h"
#include "agx_owned_program_nir_native.h"
static unsigned refs,acquired,released;
static void need(int p){if(!p){fputs("LEGACY_PREPARED_NIR_CHECK_FAIL\n",stderr);abort();}}
/* Delegate to actual functions. No allocation failure/success substitution. */
void agx_legacy_test_glsl_ref(void){glsl_type_singleton_init_or_ref();++refs;++acquired;}
void agx_legacy_test_glsl_unref(void){need(refs>0);glsl_type_singleton_decref();--refs;++released;}
unsigned legacy_prepared_refs(void){return refs;}
unsigned legacy_prepared_ref_acquired(void){return acquired;}
unsigned legacy_prepared_ref_released(void){return released;}
int legacy_prepared_nir_equal(struct nir_shader *a,struct nir_shader *b){
 need(a&&b&&a!=b);nir_validate_shader(a,"factory actual legacy NIR");nir_validate_shader(b,"direct native legacy reference");
 struct blob x,y;blob_init(&x);blob_init(&y);nir_serialize(&x,a,false);nir_serialize(&y,b,false);
 int equal=!x.out_of_memory&&!y.out_of_memory&&x.size==y.size&&!memcmp(x.data,y.data,x.size);blob_finish(&x);blob_finish(&y);return equal;
}
