"""Native-byte proof uses the captured EXP476 ADT, or AGX_HWDATA_ADT override."""
import importlib.util
import os
from pathlib import Path
import struct
import subprocess
import sys
import tempfile
import textwrap
import unittest

ROOT = Path(__file__).resolve().parents[1]
ADT = Path(os.environ.get("AGX_HWDATA_ADT", ROOT / ".local/experiments/EXP476-initdata-inputs/j313-live.adt"))
NATIVE = ROOT / "m1n1_windows"
PRIOR = "3b036bab5b27c2263bb2267c718becb2424e8315"
GENERATOR = ROOT / "tools/generate_agx_hwdata_profile.py"


class HwdataProfileGenerator(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        if not ADT.is_file():
            raise unittest.SkipTest("EXP476 raw ADT unavailable; set AGX_HWDATA_ADT")
        spec = importlib.util.spec_from_file_location("agx_hwdata_generator", GENERATOR)
        cls.generator = importlib.util.module_from_spec(spec)
        sys.modules[spec.name] = cls.generator
        spec.loader.exec_module(cls.generator)
        cls.raw = ADT.read_bytes()

    def test_full_native_output_and_relocation_placeholders(self):
        g = self.generator
        profile = g.generate_profile(self.raw)
        from m1n1.adt import load_adt
        from m1n1.agx.initdata import CHIP_INFO, populate_hwdata
        from m1n1.fw.agx.initdata import AGXHWDataA, AGXHWDataB, IOMapping
        from m1n1.constructutils import Ver
        Ver.set_version_key("V", "V13_5")
        Ver.set_version_key("G", "G13")
        sgx = load_adt(self.raw)["/arm-io/sgx"]
        a, b = AGXHWDataA(sgx, CHIP_INFO[0x8103]), AGXHWDataB(sgx, CHIP_INFO[0x8103])
        populate_hwdata(sgx, CHIP_INFO[0x8103], a, b)
        b.io_mappings = [IOMapping() for _ in range(25)]
        b.timestamp_region_base = b.sgx_sram_ptr = 0
        self.assertEqual(profile.a, a.build())
        self.assertEqual(profile.b, b.build())
        self.assertEqual((len(profile.a), len(profile.b)), (0x421c, 0x1884))
        self.assertEqual(profile.b[0x28:0x30], bytes(8))
        self.assertEqual(profile.b[0x640:0x960], bytes(25 * 32))
        self.assertEqual(a.power_zone_count, 1)
        self.assertEqual(b.sgx_sram_ptr, 0)

    def test_extracted_helper_matches_pinned_original_block(self):
        from types import SimpleNamespace
        from construct import Container
        from m1n1.adt import load_adt
        from m1n1.agx.initdata import CHIP_INFO, populate_hwdata
        from m1n1.fw.agx.initdata import AGXHWDataA, AGXHWDataB, IOMapping
        from m1n1.constructutils import Ver
        Ver.set_version_key("V", "V13_5")
        Ver.set_version_key("G", "G13")
        original = subprocess.check_output([
            "git", "-C", str(NATIVE), "show", PRIOR + ":proxyclient/m1n1/agx/initdata.py"
        ], text=True)
        block = original[original.index("    k = 1.02 #?"):original.index("    regionB.hwdata_a.push()")]
        code = compile(textwrap.dedent(block), "pinned_native_performance_block", "exec")
        for auxiliary in (False, True):
            sgx = load_adt(self.raw)["/arm-io/sgx"]
            if auxiliary:
                for name, count in (("cs-perf-states", 3), ("afr-perf-states", 2)):
                    sgx._properties[name] = Container(count=count, states=[[
                        Container(freq=200_000_000 + i * 100_000_000, volt=650_000 + i * 50_000)
                        for i in range(count)]], min_sram_volt=[700_000])
            chip = CHIP_INFO[0x8103]
            old_a, old_b = AGXHWDataA(sgx, chip), AGXHWDataB(sgx, chip)
            new_a, new_b = AGXHWDataA(sgx, chip), AGXHWDataB(sgx, chip)
            exec(code, {"sgx": sgx, "chip_info": chip, "hwdata": old_b,
                        "regionB": SimpleNamespace(hwdata_a=old_a)})
            populate_hwdata(sgx, chip, new_a, new_b)
            for b in (old_b, new_b):
                b.io_mappings = [IOMapping() for _ in range(25)]
                b.timestamp_region_base = b.sgx_sram_ptr = 0
            self.assertEqual(old_a.build(), new_a.build())
            self.assertEqual(old_b.build(), new_b.build())

    def test_raw_inputs_include_absences_and_float_bits(self):
        g = self.generator
        profile = g.generate_profile(self.raw)
        inputs = dict(profile.inputs)
        self.assertEqual(len(inputs), 66)
        self.assertEqual(inputs["gpu-power-zone-target-0"], struct.pack("<I", 30000))
        self.assertEqual(inputs["gpu-power-zone-target-offset-0"], struct.pack("<I", 100))
        self.assertEqual(inputs["gpu-power-zone-filter-tc-0"], struct.pack("<I", 6875))
        self.assertIsNone(inputs["cs-perf-states"])
        self.assertIsNone(inputs["afr-perf-states"])
        self.assertIsNone(inputs["gpu-se-kp"])
        from m1n1.adt import load_adt, build_prop
        sgx = load_adt(self.raw)["/arm-io/sgx"]
        for name, value in profile.inputs:
            if name not in sgx._properties:
                self.assertIsNone(value)
            else:
                self.assertEqual(value, build_prop(sgx._path, name, sgx._properties[name],
                    t=sgx._types[name][0]))
        # Canonical identity must distinguish every property change, and
        # missing versus present-empty even if constructor defaults agree.
        baseline = g.profile_identity(profile.inputs, profile.sources, profile.a, profile.b)
        for index, (name, raw) in enumerate(profile.inputs):
            altered = list(profile.inputs)
            altered[index] = (name, b"" if raw is None else (bytes([raw[0] ^ 1]) + raw[1:] if raw else b"\0"))
            self.assertNotEqual(baseline, g.profile_identity(altered, profile.sources, profile.a, profile.b))
        changed_sources = dict(profile.sources)
        first = next(iter(changed_sources))
        changed_sources[first] = "0" * 64
        self.assertNotEqual(baseline, g.profile_identity(profile.inputs, changed_sources, profile.a, profile.b))

    def test_generation_is_deterministic_and_matches_checked_in_header(self):
        g = self.generator
        one = g.generate_profile(self.raw)
        two = g.generate_profile(self.raw)
        self.assertEqual(g.render_header(one), g.render_header(two))
        header = ROOT / "drivers/apple-agx/shared/include/apple_agx_hwdata_profile.generated.h"
        self.assertEqual(g.render_header(one), header.read_text())
        with tempfile.TemporaryDirectory() as tmp:
            output = Path(tmp) / "profile.h"
            subprocess.run([sys.executable, str(GENERATOR), "--adt", str(ADT),
                            "--output", str(output)], check=True, cwd=ROOT)
            self.assertEqual(output.read_text(), header.read_text())

    def test_present_default_is_distinct_from_absent_input(self):
        g = self.generator
        from m1n1.adt import ADTNodeStruct
        from construct import Container
        baseline = g.generate_profile(self.raw)
        parsed = ADTNodeStruct.parse(self.raw)
        def named(parent, name):
            return next(child for child in parent.children if any(
                p.name == "name" and p.value.rstrip(b"\0") == name.encode()
                for p in child.properties))
        sgx = named(named(parsed, "arm-io"), "sgx")
        sgx.properties.append(Container(name="gpu-se-kp", size=4, value=struct.pack("<f", -5.0)))
        sgx.property_count += 1
        changed = g.generate_profile(ADTNodeStruct.build(parsed))
        self.assertEqual((baseline.a, baseline.b), (changed.a, changed.b))
        self.assertNotEqual(baseline.identity, changed.identity)
        self.assertEqual(dict(changed.inputs)["gpu-se-kp"], struct.pack("<f", -5.0))

    def test_native_constructor_reads_are_covered_by_raw_input_contract(self):
        from m1n1.adt import load_adt
        from m1n1.agx.initdata import CHIP_INFO, populate_hwdata
        from m1n1.fw.agx.initdata import AGXHWDataA, AGXHWDataB
        accessed = set()
        node = load_adt(self.raw)["/arm-io/sgx"]
        class TrackedNode:
            def getprop(self, name, default=None):
                accessed.add(name)
                return node.getprop(name, default)
            def __getattr__(self, name):
                accessed.add(name.replace("_", "-"))
                return getattr(node, name)
        sgx = TrackedNode()
        chip = CHIP_INFO[0x8103]
        a, b = AGXHWDataA(sgx, chip), AGXHWDataB(sgx, chip)
        populate_hwdata(sgx, chip, a, b)
        self.assertFalse(accessed - set(self.generator.INPUT_NAMES))

    def test_compiled_header_contains_native_profile_bytes(self):
        profile = self.generator.generate_profile(self.raw)
        # Compile stdin so this remains a single owned Python test file.
        c = r'''
#include "apple_agx_hwdata_profile.generated.h"
#include <stdio.h>
int main(void) {
    if (sizeof(AgxHwdataAProfile) != AGX_HWDATA_A_BYTES ||
        sizeof(AgxHwdataBProfile) != AGX_HWDATA_B_BYTES || AGX_HWDATA_INPUT_COUNT != 66)
        return 1;
    if (!AgxHwdataInputs[0].Name) return 2;
    if (fwrite(AgxHwdataAProfile, 1, sizeof(AgxHwdataAProfile), stdout) != sizeof(AgxHwdataAProfile) ||
        fwrite(AgxHwdataBProfile, 1, sizeof(AgxHwdataBProfile), stdout) != sizeof(AgxHwdataBProfile) ||
        fwrite(AgxHwdataProfileId, 1, sizeof(AgxHwdataProfileId), stdout) != sizeof(AgxHwdataProfileId))
        return 3;
    return 0;
}
'''
        with tempfile.TemporaryDirectory() as tmp:
            binary = Path(tmp) / "profile"
            subprocess.run(["clang", "-std=c11", "-Wall", "-Wextra", "-Werror", "-x", "c", "-",
                            "-I", str(ROOT / "drivers/apple-agx/shared/include"), "-o", str(binary)],
                           input=c, text=True, check=True)
            self.assertEqual(subprocess.check_output([str(binary)]), profile.a + profile.b + profile.identity)


def load_tests(loader, tests, pattern):
    # Match the repository's native-reference tests: normal unittest discovery
    # can invoke the pinned proxy environment without installing its packages.
    if importlib.util.find_spec("construct") is None:
        def run_native_suite():
            subprocess.run([str(ROOT / "proxyenv/bin/python"), "-m", "unittest",
                            "tests.test_agx_hwdata_profile_generator"], check=True, cwd=ROOT)
        return unittest.TestSuite([unittest.FunctionTestCase(run_native_suite)])
    return tests


if __name__ == "__main__":
    unittest.main()
