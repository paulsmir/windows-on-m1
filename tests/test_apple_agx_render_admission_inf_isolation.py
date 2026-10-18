"""The render-admission INF passes the WDK's Universal InfVerif rules.

Bug caught (EXP1279 build): the WDK build of the package runs InfVerif in
Universal mode. DIRID 13 in CopyFiles (the WOW64 ICD runs from the driver
store) needs a models section decorated for OS 10.0.16299 or later (error
1199), and a decorated Universal INF must also run its service binary from
DIRID 13 (error 2600, driver package isolation). Either omission fails the
clean ARM64 build after compilation.
"""
from pathlib import Path
import re
import unittest

INF = (Path(__file__).resolve().parents[1] /
       "drivers/apple-agx/render-admission/AppleAgxRenderAdmission.inf")


class RenderAdmissionInfIsolation(unittest.TestCase):
    def test_decorated_models_and_driver_store_service(self):
        text = INF.read_text()
        manufacturer = re.search(r"(?m)^%ManufacturerName%=Models,(\S+)$", text)
        self.assertIsNotNone(manufacturer)
        decoration = re.fullmatch(r"NTarm64\.10\.0\.\.\.(\d+)", manufacturer.group(1))
        self.assertIsNotNone(decoration, "models section must carry an OS build")
        self.assertGreaterEqual(int(decoration.group(1)), 16299)
        self.assertIn("[Models.%s]" % manufacturer.group(1), text)
        self.assertRegex(text, r"(?m)^Admission\.KmdCopyFiles=13$")
        self.assertRegex(text, r"(?m)^ServiceBinary=%13%\\AppleAgxRenderAdmission\.sys$")


if __name__ == "__main__":
    unittest.main()
