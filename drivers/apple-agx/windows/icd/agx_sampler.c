/* agx_sampler: a user-mode sampling profiler for real-app performance work.
 *
 * WPR CPU sampling is not available on the J313 guest (EXP1907: wpr -start CPU
 * fails with 0x80070032; the hypervisor exposes no profiling interrupt). This
 * tool suspends each thread of one process at a fixed interval, reads its
 * PC and LR (ARM64) or RIP (x64), and resumes it. Only threads whose CPU time
 * advanced since the previous sample are counted, so waiting threads do not
 * dilute the profile. Addresses are reported as module+RVA; resolve them
 * offline with the module's PDB.
 *
 * usage: agx_sampler <pid|name.exe> <seconds> <interval_ms> <out.txt>
 * output: SAMPLER_SUMMARY, per-thread and per-module counts, then the
 * hottest PC and LR (caller) RVAs.
 */
#define WIN32_LEAN_AND_MEAN
#define _CRT_SECURE_NO_WARNINGS
#include <windows.h>
#include <psapi.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <tlhelp32.h>
#include <timeapi.h>

#define MAX_THREADS 512
#define MAX_MODULES 512
#define MAX_SITES 65536

typedef struct {
  DWORD tid;
  HANDLE handle;
  ULONGLONG cpu;
  unsigned samples;
} THREAD_SLOT;

typedef struct {
  ULONGLONG base, size;
  char name[64];
  unsigned samples;
} MODULE_SLOT;

typedef struct {
  int module;
  ULONGLONG rva;
  unsigned count;
} SITE;

static THREAD_SLOT threads[MAX_THREADS];
static unsigned thread_count;
static MODULE_SLOT modules[MAX_MODULES];
static unsigned module_count;
static SITE pc_sites[MAX_SITES], lr_sites[MAX_SITES];
static unsigned pc_count, lr_count;

/* Cycle time, not GetThreadTimes: the latter advances only at clock ticks
 * (15.6 ms), coarser than the sampling interval. */
static ULONGLONG thread_cpu(HANDLE h) {
  ULONG64 cycles = 0;
  return QueryThreadCycleTime(h, &cycles) ? cycles : 0;
}

static void refresh_threads(DWORD pid) {
  HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
  THREADENTRY32 te;
  if (snap == INVALID_HANDLE_VALUE) return;
  te.dwSize = sizeof(te);
  for (BOOL ok = Thread32First(snap, &te); ok; ok = Thread32Next(snap, &te)) {
    unsigned i;
    if (te.th32OwnerProcessID != pid) continue;
    for (i = 0; i < thread_count && threads[i].tid != te.th32ThreadID; ++i) {}
    if (i < thread_count || thread_count == MAX_THREADS) continue;
    threads[i].handle = OpenThread(THREAD_SUSPEND_RESUME | THREAD_GET_CONTEXT |
                                   THREAD_QUERY_INFORMATION, FALSE, te.th32ThreadID);
    if (threads[i].handle == NULL) continue;
    threads[i].tid = te.th32ThreadID;
    threads[i].cpu = thread_cpu(threads[i].handle);
    threads[i].samples = 0;
    ++thread_count;
  }
  CloseHandle(snap);
}

static void refresh_modules(HANDLE process) {
  HMODULE list[MAX_MODULES];
  DWORD bytes = 0;
  if (!EnumProcessModulesEx(process, list, sizeof(list), &bytes, LIST_MODULES_ALL)) return;
  unsigned n = bytes / sizeof(HMODULE);
  if (n > MAX_MODULES) n = MAX_MODULES;
  for (unsigned i = 0; i < n; ++i) {
    MODULEINFO mi;
    unsigned j;
    if (!GetModuleInformation(process, list[i], &mi, sizeof(mi))) continue;
    for (j = 0; j < module_count && modules[j].base != (ULONGLONG)mi.lpBaseOfDll; ++j) {}
    if (j < module_count || module_count == MAX_MODULES) continue;
    modules[j].base = (ULONGLONG)mi.lpBaseOfDll;
    modules[j].size = mi.SizeOfImage;
    if (!GetModuleBaseNameA(process, list[i], modules[j].name, sizeof(modules[j].name)))
      strcpy(modules[j].name, "?");
    modules[j].samples = 0;
    ++module_count;
  }
}

static int find_module(ULONGLONG address) {
  for (unsigned i = 0; i < module_count; ++i)
    if (address >= modules[i].base && address < modules[i].base + modules[i].size) return (int)i;
  return -1;
}

static void count_site(SITE *sites, unsigned *count, int module, ULONGLONG rva) {
  for (unsigned i = 0; i < *count; ++i)
    if (sites[i].module == module && sites[i].rva == rva) {
      ++sites[i].count;
      return;
    }
  if (*count < MAX_SITES) {
    sites[*count].module = module;
    sites[*count].rva = rva;
    sites[*count].count = 1;
    ++*count;
  }
}

