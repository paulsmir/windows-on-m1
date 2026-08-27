from pathlib import Path
import os
import subprocess
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[1]
SHARED = ROOT / "drivers" / "apple-agx" / "shared"


class AppleAgxInitdataMemoryTests(unittest.TestCase):
    def test_owned_initdata_graph_suite(self):
        with tempfile.TemporaryDirectory() as tmp:
            binary = Path(tmp) / "apple_agx_initdata_memory_test"
            command = [
                os.environ.get("CC", "clang"),
                "-std=c11", "-Wall", "-Wextra", "-Werror",
                "-fsanitize=address,undefined",
                "-I", str(SHARED / "include"),
                str(SHARED / "tests" / "apple_agx_initdata_memory_test.c"),
                str(SHARED / "src" / "apple_agx_initdata_memory.c"),
                str(SHARED / "src" / "apple_agx_initdata.c"),
                str(SHARED / "src" / "apple_agx_firmware_status.c"),
                str(SHARED / "src" / "apple_agx_channel_info.c"),
                str(SHARED / "src" / "apple_agx_channel_memory.c"),
                str(SHARED / "src" / "apple_agx_regionb.c"),
                str(SHARED / "src" / "apple_agx_regionb_memory.c"),
                str(SHARED / "src" / "apple_agx_uat_memory.c"),
                str(SHARED / "src" / "apple_agx_uat_table.c"),
                str(SHARED / "src" / "apple_agx_uat.c"),
                str(SHARED / "src" / "apple_agx_memory.c"),
                "-o", str(binary),
            ]
            subprocess.run(command, check=True, cwd=ROOT)
            subprocess.run([str(binary)], check=True, cwd=ROOT)


if __name__ == "__main__":
    unittest.main()
