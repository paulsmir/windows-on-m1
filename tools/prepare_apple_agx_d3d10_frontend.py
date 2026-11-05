"""Derive the selected Windows-owned frontend without modifying pinned Mesa."""
import argparse
import hashlib
import json
from pathlib import Path
import shutil
import subprocess

INPUTS = {
    "Adapter.cpp": "e6a8e473d3574ce46970a045bf21136ce30eb206ac97d0cea34028427688f70b",
    "Device.cpp": "dcf950aec993d40743671e1f208655e151158a9b4647bc3581dcd134962086aa",
    "State.h": "4280c406ca8c1c199d09a0d062f8b52fb0baaec43a2ca7482e0d1a3acc7c4dd3",
}


def replace_once(text, old, new):
    if text.count(old) != 1:
        raise ValueError("pinned frontend anchor mismatch: " + old[:70])
    return text.replace(old, new, 1)


def transform(name, text):
    if name == "State.h":
        text = replace_once(text, '#include "DriverIncludes.h"',
                            '#include "DriverIncludes.h"\n#include "agx_d3d10_windows.h"')
        text = replace_once(text, "   struct pipe_screen *screen;",
                            "   AGX_D3D10_WINDOWS_ADAPTER *windows;")
        text = replace_once(text, "   struct pipe_context *pipe;",
                            "   struct pipe_context *pipe;\n   AGX_D3D10_WINDOWS_DEVICE *windows;")
    elif name == "Adapter.cpp":
        text = replace_once(text, "   pAdaptor->screen = d3d10_create_screen();\n   if (!pAdaptor->screen) {\n      free(pAdaptor);\n      --numAdapters;\n      return E_OUTOFMEMORY;\n   }",
            "   HRESULT result = AgxD3d10WindowsOpenAdapter(pOpenData, &pAdaptor->windows);\n   if (FAILED(result)) {\n      free(pAdaptor);\n      --numAdapters;\n      return result;\n   }")
        text = replace_once(text, "   struct pipe_screen *screen = pAdapter->screen;\n   screen->destroy(screen);",
            "   HRESULT result = AgxD3d10WindowsCloseAdapter(&pAdapter->windows);\n   if (FAILED(result)) return result;")
        text = text.replace("   D3D10_0_x_DDI_SUPPORTED,\n", "").replace("   D3D10_0_7_DDI_SUPPORTED,\n", "")
    elif name == "Device.cpp":
        text = replace_once(text, "   struct pipe_screen *screen = pAdapter->screen;\n   struct pipe_context *pipe = screen->context_create(screen, NULL, 0);\n   pDevice->pipe = pipe;\n   pDevice->cso = cso_create_context(pipe, CSO_NO_VBUF);", '''   HRESULT result = AgxD3d10WindowsCreateDevice(pAdapter->windows, pCreateData,
                                                &pDevice->windows);
   if (FAILED(result)) return result;
   struct pipe_context *pipe = AgxD3d10WindowsContext(pDevice->windows);
   struct pipe_screen *screen = pipe->screen;
   pDevice->pipe = pipe;
   // Startup safety, not a capability proof. GetCaps stays zero until the
   // complete selected feature-level/backend contract is implemented.
   if (!pipe->create_vs_state || !pipe->create_fs_state ||
       !pipe->bind_vs_state || !pipe->bind_fs_state ||
       !pipe->delete_vs_state || !pipe->delete_fs_state || !pipe->flush) {
      AgxD3d10WindowsCloseDevice(&pDevice->windows);
      pDevice->pipe = NULL;
      return E_NOTIMPL;
   }
   pDevice->cso = cso_create_context(pipe, CSO_NO_VBUF);
   if (!pDevice->cso) {
      AgxD3d10WindowsCloseDevice(&pDevice->windows);
      pDevice->pipe = NULL;
      return E_OUTOFMEMORY;
   }''')
        text = replace_once(text, "   pipe->destroy(pipe);", '''   HRESULT result = AgxD3d10WindowsCloseDevice(&pDevice->windows);
   if (FAILED(result)) SetError(hDevice, result);
   else pDevice->pipe = NULL;''')
        text = replace_once(text,
            "   pDevice->empty_fs = CreateEmptyShader(pDevice, MESA_SHADER_FRAGMENT);",
            '''   pDevice->empty_fs = CreateEmptyShader(pDevice, MESA_SHADER_FRAGMENT);
   if (!pDevice->empty_vs || !pDevice->empty_fs) {
      if (pDevice->empty_fs) DeleteEmptyShader(pDevice, MESA_SHADER_FRAGMENT, pDevice->empty_fs);
      if (pDevice->empty_vs) DeleteEmptyShader(pDevice, MESA_SHADER_VERTEX, pDevice->empty_vs);
      cso_destroy_context(pDevice->cso);
      pDevice->cso = NULL;
      AgxD3d10WindowsCloseDevice(&pDevice->windows);
      pDevice->pipe = NULL;
      return E_FAIL;
   }''')
    return text


def prepare(source, output):
    frontend = source / "src/gallium/frontends/d3d10umd"
    if output.resolve().is_relative_to(source.resolve()):
        raise ValueError("derived output must not modify the pinned source tree")
    if output.exists():
        raise ValueError("output already exists; preserve previous derived source")
    revision = subprocess.check_output(["git", "-C", str(source), "rev-parse", "HEAD"], text=True).strip()
    if revision != "9aa1215f878b504f66159dd2ead4c7973142126e":
        raise ValueError("pinned Mesa commit mismatch")
    dirty = subprocess.check_output(["git", "-C", str(source), "status", "--porcelain", "--", "src/gallium/frontends/d3d10umd"], text=True)
    if dirty:
        raise ValueError("pinned frontend is dirty")
    replacements = {}
    for name, expected in INPUTS.items():
        raw = (frontend / name).read_bytes()
        if hashlib.sha256(raw).hexdigest() != expected:
            raise ValueError("pinned source hash mismatch: " + name)
        replacements[name] = transform(name, raw.decode())
    # Preserve all upstream copyright/license notices in the derived tree.
    manifest = {"mesa_commit": "9aa1215f878b504f66159dd2ead4c7973142126e",
                "inputs": {p.name: hashlib.sha256(p.read_bytes()).hexdigest()
                           for p in frontend.iterdir() if p.is_file()},
                "excluded_link_units": ["D3DKMT.cpp", "d3d10_gdi.c"],
                "outputs": {}}
    shutil.copytree(frontend, output)
    for name, text in replacements.items():
        (output / name).write_text(text, encoding="utf-8", newline="\n")
        manifest["outputs"][name] = hashlib.sha256((output / name).read_bytes()).hexdigest()
    (output / "windows-owner-manifest.json").write_text(json.dumps(manifest, indent=2) + "\n")
    return manifest


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--source", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    print(json.dumps(prepare(args.source, args.output), indent=2))
