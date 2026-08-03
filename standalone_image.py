"""Pack and inspect the self-contained J313 m1n1 + Mu boot image."""

from dataclasses import dataclass
import binascii
import lzma
import struct


IMAGE_MAGIC = b"ASIWINGU"
FORMAT_VERSION = 1
IMAGE_ALIGNMENT = 0x4000
MANIFEST_SIZE = 64
_SUPPORTED_LAYOUT_VERSION = 1
_MANIFEST = struct.Struct("<8sHHIIIQQQII8s")


class ImageError(ValueError):
    """The standalone image does not satisfy the public binary contract."""


@dataclass(frozen=True)
class Manifest:
    manifest_offset: int
    format_version: int
    header_size: int
    flags: int
    layout_version: int
    payload_offset: int
    compressed_size: int
    uncompressed_size: int
    crc32: int


def _align_up(value: int, alignment: int = IMAGE_ALIGNMENT) -> int:
    return (value + alignment - 1) & ~(alignment - 1)


def pack_image(m1n1: bytes, firmware: bytes, layout_version: int) -> bytes:
    """Return a boot image without modifying either input buffer."""
    if not m1n1:
        raise ImageError("m1n1 image is empty")
    if not firmware:
        raise ImageError("firmware image is empty")
    if layout_version != _SUPPORTED_LAYOUT_VERSION:
        raise ImageError(f"unsupported layout version {layout_version}")

    manifest_offset = _align_up(len(m1n1))
    payload_offset = IMAGE_ALIGNMENT
    compressed = lzma.compress(firmware, format=lzma.FORMAT_XZ)
    crc32 = binascii.crc32(firmware) & 0xFFFFFFFF
    header = _MANIFEST.pack(
        IMAGE_MAGIC,
        FORMAT_VERSION,
        MANIFEST_SIZE,
        0,
        layout_version,
        0,
        payload_offset,
        len(compressed),
        len(firmware),
        crc32,
        0,
        b"\0" * 8,
    )
    assert len(header) == MANIFEST_SIZE

    return b"".join(
        (
            m1n1,
            b"\0" * (manifest_offset - len(m1n1)),
            header,
            b"\0" * (payload_offset - len(header)),
            compressed,
        )
    )


def _manifest_offsets(image: bytes) -> list[int]:
    return [
        offset
        for offset in range(0, len(image) - len(IMAGE_MAGIC) + 1, IMAGE_ALIGNMENT)
        if image.startswith(IMAGE_MAGIC, offset)
    ]


def parse_image(image: bytes) -> tuple[Manifest, bytes]:
    """Validate an image completely and return its manifest and uncompressed Mu FD."""
    offsets = _manifest_offsets(image)
    if not offsets:
        raise ImageError("standalone manifest not found")
    if len(offsets) != 1:
        raise ImageError("multiple standalone manifests found")

    manifest_offset = offsets[0]
    if len(image) - manifest_offset < MANIFEST_SIZE:
        raise ImageError("standalone manifest is truncated")
    fields = _MANIFEST.unpack_from(image, manifest_offset)
    (
        magic,
        format_version,
        header_size,
        flags,
        layout_version,
        reserved,
        payload_offset,
        compressed_size,
        uncompressed_size,
        crc32,
        reserved2,
        reserved_tail,
    ) = fields

    if magic != IMAGE_MAGIC:
        raise ImageError("invalid manifest magic")
    if format_version != FORMAT_VERSION:
        raise ImageError(f"unsupported manifest version {format_version}")
    if header_size != MANIFEST_SIZE:
        raise ImageError(f"invalid manifest size {header_size}")
    if flags or reserved or reserved2 or reserved_tail != b"\0" * 8:
        raise ImageError("unsupported manifest flags or nonzero reserved field")
    if layout_version != _SUPPORTED_LAYOUT_VERSION:
        raise ImageError(f"unsupported layout version {layout_version}")
    if payload_offset < header_size or payload_offset % IMAGE_ALIGNMENT:
        raise ImageError("payload offset is not 16 KiB aligned")
    if not compressed_size or not uncompressed_size:
        raise ImageError("payload size is zero")

    payload_start = manifest_offset + payload_offset
    payload_end = payload_start + compressed_size
    if payload_start > len(image) or payload_end != len(image):
        raise ImageError("payload bounds do not match the image")
    if any(image[manifest_offset + header_size : payload_start]):
        raise ImageError("manifest padding is not zero")

    try:
        firmware = lzma.decompress(image[payload_start:payload_end], format=lzma.FORMAT_XZ)
    except lzma.LZMAError as exc:
        raise ImageError(f"payload decompression failed: {exc}") from exc
    if len(firmware) != uncompressed_size:
        raise ImageError("payload uncompressed size mismatch")
    if (binascii.crc32(firmware) & 0xFFFFFFFF) != crc32:
        raise ImageError("payload CRC mismatch")

    manifest = Manifest(
        manifest_offset=manifest_offset,
        format_version=format_version,
        header_size=header_size,
        flags=flags,
        layout_version=layout_version,
        payload_offset=payload_offset,
        compressed_size=compressed_size,
        uncompressed_size=uncompressed_size,
        crc32=crc32,
    )
    return manifest, firmware
