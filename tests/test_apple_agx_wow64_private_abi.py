"""EXP1225: every UMD-to-KMD record has one layout for x86 and ARM64.

WOW64 thunks the D3DDDICB_*/D3DKMT_* structures but not the private driver
data, allocation private data, escape payloads and command buffers they point
to (Microsoft, "Windows Vista Display Driver Model 64-bit issues"). Once the
adapter is visible to 32-bit processes, AppleAgxRenderAdmissionUmd32.dll and
the 32-bit OpenGL ICD hand the ARM64 KMD bytes laid out by an x86 compiler,
so a pointer, SIZE_T, HANDLE or ULONG_PTR member in a record that crosses
that boundary would be misread silently. This test compiles the headers that
define those records for i686-pc-windows-msvc and aarch64-pc-windows-msvc
and requires identical offsets, member types and sizes for every record.

The only records allowed to differ are the in-process views that a parser
builds over a buffer it already holds (pointers into that buffer); they are
never copied into private data or a command buffer.
"""
from pathlib import Path
import os
import re
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
AGX = ROOT / "drivers/apple-agx"
INCLUDES = [AGX / "shared/include", AGX / "render-admission/include",
            AGX / "render-admission/umd/include"]
WIRE_HEADERS = [
    "shared/include/apple_agx_state.h",
    "shared/include/apple_agx_win32_abi.h",
    "shared/include/apple_agx_win32_device_info.h",
    "render-admission/include/render_allocation.h",
    "render-admission/include/render_win32_transport.h",
    "shared/include/apple_agx_g3_copy_abi.h",
    "shared/include/apple_agx_g4_submit.h",
    "shared/include/apple_agx_g3_private_abi.h",
    "render-admission/umd/include/direct_flip_contract.h",
    "render-admission/include/render_qualification.h",
]
PARSE_VIEWS = {
    # agx_win32_transport.c / render_win32_transport.c parse a command buffer.
    "struct _APPLE_AGX_WIN32_COMMAND_VIEW",
    # The KMD's snapshot of one parsed render submission (holds a view).
    "struct _ADMISSION_WIN32_RENDER_SNAPSHOT",
    # gpuva_g3_windows.c parses G4 submit private data into this view.
    "APPLE_AGX_G4_SUBMIT_VIEW",
}
TARGETS = ("i686-pc-windows-msvc", "aarch64-pc-windows-msvc")


def record_names(text):
    names = []
    for match in re.finditer(r"typedef\s+(?:struct|union)\s+\w*\s*\{", text):
        index, depth = match.end(), 1
        while depth and index < len(text):
            depth += {"{": 1, "}": -1}.get(text[index], 0)
            index += 1
        name = re.match(r"\s*(\w+)", text[index:])
        if name:
            names.append(name.group(1))
    return names


def layouts(dump):
    records = {}
    for block in dump.split("*** Dumping AST Record Layout")[1:]:
        lines = [line for line in block.splitlines() if line.strip()]
        head = re.sub(r"\s+", " ", lines[0].split("|", 1)[1].strip())
        head = re.sub(r"\(unnamed at [^)]*?([\w.]+:\d+:\d+)\)", r"(unnamed at \1)", head)
        body = []
        for line in lines[1:]:
            body.append(re.sub(r"\s+", " ", line.strip()))
            if "[sizeof=" in line:
                break
        records.setdefault(head, body)
    return records


class Wow64PrivateAbiTest(unittest.TestCase):
    def test_wire_records_match_between_x86_and_arm64(self):
        names = []
        source = []
        for header in WIRE_HEADERS:
            text = (AGX / header).read_text()
            source.append('#include "%s"' % Path(header).name)
            names += record_names(text)
        names = list(dict.fromkeys(names))
        source += ["char abi_probe_%d[sizeof(%s)];" % item for item in enumerate(names)]
        dumps = {}
        with tempfile.TemporaryDirectory() as tmp:
            probe = Path(tmp) / "abi_probe.c"
            probe.write_text("\n".join(source) + "\n")
            for target in TARGETS:
                command = [os.environ.get("CC", "clang"), "-target", target, "-fsyntax-only",
                           "-ffreestanding", "-w", "-Xclang", "-fdump-record-layouts"]
                command += ["-I%s" % path for path in INCLUDES] + [str(probe)]
                result = subprocess.run(command, capture_output=True, text=True)
                self.assertEqual(result.returncode, 0, result.stdout[-2000:] + result.stderr[-2000:])
                dumps[target] = layouts(result.stdout)
        x86, arm64 = (dumps[target] for target in TARGETS)
        ours = set(names)
        for header in WIRE_HEADERS:
            ours.update(re.findall(r"(?:struct|union)\s+(\w+)\s*\{", (AGX / header).read_text()))
        compared = [head for head in x86 if head.split(" ")[-1] in ours
                    or ("unnamed at" in head and re.search(r"apple_agx|render_|direct_flip", head))]
        self.assertGreaterEqual(len(compared), len(names))
        differing = sorted(head for head in compared if x86[head] != arm64.get(head))
        self.assertEqual(differing, sorted(PARSE_VIEWS))

    def test_parse_views_never_cross_the_boundary(self):
        """A view must not be the payload of a private-data or command copy."""
        sources = list((AGX / "render-admission/umd/src").glob("*.c")) + \
            list((AGX / "mesa/winsys").glob("*.c")) + list((AGX / "windows/icd").glob("*.c*"))
        for path in sources:
            text = path.read_text(errors="replace")
            for view in ("APPLE_AGX_WIN32_COMMAND_VIEW", "ADMISSION_WIN32_RENDER_SNAPSHOT",
                         "APPLE_AGX_G4_SUBMIT_VIEW"):
                self.assertNotRegex(text, r"PrivateDriverDataSize\s*=\s*sizeof\(\s*%s\s*\)" % view,
                                    path.name)
                self.assertNotRegex(text, r"PrivateDriverDataSize\s*=\s*sizeof\(\s*\*?\s*view\s*\)",
                                    path.name)


if __name__ == "__main__":
    unittest.main()
