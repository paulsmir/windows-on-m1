#include "uatomic_clang_compat.h"
/* Independently pinned SDK26100 ksarm64.h encoding; no runtime register access
 * is executed by the x64 builder. The runner inspects ARM64 instructions. */
_Static_assert(ARM64_FPCR == 0x5a20, "SDK26100 FPCR encoding");
unsigned ad04_read_fpcr(void) { return (unsigned)_ReadStatusReg(ARM64_FPCR); }
void ad04_write_fpcr(unsigned value) { _WriteStatusReg(ARM64_FPCR, value); }
