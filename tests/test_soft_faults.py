"""RC82: GPU soft faults reported to Mesa behind Wom1SoftFaults.

WHAT REAL BUG OR INVARIANT WILL THIS TEST CATCH?
- Asahi reports DRM_ASAHI_FEATURE_SOFT_FAULTS only for fault_control 0xb
  (file.rs), and under it only shader load/store returns zero for unmapped
  memory (asahi_drm.h: texture sampling still faults).  The KMD must report
  AGX_WIN32_DEVICE_FEATURE_SOFT_FAULTS only when the device value
  Wom1SoftFaults = 1 AND the RegionC word the firmware was started with reads
  0xb; a UMD that speculated loads under hard faults would turn every
  speculated out-of-range load into an MMU fault (EXP1964/EXP2005: 0x116).
- The receipt must not be written under the switch's own value name: a
  REG_BINARY Wom1SoftFaults would overwrite the REG_DWORD switch and silently
  turn the feature off on the next start.
- Mesa's agx_has_soft_fault also makes the compiler speculate texture
  instructions (set_speculate) and the vertex prolog fetch out-of-bounds
  attributes at address 0 (agx_nir_lower_vbo), which needs Mesa's unmapped
  2^36-byte carveout; Windows keeps neither, so the projection must keep
  agx_has_soft_fault false and run the loads-only pass before
  agx_compile_shader_nir.
- Wom1SoftFaultState (KMD struct, C_ASSERT 40 bytes) and the offline
  decoder must agree field by field.
"""
from pathlib import Path
import json
import os
import re
import subprocess
import sys
import tempfile
import unittest

from test_d3d10_shader_fields_init import function_body, local_tree, project

ROOT = Path(__file__).resolve().parents[1]
AGX = ROOT / 'drivers/apple-agx'
RENDER = AGX / 'render-admission'
PLATFORM = RENDER / 'src/backend_platform_windows.c'
sys.path.insert(0, str(ROOT / 'tools'))
import decode_wom1_gpu_fault as decoder  # noqa: E402

PROGRAM = r'''
#include <stdio.h>
#include <string.h>
#include "render_gpu_fault.h"
int main(void) {
  ADMISSION_GPU_SOFT_FAULTS_RECEIPT r;
  memset(&r, 0, sizeof(r));
  if (sizeof(r) != 40) return 1;
  r.Version = ADMISSION_GPU_SOFT_FAULTS_VERSION; r.Bytes = sizeof(r);
  r.Requested = 1u; r.RegionCRead = 1u;
  r.RegionCFaultControl = ADMISSION_GPU_SOFT_FAULTS_FAULT_CONTROL; r.Active = 1u;
  r.SgxRead = 1u; r.Sgx17620 = 0x11u; r.Sgx17630 = 0x22u;
  fwrite(&r, sizeof(r), 1, stdout);
  return 0;
}
'''


def platform_function(name):
    text = PLATFORM.read_text()
    m = re.search(r'\n(?:static |_Use_decl_annotations_ )[^\n]*\b' + name + r'\(', text)
    start = text.index('{', m.end())
    depth, pos = 1, start + 1
    while depth:
        depth += {'{': 1, '}': -1}.get(text[pos], 0)
        pos += 1
    return text[start:pos]


