import re
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
DOCS = (
    "INSTALL.md",
    "BUILD.md",
    "RUN.md",
    "ARCHITECTURE.md",
    "DEBUGGING.md",
    "DEVELOPMENT_HISTORY.md",
    "LIMITATIONS.md",
)
CYRILLIC = re.compile(r"[\u0400-\u04ff]")


class PublicDocumentationTests(unittest.TestCase):
    def test_root_license_and_upstream_attribution_exist(self):
        license_text = (ROOT / "LICENSE").read_text(encoding="utf-8")
        readme = (ROOT / "README.md").read_text(encoding="utf-8")
        self.assertIn("MIT License", license_text)
        for project in ("Asahi Linux", "m1n1", "Project Mu", "NT-for-ASi"):
            self.assertIn(project, readme)

    def test_changelog_does_not_promote_unvalidated_standalone_boot(self):
        text = (ROOT / "CHANGELOG.md").read_text(encoding="utf-8")
        self.assertIn("Windows desktop", text)
        self.assertIn("standalone hardware validation pending", text.lower())

    def test_current_document_set_exists_and_readme_links_resolve(self):
        readme = (ROOT / "README.md").read_text(encoding="utf-8")
        for name in DOCS:
            path = ROOT / "documentation" / name
            self.assertTrue(path.is_file(), name)
            self.assertIn(f"documentation/{name}", readme)
        for target in re.findall(r"\[[^]]+\]\(([^)]+)\)", readme):
            if "://" not in target and not target.startswith("#"):
                self.assertTrue((ROOT / target).exists(), target)

    def test_public_documents_are_english_and_have_no_private_paths(self):
        paths = [
            ROOT / "README.md",
            *(ROOT / "documentation" / name for name in DOCS),
        ]
        offenders = []
        for path in paths:
            text = path.read_text(encoding="utf-8")
            if CYRILLIC.search(text) or "/Users/pavel" in text:
                offenders.append(str(path.relative_to(ROOT)))
        self.assertEqual([], offenders)

    def test_install_guide_captures_validated_manual_deployment(self):
        text = (ROOT / "documentation/INSTALL.md").read_text(encoding="utf-8")
        required = (
            "Windows 11 ARM64",
            "exFAT",
            r"\EFI\BOOT\BOOTAA64.EFI",
            "Shift+F10",
            "create partition msr size=16",
            "dism /Get-WimInfo",
            "dism /Apply-Image",
            "bcdboot W:\\Windows /s S: /f UEFI /v",
            "start ms-cxh:localonly",
            "map -r",
            "scripts/install-esp.sh restore",
        )
        for token in required:
            self.assertIn(token, text)
        self.assertNotIn("/Index:3", text)
        self.assertNotIn("\nFS3:\\", text)
        self.assertNotIn("clean\n", text.lower())

    def test_asahi_installer_owns_the_initial_disk_resize(self):
        text = (ROOT / "documentation/INSTALL.md").read_text(encoding="utf-8")
        normalized = " ".join(text.split())
        required = (
            "curl https://alx.sh | sh",
            "Resize an existing partition to make space for a new OS",
            "Install an OS into free space",
            "UEFI environment only (m1n1 + U-Boot + ESP)",
            "Do not enable Expert Mode",
            "The Asahi installer performs the APFS shrink",
            "Do not shrink the APFS container manually",
            "Loading startup options",
            "Finish Installation",
            "machine-owner credentials",
            "stock Asahi UEFI environment",
            "Macintosh HD",
            "sudo scripts/install-esp.sh inspect --disk diskXsY",
            "sudo scripts/install-esp.sh install --disk diskXsY --image dist/j313/boot.bin",
            "sudo scripts/install-esp.sh restore --disk diskXsY",
        )
        for token in required:
            self.assertIn(token, normalized)
        self.assertNotIn("Use macOS tools to shrink", text)
        order = tuple(normalized.index(token) for token in (
            "curl https://alx.sh | sh",
            "Resize an existing partition to make space for a new OS",
            "Install an OS into free space",
            "UEFI environment only (m1n1 + U-Boot + ESP)",
            "Loading startup options",
            "Finish Installation",
            "stock Asahi UEFI environment",
            "Select `Macintosh HD` and let macOS boot normally",
            "sudo scripts/install-esp.sh install --disk diskXsY --image dist/j313/boot.bin",
            "create partition msr size=16",
        ))
        self.assertEqual(order, tuple(sorted(order)))

    def test_run_guide_preserves_both_modes_and_truthful_status(self):
        text = (ROOT / "documentation/RUN.md").read_text(encoding="utf-8")
        standalone = text.index("## Standalone mode")
        assisted = text.index("## Assisted development mode")
        self.assertLess(standalone, assisted)
        for token in (
            "second Mac is optional",
            "scripts/run-assisted.sh",
            "scripts/log-assisted.sh",
            "scripts/display-assisted.sh",
            "chainload.py",
            "reader-before-guest",
        ):
            self.assertIn(token, text)
        self.assertIn("hardware validation pending", text.lower())

    def test_debugging_guide_indexes_kd_and_framebuffer_tools(self):
        text = (ROOT / "documentation/DEBUGGING.md").read_text(encoding="utf-8")
        for token in (
            "virtual UART",
            "virtual framebuffer",
            "KD",
            "kd_devnodes.py",
            "kd_reboot.py",
            "frame rate",
            "Bad Command",
            "one proxy owner",
        ):
            self.assertIn(token, text)

    def test_standalone_monitor_workflow_is_explicit_and_abi_safe(self):
        paths = (
            ROOT / "documentation/CONFIGURATION.md",
            ROOT / "documentation/RUN.md",
            ROOT / "documentation/DEBUGGING.md",
            ROOT / "documentation/BUILD.md",
        )
        text = "\n".join(path.read_text(encoding="utf-8") for path in paths)
        for token in (
            "debug=monitor",
            "scripts/build-standalone.sh --display physical --debug monitor",
            "scripts/log-standalone.sh --output standalone-monitor-logs",
            "generation-001",
            "manifest ABI",
            "boot-physical-monitor.bin",
            "boot-physical-production.bin",
            "attach after Windows has started",
            "verbose synchronous USB logging",
            "USB backpressure",
            "production profile",
            "tools/kd/kd_liveness.py",
            "sudo scripts/install-esp.sh restore --disk",
        ):
            self.assertIn(token, text)
        self.assertIn("always starts Windows", text)
        self.assertIn("never enters the proxy loop", text)
        self.assertIn("diagnostic profile", text)
        self.assertIn("does not prove that Windows crashed", text)

    def test_kd_tools_have_one_canonical_directory(self):
        names = {
            "kd_acpi.py",
            "kd_continue.py",
            "kd_devnodes.py",
            "kd_diag.py",
            "kd_liveness.py",
            "kd_modules.py",
            "kd_peek.py",
            "kd_proclist.py",
            "kd_reboot.py",
            "kd_stack.py",
            "kd_threads.py",
            "kd_wait_bugcheck.py",
            "kd_watchdog.py",
        }
        self.assertEqual(names, {path.name for path in (ROOT / "tools/kd").glob("kd_*.py")})
        self.assertEqual([], list(ROOT.glob("kd_*.py")))

        public_text = "\n".join(
            path.read_text(encoding="utf-8")
            for path in (
                ROOT / "README.md",
                ROOT / "documentation/RUN.md",
                ROOT / "documentation/DEBUGGING.md",
                ROOT / "scripts/run-assisted.sh",
            )
        )
        self.assertNotIn("M" + "4", public_text)

    def test_clone_commands_use_the_public_repository(self):
        text = "\n".join(
            (ROOT / "documentation" / name).read_text(encoding="utf-8")
            for name in DOCS
        )
        self.assertIn(
            "git clone --recurse-submodules https://github.com/paulsmir/windows-on-m1.git",
            text,
        )
        self.assertIn("cd windows-on-m1", text)
        self.assertNotIn("windows" + "_m1", text)


if __name__ == "__main__":
    unittest.main()
