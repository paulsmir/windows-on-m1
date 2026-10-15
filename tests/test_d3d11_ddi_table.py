"""D3D-H2a: the D3D11 DDI device function table.

Invariants caught:
- every member of D3D11DDI_DEVICEFUNCS (Windows SDK d3d10umddi.h) is set by
  AgxD3d11FillDeviceFuncs except the deferred-context and command-list
  entries (THREADING caps report none, so the runtime emulates them) and
  the two entries Microsoft reserves for system use: a NULL entry the
  runtime calls crashes the calling process (DWM included);
- no entry is assigned twice (a later assignment would silently replace an
  adapter with a 10.0 function of another signature);
- the projected adapter advertises D3D11_0 only under AgxD3d11DdiEnabled()
  and the projected CreateDevice fills the D3D11 table for that interface.
The header comes from the local SDK reference copy; the member check is
skipped where it is absent.
"""
import os
from pathlib import Path
import re
import tempfile
import unittest

from test_d3d10_shader_fields_init import function_body, local_tree, project

ROOT = Path(__file__).resolve().parents[1]
MODULE = ROOT / "drivers/apple-agx/mesa/d3d11/agx_d3d11_ddi.cpp"
LEFT_NULL = {
    "pfnCheckDeferredContextHandleSizes", "pfnCalcDeferredContextHandleSize",
    "pfnCalcPrivateDeferredContextSize", "pfnCreateDeferredContext", "pfnAbandonCommandList",
    "pfnCalcPrivateCommandListSize", "pfnCreateCommandList", "pfnDestroyCommandList",
    "pfnCommandListExecute", "pfnRecycleCommandList", "pfnRecycleCreateCommandList",
    "pfnRecycleCreateDeferredContext", "pfnRecycleDestroyCommandList",
    # reserved for system use
    "pfnResetPrimitiveID", "pfnSetVertexPipelineOutput",
}


def struct_members(header, name):
    text = header.read_text(errors="replace")
    start = text.index("typedef struct " + name)
    body = text[text.index("{", start):text.index("} " + name + ";", start)]
    return re.findall(r"\b(pfn\w+)\s*;", body)


class D3d11DdiTable(unittest.TestCase):
    def test_every_entry_is_filled_once(self):
        header = local_tree("reference", "wdk26100", "d3d10umddi.h")
        if header is None:
            self.skipTest("Windows SDK reference header not available")
        members = struct_members(header, "D3D11DDI_DEVICEFUNCS")
        self.assertGreater(len(members), 140)
        fill = MODULE.read_text()
        fill = fill[fill.index("AgxD3d11FillDeviceFuncs(D3D11DDI_DEVICEFUNCS *f,"):]
        assigned = re.findall(r"\bf->(pfn\w+)\s*=", fill)
        self.assertEqual(len(assigned), len(set(assigned)), "an entry is assigned twice")
        for member in members:
            with self.subTest(member=member):
                if member in LEFT_NULL:
                    self.assertNotIn(member, assigned)
                else:
                    self.assertIn(member, assigned)
        self.assertFalse(set(assigned) - set(members), "assigned names outside the table")
        # the D3D10-typed entries come from the filled 10.0 table
        base = set(re.findall(r"\bbase->(pfn\w+)", fill))
        self.assertTrue(base)
        self.assertFalse(base - set(struct_members(header, "D3D10DDI_DEVICEFUNCS")),
                         "base entries outside the D3D10 table")

    def test_interface_is_advertised_and_filled_under_the_switch(self):
        with tempfile.TemporaryDirectory(prefix="d3d11-ddi-") as tmp:
            out = project(Path(tmp) / "out")
            if out is None:
                self.skipTest("reference Mesa tree not available")
            frontend = out / "src/gallium/frontends/d3d10umd"
            adapter = (frontend / "Adapter.cpp").read_text()
            device = (frontend / "Device.cpp").read_text()
            versions = adapter[adapter.index("SupportedDDIInterfaceVersions[] = {"):]
            versions = versions[:versions.index("};")]
            self.assertTrue(versions.rstrip().rstrip(",").endswith("D3D11_0_DDI_SUPPORTED"))
            supported = function_body(adapter, "GetSupportedVersions")
            self.assertIn("AgxD3d11DdiEnabled() ? 0u : 1u", supported)
            create = function_body(device, "CreateDevice")
            self.assertIn("case D3D11_0_DDI_INTERFACE_VERSION:", create)
            self.assertIn("const D3D10DDI_DEVICEFUNCS base = *pDeviceFuncs;", create)
            self.assertIn("AgxD3d11FillDeviceFuncs(pCreateData->p11DeviceFuncs, &base);", create)


if __name__ == "__main__":
    unittest.main()
