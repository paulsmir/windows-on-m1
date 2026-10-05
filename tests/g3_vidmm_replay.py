"""Compile selected unmodified G3 KMD functions with a small host WDK shim.

The function bodies come from the working tree at build time.  The shim owns
only allocation, memory views, WDK objects, and broker results.
"""

from pathlib import Path
import argparse
import os
import re
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[1]
SRC = ROOT / "drivers/apple-agx/render-admission/src"
SHARED = ROOT / "drivers/apple-agx/shared"
M1N1 = ROOT / "m1n1_windows/src"

FUNCTIONS = {
    "receipts.c": ["AdmissionRecordG3CopyQueryFailure", "AdmissionRecordG3CopyTransferFailure", "AdmissionRecordG3PrivateFailure"],
    "gpuva_g3_windows.c": [
        "AdmissionG3AllocateNode", "AdmissionG3FreeNode",
        "AdmissionGpuvaG3FindProcess", "AdmissionG3BootstrapRoot",
        "AdmissionGpuvaG3BrokerTable", "AdmissionGpuvaG3MirrorTable",
        "AdmissionG3Fnv", "AdmissionG3UscVa", "AdmissionG3TraceUpload", "AdmissionG3VerifyUploads",
        "AdmissionG3CopyPte", "AdmissionG3CaptureCopyQueryFailure", "AdmissionGpuvaG3CopyEscape",
        "AdmissionG3PreparePrivateStorageObserved","AdmissionG3PreparePrivateStorage","AdmissionG3PrivateFreeExtent","AdmissionG3PrivateMapExtentObserved","AdmissionG3PrivateMapExtent","AdmissionG3PrivateTables","AdmissionG3PrivateReleaseScene","AdmissionGpuvaG3PrivateCancel","AdmissionGpuvaG3PrivatePreempt","AdmissionG3PrivateReap","AdmissionGpuvaG3PrivateReset","AdmissionGpuvaG3PrivateReported","AdmissionGpuvaG3PrivateRetireContext","AdmissionG3PrivateDestroyStorage","AdmissionG3CapturePrivateFailure","AdmissionGpuvaG3PrivateEscape",
        "AdmissionDdiCreateProcess", "AdmissionDdiDestroyProcess",
        "AdmissionGpuvaG3AttachContext", "AdmissionGpuvaG3DetachContext",
        "AdmissionGpuvaG3ResolveTable", "AdmissionG3RecordSetRootSeen", "AdmissionDdiSetRootPageTable",
        "AdmissionGpuvaG3SubmitVirtualPaging",
        "AdmissionG4GraphAccess", "AdmissionG4LogicalEnvelopeAccess",
        "AdmissionG4GraphAccessTyped", "AdmissionG4FindPrivateScene","AdmissionG4FindPrivateResubmission","AdmissionG4PrivateGraphAccess","AdmissionG4PrivateGeometry","AdmissionG4PrivateUnqueue","AdmissionGpuvaG3PrivateContextBusy", "AdmissionGpuvaG3BeginJob",
        "AdmissionGpuvaG3CompleteJob",
    ],
    "gpuva_g3_paging_windows.c": [
        "AdmissionG3RejectPaging", "AdmissionG3RetireSystemSubtree",
        "AdmissionG3ActivateSystemSubtree", "AdmissionG3ResetTableShadow", "AdmissionG3RegisterTable", "AdmissionG3PrepareTableReuse", "AdmissionG3UpdateParent",
        "AdmissionG3UpdateLeaf", "AdmissionG3FindPagingEdge",
        "AdmissionG3ResolveLogicalVa", "AdmissionG3SnapshotAperture",
        "AdmissionG3EncodeVirtualPaging",
        "AdmissionG3MapPagingIpa", "AdmissionG3ExecuteVirtualPaging",
        "AdmissionGpuvaG3BuildPagingBuffer",
    ],
    "callbacks.c": ["AdmissionDdiEscape", "AdmissionDdiCreateContext", "AdmissionDdiDestroyContext"],
    "render_paging.c": ["AdmissionPagingRecordsValid"],
}


