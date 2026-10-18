"""EXP844/845: StartMenuExperienceHost and Explorer faulted in
agx2_disassemble_instr.  The non-NDEBUG compiler self-test disassembles every
binary into fopen("/dev/null"), which does not exist on Windows, and the
decoder reads past the end of the binary.  The projection must disable it on
Win32."""

from pathlib import Path
import unittest

SCRIPT = Path(__file__).resolve().parents[1] / "drivers/apple-agx/mesa/scripts/build-native-asahi-state.py"


class DisassemblerSelfTest(unittest.TestCase):
    def test_selftest_is_disabled_on_win32(self):
        source = SCRIPT.read_text()
        self.assertIn("out/'src/asahi/compiler/agx_compile.c'", source)
        self.assertIn("#if !defined(NDEBUG) && !defined(_WIN32)", source)
        self.assertIn("Ambiguous agx_compile disassembler self-test anchor", source)


if __name__ == "__main__":
    unittest.main()
