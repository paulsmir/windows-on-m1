// Live kernel memory dump of a running guest (EXP1073). A GPU scheduling
// hang (no TDR) left Notepad, a D3D probe and every later submission waiting,
// and the KMD state cannot be read from user mode. This takes the same live
// dump that Task Manager's "Create live kernel memory dump" takes
// (NtSystemDebugControl, SysDbgGetLiveKernelDump); the system keeps running.
// Requires an elevated caller (SeDebugPrivilege).
// Usage: agx_livedump <output.dmp> [user]   (user: also include user pages)
#include <windows.h>
#include <stdio.h>
#pragma comment(lib, "advapi32.lib")

typedef LONG NTSTATUS;
typedef NTSTATUS(NTAPI *NtSystemDebugControlFn)(ULONG, PVOID, ULONG, PVOID, ULONG, PULONG);

enum { SysDbgGetLiveKernelDump = 37 };

typedef union {
  struct {
    ULONG UseDumpStorageStack : 1;
    ULONG CompressMemoryPagesData : 1;
    ULONG IncludeUserSpaceMemoryPages : 1;
    ULONG AbortIfMemoryPressure : 1;
    ULONG SelectiveDump : 1;
    ULONG Reserved : 27;
  };
  ULONG AsUlong;
} LIVEDUMP_FLAGS;

typedef union {
  struct {
    ULONG HypervisorPages : 1;
    ULONG NonEssentialHypervisorPages : 1;
    ULONG Reserved : 30;
  };
  ULONG AsUlong;
} LIVEDUMP_ADDPAGES;

typedef struct {
  ULONG Version;
  ULONG BugCheckCode;
  ULONG_PTR BugCheckParam1, BugCheckParam2, BugCheckParam3, BugCheckParam4;
  HANDLE DumpFileHandle;
  HANDLE CancelEventHandle;
  LIVEDUMP_FLAGS Flags;
  LIVEDUMP_ADDPAGES AddPagesControl;
} LIVEDUMP_CONTROL;

static bool enable_debug_privilege() {
  HANDLE token;
  if (!OpenProcessToken(GetCurrentProcess(), TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY, &token)) return false;
  TOKEN_PRIVILEGES tp = {1};
  bool ok = LookupPrivilegeValueW(nullptr, L"SeDebugPrivilege", &tp.Privileges[0].Luid) &&
            (tp.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED,
             AdjustTokenPrivileges(token, FALSE, &tp, sizeof(tp), nullptr, nullptr)) &&
            GetLastError() == ERROR_SUCCESS;
  CloseHandle(token);
  return ok;
}

int main(int argc, char **argv) {
  if (argc < 2) { printf("usage: agx_livedump <output.dmp> [user]\n"); return 2; }
  if (!enable_debug_privilege()) { printf("FAIL SeDebugPrivilege %lu\n", GetLastError()); return 2; }
  auto fn = (NtSystemDebugControlFn)GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "NtSystemDebugControl");
  if (!fn) { printf("FAIL NtSystemDebugControl\n"); return 2; }
  HANDLE file = CreateFileA(argv[1], GENERIC_READ | GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                            FILE_ATTRIBUTE_NORMAL, nullptr);
  if (file == INVALID_HANDLE_VALUE) { printf("FAIL create %lu\n", GetLastError()); return 2; }
  LIVEDUMP_CONTROL control = {};
  control.Version = 1;
  control.BugCheckCode = 0x161; // LIVE_SYSTEM_DUMP
  control.DumpFileHandle = file;
  control.Flags.IncludeUserSpaceMemoryPages = argc > 2 ? 1 : 0;
  ULONG returned = 0;
  NTSTATUS status = fn(SysDbgGetLiveKernelDump, &control, sizeof(control), nullptr, 0, &returned);
  LARGE_INTEGER size = {};
  GetFileSizeEx(file, &size);
  CloseHandle(file);
  printf("livedump status=0x%08lx bytes=%lld\n", (unsigned long)status, size.QuadPart);
  return status < 0 ? 1 : 0;
}
