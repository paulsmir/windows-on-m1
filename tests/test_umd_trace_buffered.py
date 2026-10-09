"""EXP1109: UMD trace lines reach the file in batches.

EXP1108 sn1108 (position-only window drag): DWM's composition thread issued
~30 NtWriteFile per frame from AdmissionUmdDiagnostic (measure lines of every
submission), each through the file-system filter stack, inside a ~9.8 ms
frame. Invariants, with the real functions compiled against stub Windows
calls:
- consecutive measure lines within the flush interval cost no write; the
  interval elapsing, a full buffer, AdmissionUmdDiagnosticFlush, or a refusal
  or failure record write everything buffered, in order, in one append;
- no line is lost or reordered, including across a buffer overflow;
- the session id is queried once per process.
"""
from pathlib import Path
import re
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
SRC = ROOT / 'drivers/apple-agx/render-admission/umd/src/umd_runtime_device.c'


def function(text, signature):
    start = text.index(signature)
    start = text.rfind('\n', 0, start) + 1
    brace = text.index('{', start); depth = 1; end = brace + 1
    while depth:
        depth += (text[end] == '{') - (text[end] == '}'); end += 1
    return text[start:end]


HARNESS = r'''
#include <cassert>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
typedef int BOOL; typedef unsigned long DWORD; typedef unsigned long ULONG;
typedef long LONG; typedef long HRESULT; typedef long long LONGLONG;
typedef const char *PCSTR; typedef wchar_t WCHAR; typedef const wchar_t *PCWSTR;
typedef void *PVOID; typedef void *HANDLE; typedef unsigned int UINT;
typedef void VOID; typedef size_t SIZE_T;
typedef union { struct { DWORD LowPart; LONG HighPart; } u; LONGLONG QuadPart; } LARGE_INTEGER;
typedef struct { void *p; } INIT_ONCE, *PINIT_ONCE; typedef struct { void *p; } SRWLOCK;
#define INIT_ONCE_STATIC_INIT {0}
#define SRWLOCK_INIT {0}
#define TRUE 1
#define FALSE 0
#define CALLBACK
#define MAX_PATH 260
#define MAXDWORD 0xffffffffUL
#define S_OK 0L
#define E_FAIL ((HRESULT)0x80004005L)
#define FAILED(x) ((x) < 0)
#define UNREFERENCED_PARAMETER(x) (void)(x)
#define ARRAYSIZE(a) (sizeof(a)/sizeof((a)[0]))
#define INVALID_HANDLE_VALUE ((HANDLE)(intptr_t)-1)
#define FILE_APPEND_DATA 4
#define FILE_SHARE_READ 1
#define FILE_SHARE_WRITE 2
#define FILE_SHARE_DELETE 4
#define OPEN_ALWAYS 4
#define FILE_ATTRIBUTE_NORMAL 0x80
#define _TRUNCATE ((size_t)-1)
#define InterlockedIncrement(p) (++*(p))
static long InterlockedExchange(volatile long *p, long x){long o=*p;*p=x;return o;}
#define InterlockedCompareExchange(p,x,c) (*(p)==(c)?(*(p)=(x),(c)):*(p))
static void *InterlockedCompareExchangePointer(void *volatile *p, void *x, void *c) {
  void *old=*p; if(old==c) *p=x; return old; }
static DWORD lastError=7; static LONGLONG qpc=1000; static int sessions, writes, creates;
static std::string file; static int locked;
static DWORD GetLastError(void){return lastError;}
static void SetLastError(DWORD e){lastError=e;}
static BOOL InitOnceExecuteOnce(PINIT_ONCE o, BOOL (*f)(PINIT_ONCE,PVOID,PVOID*), PVOID p, PVOID *c){
  static int done; if(!done){done=1;f(o,p,c);} return TRUE;}
static DWORD GetEnvironmentVariableW(PCWSTR name, WCHAR *out, DWORD n){
  if(!wcscmp(name,L"APPLE_AGX_UMD_TRACE_FILE")){wcsncpy(out,L"C:\\t.log",n);return 8;}
  return 0;}
static BOOL QueryPerformanceCounter(LARGE_INTEGER *v){v->QuadPart=qpc;return TRUE;}
static BOOL QueryPerformanceFrequency(LARGE_INTEGER *v){v->QuadPart=1000000;return TRUE;}
static DWORD GetCurrentProcessId(void){return 1224;}
static DWORD GetCurrentThreadId(void){return 1372;}
static BOOL ProcessIdToSessionId(DWORD, DWORD *s){++sessions;*s=1;return TRUE;}
static HANDLE CreateFileW(PCWSTR,DWORD,DWORD,void*,DWORD,DWORD,HANDLE){++creates;return (HANDLE)(intptr_t)42;}
static BOOL CloseHandle(HANDLE){return TRUE;}
static BOOL WriteFile(HANDLE h,const void *b,DWORD n,DWORD *w,void*){
  assert(h==(HANDLE)(intptr_t)42); ++writes; file.append((const char*)b,n); *w=n; return TRUE;}
static void AcquireSRWLockExclusive(SRWLOCK*){assert(!locked);locked=1;}
static void ReleaseSRWLockExclusive(SRWLOCK*){assert(locked);locked=0;}
static BOOL TryAcquireSRWLockExclusive(SRWLOCK*){if(locked) return FALSE;locked=1;return TRUE;}
static int _snprintf_s(char *b, size_t n, size_t, const char *f, ...){
  va_list a; va_start(a,f); int r=vsnprintf(b,n,f,a); va_end(a); return r<(int)n?r:-1;}
@@FUNCS@@
static int lines(void){int n=0;for(char c:file) n+=c=='\n';return n;}
int main(void){
  UINT v[2]={1,2};
  for(int i=0;i<100;++i){v[0]=(UINT)i;AdmissionUmdDiagnostic("measure-g4-phase",S_OK,v,2u);}
  assert(lastError==7);
  assert(writes==0 && file.empty());
  qpc+=300000; /* 300 ms later */
  AdmissionUmdDiagnostic("measure-g4-phase",S_OK,v,2u);
  assert(writes==1 && lines()==101);
  /* In order, nothing lost. */
  for(int i=0;i<100;++i){char want[32];snprintf(want,sizeof(want)," %08x %08x\n",i,2);
    size_t at=0;for(int k=0;k<i;++k) at=file.find('\n',at)+1;
    assert(file.compare(file.find('\n',at)-strlen(want)+1,strlen(want),want)==0);}
  /* A refusal is written at once, after what was buffered. */
  AdmissionUmdDiagnostic("measure-g4-phase",S_OK,v,2u);
  AdmissionUmdDiagnostic("reject-batch",E_FAIL,v,2u);
  assert(writes==2 && lines()==103 && file.rfind("reject-batch")>file.rfind("measure-g4-phase"));
  /* Overflow: buffered lines are written before the buffer would overflow. */
  int before=writes; size_t bytes=file.size();
  for(int i=0;i<2000;++i) AdmissionUmdDiagnostic("measure-ddi-time",S_OK,v,2u);
  assert(writes>before && locked==0);
  AdmissionUmdDiagnosticFlush();
  assert(lines()==2103 && file.size()>bytes);
  int after=writes; AdmissionUmdDiagnosticFlush(); assert(writes==after);
  assert(sessions==1 && creates==1);
  puts("EXP1109 buffered UMD trace: PASS");
  return 0;
}
'''


