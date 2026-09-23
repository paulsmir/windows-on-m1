"""Build-only projection of pinned Mesa software KMT for the negotiation probe.
MIT permission reviewed; preserve the upstream notice. Never a production KMD.
"""
import argparse
import hashlib
from pathlib import Path

p = argparse.ArgumentParser()
p.add_argument('--source', required=True)
p.add_argument('--output', required=True)
a = p.parse_args()
raw = Path(a.source).read_bytes()
expected = '02b1f274d5e6d52e1b4602d6c3c870c5587fb9e69a717fc5fc1dfc9491e622d6'
if hashlib.sha256(raw).hexdigest() != expected:
    raise SystemExit('Pinned software KMT source hash mismatch')
s = raw.decode()
old = '#include "DriverIncludes.h"'
assert s.count(old) == 1
s = s.replace(old, r'''#define WIN32_NO_STATUS
#include <windows.h>
#include <wingdi.h>
#undef WIN32_NO_STATUS
typedef LONG NTSTATUS;
#include <ntstatus.h>
#define D3DKMDT_SPECIAL_MULTIPLATFORM_TOOL
#include <d3dkmthk.h>
#include <assert.h>
extern "C" void AgxRuntimeProbeKmtReceipt(const char *name);
#define LOG_ENTRYPOINT() AgxRuntimeProbeKmtReceipt(__func__)
#define LOG_UNSUPPORTED_ENTRYPOINT() AgxRuntimeProbeKmtReceipt(__func__)
#define DebugPrintf(...) ((void)0)
''')
s = s.replace('EXTERN_C NTSTATUS APIENTRY',
              'EXTERN_C __declspec(dllexport) NTSTATUS APIENTRY')
s = s.replace('EXTERN_C BOOLEAN APIENTRY',
              'EXTERN_C __declspec(dllexport) BOOLEAN APIENTRY')
Path(a.output).write_text(s)
print('SOFTWARE_KMT_PROJECTION source_sha256=' + expected +
      ' output_sha256=' + hashlib.sha256(Path(a.output).read_bytes()).hexdigest())
