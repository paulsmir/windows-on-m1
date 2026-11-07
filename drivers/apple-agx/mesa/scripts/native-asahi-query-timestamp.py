#!/usr/bin/env python3
"""Project the original timestamp kernel after the native libagx projections."""
import hashlib
import json
import re


def once(text, old, new):
    if text.count(old) != 1:
        raise RuntimeError('Ambiguous query timestamp anchor: ' + old[:100])
    return text.replace(old, new, 1)


def generated(project):
    mesa = project / 'drivers/apple-agx/mesa'
    winsys = mesa / 'winsys'
    directory = winsys / 'query-timestamp-generated'
    manifest = json.loads((directory / 'manifest.json').read_text())
    for path in (winsys / 'agx_win32_query_timestamp.cl',
                 winsys / 'agx_win32_query_timestamp.h',
                 mesa / 'scripts/generate-query-timestamp.py'):
        if hashlib.sha256(path.read_bytes()).hexdigest() != manifest['inputs'][path.name]:
            raise RuntimeError('Regenerate query timestamp kernel after changing ' + path.name)
    for name, sha in manifest['outputs'].items():
        if hashlib.sha256((directory / name).read_bytes()).hexdigest() != sha:
            raise RuntimeError('Query timestamp generated output changed: ' + name)
    return directory


def project_library_c(text, project):
    if 'LIBAGX_WIN32_QUERY_TIMESTAMP' in text:
        raise RuntimeError('Query timestamp library was already projected')
    extra = (generated(project) / 'agx_win32_query_timestamp_shaders.c').read_text()
    begin = extra.index('static const uint32_t ')
    end = extra.index('const uint32_t *libagx_g13g[')
    text = once(text, '#include "libagx_shaders.h"',
                '#include "libagx_shaders.h"\n' + extra[begin:end])
    for chip in ('g13g', 'g13x'):
        anchor = 'const uint32_t *libagx_' + chip + '[LIBAGX_NUM_PROGRAMS] = {'
        entry = extra[extra.index(anchor) + len(anchor):].split('};', 1)[0]
        text = once(text, anchor, anchor + entry)
    return text


def project_sources(out, project, overlays):
    extra = (generated(project) / 'agx_win32_query_timestamp_shaders.h').read_text()
    extra = extra[extra.index('struct libagx_win32_query_timestamp_args {'):]
    extra = re.sub(r'enum libagx_program \{.*?\};', '', extra, flags=re.S)
    extra = extra[:extra.index('extern const uint32_t *')]
    path = 'src/asahi/lib/libagx_shaders.h'
    text = (out / path).read_text()
    if 'LIBAGX_WIN32_QUERY_TIMESTAMP' in text:
        raise RuntimeError('Query timestamp header was already projected')
    text = once(text, '    LIBAGX_NUM_PROGRAMS,',
                '    LIBAGX_WIN32_QUERY_TIMESTAMP,\n    LIBAGX_NUM_PROGRAMS,')
    text += '\n' + extra
    (out / path).write_text(text)
    overlays.setdefault(path, {})['after_query_timestamp'] = (
        hashlib.sha256(text.encode()).hexdigest())
