"""Original actual-DDI source renderer; reads/writes data, never invokes tools."""
from pathlib import Path
import hashlib
import json
import re

ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / 'drivers/apple-agx/d3d12/agx_d3d12_ddi.cpp'
FIXTURE = ROOT / 'tests/d3d12_failed_table_publication_fixture.cpp'
WDK_SHA256 = 'd338e00bbd079cfed42434b5bd8cb9c658238cfe22747fb7d3b7cff3a3cabc28'
TABLES = ('D3D12DDI_COMMAND_LIST_FUNCS_3D_0003', 'D3D12DDI_COMMAND_QUEUE_FUNCS_CORE_0001')


def sha(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def body(text, marker, last=False):
    """Bounded unique current-source marker; no claim to parse arbitrary C++."""
    at = text.rindex(marker) if last else text.index(marker)
    opening = text.index('{', at)
    depth, end = 1, opening + 1
    while depth:
        depth += (text[end] == '{') - (text[end] == '}')
        end += 1
    return text[at:end]


def slices(text):
    return {
        'FillTable': body(text, 'template<class T> static HRESULT FillTable('),
        'FillDDITable': body(text, 'static HRESULT APIENTRY FillDDITable(', last=True),
        **{name: body(text, 'static void Fill' + name + '(') for name in TABLES},
    }


def render(out, wdk):
    """Extract CURRENT repository functions only; no candidate/reference fallback."""
    out, wdk = Path(out), Path(wdk)
    if sha(wdk) != WDK_SHA256:
        raise RuntimeError('pinned actual WDK changed')
    text = SOURCE.read_text()
    if text.count('template<class T> static HRESULT FillTable(') != 1:
        raise RuntimeError('ambiguous actual FillTable')
    if text.count('static HRESULT APIENTRY FillDDITable(') != 2:
        raise RuntimeError('expected one actual declaration and one definition')
    extracted = slices(text)
    header = wdk.read_text(encoding='utf-8-sig')
    aliases, controls, ident = {}, [], 1
    for table in TABLES:
        record = re.search(r'typedef struct ' + table + r'\s*\{(.*?)\}\s*' + table + r'\s*;', header, re.S)
        if not record:
            raise RuntimeError('actual WDK table missing: ' + table)
        members = dict((member, kind) for kind, member in
                       re.findall(r'(\w+\*?)\s+(pfn\w+)\s*;', record[1]))
        if len(members) != (51 if table == TABLES[0] else 7):
            raise RuntimeError('pinned table member extent changed')
        if text.count('static void Fill' + table + '(') != 1:
            raise RuntimeError('ambiguous actual typed filler')
        assignments = re.findall(r'AGX_ASSIGN\(' + table + r',\s*(pfn\w+)\s*,\s*([^;]+)\);', extracted[table])
        for member, expression in assignments:
            expression = expression.strip()
            if re.fullmatch(r'\w+', expression):
                if expression not in aliases:
                    aliases[expression] = ('decltype(((' + table + '*)0)->' + member + ')', ident)
                    ident += 1
            elif not expression.startswith(('AGX_REJECT(', 'AGX_LIST_STATE(')):
                raise RuntimeError('unreviewed callback expression: ' + expression)
            controls.append((table, member, expression))
    if len(controls) != 56:
        raise RuntimeError('56 actual nonreserved typed slots required')
    prefix = PREFIX
    for name, (signature, identity) in aliases.items():
        prefix += '#define ' + name + ' LinkShim<' + signature + ',' + str(identity) + 'u>::call\n'
    prefix += 'static void FillD3D12DDI_DEVICE_FUNCS_CORE_0003(D3D12DDI_DEVICE_FUNCS_CORE_0003&){++core_invocations;}\n'
    assertions = []
    for table, member, expected in controls:
        local = 'l' if table == TABLES[0] else 'q'
        assertions.append('  check(' + local + '.' + member + ' == &' + expected + ',"typed ' + table + '.' + member + '");')
    oracle = FIXTURE.read_text()
    if oracle.count('/* ACTUAL_SLOT_CONTROLS */') != 1:
        raise RuntimeError('original oracle anchor changed')
    oracle = oracle.replace('/* ACTUAL_SLOT_CONTROLS */', '\n'.join(assertions))
    generated = prefix + '\n'.join(extracted[t] for t in TABLES) + '\n' + extracted['FillTable'] + '\n' + extracted['FillDDITable'] + '\n' + oracle + '\n} // namespace\nint main(){return run();}\n'
    out.mkdir(exist_ok=False)
    unit = out / 'actual-table-publication.cpp'
    unit.write_text(generated)
    metadata = {
        'scope': 'Actual current repository Fill functions; noninvoked typed callback bodies only',
        'source': str(SOURCE.resolve()), 'source_sha256': sha(SOURCE),
        'fixture': str(FIXTURE.resolve()), 'fixture_sha256': sha(FIXTURE),
        'wdk': str(wdk.resolve()), 'wdk_sha256': sha(wdk),
        'source_slices': {name: hashlib.sha256(value.encode()).hexdigest() for name, value in extracted.items()},
        'generated': str(unit.resolve()), 'generated_sha256': sha(unit),
        'slot_controls': 56, 'expected_checks': 137,
        'no_old_candidate_fallback': True, 'no_Create_private_record_model': True,
    }
    (out / 'generated-inputs.json').write_text(json.dumps(metadata, indent=2) + '\n')
    return metadata


PREFIX = '''/* Original actual-function CPU regression. NO GPU; real Windows SDK/WDK. */
#include <windows.h>
typedef _Return_type_success_(return >= 0) LONG NTSTATUS;
#pragma warning(push)
#pragma warning(disable:4201)
#include <d3d12umddi.h>
#pragma warning(pop)
#include <cstdio>
#include <cstring>
#include <type_traits>
#if !defined(_M_X64)
#error This isolated CPU test is the builder-native x64 gate, not ARM64 execution.
#endif
namespace {
static D3D12DDI_HRTTABLE list_table;
static unsigned shim_invocations,core_invocations;
static VOID AdmissionUmdDiagnostic(PCSTR,HRESULT,const UINT*,UINT){}
template<class Signature,unsigned Identity>struct LinkShim;
template<class Return,class...Args,unsigned Identity>
struct LinkShim<Return(APIENTRY*)(Args...),Identity>{
 static Return APIENTRY call(Args...){shim_invocations+=Identity+1u;if constexpr(!std::is_void_v<Return>)return Return{};}
};
#define AGX_ASSIGN(T,M,F) (f).M = &F
#define AGX_REJECT(T,M) LinkShim<decltype(((T*)0)->M),10000u+static_cast<unsigned>(offsetof(T,M))>::call
#define AGX_LIST_STATE(M) LinkShim<decltype(((D3D12DDI_COMMAND_LIST_FUNCS_3D_0003*)0)->M),20000u+static_cast<unsigned>(offsetof(D3D12DDI_COMMAND_LIST_FUNCS_3D_0003,M))>::call
'''
