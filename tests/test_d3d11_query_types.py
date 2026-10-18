"""EXP1336: D3D 11 query types create their Asahi queries.

Invariant caught: on an FL 11_0 device the runtime creates
D3D11DDI_QUERY_PIPELINESTATS (eleven statistics, HS/DS/CS invocations added)
and the per-stream stream-output statistics and overflow predicates
(D3D11DDI_QUERY_STREAMOUTPUTSTATS_STREAM0..3,
D3D11DDI_QUERY_STREAMOVERFLOWPREDICATE_STREAM0..3). CreateQuery accepted only
the D3D 10 types and refused these with E_NOTIMPL (reject-seterror
fn=CreateQuery), and the runtime removed the device (agx_stats_smoke:
CreateQuery 0x887a0005). The D3D 11 pipeline statistics are the eleven
single-statistic queries in the order of
D3D11_DDI_QUERY_DATA_PIPELINE_STATISTICS (PIPE_STAT_QUERY_IA_VERTICES ..
CS_INVOCATIONS); a stream's statistics and overflow predicate use that
stream's index; the D3D 10 types are unchanged. Runs the projected
CreateQuery against stubs (see test_d3d10_shader_fields_init.py). GPU
timestamps: with the screen's query_timestamp a TIMESTAMP creates Mesa's
PIPE_QUERY_TIMESTAMP; without it the event-fence CPU counter remains.
"""
from pathlib import Path
import os
import subprocess
import tempfile
import unittest

from test_d3d10_shader_fields_init import function_body, project

