"""Decode exported EXP866 bytes using inspected G13/V13_5 field layouts."""
from pathlib import Path
import json
import re
import struct

HERE = Path(__file__).resolve().parent


def obj(index):
    return (HERE / f"R152-object{index}.bin").read_bytes()


def u32(data, offset=0):
    return struct.unpack_from("<I", data, offset)[0]


def u64(data, offset=0):
    return struct.unpack_from("<Q", data, offset)[0]


def debugger_bytes(path):
    memory = {}
    for line in path.read_text().splitlines():
        match = re.match(r"^([0-9a-f]{8}`[0-9a-f]{8})  (.*)", line)
        if not match:
            continue
        base = int(match[1].replace("`", ""), 16)
        words = match[2].split("  ")[0].replace("-", " ").split()
        if all(re.fullmatch(r"[0-9a-f]{2}", b) for b in words):
            memory.update((base+i, int(b, 16)) for i, b in enumerate(words))
    return memory


memory = debugger_bytes(HERE / "history-excerpt.txt")
events = []
for slot in range(9):
    data = bytes(memory[0xffffcc03f5268000 + slot*56+i] for i in range(56))
    events.append({"slot": slot, "kind": u32(data), "payload_u64": u64(data, 4),
                   "timeout_stamp_index": u32(data, 12) if u32(data) == 4 else None})
assert [(e["kind"], e["payload_u64"]) for e in events[:8]] == [(1, 2), (1, 1)]*4
assert events[8]["kind"] == 4 and events[8]["timeout_stamp_index"] == 0

# BufferManagerInfo and BufferThing/Scene: fw/buffer.rs; template relocations.
bm = obj(1)
record = {
    "firmware_events": events,
    "buffer_manager": {
        "gpu_counter": u32(bm), "gpu_counter2": u32(bm, 0x14),
        "page_list": hex(u64(bm, 0x1c)), "block_list": hex(u64(bm, 0x38)),
        "page_count": u32(bm, 0x28), "block_count": u32(bm, 0x30),
        "block_control_total": u32(obj(20)), "block_control_write": u32(obj(20), 4),
        "counter": u32(obj(21)),
    },
    "scene_user_buffer": hex(u64(obj(13), 0x18)),
    "work_buffer_manager": {"ta": hex(u64(obj(19), 0x24)),
                            "d3": hex(u64(obj(18), 0x28))},
    "vm_slots": {"init_bm": u32(obj(16), 4), "work_ta": u32(obj(19), 0xc),
                 "work_3d": u32(obj(18), 0xc), "start_ta": u32(obj(17), 0x34),
                 "finalize_ta": u32(obj(17), 0x22c)},
    "last_init_bm_stamp": hex(u32(obj(16), 0x1c)),
    "timestamps": {str(i): u64(obj(i)) for i in (28, 29, 30, 31)},
    "limitations": ["Timestamp objects persist between jobs and have no sequence tag",
                    "No MMIO UAT fault registers or TTBR table in the dump",
                    "No retained mapping from all prior completions to Windows processes"],
}
assert set(record["vm_slots"].values()) == {1}
assert set(record["work_buffer_manager"].values()) == {"0xffffffa00036bf44"}
assert record["buffer_manager"]["page_list"] == "0x2020000"
assert record["buffer_manager"]["block_list"] == "0x2030000"
assert record["scene_user_buffer"] == "0x2440000"
(HERE / "decoded.json").write_text(json.dumps(record, indent=2) + "\n")
print(json.dumps(record, indent=2))
