"""Catch the real EXP1524 NoticeFiles case-alias parser failure before transport."""
import importlib.util
from pathlib import Path
import tempfile
import unittest

SCRIPT = Path(__file__).resolve().parents[1] / "scripts/agx_powershell_json.py"
spec = importlib.util.spec_from_file_location("agx_powershell_json", SCRIPT)
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)


class PowerShellJsonTests(unittest.TestCase):
    def test_real_notice_case_aliases_are_rejected(self):
        text = '{"NoticeFiles":[{"relative":"notices/MIT.txt","Relative":"notices/MIT.txt"}]}'
        with self.assertRaisesRegex(ValueError, "relative.*Relative"):
            module.loads_powershell_json(text)

    def test_byte_aliases_are_rejected_even_when_values_differ(self):
        with self.assertRaises(ValueError):
            module.loads_powershell_json('{"NoticeFiles":[{"bytes":1,"Bytes":2}]}')

    def test_exact_duplicate_keys_are_not_silently_lost(self):
        with self.assertRaises(ValueError):
            module.loads_powershell_json('{"SourceCommit":"a","SourceCommit":"b"}')

    def test_same_field_in_separate_objects_is_valid(self):
        text = '{"NoticeFiles":[{"Relative":"a","Bytes":1},{"Relative":"b","Bytes":2}]}'
        self.assertEqual(module.loads_powershell_json(text)["NoticeFiles"][1]["Bytes"], 2)

    def test_valid_metadata_is_preserved(self):
        text = '{"Status":"ROOT_APPROVED","Nested":{"Bytes":1,"SHA256":"abc"}}'
        self.assertEqual(module.loads_powershell_json(text)["Nested"], {"Bytes": 1, "SHA256": "abc"})

    def test_bom_file_is_supported(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "approval.json"
            path.write_text('{"Status":"ROOT_APPROVED"}', encoding="utf-8-sig")
            self.assertEqual(module.load_powershell_json(path)["Status"], "ROOT_APPROVED")


if __name__ == "__main__":
    unittest.main()
