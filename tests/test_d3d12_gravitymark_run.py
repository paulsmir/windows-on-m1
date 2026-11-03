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


    def test_full_mode_keeps_the_official_benchmark_command(self):
        """WHAT REAL BUG OR INVARIANT WILL THIS TEST CATCH?

        PERF/FAST are short closed runs of the same benchmark; FULL must stay
        the official one (EXP2134: HD 6257 / FHD 4862 baseline): default mode
        FULL, the GravityMark argument list unchanged, the timeout replaced
        only for PERF/FAST and only when -Timeout is not passed explicitly.
        """
        source = RUNNER.read_text()
        self.assertIn("[ValidateSet('FULL','PERF','FAST')][string]$Mode='FULL'", source)
        self.assertIn("$a=@('-temporal',\"$Temporal\",'-fps','1','-info','1','-benchmark','1',"
                      "'-close','1','-count','1',\"-$Api\",'-asteroids',\"$Asteroids\",'-times',"
                      "\"$d\\$Tag-times.txt\",'-image',\"$d\\$Tag-image.png\")", source)
        self.assertIn("if($Mode -ne 'FULL' -and -not $PSBoundParameters.ContainsKey('Timeout'))"
                      "{$Timeout=$modeTimeout[$Mode]}", source)
        self.assertRegex(source, r'\$modeTimeout=@\{FULL=\$Timeout;PERF=100;FAST=55\}')
        self.assertEqual(source.count('$a+='), 2, 'modes must not add GravityMark arguments')

    def test_short_modes_report_a_normal_close_as_success(self):
        """WHAT REAL BUG OR INVARIANT WILL THIS TEST CATCH?

        A PERF/FAST run ends at its timeout by design; the result file must
        say so machine-readably instead of looking like a hang, while FULL
        still requires a normal benchmark exit.
        """
        source = RUNNER.read_text()
        self.assertIn("$ok = if($Mode -eq 'FULL'){ $state -like 'EXIT *' } else "
                      "{ $state -eq 'TIMEOUT CLOSED' -or $state -like 'EXIT *' }", source)
        self.assertIn('-mode.json', source)
        self.assertIn('Mode=$Mode;Timeout=$Timeout', source)


if __name__ == '__main__':
    unittest.main()