class TraceBuffered(unittest.TestCase):
    def test_trace_lines_are_batched(self):
        text = SRC.read_text()
        start = text.index('static INIT_ONCE AdmissionUmdTraceOnce')
        end = text.index('static BOOL AdmissionUmdPresentMeasureSample(')
        funcs = text[start:end]
        funcs = re.sub(r'_Return_type_success_\([^)]*\)', '', funcs)
        with tempfile.TemporaryDirectory() as tmp:
            src = Path(tmp) / 'trace.cpp'; exe = Path(tmp) / 'trace'
            src.write_text(HARNESS.replace('@@FUNCS@@', funcs))
            built = subprocess.run(['clang++', '-std=c++17', '-Wall', '-Wextra',
                                    '-Wno-unused-function', '-Wno-unused-parameter',
                                    '-fsanitize=address,undefined', str(src), '-o', str(exe)],
                                   capture_output=True, text=True)
            self.assertEqual(built.returncode, 0, built.stdout + built.stderr)
            ran = subprocess.run([str(exe)], capture_output=True, text=True)
            self.assertEqual(ran.returncode, 0, ran.stdout + ran.stderr)
            self.assertIn('PASS', ran.stdout)

    def test_flush_on_process_detach(self):
        dll = (ROOT / 'drivers/apple-agx/render-admission/umd/src/umd.c').read_text()
        main = function(dll, 'BOOL WINAPI DllMain(')
        self.assertIn('DLL_PROCESS_DETACH', main)
        self.assertIn('AdmissionUmdDiagnosticFlush()', main)


if __name__ == '__main__':
    unittest.main()
