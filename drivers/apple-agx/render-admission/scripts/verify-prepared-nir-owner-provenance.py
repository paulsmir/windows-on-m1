"""Verify the opt-in production object hook; normal runtime verifier still applies."""
import argparse
from pathlib import Path
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / 'mesa/scripts'))
from prepared_nir_owner import verify_receipt, load, key, require


def main():
    p = argparse.ArgumentParser()
    p.add_argument('--request', type=Path, required=True)
    p.add_argument('--request-sha256', required=True)
    p.add_argument('--receipt', type=Path, required=True)
    p.add_argument('--props', type=Path, required=True)
    p.add_argument('--source-manifest', type=Path, required=True)
    p.add_argument('--native-props', type=Path, required=True)
    p.add_argument('--project-root', type=Path, required=True)
    a = p.parse_args()
    try:
        request = load(a.request)
        require(key(request['source_manifest']) == key(a.source_manifest), 'package manifest binding')
        require(key(request['native_props']) == key(a.native_props), 'package native props binding')
        require(key(request['roots']['PROJECT']) == key(a.project_root), 'package project root binding')
        verify_receipt(a.request, a.request_sha256, a.receipt, a.props)
    except (OSError, ValueError, KeyError) as error:
        print('prepared owner provenance rejected: '+str(error), file=sys.stderr)
        return 1
    print('PREPARED_NIR_OWNER_PROVENANCE_PASS')
    return 0

if __name__ == '__main__':
    raise SystemExit(main())
