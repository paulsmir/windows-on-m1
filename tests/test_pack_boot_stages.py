import hashlib
import unittest

from standalone_image import ImageError
from tools.pack_boot import describe_stages


class PackBootStageTests(unittest.TestCase):
    def test_rejects_identical_stage_images(self):
        with self.assertRaisesRegex(ImageError, "stage-0 and stage-1 identities must differ"):
            describe_stages(b"same", b"same")

    def test_records_distinct_role_hashes(self):
        metadata = describe_stages(b"bootstrap", b"hypervisor")
        self.assertEqual(metadata["stage0"]["role"], "bootstrap")
        self.assertEqual(metadata["stage1"]["role"], "hypervisor")
        self.assertEqual(metadata["stage0"]["sha256"], hashlib.sha256(b"bootstrap").hexdigest())
        self.assertNotEqual(metadata["stage0"]["sha256"], metadata["stage1"]["sha256"])


if __name__ == "__main__":
    unittest.main()
