"""Virtual framebuffer contract and async proxy-event frame assembly."""

from dataclasses import dataclass
import json
import os
from pathlib import Path
import struct
from typing import Mapping
import zlib


FB_STREAM_MAGIC = 0x31424656
FB_CHUNK_HEADER = struct.Struct("<IIIIHHII")


def _overlaps(first: tuple[int, int], second: tuple[int, int]) -> bool:
    return first[0] < second[1] and second[0] < first[1]


@dataclass(frozen=True)
class VirtualDisplayConfig:
    base: int
    width: int
    height: int
    stride: int

    def __post_init__(self) -> None:
        if self.base <= 0:
            raise ValueError("framebuffer base must be positive")
        if not 0 < self.width <= 0xFFFF or not 0 < self.height <= 0xFFFF:
            raise ValueError("framebuffer dimensions must fit the stream header")
        if self.stride < self.width * 4:
            raise ValueError("framebuffer stride is smaller than width * 4")
        if self.size > 0xFFFFFFFF:
            raise ValueError("framebuffer size must fit the stream header")
        if self.end > 1 << 64:
            raise ValueError("framebuffer address range overflows 64 bits")

    @property
    def size(self) -> int:
        return self.stride * self.height

    @property
    def end(self) -> int:
        return self.base + self.size

    @property
    def address_range(self) -> tuple[int, int]:
        return self.base, self.end

    def validate(
        self,
        guest_ram: tuple[int, int],
        occupied: Mapping[str, tuple[int, int]],
    ) -> None:
        ram_start, ram_end = guest_ram
        if ram_start >= ram_end:
            raise ValueError("guest RAM range is empty")
        if self.base < ram_start or self.end > ram_end:
            raise ValueError(
                f"framebuffer 0x{self.base:x}..0x{self.end:x} is outside guest RAM "
                f"0x{ram_start:x}..0x{ram_end:x}"
            )

        for name, window in occupied.items():
            start, end = window
            if start >= end:
                raise ValueError(f"{name} range is empty")
            if _overlaps(self.address_range, window):
                raise ValueError(
                    f"framebuffer 0x{self.base:x}..0x{self.end:x} overlaps {name} "
                    f"0x{start:x}..0x{end:x}"
                )


class FrameReceiver:
    """Assemble ordered chunks and atomically publish complete frames only."""

    def __init__(
        self,
        config: VirtualDisplayConfig,
        raw_path: str | os.PathLike[str] = "fb.raw",
        info_path: str | os.PathLike[str] = "fb-info.json",
    ) -> None:
        self.config = config
        self.raw_path = Path(raw_path)
        self.info_path = Path(info_path)
        self._buffer = bytearray(config.size)
        self._frame_id: int | None = None
        self._offset = 0
        self.generation = 0
        self.discarded_chunks = 0

    def _discard(self) -> None:
        self._frame_id = None
        self._offset = 0
        self.discarded_chunks += 1

    def _matches_contract(
        self, total_size: int, width: int, height: int, stride: int
    ) -> bool:
        return (
            total_size == self.config.size
            and width == self.config.width
            and height == self.config.height
            and stride == self.config.stride
        )

    def accept(self, data: bytes) -> bool:
        if len(data) < FB_CHUNK_HEADER.size:
            self._discard()
            return False

        (magic, frame_id, offset, total_size, width, height, stride,
         payload_size) = FB_CHUNK_HEADER.unpack_from(data)
        payload = data[FB_CHUNK_HEADER.size:]
        if (
            magic != FB_STREAM_MAGIC
            or payload_size != len(payload)
            or payload_size == 0
            or not self._matches_contract(total_size, width, height, stride)
            or offset > total_size
            or payload_size > total_size - offset
        ):
            self._discard()
            return False

        if offset == 0:
            if self._frame_id == frame_id and self._offset != 0:
                self._discard()
                return False
            self._frame_id = frame_id
            self._offset = 0
        elif self._frame_id is None:
            self._discard()
            return False

        if frame_id != self._frame_id or offset != self._offset:
            self._discard()
            return False

        self._buffer[offset:offset + payload_size] = payload
        self._offset += payload_size
        if self._offset != self.config.size:
            return False

        self._publish(frame_id)
        self._frame_id = None
        self._offset = 0
        return True

    def _atomic_write(self, path: Path, data: bytes) -> None:
        path.parent.mkdir(parents=True, exist_ok=True)
        temporary = path.with_name(path.name + ".part")
        with temporary.open("wb") as stream:
            stream.write(data)
            stream.flush()
            os.fsync(stream.fileno())
        os.replace(temporary, path)

    def _publish(self, frame_id: int) -> None:
        next_generation = self.generation + 1
        metadata = {
            "generation": next_generation,
            "frame_id": frame_id,
            "base": self.config.base,
            "width": self.config.width,
            "height": self.config.height,
            "stride": self.config.stride,
            "size": self.config.size,
            "format": "B8G8R8X8",
            "crc32": zlib.crc32(self._buffer) & 0xFFFFFFFF,
        }
        self._atomic_write(self.raw_path, self._buffer)
        self._atomic_write(
            self.info_path,
            (json.dumps(metadata, sort_keys=True) + "\n").encode("utf-8"),
        )
        self.generation = next_generation
