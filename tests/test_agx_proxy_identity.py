from types import SimpleNamespace
import unittest


class FakeProxy:
    def __init__(self, cookie):
        self.adt = SimpleNamespace(target_type="J313")
        self.version = "V13_5"
        self.base = 0x804000000
        self.cookie = cookie

    def get_boot_cookie(self):
        return self.cookie


class ProxyBootIdentityTests(unittest.TestCase):
    def test_identity_is_derived_from_device_boot_cookie_not_load_base(self):
        from tools.agx_proxy_identity import read_proxy_boot_identity

        identity = read_proxy_boot_identity(FakeProxy(0x123456789abcdef0))

        self.assertEqual(identity.boot_cookie, 0x123456789abcdef0)
        self.assertEqual(identity.m1n1_base, 0x804000000)
        self.assertEqual(
            identity.proxy_identity,
            "J313:V13_5:123456789abcdef0",
        )

    def test_zero_cookie_is_rejected(self):
        from tools.agx_proxy_identity import ProxyIdentityError, read_proxy_boot_identity

        with self.assertRaisesRegex(ProxyIdentityError, "boot cookie"):
            read_proxy_boot_identity(FakeProxy(0))


if __name__ == "__main__":
    unittest.main()
