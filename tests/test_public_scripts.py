import os
import subprocess
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


class PublicScriptTests(unittest.TestCase):
    def test_m1n1_build_configuration_is_portable_and_rebuilds_dependents(self):
        makefile = (ROOT / "m1n1_windows" / "Makefile").read_text(encoding="utf-8")

        # Apple's make does not implement GNU make grouped targets (`&:`).  A stale
        # build_cfg.h can silently mix release and diagnostic objects, invalidating
        # every hardware A/B test.
        self.assertNotIn("build-cfg src/../build/build_cfg.h &:", makefile)
        self.assertNotIn("build-tag src/../build/build_tag.h &:", makefile)
        self.assertIn("build/%.o: src/%.c build/build_tag.h build/build_cfg.h", makefile)
        self.assertIn("build/build_cfg.h: FORCE", makefile)

    def test_assisted_scripts_exist_and_do_not_embed_private_devices(self):
        names = (
            "build-development.sh",
            "run-assisted.sh",
            "reset-assisted.sh",
            "display-assisted.sh",
            "log-assisted.sh",
            "supervise-assisted.sh",
        )
        for name in names:
            path = ROOT / "scripts" / name
            self.assertTrue(path.is_file(), name)
            text = path.read_text(encoding="utf-8")
            self.assertNotIn("/Users/pavel", text)
            self.assertNotIn("C02HDNCCQ6L41", text)
            self.assertNotIn("C02HDNCCQ6L43", text)
            self.assertIn('dirname -- "$0"', text)

    def test_assisted_supervisor_dry_run_is_bounded_and_uses_public_artifacts(self):
        result = subprocess.run(
            [
                "sh", str(ROOT / "scripts/supervise-assisted.sh"),
                "--ssh-host", "192.0.2.10", "--ssh-user", "tester",
                "--max-generations", "3", "--dry-run",
            ],
            cwd="/tmp", capture_output=True, text=True, check=False,
        )
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("dist/j313/debug-forensic/m1n1.macho", result.stdout)
        self.assertIn("dist/j313/debug-forensic/J313_EFI.fd", result.stdout)
        self.assertIn("max generations: 3", result.stdout)
        self.assertIn("snapshot signal: SIGINT", result.stdout)
        self.assertIn("recovery policy: captured read-only after SSH-ready", result.stdout)
        self.assertNotIn("/Users/pavel", result.stdout)

    def test_assisted_supervisor_captures_recovery_policy_without_mutating_it(self):
        text = (ROOT / "scripts/supervise-assisted.sh").read_text(encoding="utf-8")
        self.assertIn("recovery-policy.log", text)
        self.assertIn("bcdedit /enum", text)
        result = subprocess.run(
            [
                "sh", str(ROOT / "scripts/supervise-assisted.sh"),
                "--ssh-host", "192.0.2.10", "--dry-run",
            ],
            cwd=ROOT, check=False, capture_output=True, text=True,
        )
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("recovery policy: captured read-only after SSH-ready", result.stdout)

    def test_assisted_supervisor_can_explicitly_disable_automatic_recovery(self):
        result = subprocess.run(
            [
                "sh", str(ROOT / "scripts/supervise-assisted.sh"),
                "--ssh-host", "192.0.2.10",
                "--recovery-policy", "disable-auto", "--dry-run",
            ],
            cwd=ROOT, check=False, capture_output=True, text=True,
        )
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("recovery policy: disable-auto after SSH-ready", result.stdout)

        text = (ROOT / "scripts/supervise-assisted.sh").read_text(encoding="utf-8")
        self.assertIn("bcdedit /export", text)
        self.assertIn("recoveryenabled No", text)
        self.assertIn("bootstatuspolicy IgnoreAllFailures", text)
        self.assertIn("recovery-policy-before.log", text)
        self.assertIn("recovery-policy-after.log", text)

    def test_assisted_supervisor_refuses_a_second_usb_owner(self):
        text = (ROOT / "scripts/supervise-assisted.sh").read_text(encoding="utf-8")
        self.assertIn('LOCK_DIR="$ROOT/.local/platform-stability/supervisor.lock"', text)
        self.assertIn('mkdir "$LOCK_DIR"', text)
        self.assertIn('printf \'%s\\n\' "$$" >"$LOCK_DIR/pid"', text)
        self.assertIn('kill -0 "$lock_pid"', text)
        self.assertIn('trap cleanup_lock EXIT', text)
        self.assertIn('trap terminate_supervisor HUP INT TERM', text)
        self.assertIn('exit 130', text)
        self.assertIn('another assisted supervisor already owns the USB boot path', text)

    def test_standalone_monitor_wrapper_is_location_independent_and_dry_run_safe(self):
        path = ROOT / "scripts/log-standalone.sh"
        self.assertTrue(path.is_file())
        text = path.read_text(encoding="utf-8")
        self.assertIn('dirname -- "$0"', text)
        self.assertNotIn("/Users/pavel", text)
        self.assertNotIn("C02HDNCCQ6L41", text)

        result = subprocess.run(
            [
                "sh",
                str(path),
                "--console",
                "/dev/cu.test-console",
                "--vuart",
                "/dev/cu.test-vuart",
                "--output",
                "test-captures",
                "--dry-run",
            ],
            cwd="/tmp",
            check=False,
            capture_output=True,
            text=True,
        )

        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("tools/standalone_monitor.py", result.stdout)
        self.assertIn("--console /dev/cu.test-console", result.stdout)
        self.assertIn("--vuart /dev/cu.test-vuart", result.stdout)
        self.assertIn("--output test-captures", result.stdout)

    def test_development_build_dry_run_names_replaceable_components(self):
        result = subprocess.run(
            ["sh", str(ROOT / "scripts/build-development.sh"), "--dry-run"],
            cwd=ROOT,
            check=True,
            capture_output=True,
            text=True,
        )
        self.assertIn("m1n1.macho", result.stdout)
        self.assertIn("J313_EFI.fd", result.stdout)
        self.assertIn("chainload.py", result.stdout)

    def test_development_build_forwards_standalone_profile(self):
        result = subprocess.run(
            [
                "sh", str(ROOT / "scripts/build-development.sh"),
                "--dry-run", "--display", "virtual", "--debug", "uart",
            ],
            cwd=ROOT,
            check=False,
            capture_output=True,
            text=True,
        )

        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("--display virtual --debug uart", result.stdout)

    def test_builds_and_launches_use_separate_profile_directories(self):
        release = subprocess.run(
            ["sh", str(ROOT / "scripts/build-development.sh"), "--dry-run", "--release"],
            cwd=ROOT, check=True, capture_output=True, text=True,
        )
        debug = subprocess.run(
            ["sh", str(ROOT / "scripts/build-development.sh"), "--dry-run"],
            cwd=ROOT, check=True, capture_output=True, text=True,
        )
        quiet = subprocess.run(
            ["sh", str(ROOT / "scripts/run-assisted.sh"), "--dry-run", "--debug", "off"],
            cwd=ROOT, check=True, capture_output=True, text=True,
        )
        self.assertIn("dist/j313/release/m1n1.macho", release.stdout)
        self.assertIn("dist/j313/debug-uart/m1n1.macho", debug.stdout)
        self.assertIn("dist/j313/release/J313_EFI.fd", quiet.stdout)

    def test_run_assisted_dry_run_describes_order_and_selected_paths(self):
        command = [
            "sh",
            str(ROOT / "scripts/run-assisted.sh"),
            "--dry-run",
            "--proxy",
            "/dev/cu.test-proxy",
            "--vuart",
            "/dev/cu.test-vuart",
            "--firmware",
            "firmware/test.fd",
            "--ramdisk",
            "images/test.img",
            "--contract-output",
            ".local/contracts/test/capture.bin",
        ]
        result = subprocess.run(command, cwd=ROOT, check=True, capture_output=True, text=True)
        self.assertIn("reader-before-guest", result.stdout)
        self.assertIn("/dev/cu.test-proxy", result.stdout)
        self.assertIn("/dev/cu.test-vuart", result.stdout)
        self.assertIn("firmware/test.fd", result.stdout)
        self.assertIn("images/test.img", result.stdout)
        self.assertIn(".local/contracts/test/capture.bin", result.stdout)

    def test_assisted_workers_are_detached_from_launcher_shell(self):
        text = (ROOT / "scripts/run-assisted.sh").read_text(encoding="utf-8")

        self.assertGreaterEqual(text.count("nohup "), 2)
        self.assertGreaterEqual(text.count("</dev/null"), 2)
        self.assertIn("tools/proxy_port_roles.py", text)
        self.assertIn("runner exited before initialization", text)
        self.assertIn("m1n1.macho=assisted-chainload", text)

    def test_release_runner_keeps_a_host_bootstrap_log(self):
        text = (ROOT / "scripts/run-assisted.sh").read_text(encoding="utf-8")

        self.assertIn("assisted-runner.log", text)
        self.assertNotIn(
            'nohup "$PYTHON" -u "$ROOT/run_uefi.py" "$@" </dev/null >/dev/null 2>&1 &',
            text,
        )

    def test_assisted_launcher_waits_for_guest_handoff(self):
        text = (ROOT / "scripts/run-assisted.sh").read_text(encoding="utf-8")

        self.assertIn("Starting guest...", text)
        self.assertIn("runner did not reach guest handoff", text)
        self.assertIn("BOOTSTRAP_TIMEOUT", text)

    def test_assisted_launcher_rejects_failed_hardware_gates(self):
        text = (ROOT / "scripts/run-assisted.sh").read_text(encoding="utf-8")

        self.assertIn("Apple ANS initialization failed", text)
        self.assertIn("backend=0", text)
        self.assertIn("secondary CPU startup failed", text)
        self.assertIn("runner failed a hardware bootstrap gate", text)

    def test_assisted_foreground_keeps_runner_owned_and_observable(self):
        result = subprocess.run(
            [
                "sh", str(ROOT / "scripts/run-assisted.sh"),
                "--dry-run", "--foreground",
                "--proxy", "/dev/cu.test-proxy",
                "--vuart", "/dev/cu.test-vuart",
                "--display", "both", "--debug", "monitor",
            ],
            cwd=ROOT, check=True, capture_output=True, text=True,
        )
        self.assertIn("execution: foreground", result.stdout)
        self.assertIn("USB framebuffer: enabled", result.stdout)

        source = (ROOT / "scripts/run-assisted.sh").read_text(encoding="utf-8")
        self.assertIn('--foreground) FOREGROUND=1', source)
        self.assertIn('exec "$PYTHON" -u "$ROOT/run_uefi.py"', source)
        self.assertIn('>hv.log 2>&1', source)
        self.assertNotIn('| tee', source)

    def test_assisted_launcher_requires_cpufreq_preflight(self):
        text = (ROOT / "run_uefi.py").read_text(encoding="utf-8")

        self.assertIn("p.cpufreq_init()", text)
        self.assertIn("CPU frequency preflight", text)

    def test_assisted_sigint_uses_pre_rendezvous_snapshot_and_continues(self):
        text = (ROOT / "run_uefi.py").read_text(encoding="utf-8")

        self.assertNotIn("p.hv_watchdog_dump()", text)
        self.assertIn("HOST CONTROL: diagnostic snapshot captured; continuing guest", text)
        self.assertIn("return EXC_RET.HANDLED", text)
        self.assertIn("if snapshot_reboot_requested:", text)
        self.assertNotIn("hv.run_shell = lambda *a, **k: True", text)

    def test_assisted_sigterm_is_the_explicit_snapshot_and_reboot_action(self):
        text = (ROOT / "run_uefi.py").read_text(encoding="utf-8")

        self.assertIn("def request_snapshot_reboot", text)
        self.assertIn("signal.signal(signal.SIGTERM, request_snapshot_reboot)", text)
        self.assertIn("HOST CONTROL: diagnostic snapshot captured; rebooting Air", text)
        self.assertIn("return EXC_RET.UNHANDLED", text)
        self.assertIn("reboot signal: SIGTERM (explicit hardware reboot)",
                      (ROOT / "scripts" / "supervise-assisted.sh").read_text(encoding="utf-8"))

    def test_supervisor_documents_snapshot_as_non_destructive(self):
        source = (ROOT / "scripts" / "supervise-assisted.sh").read_text(encoding="utf-8")

        self.assertIn("snapshot signal: SIGINT (guest continues)", source)
        self.assertNotIn("snapshot signal: SIGINT to the sole run_uefi.py owner", source)

    def test_display_and_log_dry_runs_are_hardware_free(self):
        cases = {
            "display-assisted.sh": "http://127.0.0.1:8766/",
            "log-assisted.sh": "http://127.0.0.1:8765/",
        }
        for name, expected in cases.items():
            result = subprocess.run(
                ["sh", str(ROOT / "scripts" / name), "--dry-run"],
                cwd=ROOT,
                check=True,
                capture_output=True,
                text=True,
                env={**os.environ, "PATH": "/usr/bin:/bin:/usr/sbin:/sbin"},
            )
            self.assertIn(expected, result.stdout)

    def test_run_windows_defaults_to_quiet_standalone_physical(self):
        result = subprocess.run(
            ["sh", str(ROOT / "scripts/run-windows.sh"), "--dry-run"],
            cwd=ROOT,
            check=True,
            capture_output=True,
            text=True,
        )

        self.assertIn("execution: standalone", result.stdout)
        self.assertIn("display: physical", result.stdout)
        self.assertIn("debug: off", result.stdout)
        self.assertIn("virtual UART: disabled", result.stdout)
        self.assertIn("USB framebuffer: disabled", result.stdout)
        self.assertIn("telemetry: disabled", result.stdout)

    def test_quiet_assisted_physical_does_not_require_vuart(self):
        result = subprocess.run(
            [
                "sh",
                str(ROOT / "scripts/run-assisted.sh"),
                "--dry-run",
                "--proxy",
                "/dev/cu.test-proxy",
                "--display",
                "physical",
                "--debug",
                "off",
            ],
            cwd=ROOT,
            check=True,
            capture_output=True,
            text=True,
        )

        self.assertIn("display: physical", result.stdout)
        self.assertIn("debug: off", result.stdout)
        self.assertIn("virtual UART: disabled", result.stdout)
        self.assertIn("USB framebuffer: disabled", result.stdout)
        self.assertIn("telemetry: disabled", result.stdout)
        self.assertNotIn("reader-before-guest", result.stdout)

    def test_monitor_assisted_uses_debug_artifacts_without_full_telemetry(self):
        result = subprocess.run(
            [
                "sh", str(ROOT / "scripts/run-assisted.sh"),
                "--dry-run", "--proxy", "/dev/cu.test-proxy",
                "--vuart", "/dev/cu.test-vuart", "--display", "physical",
                "--debug", "monitor",
            ],
            cwd=ROOT, check=False, capture_output=True, text=True,
        )
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("debug: monitor", result.stdout)
        self.assertIn("telemetry: disabled", result.stdout)
        self.assertIn("dist/j313/debug-monitor/J313_EFI.fd", result.stdout)

    def test_supervisor_selects_forensic_or_monitor_diagnostics(self):
        for diagnostics, wire_mode in (("forensic", "full"), ("monitor", "monitor")):
            result = subprocess.run(
                [
                    "sh", str(ROOT / "scripts/supervise-assisted.sh"),
                    "--ssh-host", "192.0.2.10", "--diagnostics", diagnostics,
                    "--dry-run",
                ],
                cwd=ROOT, check=False, capture_output=True, text=True,
            )
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertIn(f"diagnostics: {diagnostics} (artifact debug={wire_mode})",
                          result.stdout)
            expected_dir = "debug-forensic" if diagnostics == "forensic" else "debug-monitor"
            self.assertIn(f"dist/j313/{expected_dir}", result.stdout)

    def test_both_full_resolves_every_observer(self):
        result = subprocess.run(
            [
                "sh",
                str(ROOT / "scripts/run-windows.sh"),
                "--execution",
                "assisted",
                "--display",
                "both",
                "--debug",
                "full",
                "--proxy",
                "/dev/cu.test-proxy",
                "--vuart",
                "/dev/cu.test-vuart",
                "--dry-run",
            ],
            cwd=ROOT,
            check=True,
            capture_output=True,
            text=True,
        )

        self.assertIn("execution: assisted", result.stdout)
        self.assertIn("display: both", result.stdout)
        self.assertIn("debug: full", result.stdout)
        self.assertIn("virtual UART: /dev/cu.test-vuart", result.stdout)
        self.assertIn("USB framebuffer: enabled", result.stdout)
        self.assertIn("telemetry: enabled", result.stdout)
        self.assertIn("chainload: dist/j313/debug-forensic/m1n1.macho", result.stdout)

    def test_assisted_public_entrypoint_chainloads_by_default(self):
        result = subprocess.run(
            [
                "sh", str(ROOT / "scripts/run-windows.sh"),
                "--execution", "assisted",
                "--display", "physical",
                "--debug", "off",
                "--proxy", "/dev/cu.test-proxy",
                "--dry-run",
            ],
            cwd=ROOT,
            check=False,
            capture_output=True,
            text=True,
        )

        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("chainload: dist/j313/release/m1n1.macho", result.stdout)

    def test_assisted_proxy_reuse_requires_explicit_flag(self):
        result = subprocess.run(
            [
                "sh", str(ROOT / "scripts/run-windows.sh"),
                "--execution", "assisted",
                "--display", "physical",
                "--debug", "off",
                "--proxy", "/dev/cu.test-proxy",
                "--reuse-proxy",
                "--dry-run",
            ],
            cwd=ROOT,
            check=False,
            capture_output=True,
            text=True,
        )

        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("chainload: disabled (explicit proxy reuse)", result.stdout)

    def test_chainload_and_proxy_reuse_are_mutually_exclusive(self):
        result = subprocess.run(
            [
                "sh", str(ROOT / "scripts/run-windows.sh"),
                "--execution", "assisted",
                "--chainload", "--reuse-proxy", "--dry-run",
            ],
            cwd=ROOT,
            check=False,
            capture_output=True,
            text=True,
        )

        self.assertEqual(result.returncode, 2)

    def test_assisted_dry_run_can_include_matching_m1n1_chainload(self):
        result = subprocess.run(
            [
                "sh", str(ROOT / "scripts/run-windows.sh"),
                "--execution", "assisted",
                "--display", "both",
                "--debug", "full",
                "--proxy", "/dev/cu.test-proxy",
                "--vuart", "/dev/cu.test-vuart",
                "--chainload",
                "--m1n1", "dist/j313/test.macho",
                "--dry-run",
            ],
            cwd=ROOT,
            check=False,
            capture_output=True,
            text=True,
        )

        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("chainload: dist/j313/test.macho", result.stdout)

    def test_launchers_reject_unknown_profile_values(self):
        cases = (
            ("--execution", "remote"),
            ("--display", "mirror"),
            ("--debug", "yes"),
        )
        for option, value in cases:
            with self.subTest(option=option, value=value):
                result = subprocess.run(
                    [
                        "sh",
                        str(ROOT / "scripts/run-windows.sh"),
                        option,
                        value,
                        "--dry-run",
                    ],
                    cwd=ROOT,
                    capture_output=True,
                    text=True,
                )
                self.assertEqual(result.returncode, 2)


if __name__ == "__main__":
    unittest.main()
