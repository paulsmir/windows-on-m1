/* Original project fixture. SPDX-License-Identifier: MIT */
#ifndef AGX_ENUM_ABI_PROBE_H
#define AGX_ENUM_ABI_PROBE_H
#include <stddef.h>
#include "compiler/shader_info.h"
#ifdef __cplusplus
extern "C" {
#endif
void c_write(shader_info *, unsigned, unsigned);
void cpp_write(shader_info *, unsigned, unsigned);
int c_read(const shader_info *, unsigned);
int cpp_read(const shader_info *, unsigned);
void c_layout(size_t out[6]);
void cpp_layout(size_t out[6]);
#ifdef __cplusplus
}
#endif
#endif
