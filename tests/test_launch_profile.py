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

        for flags in (0xC, 0x14, 0x18, 0x1C, 0x20, 0xFFFFFFFF):
            with self.subTest(flags=flags):
                with self.assertRaises(ValueError):
                    api.profile_from_manifest_flags(flags)


if __name__ == "__main__":
    unittest.main()