PROGRAM = r'''
#include <cassert>
#include <cstring>
#include <vector>
typedef unsigned UINT; typedef unsigned long ULONG; typedef unsigned long long ULONGLONG; typedef long HRESULT;
#define S_OK ((HRESULT)0)
#define E_NOTIMPL ((HRESULT)0x80004001L)
#define E_FAIL ((HRESULT)0x80004005L)
#define E_OUTOFMEMORY ((HRESULT)0x8007000EL)
#define APIENTRY
#define __in
#define LOG_ENTRYPOINT()
enum D3D10DDI_QUERY { D3D10DDI_QUERY_EVENT = 0, D3D10DDI_QUERY_OCCLUSION, D3D10DDI_QUERY_TIMESTAMP,
  D3D10DDI_QUERY_TIMESTAMPDISJOINT, D3D10DDI_QUERY_PIPELINESTATS, D3D10DDI_QUERY_OCCLUSIONPREDICATE,
  D3D10DDI_QUERY_STREAMOUTPUTSTATS, D3D10DDI_QUERY_STREAMOVERFLOWPREDICATE, D3D11DDI_QUERY_PIPELINESTATS,
  D3D11DDI_QUERY_STREAMOUTPUTSTATS_STREAM0, D3D11DDI_QUERY_STREAMOUTPUTSTATS_STREAM1,
  D3D11DDI_QUERY_STREAMOUTPUTSTATS_STREAM2, D3D11DDI_QUERY_STREAMOUTPUTSTATS_STREAM3,
  D3D11DDI_QUERY_STREAMOVERFLOWPREDICATE_STREAM0, D3D11DDI_QUERY_STREAMOVERFLOWPREDICATE_STREAM1,
  D3D11DDI_QUERY_STREAMOVERFLOWPREDICATE_STREAM2, D3D11DDI_QUERY_STREAMOVERFLOWPREDICATE_STREAM3 };
#define D3D10DDI_QUERY_MISCFLAG_PREDICATEHINT 1u
enum { PIPE_QUERY_OCCLUSION_COUNTER = 1, PIPE_QUERY_OCCLUSION_PREDICATE, PIPE_QUERY_PIPELINE_STATISTICS_SINGLE,
  PIPE_QUERY_PRIMITIVES_EMITTED, PIPE_QUERY_PRIMITIVES_GENERATED, PIPE_QUERY_SO_OVERFLOW_PREDICATE,
  PIPE_QUERY_TIMESTAMP };
struct pipe_screen { struct { bool query_timestamp; } caps; };
struct D3D10DDIARG_CREATEQUERY { D3D10DDI_QUERY Query; UINT MiscFlags; };
struct D3D10DDI_HDEVICE { void *pDrvPrivate; };
struct D3D10DDI_HQUERY { void *pDrvPrivate; };
struct D3D10DDI_HRTQUERY { void *handle; };
struct pipe_query { int type; unsigned index; };
static std::vector<pipe_query *> made;
struct pipe_context {
  pipe_screen *screen;
  pipe_query *(*create_query)(pipe_context *, unsigned, unsigned);
  void (*destroy_query)(pipe_context *, pipe_query *);
};
static pipe_query *create_query(pipe_context *, unsigned type, unsigned index) {
  pipe_query *q = new pipe_query{(int)type, index}; made.push_back(q); return q; }
static void destroy_query(pipe_context *, pipe_query *q) { delete q; }
struct AGX_D3D10_WINDOWS_DEVICE { int unused; };
struct Device { pipe_context *pipe; AGX_D3D10_WINDOWS_DEVICE *windows; };
enum QueryPhase { QueryCreated, QueryIssued, QuerySignaled, QueryFailed, QueryDestroyed };
#define AGX_EVENT_QUERY_MAGIC 0x51564541u
struct Query { UINT Magic; Device *OwnerDevice; ULONGLONG OwnerCookie; ULONG DeviceGeneration;
  QueryPhase Phase; HRESULT LastError; D3D10DDI_QUERY Type; UINT Flags; pipe_query *handle;
  pipe_query *extra[10]; UINT extra_count; };
static Device *CastDevice(D3D10DDI_HDEVICE h) { return (Device *)h.pDrvPrivate; }
static Query *CastQuery(D3D10DDI_HQUERY h) { return (Query *)h.pDrvPrivate; }
static bool AgxD3d10WindowsIdentity(AGX_D3D10_WINDOWS_DEVICE *, ULONGLONG *o, ULONG *g) { *o = 1; *g = 1; return true; }
static HRESULT lastError;
static void SetError(D3D10DDI_HDEVICE, HRESULT hr) { lastError = hr; }
void APIENTRY
CreateQuery(D3D10DDI_HDEVICE hDevice, __in const D3D10DDIARG_CREATEQUERY *pCreateQuery,
            D3D10DDI_HQUERY hQuery, D3D10DDI_HRTQUERY hRTQuery)
@@BODY@@
static pipe_screen screen = {{false}};
static Query create(D3D10DDI_QUERY type, UINT flags = 0) {
  static pipe_context pipe = {&screen, create_query, destroy_query};
  static AGX_D3D10_WINDOWS_DEVICE w;
  static Device dev = {&pipe, &w};
  Query q; memset(&q, 0, sizeof(q));
  D3D10DDIARG_CREATEQUERY args = {type, flags};
  lastError = S_OK; made.clear();
  CreateQuery(D3D10DDI_HDEVICE{&dev}, &args, D3D10DDI_HQUERY{&q}, D3D10DDI_HRTQUERY{nullptr});
  return q;
}
int main() {
  Query q = create(D3D11DDI_QUERY_PIPELINESTATS);
  assert(lastError == S_OK && made.size() == 11 && q.extra_count == 10 && q.handle == made[0]);
  for (unsigned i = 0; i < 11; ++i)
    assert(made[i]->type == PIPE_QUERY_PIPELINE_STATISTICS_SINGLE && made[i]->index == i);
  q = create(D3D10DDI_QUERY_PIPELINESTATS);
  assert(lastError == S_OK && made.size() == 8 && q.extra_count == 7);
  q = create(D3D11DDI_QUERY_STREAMOUTPUTSTATS_STREAM2);
  assert(lastError == S_OK && made.size() == 2 && made[0]->type == PIPE_QUERY_PRIMITIVES_EMITTED &&
         made[0]->index == 2 && made[1]->type == PIPE_QUERY_PRIMITIVES_GENERATED && made[1]->index == 2);
  q = create(D3D10DDI_QUERY_STREAMOUTPUTSTATS);
  assert(lastError == S_OK && made.size() == 2 && made[0]->index == 0);
  q = create(D3D11DDI_QUERY_STREAMOVERFLOWPREDICATE_STREAM3, D3D10DDI_QUERY_MISCFLAG_PREDICATEHINT);
  assert(lastError == S_OK && made.size() == 1 && made[0]->type == PIPE_QUERY_SO_OVERFLOW_PREDICATE &&
         made[0]->index == 3 && q.Flags == D3D10DDI_QUERY_MISCFLAG_PREDICATEHINT);
  q = create(D3D10DDI_QUERY_STREAMOVERFLOWPREDICATE);
  assert(lastError == S_OK && made.size() == 1 && made[0]->index == 0);
  /* Flags stay predicate-only; unknown types stay refused. */
  q = create(D3D11DDI_QUERY_STREAMOUTPUTSTATS_STREAM1, D3D10DDI_QUERY_MISCFLAG_PREDICATEHINT);
  assert(lastError == E_NOTIMPL && made.empty());
  q = create((D3D10DDI_QUERY)0x1000);
  assert(lastError == E_NOTIMPL && made.empty());
  /* GPU timestamps: a TIMESTAMP is Mesa's query when the screen has them,
   * the CPU-counter event otherwise; TIMESTAMPDISJOINT stays an event. */
  q = create(D3D10DDI_QUERY_TIMESTAMP);
  assert(lastError == S_OK && made.empty() && !q.handle);
  screen.caps.query_timestamp = true;
  q = create(D3D10DDI_QUERY_TIMESTAMP);
  assert(lastError == S_OK && made.size() == 1 && made[0]->type == PIPE_QUERY_TIMESTAMP &&
         q.handle == made[0] && q.extra_count == 0);
  q = create(D3D10DDI_QUERY_TIMESTAMPDISJOINT);
  assert(lastError == S_OK && made.empty() && !q.handle);
  return 0;
}
'''


class D3d11QueryTypes(unittest.TestCase):
    def test_d3d11_query_types_create_asahi_queries(self):
        with tempfile.TemporaryDirectory(prefix="d3d11-query-") as tmp:
            out = project(Path(tmp) / "out")
            if out is None:
                self.skipTest("reference Mesa tree not available")
            text = (out / "src/gallium/frontends/d3d10umd/Query.cpp").read_text()
            body = function_body(text, "CreateQuery")
            src = Path(tmp) / "q.cpp"; exe = Path(tmp) / "q"
            src.write_text(PROGRAM.replace("@@BODY@@", body))
            subprocess.run([os.environ.get("CXX", "clang++"), "-std=c++17", "-Wall", "-Werror",
                            "-Wno-unused-function", "-Wno-unused-parameter", "-fsanitize=address,undefined",
                            str(src), "-o", str(exe)], check=True)
            ran = subprocess.run([str(exe)], capture_output=True, text=True, timeout=30)
            self.assertEqual(ran.returncode, 0, ran.stdout + ran.stderr)


if __name__ == "__main__":
    unittest.main()
