#ifndef APPLE_AGX_RENDER_UMD_COMMAND_H
#define APPLE_AGX_RENDER_UMD_COMMAND_H

#define ADMISSION_UMD_COMMAND_MAGIC 0x434d5552u /* "RUMC" */
#define ADMISSION_UMD_COMMAND_VERSION 1u
#define ADMISSION_UMD_CORRELATION_COOKIE 0x51504341u /* "ACPQ" */

typedef enum _ADMISSION_UMD_OPCODE {
  AdmissionUmdOpcodeColorFill = 1u,
} ADMISSION_UMD_OPCODE;

typedef enum _ADMISSION_UMD_ROP {
  AdmissionUmdRopPatCopy = 1u,
} ADMISSION_UMD_ROP;

typedef struct _ADMISSION_UMD_RECT {
  unsigned int Left;
  unsigned int Top;
  unsigned int Right;
  unsigned int Bottom;
} ADMISSION_UMD_RECT;

typedef struct _ADMISSION_UMD_COLOR_FILL_COMMAND {
  unsigned int Magic;
  unsigned int Version;
  unsigned int Bytes;
  unsigned int Opcode;
  ADMISSION_UMD_RECT Destination;
  unsigned int DestinationAllocationIndex;
  unsigned int Color;
  unsigned int Rop;
  unsigned int Rop3;
} ADMISSION_UMD_COLOR_FILL_COMMAND;

int AdmissionUmdColorFillCommandValid(
    const ADMISSION_UMD_COLOR_FILL_COMMAND *Command,
    unsigned int AllocationCount);

#endif /* APPLE_AGX_RENDER_UMD_COMMAND_H */
