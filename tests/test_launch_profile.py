import unittest


class LaunchProfileTests(unittest.TestCase):
    def load_api(self):
        import launch_profile

        return launch_profile

    def test_defaults_are_quiet_standalone_physical(self):
        api = self.load_api()

        profile = api.parse_profile()

        self.assertEqual(profile.execution.value, "standalone")
        self.assertEqual(profile.display.value, "physical")
        self.assertEqual(profile.debug.value, "off")
        self.assertTrue(profile.physical_display)
        self.assertFalse(profile.virtual_display)
        self.assertFalse(profile.capture_uart)
        self.assertFalse(profile.telemetry)
        self.assertFalse(profile.proxy_takeover)
        self.assertEqual(profile.manifest_flags, 0x1)

    def test_display_modes_derive_independent_consumers(self):
        api = self.load_api()
        expected = {
            "physical": (True, False, 0x1),
            "virtual": (False, True, 0x2),
            "both": (True, True, 0x3),
            "none": (False, False, 0x0),
        }

        for name, want in expected.items():
            with self.subTest(name=name):
                profile = api.parse_profile(display=name)
                self.assertEqual(
                    (profile.physical_display, profile.virtual_display, profile.manifest_flags),
                    want,
                )

    def test_debug_modes_derive_workers_and_manifest_bits(self):
        api = self.load_api()
        expected = {
            "off": (False, False, False, 0x1),
            "uart": (True, False, True, 0x5),
            "full": (True, True, True, 0x9),
            "monitor": (True, True, False, 0x11),
        }

        for name, want in expected.items():
            with self.subTest(name=name):
                profile = api.parse_profile(debug=name)
                self.assertEqual(
                    (
                        profile.capture_uart,
                        profile.telemetry,
                        profile.proxy_takeover,
                        profile.manifest_flags,
                    ),
                    want,
                )

    def test_manifest_round_trip_preserves_display_and_debug(self):
        api = self.load_api()

        for display in ("none", "physical", "virtual", "both"):
            for debug in ("off", "uart", "full", "monitor"):
                with self.subTest(display=display, debug=debug):
                    profile = api.parse_profile(display=display, debug=debug)
                    decoded = api.profile_from_manifest_flags(profile.manifest_flags)
                    self.assertEqual(decoded, profile)

    def test_invalid_profile_values_and_manifest_bits_are_rejected(self):
        api = self.load_api()

        for kwargs in (
            {"execution": "remote"},
            {"display": "mirror"},
            {"debug": "yes"},
        ):
            with self.subTest(kwargs=kwargs):
                with self.assertRaises(ValueError):
                    api.parse_profile(**kwargs)

        for flags in (0xC, 0x14, 0x18, 0x1C, 0x200, 0x10000, 0xD001, 0xF001, 0xFFFFFFFF):
            with self.subTest(flags=flags):
                with self.assertRaises(ValueError):
                    api.profile_from_manifest_flags(flags)


    def test_working_profile_is_the_exp2481_bench_launch(self):
        api = self.load_api()

        profile = api.working_profile(api.parse_profile())
        self.assertTrue(profile.ans_direct)
        self.assertTrue(profile.agx_g2_broker)
        self.assertTrue(profile.cppc)
        self.assertTrue(profile.expanded_ram)
        self.assertEqual(profile.pcpu_pstate, 12)
        self.assertEqual(profile.display.value, "physical")
        self.assertEqual(profile.debug.value, "off")
        self.assertEqual(profile.manifest_flags, 0xC1E1)
        self.assertEqual(api.profile_from_manifest_flags(0xC1E1), profile)
        # The historical profiles keep their flag values.
        self.assertEqual(api.parse_profile().manifest_flags, 0x1)
        self.assertFalse(api.profile_from_manifest_flags(0x1).ans_direct)

    def test_working_knobs_round_trip_with_every_display_and_debug(self):
        api = self.load_api()

        for display in ("none", "physical", "virtual", "both"):
            for debug in ("off", "uart", "full", "monitor"):
                for pstate in (0, 1, 7, 12):
                    profile = api.working_profile(api.parse_profile(display=display, debug=debug))
                    from dataclasses import replace
                    profile = replace(profile, pcpu_pstate=pstate)
                    with self.subTest(display=display, debug=debug, pstate=pstate):
                        self.assertEqual(
                            api.profile_from_manifest_flags(profile.manifest_flags), profile)
        with self.assertRaises(ValueError):
            api.LaunchProfile(api.Execution.STANDALONE, api.Display.PHYSICAL, api.Debug.OFF,
                              pcpu_pstate=13)

    def test_python_flag_bits_match_the_native_manifest_header(self):
        import re
        from pathlib import Path

        api = self.load_api()
        header = (Path(__file__).resolve().parents[1] /
                  "m1n1_windows/src/hv_autonomous_manifest.h").read_text()

        def integer(name):
            match = re.search(rf"^#define {name}\s+(0x[0-9a-fA-F]+|[0-9]+)u?\b", header, re.M)
            self.assertIsNotNone(match, f"missing C constant {name}")
            return int(match.group(1), 0)

        self.assertEqual(integer("HV_AUTONOMOUS_ANS_DIRECT"), api.ANS_DIRECT_FLAG)
        self.assertEqual(integer("HV_AUTONOMOUS_AGX_G2_BROKER"), api.AGX_G2_BROKER_FLAG)
        self.assertEqual(integer("HV_AUTONOMOUS_CPPC"), api.CPPC_FLAG)
        self.assertEqual(integer("HV_AUTONOMOUS_EXPANDED_RAM"), api.EXPANDED_RAM_FLAG)
        self.assertEqual(integer("HV_AUTONOMOUS_PCPU_PSTATE_SHIFT"), api.PCPU_PSTATE_SHIFT)
        self.assertEqual(integer("HV_AUTONOMOUS_PCPU_PSTATE_MASK"), api.PCPU_PSTATE_MASK)
        self.assertEqual(integer("HV_AUTONOMOUS_PCPU_PSTATE_MAX"), api.PCPU_PSTATE_MAX)
        self.assertEqual(integer("HV_AUTONOMOUS_KNOWN_FLAGS"), api._KNOWN_FLAGS)


if __name__ == "__main__":
    unittest.main()