def body(source, name):
    match = re.search(r"(?m)^(?:_Use_decl_annotations_\s+)?(?:static\s+)?(?:const\s+)?(?:unsigned long long|[A-Za-z_][A-Za-z_0-9]*)(?:\s+|\s*\*+\s*)" + re.escape(name) + r"\s*\([^;]*?\)\s*\{", source, re.S)
    if match is None:
        raise ValueError(f"missing KMD function {name}")
    depth = 1
    pos = match.end()
    while depth:
        if source[pos] == "{":
            depth += 1
        elif source[pos] == "}":
            depth -= 1
        pos += 1
    start = source.rfind("\n", 0, match.start()) + 1
    # Preserve the return type and SAL annotation on the line before name.
    while start > 0 and source[start - 2:start - 1] not in ("}", ";"):
        previous = source.rfind("\n", 0, start - 1) + 1
        line = source[previous:start].strip()
        if not line or line.startswith("#"):
            break
        start = previous
    result = source[start:pos]
    if "{" not in result:
        raise ValueError(name)
    return result


def generate(revision=None, function_revisions=None):
    function_revisions = function_revisions or {}
    parts = ['#include "g3_vidmm_replay_shim.h"\n']
    if os.environ.get("G3_REPLAY_R144"):
        parts.append(body((SRC / "memory_runtime_windows.c").read_text(),
                          "AdmissionMemoryRuntimeScanoutView") + "\n")
    for filename, names in FUNCTIONS.items():
        source = ((SRC / filename).read_text() if revision is None or filename == "receipts.c" else
                  subprocess.check_output(
                      ["git", "show", f"{revision}:drivers/apple-agx/render-admission/src/{filename}"],
                      cwd=ROOT, text=True))
        if filename == "gpuva_g3_paging_windows.c":
            for include in re.findall(r'(?m)^#include "([^\"]+)"', source):
                if (SHARED / "include" / include).is_file():
                    parts.append(f'#include "{include}"\n')
            parts.append("enum { AdmissionG3PagingFailureTableAddress=1, AdmissionG3PagingFailureTableGraph=2, AdmissionG3PagingFailureParentFlags=3, AdmissionG3PagingFailureChildAddress=4, AdmissionG3PagingFailureChildGraph=5, AdmissionG3PagingFailureParentLink=6, AdmissionG3PagingFailureLeafGraph=7, AdmissionG3PagingTableInitialized=8, AdmissionG3PagingFailureTableMirror=9, AdmissionG3PagingFailureSubpage=10 };\n")
        for name in names:
            if filename == "gpuva_g3_windows.c" and name == "AdmissionGpuvaG3CopyEscape":
                marker = "typedef struct _ADMISSION_G3_COPY_PAGING_QUIESCENCE"
                if marker in source:
                    parts.append(source[source.index(marker):
                                        source.index("NTSTATUS AdmissionGpuvaG3CopyEscape", source.index(marker))])
            parts.append(f'#line 1 "{filename}:{name}"\n')
            function_source = source
            if revision is not None and name in (
                    "AdmissionG3PreparePrivateStorageObserved", "AdmissionG3PrivateMapExtentObserved", "AdmissionG3CapturePrivateFailure",
                    "AdmissionG3PreparePrivateStorage","AdmissionG3PrivateFreeExtent","AdmissionG3PrivateMapExtent","AdmissionG3PrivateTables","AdmissionG3PrivateReleaseScene","AdmissionGpuvaG3PrivateCancel","AdmissionGpuvaG3PrivatePreempt","AdmissionG3PrivateReap","AdmissionGpuvaG3PrivateReset","AdmissionGpuvaG3PrivateReported","AdmissionGpuvaG3PrivateRetireContext","AdmissionG3PrivateDestroyStorage","AdmissionGpuvaG3PrivateEscape", "AdmissionDdiEscape",
                    "AdmissionGpuvaG3BrokerTable",
                    "AdmissionG3RetireSystemSubtree", "AdmissionG3ActivateSystemSubtree",
                    "AdmissionG3ResetTableShadow", "AdmissionG3RegisterTable", "AdmissionG3PrepareTableReuse",
                    "AdmissionGpuvaG3MirrorTable",
                    "AdmissionG3Fnv", "AdmissionG3UscVa", "AdmissionG3TraceUpload", "AdmissionG3VerifyUploads",
                    "AdmissionG3CopyPte", "AdmissionG3CaptureCopyQueryFailure", "AdmissionGpuvaG3CopyEscape",
                    "AdmissionG3RecordSetRootSeen",
                    "AdmissionGpuvaG3SubmitVirtualPaging",
                    "AdmissionG4GraphAccess", "AdmissionG4LogicalEnvelopeAccess",
                    "AdmissionG4GraphAccessTyped", "AdmissionG4FindPrivateScene","AdmissionG4FindPrivateResubmission","AdmissionG4PrivateGraphAccess","AdmissionG4PrivateGeometry","AdmissionG4PrivateUnqueue","AdmissionGpuvaG3PrivateContextBusy", "AdmissionGpuvaG3BeginJob",
                    "AdmissionGpuvaG3CompleteJob",
                    "AdmissionG3FindPagingEdge",
                    "AdmissionG3ResolveLogicalVa",
                    "AdmissionG3SnapshotAperture",
                    "AdmissionG3EncodeVirtualPaging",
                    "AdmissionG3MapPagingIpa",
                    "AdmissionG3ExecuteVirtualPaging"):
                function_source = (SRC / filename).read_text()
            if name in function_revisions:
                function_source = subprocess.check_output(
                    ["git", "show", f"{function_revisions[name]}:drivers/apple-agx/render-admission/src/{filename}"],
                    cwd=ROOT, text=True)
            parts.append(body(function_source, name) + "\n")
    platform = (M1N1 / "hv_agx_retained_platform.c").read_text()
    parts.append('#line 1 "hv_agx_retained_platform.c:gpuva_execute"\n')
    parts.append(body(platform, "translate_guest") + "\n")
    parts.append(body(platform, "gpuva_execute") + "\n")
    parts.append("#if defined(G3_PRIVATE_COMBINED)\n")
    completion = (SHARED / "src/apple_agx_submission.c").read_text()
    parts.append("#define APPLE_AGX_SUBMISSION_NULL ((void *)0)\n")
    parts.append(body(completion,"AppleAgxCompletionTransitionValid"))
    for name in ("Initialize", "Begin", "Matches", "Advance", "CanReport", "MarkReported", "Finish"):
        parts.append(body(completion,"AppleAgxCompletionTransaction"+name))
    backend = (SRC / "backend_platform_windows.c").read_text()
    for name in ("AdmissionNotifyCompletionAtInterrupt", "AdmissionBackendComplete"):
        parts.append(body(backend,name))
    parts.append("#endif\n")
    if revision is None and "AdmissionGpuvaG3CopyEscape" not in function_revisions:
        parts.append("#define APPLE_AGX_LATE_COPY_RETRY 1\n")
    if os.environ.get("G3_REPLAY_QUERY_V2") or os.environ.get("G3_REPLAY_R147"):
        scenarios = (ROOT / "tests/g3_vidmm_replay_scenarios.c").read_text()
        system = (ROOT / "tests/g3_system_lifetime_cases.c").read_text()
        bind = "  assert(AppleAgxGpuvaG3GraphBindRoot(&p->Graph,s->BrokerIpa));"
        assert system.count(bind) == 1
        system = system.replace(bind, "  /* QUERY v2: leave OS root unselected. */")
        copy = (ROOT / "tests/g3_r145_copy_cases.c").read_text()
        marker = "  escape.pPrivateDriverData=q;escape.PrivateDriverDataSize=sizeof(*q);"
        assert copy.count(marker) == 1
        copy = copy.replace(marker, marker + "\n" +
            (ROOT / ("tests/g3_r147_root_cases.c" if os.environ.get("G3_REPLAY_R147") else
                     "tests/g3_copy_query_v2_cases.c")).read_text())
        scenarios = scenarios.replace('#include "g3_system_lifetime_cases.c"', system)
        parts.append(scenarios.replace('#include "g3_r145_copy_cases.c"', copy))
    else:
        parts.append('#include "g3_vidmm_replay_scenarios.c"\n')
    return "\n".join(parts)


