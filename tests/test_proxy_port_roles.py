import unittest

from tools.proxy_port_roles import parse_role_output, select_proxy_and_vuart


class ProxyPortRoleTests(unittest.TestCase):
    def test_selects_the_only_port_that_answers_proxy_nop(self):
        proxy, vuart = select_proxy_and_vuart(
            ["/dev/cu.usbmodem41", "/dev/cu.usbmodem43"],
            lambda path: path.endswith("43"),
        )

        self.assertEqual(proxy, "/dev/cu.usbmodem43")
        self.assertEqual(vuart, "/dev/cu.usbmodem41")

    def test_rejects_ambiguous_or_missing_proxy(self):
        with self.assertRaisesRegex(RuntimeError, "exactly one"):
            select_proxy_and_vuart(["a", "b"], lambda _path: False)
        with self.assertRaisesRegex(RuntimeError, "exactly one"):
            select_proxy_and_vuart(["a", "b"], lambda _path: True)

    def test_parser_ignores_tty_noise_and_requires_usbmodem_paths(self):
        proxy, vuart = parse_role_output(
            "TTY> Running proxy:\n"
            "/dev/cu.usbmodem43\n"
            "early firmware message\n"
            "/dev/cu.usbmodem41\n"
        )

        self.assertEqual(proxy, "/dev/cu.usbmodem43")
        self.assertEqual(vuart, "/dev/cu.usbmodem41")

        with self.assertRaisesRegex(RuntimeError, "two USB modem paths"):
            parse_role_output("TTY> Running proxy:\n/dev/cu.usbmodem43\n")


if __name__ == "__main__":
    unittest.main()
