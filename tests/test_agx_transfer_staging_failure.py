"""EXP1296: a failed staging allocation fails agx_transfer_map.

Invariant caught: once the device is lost, agx_alloc_staging returns NULL;
Mesa's agx_transfer_map only asserts it (compiled out in the release
build) and read staging->layout, so the x86 GL smoke crashed in
glReadPixels (AppleAgxOpenGL32.dll+0xb5441, NULL+0x9c =
layout.linear_stride_B).  The projected map must return NULL with no
transfer before any use of the staging resource.
"""
from pathlib import Path
import tempfile
import unittest

from test_d3d10_shader_fields_init import function_body, project


class TransferStagingFailure(unittest.TestCase):
    def test_staging_failure_fails_the_map(self):
        with tempfile.TemporaryDirectory(prefix="agx-staging-") as tmp:
            out = project(Path(tmp) / "out")
            if out is None:
                self.skipTest("reference Mesa tree not available")
            pipe = (out / "src/gallium/drivers/asahi/agx_pipe.c").read_text()
            body = function_body(pipe, "agx_transfer_map")
            alloc = body.index("agx_alloc_staging(")
            check = body.index("if (!staging) {", alloc)
            self.assertLess(check, body.index("staging->layout", alloc))
            failure = body[check:body.index("}", check)]
            self.assertIn("*out_transfer = NULL;", failure)
            self.assertIn("FREE(transfer);", failure)
            self.assertIn("return NULL;", failure)


if __name__ == "__main__":
    unittest.main()