def main(revision=None, function_revisions=None, old_context_flags=False):
    with tempfile.TemporaryDirectory(prefix="g3-vidmm-") as directory:
        source = Path(directory) / "replay.c"
        binary = Path(directory) / "replay"
        source.write_text(generate(revision, function_revisions))
        command = [os.environ.get("CC", "clang"), "-std=gnu11", "-O0", "-g", "-Wno-unused-function",
                   "-Wno-multichar", "-I", str(Path(__file__).parent),
                   "-I", str(M1N1),
                   "-I", str(SHARED / "include"), str(source),
                   str(SHARED / "src/apple_agx_gpuva_g3_translation.c"),
                   str(SHARED / "src/apple_agx_g4_submit.c"),
                   str(SHARED / "src/apple_agx_gpuva_g3_graph.c"),
                   str(SHARED / "src/apple_agx_gpuva_broker_v5_client.c"),
                   str(M1N1 / "hv_agx_gpuva_v5.c"),
                   str(M1N1 / "hv_agx_gpuva_v5_mmio.c"),
                   str(M1N1 / "hv_agx_retained_backing.c"),
                   str(M1N1 / "hv_agx_retained_tables.c"),
                   "-o", str(binary)]
        if os.environ.get("G3_REPLAY_R137_COMBINED"):
            mesa = (ROOT / "drivers/apple-agx/mesa/winsys/agx_win32_gpuva_batch.c").read_text()
            (Path(directory) / "g3_r137_mesa_prepare.inc").write_text("\n".join(
                body(mesa, name) for name in ("append_native", "append_attachments",
                                              "prepare_process_buffers")))
            command[1:1] = ["-DG3_PRIVATE_COMBINED=1", "-ftrivial-auto-var-init=pattern", "-I", directory]
            command += [str(SHARED / "src" / n) for n in (
                "apple_agx_g4_builder.c", "apple_agx_render_template.generated.c",
                "apple_agx_render_template_rebase.c", "apple_agx_render_template_vm_slot.c",
                "apple_agx_relocation.c", "apple_agx_exp208_adapter.c")]
        if os.environ.get("G3_REPLAY_R132") or os.environ.get("G3_REPLAY_R133"):
            command[1:1] = ["-fsanitize=address,undefined"]
        if any(os.environ.get(k) for k in ("G3_REPLAY_R134", "G3_REPLAY_R135", "G3_REPLAY_R137", "G3_REPLAY_R145")):
            command[1:1] = ["-DADMISSION_GPUVA_G1B_PAGE_PROFILE=" + os.environ.get("G3_REPLAY_PROFILE", "16"),
                            "-fsanitize=address,undefined"]
        if os.environ.get("G3_REPLAY_R144"):
            command[1:1] = ["-DG3_REPLAY_FULL_LOCAL=1",
                "-DADMISSION_GPUVA_G1B_PAGE_PROFILE=" + os.environ.get("G3_REPLAY_PROFILE", "16"),
                "-fsanitize=address,undefined"]
        if old_context_flags:
            command.insert(1, "-DADMISSION_CONTEXT_VALID_FLAGS=3")
        if revision is None and "AdmissionDdiCreateContext" not in (function_revisions or {}):
            command.insert(1, "-DG3_REPLAY_CONTEXT_SEGMENT_CHECK")
        subprocess.run(command, check=True, cwd=ROOT)
        env = dict(os.environ)
        if revision is not None or function_revisions or old_context_flags:
            env["G3_REPLAY_HISTORICAL"] = "1"
        subprocess.run([str(binary)], check=True, cwd=ROOT, env=env)


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--revision", help="KMD revision for historical RED replay")
    parser.add_argument("--function-revision", action="append", default=[],
                        metavar="NAME=REV", help="replace one function body from a historical revision")
    parser.add_argument("--old-context-flags", action="store_true",
                        help="use pre-e4aacfd0 render_objects.h flag mask")
    args = parser.parse_args()
    overrides = dict(entry.split("=", 1) for entry in args.function_revision)
    sys.exit(main(args.revision, overrides, args.old_context_flags))
