"""Contract of the AGX lab tooling (tools/agx-lab) and the skill that drives it.

The skill and README name scripts by path; a rename must not leave them
pointing at nothing. The tools run on macOS /bin/bash 3.2 and get every
host-specific value from the untracked lab configuration.
"""
import re
import subprocess
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
LAB = ROOT / "tools" / "agx-lab"
SKILL = ROOT / ".claude" / "skills" / "agx-hardware-experiment"
LAB_PATH = re.compile(r"tools/agx-lab/[\w./-]*[\w]")
SHORT_PATH = re.compile(r"`((?:experiment|probes|guest|builder|analysis)/[\w.-]+\.(?:sh|py|ps1|wprp))")
BASH4_ONLY = re.compile(r"\b(mapfile|readarray|declare -A|local -n|coproc)\b|\$\{\w+[,^]{1,2}\}")
LAB_IDENTITY = re.compile(
    r"\b(?:10|172\.(?:1[6-9]|2\d|3[01])|192\.168)\.\d{1,3}\.\d{1,3}\b"  # private IPv4
    r"|usbmodem(?!X+)\w+"                                                # serial devices
    r"|[Cc]:[\\/]+Users[\\/]+(?!user\b|<)\w+",                           # Windows profiles
)


def lab_files(*suffixes):
    return sorted(p for p in LAB.rglob("*") if p.is_file() and p.suffix in suffixes)


class AgxLabToolingTests(unittest.TestCase):
    def test_skill_and_readme_reference_existing_tools(self):
        docs = [SKILL / "SKILL.md", *sorted((SKILL / "references").glob("*.md")), LAB / "README.md"]
        missing = []
        for doc in docs:
            text = doc.read_text(encoding="utf-8")
            for ref in LAB_PATH.findall(text):
                if not (ROOT / ref).exists():
                    missing.append(f"{doc.name}: {ref}")
            for ref in SHORT_PATH.findall(text):
                if not (LAB / ref).exists():
                    missing.append(f"{doc.name}: {ref}")
        self.assertEqual([], missing)

    def test_shell_scripts_parse_with_bash_3_2_features(self):
        offenders = []
        for script in lab_files(".sh"):
            text = script.read_text(encoding="utf-8")
            if BASH4_ONLY.search(text):
                offenders.append(f"{script.name}: bash 4 feature")
            result = subprocess.run(["/bin/bash", "-n", str(script)], capture_output=True, text=True)
            if result.returncode:
                offenders.append(f"{script.name}: {result.stderr.strip()}")
        self.assertEqual([], offenders)

    def test_python_tools_compile(self):
        for script in lab_files(".py"):
            compile(script.read_text(encoding="utf-8"), str(script), "exec")

    def test_no_lab_identity_in_tracked_tooling(self):
        offenders = []
        for path in lab_files(".sh", ".py", ".ps1", ".md", ".example", ".wprp"):
            for match in LAB_IDENTITY.finditer(path.read_text(encoding="utf-8")):
                offenders.append(f"{path.relative_to(ROOT)}: {match.group()}")
        self.assertEqual([], offenders)


if __name__ == "__main__":
    unittest.main()
