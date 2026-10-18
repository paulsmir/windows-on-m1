"""CS 1.6 ICD phase 1: the D3DKMT bridge must supply every runtime callback
the GPUVA umd_* device initialisation requires.

AdmissionUmdRuntimeDeviceInitialize (umd_runtime_device.c) refuses a device
unless each listed pKTCallbacks entry is non-null; an ICD has no D3D runtime,
so agx_kmt_gpuva_bridge.c must install each of them (and the adapter query
and SetError callbacks). The Windows-side translation itself is checked by
agx_kmt_gpuva_bridge_test.c on x64 and x86 (build-icd-bridge-test.ps1).
"""
from pathlib import Path
import re
import unittest

ROOT = Path(__file__).resolve().parents[1]
RUNTIME = ROOT / "drivers/apple-agx/render-admission/umd/src/umd_runtime_device.c"
BRIDGE = ROOT / "drivers/apple-agx/windows/icd/agx_kmt_gpuva_bridge.c"


def required_gpuva_callbacks(text):
    start = text.index("HRESULT AdmissionUmdRuntimeDeviceInitialize(")
    check = text[start:text.index("return E_INVALIDARG;", start)]
    # Drop the non-GPUVA (#else / #ifndef) branches of the check.
    kept, skip = [], False
    for line in check.splitlines():
        s = line.strip()
        if s.startswith("#ifdef APPLE_AGX_GPUVA_WINSYS"):
            skip = False
        elif s.startswith("#ifndef APPLE_AGX_GPUVA_WINSYS") or s.startswith("#else"):
            skip = True
        elif s.startswith("#endif"):
            skip = False
        elif not skip:
            kept.append(line)
    return set(re.findall(r"pKTCallbacks->(pfn\w+)\s*==\s*NULL", "\n".join(kept)))


class IcdKmtBridgeCoverageTests(unittest.TestCase):
    def test_bridge_installs_every_required_gpuva_callback(self):
        required = required_gpuva_callbacks(RUNTIME.read_text())
        self.assertIn("pfnSubmitCommandCb", required)
        self.assertNotIn("pfnRenderCb", required)
        installed = set(re.findall(r"\bd->(pfn\w+)\s*=", BRIDGE.read_text()))
        self.assertEqual(sorted(required - installed), [])

    def test_adapter_query_and_error_callbacks(self):
        bridge = BRIDGE.read_text()
        self.assertRegex(bridge, r"AdapterCallbacks\.pfnQueryAdapterInfoCb\s*=")
        self.assertRegex(bridge, r"CoreCallbacks\.pfnSetErrorCb\s*=")
        self.assertIn("D3DKMT_CLIENTHINT_OPENGL", bridge)


if __name__ == "__main__":
    unittest.main()
