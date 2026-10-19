"""Validate build metadata before passing it to PowerShell ConvertFrom-Json."""
import json
from pathlib import Path


def _unique_pairs(pairs):
    result = {}
    spellings = {}
    for name, value in pairs:
        # Build metadata uses ASCII field names. Conservatively reject any
        # Unicode case-fold aliases too, rather than discard a value silently.
        folded = name.casefold()
        if folded in spellings:
            raise ValueError(
                "case-insensitive duplicate keys {!r} and {!r}".format(
                    spellings[folded], name
                )
            )
        spellings[folded] = name
        result[name] = value
    return result


def loads_powershell_json(text):
    return json.loads(text, object_pairs_hook=_unique_pairs)


def load_powershell_json(path):
    return loads_powershell_json(Path(path).read_text(encoding="utf-8-sig"))


def main():
    import argparse
    import sys

    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("paths", nargs="+", type=Path)
    args = parser.parse_args()
    for path in args.paths:
        try:
            load_powershell_json(path)
        except (OSError, ValueError) as error:
            print("POWERSHELL_JSON: FAIL {}: {}".format(path, error), file=sys.stderr)
            return 1
        print("POWERSHELL_JSON: PASS {}".format(path))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
