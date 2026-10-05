#!/usr/bin/env python3
"""Build an AD03 fixture with the exact pinned Mesa asahi_clc flags/libs."""
import argparse
import shlex
import subprocess
import sys
from pathlib import Path


def commands(build):
    result = subprocess.run(
        ["ninja", "-C", str(build), "-t", "commands",
         "src/asahi/clc/asahi_clc"],
        check=True, text=True, capture_output=True,
    )
    return result.stdout.splitlines()


def replace_compile(command, source, output):
    args = shlex.split(command)
    if "-c" not in args or "-o" not in args:
        raise ValueError("Mesa compile command")
    args[args.index("-c") + 1] = str(source)
    args[args.index("-o") + 1] = str(output)
    if "-MF" in args:
        args[args.index("-MF") + 1] = str(output) + ".d"
    if "-MQ" in args:
        args[args.index("-MQ") + 1] = str(output)
    return args


def replace_link(command, object_path, output):
    args = shlex.split(command)
    if "-o" not in args:
        raise ValueError("Mesa link command")
    args[args.index("-o") + 1] = str(output)
    old = "src/asahi/clc/asahi_clc.p/asahi_clc.c.o"
    if old not in args:
        raise ValueError("Mesa fixture object")
    args[args.index(old)] = str(object_path)
    return args


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--mesa-build", type=Path, required=True)
    parser.add_argument("--source", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    try:
        lines = commands(args.mesa_build)
        compile_line = next(x for x in lines if
                            "asahi_clc.c.o" in x and " -c " in x)
        link_line = next(x for x in lines if
                         " -o src/asahi/clc/asahi_clc " in x)
        output = args.output.resolve()
        source = args.source.resolve()
        object_path = output.with_suffix(".o")
        subprocess.run(replace_compile(compile_line, source, object_path),
                       cwd=args.mesa_build, check=True)
        subprocess.run(replace_link(link_line, object_path, output),
                       cwd=args.mesa_build, check=True)
    except (OSError, ValueError, StopIteration,
            subprocess.CalledProcessError) as error:
        print(str(error), file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
