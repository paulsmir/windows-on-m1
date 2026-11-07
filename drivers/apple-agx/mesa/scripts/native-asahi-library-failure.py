"""Windows allocation-failure projection for the shared Asahi libraries."""
import hashlib


def project_sources(out, project, overlays):
    def change(name, replacements):
        path = 'src/asahi/lib/' + name
        target = out/path
        text = target.read_text()
        before = hashlib.sha256(target.read_bytes()).hexdigest()
        for old, new in replacements:
            if text.count(old) != 1:
                raise SystemExit('Ambiguous library failure anchor: ' + path + ': ' + old)
            text = text.replace(old, new, 1)
        target.write_text(text)
        overlays.setdefault(path, {})['before_library_failure'] = before
        overlays[path]['after_library_failure'] = hashlib.sha256(target.read_bytes()).hexdigest()

    change('agx_scratch.c', [
        ('   struct spill_size size = agx_scratch_get_spill_size(scratch->size_dwords);',
         '   scratch->buf = NULL;\n   scratch->header = NULL;\n\n'
         '   struct spill_size size = agx_scratch_get_spill_size(scratch->size_dwords);'),
        ('   void *map = agx_bo_map(scratch->buf);\n   memset(map, 0, blocks_off);',
         '   void *map = scratch->buf ? agx_bo_map(scratch->buf) : NULL;\n'
         '   if (!map) {\n'
         '      if (scratch->buf)\n'
         '         agx_bo_unreference(scratch->dev, scratch->buf);\n'
         '      scratch->buf = NULL;\n'
         '      scratch->header = NULL;\n'
         '      scratch->size_dwords = scratch->subgroups = 0;\n'
         '      scratch->max_core_id = 0;\n'
         '      return;\n'
         '   }\n'
         '   memset(map, 0, blocks_off);')])

    change('agx_linker.c', [
        ('   uint8_t *linked_map = agx_bo_map(linked->bo);',
         '   if (!linked->bo) return;\n'
         '   uint8_t *linked_map = agx_bo_map(linked->bo);\n'
         '   if (!linked_map) {\n'
         '      agx_bo_unreference(dev, linked->bo);\n'
         '      linked->bo = NULL;\n      return;\n   }')])

    change('agx_bg_eot.c', [
        ('   struct agx_shader_part bin;\n   agx_compile_shader_nir(shader, key, &bin);',
         '   if (!res) {\n      ralloc_free(shader);\n      return NULL;\n   }\n'
         '   struct agx_shader_part bin;\n   agx_compile_shader_nir(shader, key, &bin);'),
        ('   ralloc_free(shader);\n\n   return res;',
         '   ralloc_free(shader);\n'
         '   if (!res->ptr || !res->bo) {\n      ralloc_free(res);\n      return NULL;\n   }\n\n'
         '   return res;'),
        ('   struct hash_entry *ent = _mesa_hash_table_search(cache->ht, key);',
         '   if (!cache->ht) return NULL;\n'
         '   struct hash_entry *ent = _mesa_hash_table_search(cache->ht, key);'),
        ('         ret = agx_build_end_of_tile_shader(cache, key);\n         break;',
         '         ret = agx_build_end_of_tile_shader(cache, key);\n'
         '         if (!ret) return NULL;\n         break;'),
        ('   ret->key = *key;\n   _mesa_hash_table_insert(cache->ht, &ret->key, ret);',
         '   if (!ret) return NULL;\n'
         '   ret->key = *key;\n'
         '   if (!_mesa_hash_table_insert(cache->ht, &ret->key, ret)) {\n'
         '      ralloc_free(ret);\n      return NULL;\n   }'),
        ('      ralloc(cache->ht, struct agx_precompiled_shader);',
         '      ralloc(cache->ht, struct agx_precompiled_shader);\n'
         '   if (!p) return NULL;'),
        ('   /* Bake launch */',
         '   if (!p->ptr || !p->bo) {\n'
         '      ralloc_free(p);\n      return NULL;\n   }\n\n'
         '   /* Bake launch */'),
        ('   return pc->ptr | 1;', '   return pc ? pc->ptr | 1 : 0;')])
