"""Source contracts for the Windows runner; these do not execute PowerShell."""
from pathlib import Path
import re
import unittest

ROOT = Path(__file__).resolve().parents[1]
RUNNER = ROOT / 'tools/agx_gravitymark_run.ps1'


class GravityMarkRun(unittest.TestCase):
    def test_dimensions_are_validated_named_parameters(self):
        """WHAT REAL BUG OR INVARIANT WILL THIS TEST CATCH?

        EXP1971 passed Width/Height to a script which did not bind them, so
        the reported FHD workload silently ran at 720p. Require real positive
        integer parameters while preserving the old default dimensions.
        """
        source = RUNNER.read_text()
        params = source[:source.index("$d=")]
        for name, default in (('Width', 1280), ('Height', 720)):
            self.assertRegex(params, rf'\[ValidateRange\(1,\s*2147483647\)\]'
                             rf'\s*\[int\]\${name}\s*=\s*{default}\b')

    def test_requested_dimensions_reach_both_display_modes(self):
        """WHAT REAL BUG OR INVARIANT WILL THIS TEST CATCH?

        Declaring parameters alone still permits the old hardcoded 1280x720
        command. The shared argument list must forward both values before
        any fullscreen-only arguments, so neither mode silently drops them.
        This is a source invariant, not an execution/syntax check of PS.
        """
        source = RUNNER.read_text()
        build = source[source.index('$a=@('):source.index('$t0=')]
        common = build.split('if($Fullscreen)', 1)[0]
        self.assertRegex(common, r"'-width'\s*,\s*\"\$Width\"")
        self.assertRegex(common, r"'-height'\s*,\s*\"\$Height\"")
        self.assertNotRegex(build, r"'-(?:width|height)'\s*,\s*['\"]\d+")
        self.assertIn("'-fullscreen','1','-screen','0'", build)

    def test_unknown_named_arguments_cannot_be_silently_ignored(self):
        """WHAT REAL BUG OR INVARIANT WILL THIS TEST CATCH?

        A misspelled or unsupported workload parameter must fail parameter
        binding instead of running a different benchmark under the same tag.
        Advanced-script binding supplies this Windows PowerShell contract.
        """
        source = RUNNER.read_text()
        uncommented = re.sub(r'(?m)^\s*#.*$', '', source).lstrip()
        self.assertRegex(uncommented, r'^\[CmdletBinding\(\)\]\s*param\(')

    def test_legacy_fullscreen_keeps_app_selected_dimensions(self):
        """WHAT REAL BUG OR INVARIANT WILL THIS TEST CATCH?

        The old fullscreen runner left resolution to GravityMark. Adding
        default width/height unconditionally silently changes that existing
        workload. Size forwarding must cover windowed runs or an explicit
        dimension request, with the manifest using the same condition.
        """
        source = RUNNER.read_text()
        self.assertIn("$sized = -not $Fullscreen -or "
                      "$PSBoundParameters.ContainsKey('Width') -or "
                      "$PSBoundParameters.ContainsKey('Height')", source)
        self.assertRegex(source, r'if\(\$sized\)\s*\{\s*\$a\+=@\('
                         r"'-width',\s*\"\$Width\",\s*'-height',\s*\"\$Height\"\)\s*\}")
        self.assertIn('DimensionsSpecified=$sized', source)


if __name__ == '__main__':
    unittest.main()
