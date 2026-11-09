/* Firmware restart drill (hardware stage H1 of
 * investigation/analysis/TDR-FIRMWARE-RESTART-RECOVERY.md): asks the KMD for
 * one orderly warm restart of the idle AGX firmware per cycle
 * (DxgkDdiEscape payload ADMISSION_FW_RESTART_DRILL,
 * render-admission/include/render_gpu_fault.h; accepted only with the device
 * value Wom1FwRestartDrillAllowed = 1 and with no job in flight).
 * The tool creates no D3D device: nothing of its own is with the firmware.
 * A refusal with status 0xC00000BB means the gate is closed; a payload status
 * of 0x80000011 (STATUS_DEVICE_BUSY) means a job was in flight: the cycle is
 * retried up to <retries> times, <delay_ms> apart. The verdict of each cycle
 * is the payload plus Wom1GpuReset v5 (tools/decode_wom1_gpu_fault.py).
 * Every line is written through, so the log survives a bugcheck.
 * Usage: agx_fw_restart_drill.exe <out.txt> [cycles] [delay_ms] [retries] */
#define COBJMACROS
#define INITGUID
#include <windows.h>
#include <dxgi1_2.h>
#include <d3dkmthk.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* render_gpu_fault.h ADMISSION_FW_RESTART_DRILL v1 */
#define ADMISSION_FW_RESTART_DRILL_MAGIC 0x52525746u
#define ADMISSION_FW_RESTART_DRILL_VERSION 1u
#define ADMISSION_FW_RESTART_DRILL_WARM_ORDERLY 1u
typedef struct {
  unsigned int Magic, Version, Bytes, Operation;
  unsigned int Status, Stage, Restarts, Succeeded;
} ADMISSION_FW_RESTART_DRILL;

#define DRILL_STATUS_DEVICE_BUSY 0x80000011u

static HANDLE out = INVALID_HANDLE_VALUE;

static void say(const char *format, ...) {
  char line[512];
  DWORD written;
  va_list args;
  int n;
  va_start(args, format);
  n = vsnprintf(line, sizeof(line) - 2, format, args);
  va_end(args);
  if (n < 0) return;
  if (n > (int)sizeof(line) - 3) n = (int)sizeof(line) - 3;
  line[n++] = '\r';
  line[n++] = '\n';
  fwrite(line, 1, (size_t)n, stdout);
  fflush(stdout);
  if (out != INVALID_HANDLE_VALUE) {
    WriteFile(out, line, (DWORD)n, &written, NULL);
    FlushFileBuffers(out);
  }
}

static long restart(LUID luid, ADMISSION_FW_RESTART_DRILL *drill) {
  D3DKMT_OPENADAPTERFROMLUID open;
  D3DKMT_CLOSEADAPTER close;
  D3DKMT_ESCAPE escape;
  NTSTATUS status;
  memset(&open, 0, sizeof(open));
  open.AdapterLuid = luid;
  status = D3DKMTOpenAdapterFromLuid(&open);
  if (status < 0) return (long)status;
  memset(drill, 0, sizeof(*drill));
  drill->Magic = ADMISSION_FW_RESTART_DRILL_MAGIC;
  drill->Version = ADMISSION_FW_RESTART_DRILL_VERSION;
  drill->Bytes = sizeof(*drill);
  drill->Operation = ADMISSION_FW_RESTART_DRILL_WARM_ORDERLY;
  memset(&escape, 0, sizeof(escape));
  escape.hAdapter = open.hAdapter;
  escape.Type = D3DKMT_ESCAPE_DRIVERPRIVATE;
  escape.pPrivateDriverData = drill;
  escape.PrivateDriverDataSize = sizeof(*drill);
  status = D3DKMTEscape(&escape);
  memset(&close, 0, sizeof(close));
  close.hAdapter = open.hAdapter;
  D3DKMTCloseAdapter(&close);
  return (long)status;
}

int main(int argc, char **argv) {
  IDXGIFactory1 *factory = NULL;
  IDXGIAdapter1 *adapter = NULL;
  DXGI_ADAPTER_DESC1 desc;
  unsigned int cycles = 1u, delay_ms = 2000u, retries = 20u, cycle, passed = 0u;
  int found = 0;
  HRESULT hr;

  if (argc < 2) {
    printf("usage: agx_fw_restart_drill.exe <out.txt> [cycles] [delay_ms] [retries]\n");
    return 2;
  }
  if (argc > 2) cycles = (unsigned int)strtoul(argv[2], NULL, 10);
  if (argc > 3) delay_ms = (unsigned int)strtoul(argv[3], NULL, 10);
  if (argc > 4) retries = (unsigned int)strtoul(argv[4], NULL, 10);
  out = CreateFileA(argv[1], GENERIC_WRITE, FILE_SHARE_READ, NULL, CREATE_ALWAYS,
                    FILE_ATTRIBUTE_NORMAL | FILE_FLAG_WRITE_THROUGH, NULL);
  say("FW_RESTART_DRILL cycles %u delay_ms %u retries %u pid %lu", cycles, delay_ms,
      retries, GetCurrentProcessId());
  hr = CreateDXGIFactory1(&IID_IDXGIFactory1, (void **)&factory);
  if (FAILED(hr)) { say("CreateDXGIFactory1 0x%08lx", (unsigned long)hr); return 1; }
  for (UINT a = 0; IDXGIFactory1_EnumAdapters1(factory, a, &adapter) != DXGI_ERROR_NOT_FOUND; ++a) {
    DXGI_ADAPTER_DESC1 d;
    IDXGIAdapter1_GetDesc1(adapter, &d);
    say("ADAPTER %u vendor %04x flags %x %ls", a, d.VendorId, d.Flags, d.Description);
    if (!found && !(d.Flags & DXGI_ADAPTER_FLAG_SOFTWARE)) { desc = d; found = 1; }
    IDXGIAdapter1_Release(adapter);
  }
  if (!found) { say("NO_HARDWARE_ADAPTER"); return 1; }
  for (cycle = 1u; cycle <= cycles; ++cycle) {
    ADMISSION_FW_RESTART_DRILL drill;
    unsigned int attempt = 0u;
    long status;
    for (;;) {
      say("CYCLE %u ATTEMPT %u ESCAPE", cycle, attempt);
      status = restart(desc.AdapterLuid, &drill);
      say("CYCLE %u ATTEMPT %u escape 0x%08lx status 0x%08x stage %u restarts %u succeeded %u",
          cycle, attempt, (unsigned long)status, drill.Status, drill.Stage, drill.Restarts,
          drill.Succeeded);
      if (status < 0 || drill.Status != DRILL_STATUS_DEVICE_BUSY || attempt >= retries)
        break;
      ++attempt;
      Sleep(delay_ms);
    }
    if (status < 0) { say("ESCAPE_REFUSED"); return 1; }
    if (drill.Status != 0u) { say("RESTART_FAILED cycle %u", cycle); return 1; }
    ++passed;
    Sleep(delay_ms);
  }
  say("FW_RESTART_DRILL_DONE passed %u of %u", passed, cycles);
  return 0;
}
