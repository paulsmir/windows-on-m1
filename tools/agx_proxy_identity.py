"""Read the immutable device-side identity of one m1n1 boot."""

from dataclasses import dataclass


class ProxyIdentityError(RuntimeError):
    """The live proxy did not provide a valid boot identity."""


@dataclass(frozen=True)
class ProxyBootIdentity:
    platform: str
    firmware: str
    boot_cookie: int
    m1n1_base: int
    proxy_identity: str


def read_proxy_boot_identity(u) -> ProxyBootIdentity:
    platform = getattr(getattr(u, "adt", None), "target_type", None)
    firmware = getattr(u, "version", None)
    base = getattr(u, "base", None)
    if not isinstance(platform, str) or not platform:
        raise ProxyIdentityError("proxy platform identity is unavailable")
    if not isinstance(firmware, str) or not firmware:
        raise ProxyIdentityError("proxy firmware identity is unavailable")
    if isinstance(base, bool) or not isinstance(base, int) or base <= 0:
        raise ProxyIdentityError("m1n1 base must be a positive integer")
    getter = getattr(u, "get_boot_cookie", None)
    if getter is None:
        raise ProxyIdentityError("proxy firmware has no boot cookie API")
    cookie = getter()
    if isinstance(cookie, bool) or not isinstance(cookie, int) or cookie <= 0:
        raise ProxyIdentityError("proxy boot cookie must be a positive integer")
    return ProxyBootIdentity(
        platform=platform,
        firmware=firmware,
        boot_cookie=cookie,
        m1n1_base=base,
        proxy_identity=f"{platform}:{firmware}:{cookie:016x}",
    )
