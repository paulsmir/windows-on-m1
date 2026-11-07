/* Original TEST-ONLY observation of project frontend's real GLSL ref calls.
 * Include only when compiling standalone frontend test object. MIT */
#pragma once
#include "nir.h"
void agx_legacy_test_glsl_ref(void);
void agx_legacy_test_glsl_unref(void);
#define glsl_type_singleton_init_or_ref agx_legacy_test_glsl_ref
#define glsl_type_singleton_decref agx_legacy_test_glsl_unref
