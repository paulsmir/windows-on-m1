# Original project code. SPDX-License-Identifier: MIT
import argparse, hashlib, json
from pathlib import Path
EXPECTED = "893b5a63f7dd0fbaf1d96953451fb073e35fd38acd8be60a51528e2f37204a09"
ANCHORS = {
 "enum shader_info_hash_type hash_type:2;": "unsigned hash_type:2;",
 "enum gl_derivative_group derivative_group:2;": "unsigned derivative_group:2;",
 "enum gl_frag_depth_layout depth_layout:3;": "unsigned depth_layout:3;",
 "enum gl_frag_stencil_layout stencil_front_layout:3;": "unsigned stencil_front_layout:3;",
 "enum gl_frag_stencil_layout stencil_back_layout:3;": "unsigned stencil_back_layout:3;",
}
def project(data):
    if hashlib.sha256(data).hexdigest() != EXPECTED:
        raise ValueError("pristine source hash mismatch")
    out = data
    for old, new in ANCHORS.items():
        old, new = old.encode(), new.encode()
        if out.count(old) != 1: raise ValueError("declaration anchor count")
        out = out.replace(old, new)
    if out.split(b"#ifndef", 1)[0] != data.split(b"#ifndef", 1)[0]:
        raise ValueError("license prefix changed")
    if out.count(b"unsigned hash_type:2;") != 1:
        raise ValueError("fifth field anchor changed")
    return out
if __name__ == "__main__":
    p=argparse.ArgumentParser();p.add_argument("source");p.add_argument("output");a=p.parse_args()
    src=Path(a.source).read_bytes();out=project(src)
    target=Path(a.output)
    if target.exists(): raise ValueError("refuse existing output")
    with target.open("xb") as f: f.write(out)
    print(json.dumps({"scope":"FIVE_CONFIRMED_FIELDS", "source_sha256":EXPECTED,
                      "output_sha256":hashlib.sha256(out).hexdigest(),"bytes":len(out)}))
