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
  and the projected CreateDevice fills the D3D11 table for that interface;
- D3D-H2b: compute on 10.x hardware is reported only under its switch and
  only while every entry the runtime calls for it is implemented (a
  fail-closed entry behind an advertised capability fails the application's
  CreateComputeShader/Dispatch), and the projected frontend accepts the
  compute stage in its setters, deletes compute states and admits
  unordered-access, raw-view and structured buffers.
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

    def test_compute_is_reported_only_with_its_entries_implemented(self):
        fill = MODULE.read_text()
        enabled = function_body(fill, "AgxD3d11ComputeEnabled")
        self.assertIn("AgxD3d11DdiEnabled()", enabled)
        self.assertIn('"APPLE_AGX_D3D11_COMPUTE"', enabled)
        body = fill[fill.index("AgxD3d11FillDeviceFuncs(D3D11DDI_DEVICEFUNCS *f,"):]
        assigned = dict(re.findall(r"\bf->(pfn\w+)\s*=\s*(\w+);", body))
        for entry in ("pfnCreateComputeShader", "pfnCsSetShader", "pfnCsSetShaderWithIfaces",
                      "pfnCsSetShaderResources", "pfnCsSetSamplers", "pfnCsSetConstantBuffers",
                      "pfnCalcPrivateUnorderedAccessViewSize", "pfnCreateUnorderedAccessView",
                      "pfnDestroyUnorderedAccessView", "pfnCsSetUnorderedAccessViews",
                      "pfnClearUnorderedAccessViewUint", "pfnDispatch"):
            with self.subTest(entry=entry):
                self.assertFalse(assigned[entry].endswith("None"), assigned[entry])
        create = function_body(fill, "CreateComputeShader11")
        self.assertIn("memset(shader, 0, sizeof(*shader));", create)
        self.assertLess(create.index("memset"), create.index("create_compute_state"))
        dispatch = function_body(fill, "Dispatch11")
        self.assertLess(dispatch.index("launch_grid"), dispatch.index("memory_barrier"))
        with tempfile.TemporaryDirectory(prefix="d3d11-compute-") as tmp:
            out = project(Path(tmp) / "out")
            if out is None:
                self.skipTest("reference Mesa tree not available")
            frontend = out / "src/gallium/frontends/d3d10umd"
            adapter = (frontend / "Adapter.cpp").read_text()
            caps = adapter[adapter.index("case D3D11DDICAPS_SHADER:"):]
            caps = caps[:caps.index("return S_OK;")]
            self.assertIn("AgxD3d11ComputeEnabled() ?", caps)
            levels = adapter[adapter.index("case D3D11DDICAPS_3DPIPELINESUPPORT:"):]
            levels = levels[:levels.index("return S_OK;")]
            self.assertIn("AgxD3d11Fl11Enabled() ?", levels)
            self.assertIn("D3D11DDI_3DPIPELINELEVEL_11_0", levels)
            fl11 = function_body(fill, "AgxD3d11Fl11Enabled")
            self.assertIn("AgxD3d11DdiEnabled()", fl11)
            self.assertIn("COMPUTE_PLUS_RAW_AND_STRUCTURED_BUFFERS_IN_SHADER_4_X", caps)
            shader = (frontend / "Shader.cpp").read_text()
            for setter in ("SetShaderResources", "SetSamplers", "SetConstantBuffers"):
                with self.subTest(setter=setter):
                    self.assertIn("shader_type == MESA_SHADER_COMPUTE", function_body(shader, setter))
            for wrapper, setter in (("CsSetShaderResources", "SetShaderResources"),
                                    ("CsSetSamplers", "SetSamplers"),
                                    ("CsSetConstantBuffers", "SetConstantBuffers")):
                with self.subTest(wrapper=wrapper):
                    self.assertIn(setter + "(MESA_SHADER_COMPUTE,", function_body(shader, wrapper))
            self.assertIn("delete_compute_state", function_body(shader, "DestroyShader"))
            resource = (frontend / "Resource.cpp").read_text()
            self.assertIn("PIPE_BIND_SHADER_BUFFER", function_body(resource, "translate_resource_flags"))
            create = function_body(resource, "CreateResource")
            self.assertIn("D3D11_DDI_BIND_UNORDERED_ACCESS", create)
            self.assertIn("D3D11_DDI_RESOURCE_MISC_BUFFER_STRUCTURED", create)

    def test_uav_bindings_match_the_translator(self):
        # The frontend binds what the translated shaders read: raw/structured
        # views at shader buffer u#, typed views at image u#, hidden counters
        # at DXBC_NIR_UAV_COUNTER_SLOT + u#.  A different counter base binds
        # every append/consume counter to a slot the shader never reads.
        header = (ROOT / "drivers/apple-agx/mesa/dxbc/dxbc_to_nir.h").read_text()
        self.assertIn("#define DXBC_NIR_UAV_COUNTER_SLOT DXBC_NIR_MAX_UAVS", header)
        fill = MODULE.read_text()
        self.assertIn("#define AGX_D3D11_UAV_SLOTS DXBC_NIR_MAX_UAVS", fill)
        bind = function_body(fill, "bind_uavs")
        self.assertIn("set_shader_buffers(pipe, stage, first, count, buffers", bind)
        self.assertIn("set_shader_buffers(pipe, stage, AGX_D3D11_UAV_SLOTS + first, count, counters", bind)
        self.assertIn("set_shader_images(pipe, stage, first, count, 0, images)", bind)
        self.assertIn("bind_uavs(device, MESA_SHADER_FRAGMENT", function_body(fill, "SetRenderTargets11"))
        body = fill[fill.index("AgxD3d11FillDeviceFuncs(D3D11DDI_DEVICEFUNCS *f,"):]
        assigned = dict(re.findall(r"\bf->(pfn\w+)\s*=\s*(\w+);", body))
        self.assertEqual(assigned["pfnCopyStructureCount"], "CopyStructureCount11")
        for entry in ("pfnDrawInstancedIndirect", "pfnDrawIndexedInstancedIndirect",
                      "pfnDispatchIndirect"):
            with self.subTest(entry=entry):
                self.assertFalse(assigned[entry].endswith("None"), assigned[entry])
        # Asahi fetches indirect indices from the buffer start: an
        # IASetIndexBuffer offset must not be dropped
        self.assertIn("device->ib_offset", function_body(fill, "draw_indirect"))
        with tempfile.TemporaryDirectory(prefix="d3d11-uav-") as tmp:
            out = project(Path(tmp) / "out")
            if out is None:
                self.skipTest("reference Mesa tree not available")
            resource = (out / "src/gallium/frontends/d3d10umd/Resource.cpp").read_text()
            self.assertIn("PIPE_BIND_SHADER_BUFFER | PIPE_BIND_SHADER_IMAGE",
                          function_body(resource, "translate_resource_flags"))
            create = function_body(resource, "CreateResource")
            self.assertIn("(pCreateResource->BindFlags & ~D3D11_DDI_BIND_UNORDERED_ACCESS) ==", create)
            self.assertIn("else if (pCreateResource->BindFlags & D3D11_DDI_BIND_UNORDERED_ACCESS)", create)

    def test_bc6h_bc7_are_formats(self):
        # FL 11_0 requires sampling BC6H/BC7; FormatTranslate returning
        # PIPE_FORMAT_NONE fails their resource creation.  Typeless parents
        # admit the typed views (cast families).
        with tempfile.TemporaryDirectory(prefix="d3d11-bptc-") as tmp:
            out = project(Path(tmp) / "out")
            if out is None:
                self.skipTest("reference Mesa tree not available")
            translate = function_body((out / "src/gallium/frontends/d3d10umd/Format.cpp").read_text(),
                                      "FormatTranslate")
            for dxgi, pipe in (("BC6H_UF16", "BPTC_RGB_UFLOAT"), ("BC6H_SF16", "BPTC_RGB_FLOAT"),
                               ("BC7_UNORM", "BPTC_RGBA_UNORM"), ("BC7_UNORM_SRGB", "BPTC_SRGBA")):
                with self.subTest(format=dxgi):
                    case = translate[translate.index("case DXGI_FORMAT_" + dxgi + ":"):]
                    self.assertTrue(case[:case.index(";")].endswith("PIPE_FORMAT_" + pipe))
        casts = (ROOT / "drivers/apple-agx/mesa/winsys/agx_d3d10_windows.cpp").read_text()
        for member in ("R(BC6H_UF16,BC6H_TYPELESS)", "R(BC6H_SF16,BC6H_TYPELESS)",
                       "R(BC7_UNORM,BC7_TYPELESS)", "R(BC7_UNORM_SRGB,BC7_TYPELESS)"):
            self.assertIn(member, casts)

    def test_fl11_resource_limits_are_admitted(self):
        # FL 11_0 creates 16384-texel 1D/2D/cube textures with 15 mips and
        # 2048-slice arrays; the driver's own limit check refusing them
        # (D3D10's 8192/14/512) fails CreateResource at FL 11_0.
        with tempfile.TemporaryDirectory(prefix="d3d11-limits-") as tmp:
            out = project(Path(tmp) / "out")
            if out is None:
                self.skipTest("reference Mesa tree not available")
            resource = (out / "src/gallium/frontends/d3d10umd/Resource.cpp").read_text()
            limits = function_body(resource, "AgxD3d10ResourceWithinRequiredLimits")
            self.assertNotIn("D3D10_REQ_TEXTURE", limits)
            self.assertNotIn("D3D10_REQ_MIP_LEVELS", limits)
            for define, value in (("AGX_D3D11_REQ_TEXTURE2D_U_OR_V_DIMENSION", "16384u"),
                                  ("AGX_D3D11_REQ_TEXTURECUBE_DIMENSION", "16384u"),
                                  ("AGX_D3D11_REQ_TEXTURE2D_ARRAY_AXIS_DIMENSION", "2048u"),
                                  ("AGX_D3D11_REQ_MIP_LEVELS", "15u")):
                with self.subTest(define=define):
                    self.assertIn("#define " + define + " " + value, resource)

    def test_tessellation_stages_reach_asahi(self):
        # D3D-H4: hull/domain shaders through the translator, their setters
        # on the common frontend setters, patch topologies, and the winding
        # D3D declares in the hull shader reaching Asahi's tessellator and
        # front face (Asahi read it from the TES only).
        fill = MODULE.read_text()
        body = fill[fill.index("AgxD3d11FillDeviceFuncs(D3D11DDI_DEVICEFUNCS *f,"):]
        assigned = dict(re.findall(r"\bf->(pfn\w+)\s*=\s*(\w+);", body))
        for entry in ("pfnCreateHullShader", "pfnCreateDomainShader", "pfnHsSetShader",
                      "pfnDsSetShader", "pfnHsSetShaderResources", "pfnDsSetShaderResources",
                      "pfnHsSetSamplers", "pfnDsSetSamplers", "pfnHsSetConstantBuffers",
                      "pfnDsSetConstantBuffers", "pfnIaSetTopology"):
            with self.subTest(entry=entry):
                self.assertFalse(assigned[entry].endswith("None"), assigned[entry])
        self.assertIn("set_patch_vertices", function_body(fill, "IaSetTopology11"))
        create = function_body(fill, "create_tessellation_shader")
        self.assertLess(create.index("memset"), create.index("dxbc_translate_tokens_hs"))
        with tempfile.TemporaryDirectory(prefix="d3d11-tess-") as tmp:
            out = project(Path(tmp) / "out")
            if out is None:
                self.skipTest("reference Mesa tree not available")
            frontend = out / "src/gallium/frontends/d3d10umd"
            shader = (frontend / "Shader.cpp").read_text()
            for setter in ("SetShaderResources", "SetSamplers", "SetConstantBuffers"):
                with self.subTest(setter=setter):
                    body = function_body(shader, setter)
                    self.assertIn("MESA_SHADER_TESS_CTRL", body)
                    self.assertIn("MESA_SHADER_TESS_EVAL", body)
            destroy = function_body(shader, "DestroyShader")
            self.assertIn("delete_tcs_state", destroy)
            self.assertIn("delete_tes_state", destroy)
            self.assertIn("AGX_D3D11_STAGE_SETTERS(Hs, MESA_SHADER_TESS_CTRL)", shader)
            state = (out / "src/gallium/drivers/asahi/agx_state.c").read_text()
            self.assertIn("params.ccw = !(tes->tess.ccw || tcs->tess.ccw);", state)
            self.assertIn("ctx->stage[MESA_SHADER_TESS_CTRL].shader->tess.ccw));", state)


if __name__ == "__main__":
    unittest.main()
