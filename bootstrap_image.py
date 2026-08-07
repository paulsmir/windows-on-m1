"""Pack and inspect the outer m1n1 self-chainload bootstrap image."""

from dataclasses import dataclass
import binascii
import lzma
import struct

from launch_profile import profile_from_manifest_flags


BOOTSTRAP_MAGIC = b"ASIBOOT0"
BOOTSTRAP_FORMAT_VERSION = 1
BOOTSTRAP_ALIGNMENT = 0x4000
BOOTSTRAP_HEADER_SIZE = 64
_BOOTSTRAP = struct.Struct("<8sHHIIQQQII12s")


class BootstrapImageError(ValueError):
    """The outer bootstrap image does not satisfy its binary contract."""


@dataclass(frozen=True)
class BootstrapManifest:
    manifest_offset: int
    format_version: int
    header_size: int
    flags: int
    payload_offset: int
    compressed_size: int
    uncompressed_size: int
    crc32: int


def pack_bootstrap(stage0: bytes, inner: bytes, flags: int) -> bytes:
    """Return Stage 0 followed by a validated, compressed inner boot image."""
    if not stage0:
        raise BootstrapImageError("Stage 0 image is empty")
    if len(stage0) % BOOTSTRAP_ALIGNMENT:
        raise BootstrapImageError("Stage 0 image is not 16 KiB aligned")
    if not inner:
        raise BootstrapImageError("inner image is empty")
    try:
        profile_from_manifest_flags(flags)
    except ValueError as exc:
        raise BootstrapImageError(f"unsupported bootstrap flags {flags:#x}") from exc

    compressed = lzma.compress(inner, format=lzma.FORMAT_XZ)
    crc32 = binascii.crc32(inner) & 0xFFFFFFFF
    header = _BOOTSTRAP.pack(
        BOOTSTRAP_MAGIC,
        BOOTSTRAP_FORMAT_VERSION,
        BOOTSTRAP_HEADER_SIZE,
        flags,
        0,
        BOOTSTRAP_ALIGNMENT,
        len(compressed),
        len(inner),
        crc32,
        0,
        b"\0" * 12,
    )
    return b"".join(
        (
            stage0,
            header,
            b"\0" * (BOOTSTRAP_ALIGNMENT - len(header)),
            compressed,
        )
    )

def _manifest_offsets(image: bytes) -> list[int]:
    return [
        offset
        for offset in range(0, len(image) - len(BOOTSTRAP_MAGIC) + 1, BOOTSTRAP_ALIGNMENT)
        if image.startswith(BOOTSTRAP_MAGIC, offset)
    ]


def parse_bootstrap(image: bytes) -> tuple[BootstrapManifest, bytes]:
    """Validate an outer bootstrap image and return its decoded inner image."""
    offsets = _manifest_offsets(image)
    if not offsets:
        raise BootstrapImageError("bootstrap manifest not found")
    if len(offsets) != 1:
        raise BootstrapImageError("multiple bootstrap manifests found")

    manifest_offset = offsets[0]
    if len(image) - manifest_offset < BOOTSTRAP_HEADER_SIZE:
        raise BootstrapImageError("bootstrap manifest is truncated")
    (
        magic,
        format_version,
        header_size,
        flags,
        reserved,
        payload_offset,
        compressed_size,
        uncompressed_size,
        crc32,
        reserved2,
        reserved_tail,
    ) = _BOOTSTRAP.unpack_from(image, manifest_offset)

    if magic != BOOTSTRAP_MAGIC:
        raise BootstrapImageError("invalid bootstrap magic")
    if format_version != BOOTSTRAP_FORMAT_VERSION:
        raise BootstrapImageError(f"unsupported bootstrap version {format_version}")
    if header_size != BOOTSTRAP_HEADER_SIZE:
        raise BootstrapImageError(f"invalid bootstrap header size {header_size}")
    try:
        profile_from_manifest_flags(flags)
    except ValueError as exc:
        raise BootstrapImageError(f"unsupported bootstrap flags {flags:#x}") from exc
    if reserved or reserved2 or reserved_tail != b"\0" * 12:
        raise BootstrapImageError("nonzero bootstrap reserved field")
    if payload_offset < header_size or payload_offset % BOOTSTRAP_ALIGNMENT:
        raise BootstrapImageError("bootstrap payload offset is not 16 KiB aligned")
    if not compressed_size or not uncompressed_size:
        raise BootstrapImageError("bootstrap payload size is zero")

    payload_start = manifest_offset + payload_offset
    payload_end = payload_start + compressed_size
    if payload_start > len(image) or payload_end != len(image):
        raise BootstrapImageError("bootstrap payload bounds do not match the image")
    if any(image[manifest_offset + header_size : payload_start]):
        raise BootstrapImageError("bootstrap manifest padding is not zero")

    try:
        inner = lzma.decompress(image[payload_start:payload_end], format=lzma.FORMAT_XZ)
    except lzma.LZMAError as exc:
        raise BootstrapImageError(f"bootstrap payload decompression failed: {exc}") from exc
    if len(inner) != uncompressed_size:
        raise BootstrapImageError("bootstrap payload uncompressed size mismatch")
    if (binascii.crc32(inner) & 0xFFFFFFFF) != crc32:
        raise BootstrapImageError("bootstrap payload CRC mismatch")

    return (
        BootstrapManifest(
            manifest_offset=manifest_offset,
            format_version=format_version,
            header_size=header_size,
            flags=flags,
            payload_offset=payload_offset,
            compressed_size=compressed_size,
            uncompressed_size=uncompressed_size,
            crc32=crc32,
        ),
        inner,
    )
