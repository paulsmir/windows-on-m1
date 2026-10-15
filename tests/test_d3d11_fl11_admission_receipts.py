"""Keep the FL 11_0 pre-CreateDevice admission receipts visible."""

from pathlib import Path
import unittest


ROOT = Path(__file__).resolve().parents[1]
WINSYS = ROOT / "drivers/apple-agx/mesa/winsys/agx_d3d10_windows.cpp"
PROJECTOR = ROOT / "drivers/apple-agx/mesa/scripts/build-native-asahi-state.py"


class Fl11AdmissionReceipts(unittest.TestCase):
    def test_open_adapter_runtime_arguments(self):
        source = WINSYS.read_text()
        self.assertIn('"g4-open-adapter-args"', source)
        self.assertIn('Args->Interface', source)
        self.assertIn('Args->Version', source)

    def test_calc_private_device_size_arguments_and_result(self):
        source = PROJECTOR.read_text()
        self.assertIn('"measure-calc-private-device-size"', source)
        for field in ('pData->Interface', 'pData->Version', 'pData->Flags'):
            self.assertIn(field, source)
        self.assertIn('sizeof(Device)', source)

    def test_create_device_runtime_arguments(self):
        source = WINSYS.read_text()
        self.assertIn('"g4-create-device-args"', source)
        self.assertIn('Args->Interface', source)
        self.assertIn('Args->Version', source)


if __name__ == "__main__":
    unittest.main()