class AsahiPremises(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        mesa = local_tree('reference', 'mesa')
        asahi = local_tree('reference', 'asahi-linux-asahi', 'drivers', 'gpu', 'drm', 'asahi')
        if not mesa or not asahi:
            raise unittest.SkipTest('reference Mesa / Asahi trees not available')
        cls.mesa, cls.asahi = mesa, asahi

    def test_the_kernel_reports_soft_faults_for_fault_control_0xb_only(self):
        files = (self.asahi / 'file.rs').read_text()
        self.assertRegex(files, r'if \*module_parameters::fault_control\.value\(\) == 0xb \{\s+'
                                r'params\.features \|= uapi::drm_asahi_feature_DRM_ASAHI_FEATURE_SOFT_FAULTS')
        uapi = (self.mesa / 'include/drm-uapi/asahi_drm.h').read_text()
        self.assertIn('loads of unmapped memory will return zero', uapi)
        self.assertIn('notably including\n\t * texture sampling.', uapi)

    def test_mesa_speculates_textures_and_fetches_vertices_at_zero_under_the_feature(self):
        compiler = (self.mesa / 'src/asahi/compiler/agx_compile.c').read_text()
        speculate = function_body(compiler, 'set_speculate')
        self.assertIn('nir_instr_as_tex(instr)->can_speculate = true;', speculate)
        vbo = (self.mesa / 'src/asahi/lib/agx_nir_lower_vbo.c').read_text()
        self.assertIn('base = nir_bcsel(b, oob, nir_imm_int64(b, 0), base);', vbo)
        self.assertIn('bool rs_address_fixup = interchange_comps > 2 && ctx->rs.soft_fault;', vbo)


class KmdReport(unittest.TestCase):
    def test_active_needs_the_switch_and_regionc_0xb(self):
        body = platform_function('AdmissionPublishSoftFaults')
        self.assertRegex(body, r'receipt\.Active = receipt\.Requested && receipt\.RegionCRead &&\s+'
                               r'receipt\.RegionCFaultControl == ADMISSION_GPU_SOFT_FAULTS_FAULT_CONTROL;')
        self.assertIn('ADMISSION_REGIONC_FAULT_CONTROL_OFFSET', body)
        self.assertIn('InterlockedExchange(&Context->GpuSoftFaults, receipt.Active ? 1 : 0);', body)
        header = (RENDER / 'include/render_gpu_fault.h').read_text()
        self.assertIn('#define ADMISSION_GPU_SOFT_FAULTS_FAULT_CONTROL 0xbu', header)
        regionc = (AGX / 'shared/src/apple_agx_regionc.c').read_text()
        self.assertIn('#define APPLE_AGX_REGIONC_J313_FAULT_CONTROL 0xbu', regionc)

    def test_receipt_does_not_overwrite_the_switch(self):
        text = PLATFORM.read_text()
        switch = re.search(r'AdmissionPlatformSwitchEnabled\(Context, L"(Wom1SoftFaults)"\)', text)
        receipt = re.findall(r'RtlInitUnicodeString\(&name, L"(Wom1SoftFault[A-Za-z]*)"\)', text)
        self.assertTrue(switch)
        self.assertEqual(receipt, [decoder.SOFT_FAULTS_NAME])
        self.assertNotEqual(receipt[0], switch.group(1))

    def test_published_after_the_backend_started_and_cleared_on_stop(self):
        start = platform_function('AdmissionPlatformRuntimeStart')
        self.assertLess(start.index('runtime->BackendStarted = TRUE;'),
                        start.index('AdmissionPublishSoftFaults(Context, runtime);'))
        self.assertLess(start.index('InterlockedExchange(&Context->GpuSoftFaults, 0);'),
                        start.index('AdmissionPublishSoftFaults(Context, runtime);'))
        fail = start[start.index('\nFail:'):]
        self.assertIn('InterlockedExchange(&Context->GpuSoftFaults, 0);', fail)
        stop = platform_function('AdmissionPlatformRuntimeStop')
        self.assertLess(stop.index('InterlockedExchange(&Context->GpuSoftFaults, 0);'),
                        stop.index('AdmissionPlatformDestroy(runtime)'))

    def test_the_device_info_reports_only_the_published_state(self):
        lifecycle = (RENDER / 'src/lifecycle.c').read_text()
        self.assertRegex(lifecycle, r'info->Features = InterlockedCompareExchange\(&context->GpuSoftFaults, 0, 0\) != 0\s+'
                                    r'\? AGX_WIN32_DEVICE_FEATURE_SOFT_FAULTS : 0u;')
        scene = (AGX / 'mesa/winsys/agx_win32_asahi_scene.c').read_text()
        self.assertRegex(scene, r'params\.features=\(windows->Info\.Features & AGX_WIN32_DEVICE_FEATURE_SOFT_FAULTS\) \?\s+'
                                r'DRM_ASAHI_FEATURE_SOFT_FAULTS : 0;')

    def test_receipt_layout_matches_decoder(self):
        with tempfile.TemporaryDirectory() as tmp:
            source = Path(tmp) / 'layout.c'
            source.write_text(PROGRAM)
            binary = Path(tmp) / 'layout'
            subprocess.run([os.environ.get('CC', 'clang'), '-std=c11', '-Wall', '-Wextra', '-Werror',
                            '-I', str(RENDER / 'include'), str(source), '-o', str(binary)], check=True)
            raw = subprocess.run([str(binary)], check=True, capture_output=True).stdout
            path = Path(tmp) / f'{decoder.SOFT_FAULTS_NAME}.bin'
            path.write_bytes(raw)
            self.assertEqual(decoder.read_receipts(path), {decoder.SOFT_FAULTS_NAME: raw})
        decoded = decoder.DECODERS[decoder.SOFT_FAULTS_NAME](raw)
        self.assertEqual((decoded['requested'], decoded['active']), (True, True))
        self.assertEqual(decoded['regionc_fault_control'], '0xb')
        self.assertEqual(decoded['sgx_fault_control'], {'0x17620': '0x11', '0x17630': '0x22'})
        json.dumps(decoded)


class Projection(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.tmp = tempfile.TemporaryDirectory(prefix='soft-fault-projection-')
        out = project(Path(cls.tmp.name) / 'out')
        if out is None:
            cls.tmp.cleanup()
            raise unittest.SkipTest('reference Mesa tree not available')
        cls.state = (out / 'src/gallium/drivers/asahi/agx_state.c').read_text()
        cls.device = (out / 'src/asahi/lib/agx_device.h').read_text()

    @classmethod
    def tearDownClass(cls):
        cls.tmp.cleanup()

    def test_asahi_consumers_stay_off(self):
        body = function_body(self.device, 'agx_has_soft_fault')
        self.assertIn('return false;', body)
        self.assertNotIn('features', body)
        feature = function_body(self.device, 'agx_win32_soft_faults')
        self.assertIn('dev->params.features & DRM_ASAHI_FEATURE_SOFT_FAULTS', feature)
        # the vertex prolog keeps reading agx_has_soft_fault (false)
        self.assertIn('.prolog.vs.robustness.soft_fault = agx_has_soft_fault(dev),',
                      function_body(self.state, 'agx_update_vs'))

    def test_loads_speculate_right_before_the_backend(self):
        self.assertIn('#include "agx_win32_soft_fault.h"', self.state)
        body = function_body(self.state, 'agx_compile_nir')
        call = re.search(r'if \(agx_win32_soft_faults\(dev\)\)\s+'
                         r'NIR_PASS\(_, nir, agx_win32_speculate_soft_fault_loads\);\s+'
                         r'agx_compile_shader_nir\(nir, &key, &compiled->b\);', body)
        self.assertTrue(call, 'soft-fault pass before agx_compile_shader_nir')
        self.assertEqual(self.state.count('agx_win32_speculate_soft_fault_loads'), 1)


if __name__ == '__main__':
    unittest.main()
