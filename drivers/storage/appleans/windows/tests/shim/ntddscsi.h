/* SPDX-License-Identifier: MIT */
/* Host stand-in for <ntddscsi.h>. Test code. */
#include "storport.h"
typedef struct { ULONG HeaderLength; UCHAR Signature[8]; ULONG Timeout, ControlCode, ReturnCode, Length; } SRB_IO_CONTROL;
