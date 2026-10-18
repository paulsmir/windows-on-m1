"""D3D-H7: the WDDM 2.0 D3D11 device table (agx_wddm2_ddi.cpp).

Invariant caught: AgxWddm2_0FillDeviceFuncs starts from the D3D11 table
(AgxD3d11FillDeviceFuncs over the same memory), so every WDDM 2.0 slot whose
function type differs from the D3D11 slot, and every slot the D3D11 table
does not have, must be assigned explicitly.  A slot left with its 11.0
pointer is called by the runtime with another signature (11.1 constant
buffer ranges, WDDM 2.0 view arguments, the 3-argument flush).  The slot
lists come from the pinned d3d10umddi.h (the header-minor-9+ layout the
build uses), not from this repository.
"""
from pathlib import Path
import re
import unittest

ROOT = Path(__file__).resolve().parents[1]
MODULE = ROOT / "drivers/apple-agx/mesa/d3d11/agx_wddm2_ddi.cpp"


def header():
    for base in (ROOT / ".local", ROOT.parents[1] / ".local"):
        path = base / "reference/wdk26100/d3d10umddi.h"
        if path.exists():
            return path.read_text(errors="replace")
    return None


def table(text, name, last=False):
    """Slots (name -> function type) of the last or first definition."""
    bodies = re.findall(r"typedef struct %s\s*\{(.*?)\}\s*%s;" % (name, name), text, re.S)
    body = bodies[-1] if last else bodies[0]
    return dict((m.group(2), m.group(1))
                for m in re.finditer(r"^\s*(PFND3D\w+)\s+(pfn\w+);", body, re.M))


def function_body(text, name):
    start = text.index("{", text.index("\n" + name + "("))
    depth, end = 1, start + 1
    while depth:
        depth += {"{": 1, "}": -1}.get(text[end], 0)
        end += 1
    return text[start:end]


class Wddm2DdiTable(unittest.TestCase):
    def test_every_retyped_or_new_slot_is_assigned(self):
        text = header()
        if text is None:
            self.skipTest("pinned WDK header not available")
        d3d11 = table(text, "D3D11DDI_DEVICEFUNCS")
        wddm2 = table(text, "D3DWDDM2_0DDI_DEVICEFUNCS", last=True)
        retyped = {n for n, t in wddm2.items() if n in d3d11 and d3d11[n] != t}
        added = {n for n in wddm2 if n not in d3d11}
        self.assertEqual(len(retyped), 34)
        self.assertEqual(len(added), 18)
        fill = function_body(MODULE.read_text(), "AgxWddm2_0FillDeviceFuncs")
        assigned = set(re.findall(r"f->(pfn\w+)\s*=", fill))
        self.assertEqual(sorted(retyped - assigned), [])
        self.assertEqual(sorted(added - assigned), [])

    def test_created_only_for_its_interface_with_dxgi_1_4(self):
        from test_d3d10_shader_fields_init import project
        import tempfile
        with tempfile.TemporaryDirectory(prefix="wddm2-ddi-") as tmp:
            out = project(Path(tmp) / "out")
            if out is None:
                self.skipTest("reference Mesa tree not available")
            device = (out / "src/gallium/frontends/d3d10umd/Device.cpp").read_text()
            create = function_body(device, "CreateDevice")
            self.assertIn("case D3DWDDM2_0_DDI_INTERFACE_VERSION:", create)
            wddm2 = create[create.index("if (pCreateData->Interface == D3DWDDM2_0_DDI_INTERFACE_VERSION) {"):]
            self.assertIn("AgxWddm2_0FillDeviceFuncs(pCreateData->pWDDM2_0DeviceFuncs, &base);", wddm2)
            self.assertIn("IS_DXGI1_4_BASE_FUNCTIONS(pCreateData->Interface, pCreateData->Version)", wddm2)
            self.assertIn("AgxWddm2_0FillDxgiFuncs(pCreateData->DXGIBaseDDI.pDXGIDDIBaseFunctions5);", wddm2)


if __name__ == "__main__":
    unittest.main()