static int by_count(const void *a, const void *b) {
  const SITE *x = (const SITE *)a, *y = (const SITE *)b;
  return x->count < y->count ? 1 : x->count > y->count ? -1 : 0;
}

static DWORD find_pid(const char *name) {
  HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
  PROCESSENTRY32 pe;
  DWORD pid = 0;
  if (snap == INVALID_HANDLE_VALUE) return 0;
  pe.dwSize = sizeof(pe);
  for (BOOL ok = Process32First(snap, &pe); ok; ok = Process32Next(snap, &pe))
    if (_stricmp(pe.szExeFile, name) == 0) {
      pid = pe.th32ProcessID;
      break;
    }
  CloseHandle(snap);
  return pid;
}

static void print_sites(FILE *out, const char *tag, SITE *sites, unsigned count) {
  qsort(sites, count, sizeof(SITE), by_count);
  for (unsigned i = 0; i < count && i < 120; ++i)
    fprintf(out, "%s %u %s+0x%llx\n", tag, sites[i].count,
            sites[i].module >= 0 ? modules[sites[i].module].name : "?",
            (unsigned long long)sites[i].rva);
}

int main(int argc, char **argv) {
  DWORD pid;
  HANDLE process;
  FILE *out;
  unsigned seconds, interval, ticks, taken = 0, idle = 0, failed = 0;
  if (argc != 5) {
    fprintf(stderr, "usage: agx_sampler <pid|name.exe> <seconds> <interval_ms> <out.txt>\n");
    return 2;
  }
  pid = strtoul(argv[1], NULL, 10);
  if (pid == 0) pid = find_pid(argv[1]);
  seconds = strtoul(argv[2], NULL, 10);
  interval = strtoul(argv[3], NULL, 10);
  if (pid == 0 || seconds == 0 || interval == 0) {
    fprintf(stderr, "SAMPLER_NO_TARGET %s\n", argv[1]);
    return 3;
  }
  process = OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, FALSE, pid);
  out = fopen(argv[4], "w");
  if (process == NULL || out == NULL) {
    fprintf(stderr, "SAMPLER_OPEN_FAIL %lu\n", GetLastError());
    return 4;
  }
  timeBeginPeriod(1);
  ticks = seconds * 1000u / interval;
  for (unsigned t = 0; t < ticks; ++t) {
    if (t % (1000u / interval + 1u) == 0) {
      refresh_threads(pid);
      refresh_modules(process);
    }
    for (unsigned i = 0; i < thread_count; ++i) {
      CONTEXT ctx;
      ULONGLONG pc, lr = 0, cpu;
      if (threads[i].handle == NULL) continue;
      cpu = thread_cpu(threads[i].handle);
      if (cpu == threads[i].cpu) {
        ++idle;
        continue;
      }
      threads[i].cpu = cpu;
      if (SuspendThread(threads[i].handle) == (DWORD)-1) {
        ++failed;
        continue;
      }
      memset(&ctx, 0, sizeof(ctx));
      ctx.ContextFlags = CONTEXT_CONTROL | CONTEXT_INTEGER;
      if (!GetThreadContext(threads[i].handle, &ctx)) {
        ResumeThread(threads[i].handle);
        ++failed;
        continue;
      }
      ResumeThread(threads[i].handle);
#if defined(_M_ARM64)
      pc = ctx.Pc;
      lr = ctx.Lr;
#else
      pc = ctx.Rip;
#endif
      ++threads[i].samples;
      ++taken;
      int m = find_module(pc);
      if (m >= 0) ++modules[m].samples;
      count_site(pc_sites, &pc_count, m, m >= 0 ? pc - modules[m].base : pc);
      if (lr) {
        int lm = find_module(lr);
        count_site(lr_sites, &lr_count, lm, lm >= 0 ? lr - modules[lm].base : lr);
      }
    }
    Sleep(interval);
  }
  timeEndPeriod(1);
  fprintf(out, "SAMPLER_SUMMARY pid %lu seconds %u interval_ms %u samples %u idle %u failed %u threads %u modules %u\n",
          pid, seconds, interval, taken, idle, failed, thread_count, module_count);
  for (unsigned i = 0; i < thread_count; ++i)
    if (threads[i].samples)
      fprintf(out, "THREAD %lu %u\n", threads[i].tid, threads[i].samples);
  for (unsigned i = 0; i < module_count; ++i)
    if (modules[i].samples)
      fprintf(out, "MODULE %u %s base 0x%llx\n", modules[i].samples, modules[i].name,
              (unsigned long long)modules[i].base);
  print_sites(out, "PC", pc_sites, pc_count);
  print_sites(out, "LR", lr_sites, lr_count);
  fclose(out);
  printf("SAMPLER_DONE samples %u\n", taken);
  return 0;
}
