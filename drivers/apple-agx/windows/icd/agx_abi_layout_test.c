/* CS 1.6 ICD plan, phase 1: every private blob the KMD receives must have
 * the same layout in a 32-bit x86 (WOW64) process as in the ARM64 UMD that
 * the KMD was validated with; D3DKMT passes the bytes through unchanged and
 * the KMD checks exact sizes. Built for ARM64, x64 and x86 by
 * build-icd-bridge-test.ps1; sizes and the offsets after the hidden pads
 * are the ARM64 values (2026-10-10 ABI audit). */
#include <windows.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include "apple_agx_g3_private_abi.h"
#include "apple_agx_g3_copy_abi.h"
#include "apple_agx_g4_submit.h"
#include "render_win32_transport.h"
#include "render_qualification.h"

static int failures;
/* Compiled as C++: the static check also makes the ARM64 build (not run on
 * the x64 builder) a real comparison; the runtime print names the field. */
#define LAYOUT(expr, value) do { static_assert((expr) == (value), #expr); \
  size_t got = (size_t)(expr); \
  if (got != (size_t)(value)) { printf("FAIL %s = %u, expected %u\n", #expr, \
    (unsigned)got, (unsigned)(value)); ++failures; } } while (0)

int main(void) {
  LAYOUT(sizeof(APPLE_AGX_G3_PRIVATE_REQUEST), 224);
  LAYOUT(sizeof(APPLE_AGX_G3_COPY_REQUEST), 65600);
  LAYOUT(sizeof(APPLE_AGX_G4_PRIVATE_HEADER), 24);
  LAYOUT(sizeof(APPLE_AGX_G4_PRIVATE_HEADER_V2), 168);
  LAYOUT(sizeof(APPLE_AGX_G4_PRIVATE_HEADER_V3), 200);
  LAYOUT(sizeof(APPLE_AGX_G4_NATIVE_RENDER), 240);
  LAYOUT(sizeof(ADMISSION_ALLOCATION_DESCRIPTION), 48);
  LAYOUT(sizeof(ADMISSION_PRESENT_RESOURCE_DATA), 28);
  LAYOUT(sizeof(ADMISSION_WIN32_ALLOCATION_CREATE), 72);
  /* Hidden pad: the 8-aligned description starts at 24, not 20. */
  LAYOUT(offsetof(ADMISSION_WIN32_ALLOCATION_CREATE, Allocation), 24);
  LAYOUT(sizeof(ADMISSION_WIN32_CONTEXT_CREATE), 16);
  LAYOUT(sizeof(AGX_WIN32_DEVICE_INFO), 104);
  /* Hidden tail pad of each class entry. */
  LAYOUT(sizeof(AGX_WIN32_BUFFER_CLASS_INFO), 24);
  LAYOUT(offsetof(AGX_WIN32_DEVICE_INFO, Classes), 32);
  LAYOUT(sizeof(ADMISSION_DWM_FRAME_ARM), 32);
  printf("pointer bytes %u, layout failures %d\n", (unsigned)sizeof(void *), failures);
  if (failures) return 1;
  printf("agx_abi_layout_test: PASS\n");
  return 0;
}
