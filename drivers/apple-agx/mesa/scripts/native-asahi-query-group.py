#!/usr/bin/env python3
"""RC67: one BO per D3D12 query index (mesa/winsys/agx_win32_query_group.h).

Projected after the GPUVA query lifecycle (native-asahi-batch-lifecycle.py):
the group helpers use its retirement list and the native busy test.
"""
import hashlib


def once(text, old, new):
    if text.count(old) != 1:
        raise RuntimeError('Ambiguous query group anchor: ' + old[:100])
    return text.replace(old, new, 1)


def project_sources(out, project, overlays):
    path = 'src/gallium/drivers/asahi/agx_query.c'
    text = (out / path).read_text()
    text = once(text, '#include "agx_win32_query_lifecycle.h"\n#endif',
                '#include "agx_win32_query_lifecycle.h"\n#include "agx_win32_query_group.h"\n#endif')
    (out / path).write_text(text)
    overlays.setdefault(path, {})['after_query_group'] = (
        hashlib.sha256(text.encode()).hexdigest())
