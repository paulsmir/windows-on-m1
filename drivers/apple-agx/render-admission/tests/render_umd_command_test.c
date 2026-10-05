#include "render_umd_command.h"

#include <assert.h>

static ADMISSION_UMD_COLOR_FILL_COMMAND exact_command(void) {
  ADMISSION_UMD_COLOR_FILL_COMMAND command = {0};
  command.Magic = ADMISSION_UMD_COMMAND_MAGIC;
  command.Version = ADMISSION_UMD_COMMAND_VERSION;
  command.Bytes = sizeof(command);
  command.Opcode = AdmissionUmdOpcodeColorFill;
  command.Destination.Right = 2560u;
  command.Destination.Bottom = 1600u;
  command.DestinationAllocationIndex = 0u;
  command.Color = 0xff336699u;
  command.Rop = AdmissionUmdRopPatCopy;
  command.Rop3 = 0u;
  return command;
}

int main(void) {
  ADMISSION_UMD_COLOR_FILL_COMMAND command = exact_command();
  assert(sizeof(command) == 48u);
  assert(AdmissionUmdColorFillCommandValid(&command, 1u));

  command.Magic ^= 1u;
  assert(!AdmissionUmdColorFillCommandValid(&command, 1u));
  command = exact_command();
  command.Version += 1u;
  assert(!AdmissionUmdColorFillCommandValid(&command, 1u));
  command = exact_command();
  command.Bytes -= 4u;
  assert(!AdmissionUmdColorFillCommandValid(&command, 1u));
  command = exact_command();
  command.Opcode = 2u;
  assert(!AdmissionUmdColorFillCommandValid(&command, 1u));
  command = exact_command();
  command.Destination.Right = command.Destination.Left;
  assert(!AdmissionUmdColorFillCommandValid(&command, 1u));
  command = exact_command();
  command.Destination.Bottom = command.Destination.Top;
  assert(!AdmissionUmdColorFillCommandValid(&command, 1u));
  command = exact_command();
  command.DestinationAllocationIndex = 1u;
  assert(!AdmissionUmdColorFillCommandValid(&command, 1u));
  command = exact_command();
  command.Rop = 2u;
  assert(!AdmissionUmdColorFillCommandValid(&command, 1u));
  command = exact_command();
  command.Rop3 = 1u;
  assert(!AdmissionUmdColorFillCommandValid(&command, 1u));
  assert(!AdmissionUmdColorFillCommandValid(&command, 0u));
  assert(!AdmissionUmdColorFillCommandValid(0, 1u));
  return 0;
}
