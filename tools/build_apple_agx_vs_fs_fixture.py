#!/usr/bin/env python3
"""Build the AD03 VS/FS fixture with exact pinned Mesa commands."""
import argparse
import shlex
import subprocess
from pathlib import Path


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--mesa-build", type=Path, required=True)
    parser.add_argument("--source", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    build = args.mesa_build.resolve()
    subprocess.run([
        "ninja", "-C", str(build),
        "src/asahi/lib/libasahi_lib.a.p/agx_nir_lower_uvs.c.o",
        "src/asahi/lib/libasahi_lib.a.p/agx_nir_prolog_epilog.c.o",
        "src/asahi/lib/libasahi_lib.a.p/agx_tilebuffer.c.o",
        "src/asahi/lib/libasahi_lib.a.p/agx_nir_lower_msaa.c.o",
        "src/asahi/lib/libasahi_lib.a.p/agx_nir_lower_tilebuffer.c.o",
        "src/asahi/lib/libasahi_lib.a.p/agx_nir_lower_vbo.c.o",
        "src/poly/nir/liblibpoly_nir.a",
        "src/asahi/layout/libasahi_layout.a",
    ], check=True)
    lines = subprocess.run(
        ["ninja", "-C", str(build), "-t", "commands",
         "src/asahi/clc/asahi_clc"], check=True, text=True,
        capture_output=True).stdout.splitlines()
    compile_line = next(x for x in lines if
                        "asahi_clc.c.o" in x and " -c " in x)
    link_line = next(x for x in lines if
                     " -o src/asahi/clc/asahi_clc " in x)
    output = args.output.resolve()
    source = args.source.resolve()
    object_path = output.with_suffix(".o")
    compile_args = shlex.split(compile_line)
    compile_args[compile_args.index("-c") + 1] = str(source)
    compile_args[compile_args.index("-o") + 1] = str(object_path)
    compile_args[1:1] = [
        "-Isrc/asahi/compiler",
        "-I../../reference/mesa/src/asahi/compiler",
        "-Isrc/asahi/lib",
        "-I../../reference/mesa/src/asahi/lib",
        "-Isrc/asahi/genxml",
        "-I../../reference/mesa/src/asahi/isa",
        "-Isrc/asahi/libagx",
        "-I../../reference/mesa/src/asahi/libagx",
    ]
    if "-MF" in compile_args:
        compile_args[compile_args.index("-MF") + 1] = str(object_path) + ".d"
    if "-MQ" in compile_args:
        compile_args[compile_args.index("-MQ") + 1] = str(object_path)
    subprocess.run(compile_args, cwd=build, check=True)
    link_args = shlex.split(link_line)
    link_args[link_args.index("-o") + 1] = str(output)
    old = "src/asahi/clc/asahi_clc.p/asahi_clc.c.o"
    link_args[link_args.index(old)] = str(object_path)
    insert = link_args.index(str(object_path)) + 1
    extra = [
        "src/asahi/lib/libasahi_lib.a.p/agx_nir_lower_uvs.c.o",
        "src/asahi/lib/libasahi_lib.a.p/agx_nir_prolog_epilog.c.o",
        "src/asahi/lib/libasahi_lib.a.p/agx_tilebuffer.c.o",
        "src/asahi/lib/libasahi_lib.a.p/agx_nir_lower_msaa.c.o",
        "src/asahi/lib/libasahi_lib.a.p/agx_nir_lower_tilebuffer.c.o",
        "src/asahi/lib/libasahi_lib.a.p/agx_nir_lower_vbo.c.o",
        "src/poly/nir/liblibpoly_nir.a",
        "src/asahi/layout/libasahi_layout.a",
    ]
    link_args[insert:insert] = extra
    subprocess.run(link_args, cwd=build, check=True)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
