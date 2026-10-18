"""Run the production verifier against immutable native and Windows captures."""
import ctypes as C
import hashlib
from pathlib import Path
import struct
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
DRIVER = ROOT / "drivers/apple-agx/render-admission"


class Expectation(C.Structure):
    _fields_ = [("words", C.c_uint32 * 15)]


class Result(C.Structure):
    _fields_ = [("valid", C.c_uint32), ("observed", C.c_uint32),
                ("background", C.c_uint32), ("foreground", C.c_uint32),
                ("poison", C.c_uint32), ("first_invalid", C.c_uint32),
                ("invalid_value", C.c_uint32), ("bytes", C.c_uint32),
                ("hash", C.c_uint64)]


class NativeOutputOracleTests(unittest.TestCase):
    def test_native_red_gray_and_wrong_mask(self):
        native_path = ROOT / ".local/experiments/EXP659-native-triangle/evidence/final-attachment.bin"
        gray_path = ROOT / ".local/experiments/EXP680-retained-output-oracle/evidence/output.bin"
        if not native_path.exists() or not gray_path.exists():
            self.skipTest("immutable EXP659/680 hardware captures unavailable")
        native = native_path.read_bytes()
        self.assertEqual(hashlib.sha256(native).hexdigest(),
                         "f8b13b60fe42f8a8a6616688871bac7b22e9dce0bdfb579bd115bff8c8f79d8f")
        native = native[:1024]
        gray = gray_path.read_bytes()
        self.assertEqual(hashlib.sha256(gray).hexdigest(),
                         "c0328a8cf1b372ff1d1d86d2a859a964548aaacf602eb5a1aa2aba43400d2393")
        with tempfile.TemporaryDirectory() as tmp:
            libpath = Path(tmp) / "oracle.dylib"
            subprocess.run(["clang", "-shared", "-fPIC", "-std=c11",
                            "-Wall", "-Wextra", "-Werror",
                            "-I", str(DRIVER / "include"),
                            "-I", str(ROOT / "drivers/apple-agx/shared/include"),
                            str(DRIVER / "src/render_dynamic_output.c"),
                            "-o", str(libpath)], check=True)
            lib = C.CDLL(str(libpath))
            describe = lib.AdmissionDynamicOutputDescribeExpectation
            describe.argtypes = [C.c_uint32] * 6 + [C.POINTER(Expectation)]
            verify = lib.AdmissionDynamicOutputVerify
            verify.argtypes = [C.c_void_p, C.c_uint32, C.POINTER(Expectation),
                               C.c_uint32, C.c_void_p, C.c_void_p, C.POINTER(Result)]

            def check(data, expected):
                expectation, result = Expectation(), Result()
                self.assertEqual(describe(16, 16, 64, 0xff112233, expected,
                                          1, C.byref(expectation)), 1)
                buf = C.create_string_buffer(data)
                self.assertEqual(verify(buf, len(data), C.byref(expectation),
                                        0, None, None, C.byref(result)), 1)
                return result

            for data, expected in [(native, 0xffff0000), (gray, 0x80808080)]:
                result = check(data, expected)
                self.assertEqual((result.valid, result.observed, result.foreground,
                                  result.background, result.poison, result.bytes),
                                 (1, expected, 72, 184, 0, 1024))
            pixels = list(struct.unpack("<256I", native))
            for wrong in (0, 0xa5a5a5a5):
                changed = struct.pack("<256I", *[
                    wrong if p == 0xffff0000 else p for p in pixels])
                self.assertEqual(check(changed, 0xffff0000).valid, 0)
            red_index = pixels.index(0xffff0000)
            background_index = pixels.index(0xff112233)
            pixels[red_index], pixels[background_index] = pixels[background_index], pixels[red_index]
            self.assertEqual(check(struct.pack("<256I", *pixels), 0xffff0000).valid, 0)
