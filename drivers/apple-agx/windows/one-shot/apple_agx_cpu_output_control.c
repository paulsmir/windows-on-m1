#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include "render_gdi_receipt.h"

/* CPU-only device-buffer diagnostic: never opens an adapter or submits work. */
#define CONTROL_BYTES (2560u * 1600u * 4u)
#define CONTROL_COLOR 0xffcc8844u

int main(void) {
  HANDLE mapping = NULL;
  unsigned int *pixels = NULL;
  DWORD_PTR previousAffinity = 0;
  ADMISSION_TERMINAL_RECEIPT receipt;
  unsigned int index, pass;
  int result = 1;
  printf("CPU_CONTROL_BEGIN bytes=%u passes=16 cpu=4 gpu_calls=0\n", CONTROL_BYTES);
  fflush(stdout);
  if (GetActiveProcessorCount(ALL_PROCESSOR_GROUPS) != 8u)
    goto cleanup;
  mapping = CreateFileMappingW(INVALID_HANDLE_VALUE, NULL,
      PAGE_READWRITE | SEC_COMMIT | SEC_NOCACHE, 0u, CONTROL_BYTES, NULL);
  if (mapping == NULL)
    goto cleanup;
  pixels = (unsigned int *)MapViewOfFile(mapping, FILE_MAP_ALL_ACCESS,
                                        0u, 0u, CONTROL_BYTES);
  if (pixels == NULL)
    goto cleanup;
  for (index = 0u; index < CONTROL_BYTES / 4u; ++index)
    pixels[index] = CONTROL_COLOR;
  previousAffinity = SetThreadAffinityMask(GetCurrentThread(), (DWORD_PTR)1u << 4u);
  if (previousAffinity == 0)
    goto cleanup;
  for (pass = 0u; pass < 16u; ++pass) {
    ULONGLONG started;
    int captured;
    ZeroMemory(&receipt, sizeof(receipt));
    /* Helper input only; these are not KMD/hardware completion receipts. */
    receipt.ValidMask = ADMISSION_TERMINAL_VALID_TERMINAL;
    receipt.Fence = pass + 1u;
    printf("CPU_SCAN_BEGIN pass=%u cpu=%lu thread=%lu\n", pass,
           GetCurrentProcessorNumber(), GetCurrentThreadId());
    fflush(stdout);
    started = GetTickCount64();
    captured = AdmissionTerminalReceiptCaptureOutput(&receipt, pass + 1u,
        (const unsigned char *)pixels, CONTROL_BYTES, CONTROL_BYTES,
        CONTROL_COLOR, 0xa5u);
    printf("CPU_SCAN_END pass=%u cpu=%lu ms=%llu captured=%d pixels=%u "
           "bytes=%u hash=0x%llx\n", pass, GetCurrentProcessorNumber(),
           (unsigned long long)(GetTickCount64() - started), captured,
           receipt.OutputPixelsExpected, receipt.OutputBytesExamined,
           receipt.OutputTargetFnv1a);
    fflush(stdout);
    if (!captured || receipt.OutputPixelsExpected != CONTROL_BYTES / 4u ||
        receipt.OutputBytesExamined != CONTROL_BYTES ||
        receipt.OutputTargetFnv1a != 0xad1245c8bf762325ULL)
      goto cleanup;
  }
  result = 0;
cleanup:
  printf("CPU_CONTROL_END result=%d last_error=%lu\n", result, GetLastError());
  fflush(stdout);
  if (previousAffinity != 0)
    (void)SetThreadAffinityMask(GetCurrentThread(), previousAffinity);
  if (pixels != NULL)
    (void)UnmapViewOfFile(pixels);
  if (mapping != NULL)
    (void)CloseHandle(mapping);
  return result;
}
