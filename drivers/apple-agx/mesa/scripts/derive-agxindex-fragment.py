#!/usr/bin/env python3
"""Extract a hash-pinned, standalone agx_index constructor fragment."""
import argparse
import hashlib
import json
import re
from pathlib import Path

EXPECTED_PINNED_SHA256 = "c41df12679c1692cc50617e81d57eb03cf906d8c5fbada482b89fc6dde0c7741"
FUNCTIONS = (
    "agx_get_vec_index", "agx_immediate", "agx_memory_register",
    "agx_register_like", "agx_undef", "agx_uniform", "agx_null",
    "agx_zero", "agx_negzero", "agx_abs", "agx_neg",
    "agx_replace_index", "agx_is_null",
)


def digest(text):
    return hashlib.sha256(text.encode("utf-8")).hexdigest()


def section(text, start, end):
    begin = text.index(start)
    finish = text.index(end, begin) + len(end)
    return text[begin:finish]


def function(text, name):
    match = re.search(r"static inline[^{}]*?\n" + re.escape(name) + r"\s*\(", text)
    if not match:
        raise ValueError(f"missing {name}")
    start = match.start()
    brace = text.index("{", start)
    depth = 0
    for index in range(brace, len(text)):
        if text[index] == "{":
            depth += 1
        elif text[index] == "}":
            depth -= 1
            if depth == 0:
                return text[start:index + 1] + "\n"
    raise ValueError(f"unterminated {name}")


def extract(text):
    pieces = [
        section(text, "/* u0-u255 inclusive", "#define AGX_NUM_UNIFORMS (512)"),
        section(text, "#define AGX_NUM_MODELED_REGS_LOG2 (11)", "#define AGX_NUM_MODELED_REGS_LOG2 (11)"),
        section(text, "enum agx_index_type", "enum agx_size { AGX_SIZE_16 = 0, AGX_SIZE_32 = 1, AGX_SIZE_64 = 2 };"),
        section(text, "/* Keep synced with hash_index */", 'static_assert(sizeof(agx_index) == 8, "packed");'),
    ]
    pieces.extend(function(text, name) for name in FUNCTIONS)
    return "\n".join(pieces)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--pinned", required=True, type=Path)
    parser.add_argument("--actual", required=True, type=Path)
    parser.add_argument("--pinned-output", required=True, type=Path)
    parser.add_argument("--actual-output", required=True, type=Path)
    parser.add_argument("--manifest", required=True, type=Path)
    args = parser.parse_args()

    pinned = args.pinned.read_text(encoding="utf-8")
    actual = args.actual.read_text(encoding="utf-8")
    if digest(pinned) != EXPECTED_PINNED_SHA256:
        raise SystemExit(f"pinned agx_compiler.h hash mismatch: {digest(pinned)}")
    pinned_fragment = extract(pinned)
    actual_fragment = extract(actual)
    for output, fragment in ((args.pinned_output, pinned_fragment),
                             (args.actual_output, actual_fragment)):
        output.parent.mkdir(parents=True, exist_ok=True)
        output.write_text(fragment, encoding="utf-8")
    manifest = {
        "pinned_header_sha256": digest(pinned),
        "actual_header_sha256": digest(actual),
        "pinned_fragment_sha256": digest(pinned_fragment),
        "actual_fragment_sha256": digest(actual_fragment),
        "constructors": FUNCTIONS,
        "source_mutated": False,
    }
    args.manifest.parent.mkdir(parents=True, exist_ok=True)
    args.manifest.write_text(json.dumps(manifest, indent=2, sort_keys=True) + "\n",
                             encoding="utf-8")
    print(json.dumps(manifest, sort_keys=True))


if __name__ == "__main__":
    main()
