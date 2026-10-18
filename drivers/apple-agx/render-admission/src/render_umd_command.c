#include "render_umd_command.h"

int AdmissionUmdColorFillCommandValid(
    const ADMISSION_UMD_COLOR_FILL_COMMAND *Command,
    unsigned int AllocationCount) {
  return Command != (const ADMISSION_UMD_COLOR_FILL_COMMAND *)0 &&
         Command->Magic == ADMISSION_UMD_COMMAND_MAGIC &&
         Command->Version == ADMISSION_UMD_COMMAND_VERSION &&
         Command->Bytes == sizeof(*Command) &&
         Command->Opcode == (unsigned int)AdmissionUmdOpcodeColorFill &&
         Command->Destination.Left < Command->Destination.Right &&
         Command->Destination.Top < Command->Destination.Bottom &&
         AllocationCount != 0u &&
         Command->DestinationAllocationIndex < AllocationCount &&
         Command->Rop == (unsigned int)AdmissionUmdRopPatCopy &&
         Command->Rop3 == 0u;
}
