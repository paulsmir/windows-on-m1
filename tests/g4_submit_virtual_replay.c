#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "apple_agx_g4_submit.h"

typedef int NTSTATUS;
typedef unsigned char BOOLEAN;
typedef unsigned long long ULONGLONG;
#define PASSIVE_LEVEL 0
#define STATUS_INVALID_PARAMETER ((NTSTATUS)0xC000000D)
#define STATUS_INVALID_ADDRESS ((NTSTATUS)0xC0000141)
#define STATUS_INVALID_DEVICE_STATE ((NTSTATUS)0xC0000184)
#define STATUS_NOT_SUPPORTED ((NTSTATUS)0xC00000BB)
#define ADMISSION_CONTEXT_VIRTUAL_ADDRESSING 4u
#define ADMISSION_CONTEXT_SYSTEM 1u
#define ADMISSION_CONTEXT_GDI 2u

typedef struct {
  ULONGLONG RootIpa;
  unsigned Uncertain;
  unsigned AllowProcessRanges;
} APPLE_AGX_GPUVA_G3_GRAPH;
typedef struct _ADMISSION_G3_STATE {
  int Lock;
} ADMISSION_G3_STATE;
typedef struct _ADMISSION_G3_PROCESS {
  ADMISSION_G3_STATE *State;
  APPLE_AGX_GPUVA_G3_GRAPH Graph;
  unsigned Poisoned;
} ADMISSION_G3_PROCESS;
typedef struct {
  unsigned Flags;
} ADMISSION_OBJECT_CONTEXT;
typedef struct {
  void *GpuvaG3State;
} ADMISSION_CONTEXT;
typedef struct {
  ADMISSION_G3_PROCESS *GpuvaG3Process;
  unsigned GpuvaG3Poisoned;
  ULONGLONG GpuvaG3RootIpa;
  unsigned Win32Transport;
  ADMISSION_OBJECT_CONTEXT Object;
} ADMISSION_RENDER_CONTEXT;
typedef struct {
  struct { unsigned Value; } Flags;
  const void *pDmaBufferPrivateData;
  unsigned DmaBufferPrivateDataSize;
  unsigned DmaBufferUmdPrivateDataSize;
  ULONGLONG DmaBufferVirtualAddress;
  unsigned DmaBufferSize;
} DXGKARG_SUBMITCOMMANDVIRTUAL;

static int replay_irql;
static int KeGetCurrentIrql(void) { return replay_irql; }
static void ExAcquireFastMutex(int *lock) { assert(!*lock); *lock = 1; }
static void ExReleaseFastMutex(int *lock) { assert(*lock); *lock = 0; }
static int AppleAgxGpuvaG3GraphContainsRangeAccess(
    APPLE_AGX_GPUVA_G3_GRAPH *graph, ULONGLONG va, unsigned bytes,
    int write) {
  if (!graph || !bytes) return 0;
  if (va == 0x20000 && !write && bytes == 248) return 1;
  if (va == 0x30000 && !write && bytes == 1) return 1;
  if (va >= 0x100000 && va < 0x190000 && write &&
      bytes == 0x10000 && !(va & 0xffff)) return graph->AllowProcessRanges;
  return 0;
}

/* The Python driver inserts the unmodified production KMD functions here. */
#include "g4_submit_virtual_functions.inc"

typedef struct {
  APPLE_AGX_G4_PRIVATE_HEADER_V2 Header;
  APPLE_AGX_G4_NATIVE_HEADER Command;
  APPLE_AGX_G4_NATIVE_RENDER Render;
} PACKET;

int main(void) {
  PACKET packet = {0};
  ADMISSION_G3_STATE state = {0};
  ADMISSION_G3_PROCESS process = {0};
  ADMISSION_CONTEXT adapter = {0};
  ADMISSION_RENDER_CONTEXT context = {0};
  DXGKARG_SUBMITCOMMANDVIRTUAL args = {0};
  process.State = &state;
  process.Graph.RootIpa = 0x9bf000000ULL;
  process.Graph.AllowProcessRanges = 1;
  adapter.GpuvaG3State = &state;
  context.GpuvaG3Process = &process;
  context.GpuvaG3RootIpa = process.Graph.RootIpa;
  context.Win32Transport = 1;
  context.Object.Flags = ADMISSION_CONTEXT_VIRTUAL_ADDRESSING;
  packet.Header.Base.Magic = APPLE_AGX_G4_PRIVATE_MAGIC;
  packet.Header.Base.Version = APPLE_AGX_G4_PRIVATE_VERSION_PROCESS_VA;
  packet.Header.Base.HeaderBytes = sizeof(packet.Header);
  packet.Header.Base.CommandBytes = sizeof(packet) - sizeof(packet.Header);
  packet.Header.Base.CommandVa = 0x20000;
  for (unsigned i = 0; i < APPLE_AGX_G4_PROCESS_RANGE_COUNT; ++i) {
    packet.Header.Process[i].Va = 0x100000ULL + (ULONGLONG)i * 0x10000;
    packet.Header.Process[i].Bytes = 0x10000;
  }
  packet.Command.Type = APPLE_AGX_G4_RENDER;
  packet.Command.Size = sizeof(packet.Render);
  packet.Render.VdmCtrlStreamBase = 0x30000;
  packet.Render.WidthPx = 32;
  packet.Render.HeightPx = 32;
  packet.Render.Layers = 1;
  packet.Render.UtileWidthPx = 32;
  packet.Render.UtileHeightPx = 32;
  packet.Render.Samples = 1;
  packet.Render.SampleSizeBytes = 4;
  args.pDmaBufferPrivateData = &packet;
  args.DmaBufferPrivateDataSize = sizeof(packet);
  args.DmaBufferUmdPrivateDataSize = sizeof(packet);
  args.DmaBufferVirtualAddress = 0x20000;
  args.DmaBufferSize = packet.Header.Base.CommandBytes;
  /* Until native construction exists, valid work must fail through the only
   * non-bugchecking WDDM rejection status. */
  assert(AdmissionG4SubmitVirtualEnvelope(&adapter, &context, &args) ==
         STATUS_INVALID_PARAMETER);
  process.Graph.AllowProcessRanges = 0;
  assert(AdmissionG4SubmitVirtualEnvelope(&adapter, &context, &args) ==
         STATUS_INVALID_PARAMETER);
  process.Graph.AllowProcessRanges = 1;
  process.Graph.Uncertain = 1;
  assert(AdmissionG4SubmitVirtualEnvelope(&adapter, &context, &args) ==
         STATUS_INVALID_PARAMETER);
  assert(state.Lock == 0);
  puts("g4_submit_virtual_replay: PASS");
  return 0;
}
