"""Keep the native archive mode bound to the UMD GPUVA build profile."""
from importlib.util import module_from_spec, spec_from_file_location
from pathlib import Path
import tempfile
import unittest
import xml.etree.ElementTree as ET


SCRIPT = (Path(__file__).resolve().parents[1] / "drivers" / "apple-agx" /
          "mesa" / "scripts" / "build-asahi-runtime-closure.py")


class G4NativePropsTests(unittest.TestCase):
    def test_generated_props_identifies_gpuva_mode_and_rejects_mismatch(self):
        spec = spec_from_file_location("native_closure", SCRIPT)
        module = module_from_spec(spec)
        spec.loader.exec_module(module)
        with tempfile.TemporaryDirectory() as temp:
            path = Path(temp) / "NativeRuntime.props"
            for gpuva in (False, True):
                module.write_props(path, "arm64", Path("native"),
                                   Path("runtime.lib"), [], gpuva)
                root = ET.parse(path).getroot()
                ns = {"m": "http://schemas.microsoft.com/developer/msbuild/2003"}
                self.assertEqual(root.findtext(
                    "m:PropertyGroup/m:NativeRuntimeGpuva", namespaces=ns),
                    "true" if gpuva else "false")
                errors = root.findall("m:Target/m:Error", ns)
                self.assertTrue(any("EnableGpuvaWinsys" in error.attrib.get(
                    "Condition", "") for error in errors))
