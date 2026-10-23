"""D3D12 C4b native owner (mesa/winsys/agx_d3d12_windows.cpp).

Invariants caught (investigation/analysis/D3D12-C4-copy.md):
- the owner's UMD device storage borrows a context the owner itself creates
  and destroys (never the D3D10 initializer, which needs D3D10 device args);
- CopyBufferRegion is a GPU copy (the context's resource_copy_region, whose
  Asahi buffer path runs libagx copy kernels), never the winsys CPU memcpy
  helper AgxWin32AsahiBufferCopy;
- the copy validates both byte ranges before recording;
- submission waits for the context to retire and reports faults;
- teardown order: context, screen, UMD device storage, kernel context.
"""
from pathlib import Path
import unittest
import re
import shutil
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
OWNER = ROOT / 'drivers/apple-agx/mesa/winsys/agx_d3d12_windows.cpp'
PROJECT = ROOT / 'drivers/apple-agx/render-admission/umd/AppleAgxRenderAdmissionUmd.vcxproj'


def body(text, name):
    start = text.index(name)
    return text[start:text.index('\n}\n', start) + 3]


class NativeOwner(unittest.TestCase):
    def test_context_and_initializer(self):
        text = OWNER.read_text()
        create = body(text, 'extern "C" HRESULT AgxD3d12OwnerCreate(')
        self.assertIn('pfnCreateContextVirtualCb(owner->RuntimeDevice, &context)', create)
        self.assertIn('AdmissionUmdRuntimeDeviceInitializeForQueue(', create)
        self.assertNotIn('AdmissionUmdRuntimeDeviceInitialize(&', create)
        self.assertLess(create.index('pfnCreateContextVirtualCb'), create.index('AdmissionUmdRuntimeDeviceInitializeForQueue('))
        self.assertLess(create.index('AgxWin32AsahiScreenCreateForWindows'), create.index('AgxWin32AsahiContextCreate'))
        # EXP1365: the KMD binds one Win32 generation per kernel device
        self.assertIn('contract.Generation = args->Generation;', create)
        self.assertIn('owner->KernelContext, args->Generation);', create)

    def test_gpu_copy(self):
        text = OWNER.read_text()
        copy = body(text, 'extern "C" HRESULT AgxD3d12OwnerCopy(')
        self.assertIn('->resource_copy_region(', copy)
        self.assertNotIn('AgxWin32AsahiBufferCopy', text)
        self.assertIn('bytes > dst->Bytes - dst_offset', copy)
        self.assertIn('bytes > src->Bytes - src_offset', copy)
        submit = body(text, 'extern "C" HRESULT AgxD3d12OwnerSubmitAndWait(')
        self.assertLess(submit.index('->flush('), submit.index('AgxWin32AsahiContextRetire('))
        self.assertIn('AgxWin32AsahiContextFaulted(', submit)

    def test_compute(self):
        # C5: the compute program goes through the repository's DXBC -> NIR
        # translator (as the D3D11 path does); a dispatch binds the UAVs as
        # shader buffers, launches and drops the bindings so they never
        # outlive a heap. EXP1850: no memory_barrier -- Asahi orders the
        # launches of a batch (CDM barrier) and the batches (writer tracking);
        # the barrier made every dispatch its own submission.
        text = OWNER.read_text()
        # C65V: the plain entry is the flags entry without AGX_D3D12_SHADER_MINMAX
        self.assertIn('return AgxD3d12OwnerCreateComputeFlags(owner, tokens, 0u, compute);',
                      body(text, 'extern "C" HRESULT AgxD3d12OwnerCreateCompute('))
        create = body(text, 'extern "C" HRESULT AgxD3d12OwnerCreateComputeFlags(')
        # C44: DXBC tokens or a DXIL program part, one translation entry
        self.assertIn('translate_shader(tokens, MESA_SHADER_COMPUTE,', create)
        self.assertIn('&compute->Bindings, 0u, 0u, NULL, &error)', create)
        translate = body(text, 'static struct nir_shader *translate_shader(')
        self.assertIn('return dxbc_translate_tokens_bindings_xfb(tokens, options, shader_binding_limits(), bindings,', translate)
        self.assertIn('agx_dxil_to_nir(tokens, static_cast<size_t>(tokens[1]) * 4u, shader_binding_limits(), options, &dxil)', translate)
        # C65G/C65V: min/max emulation only for a PSO's min/max variant (the
        # creating call's AGX_D3D12_SHADER_MINMAX); the default limits carry
        # no DXBC_BINDING_MINMAX
        limits = body(text, 'static const uint32_t *shader_binding_limits(')
        self.assertIn('return t_shader_minmax ? minmax_limits : owner_binding_limits;', limits)
        defaults = text.split('static const uint32_t owner_binding_limits[', 1)[1].split('};', 1)[0]
        self.assertNotIn('DXBC_BINDING_MINMAX', defaults)
        self.assertIn('ShaderMinmaxScope minmax(flags);', body(text, 'extern "C" HRESULT AgxD3d12OwnerCreateComputeFlags('))
        self.assertIn('ShaderMinmaxScope minmax(desc->Flags);', body(text, 'extern "C" HRESULT AgxD3d12OwnerCreateGraphics('))
        self.assertIn('dxbc_nir_compute_info(', create)
        self.assertIn('->create_compute_state(', create)
        dispatch = body(text, 'extern "C" HRESULT AgxD3d12OwnerDispatch(')
        order = [dispatch.index(k) for k in ('bind_stage(pipe, MESA_SHADER_COMPUTE, cbvs, srvs, uavs, views, &view_count, compute->SrvDimensions, compute->UavDimensions)',
                                             'bind_compute_state', 'launch_grid',
                                             'unbind_stage(pipe, MESA_SHADER_COMPUTE, views, view_count)')]
        self.assertEqual(order, sorted(order))
        self.assertNotIn('pipe->memory_barrier(', dispatch)
        # C13: UAVs of every stage bind in bind_stage (buffers u#, typed
        # images u#) and unbind_stage drops both so no binding outlives a heap
        stage_b = body(text, 'static HRESULT bind_stage(')
        self.assertIn('uavs[i].Offset >= buffer->Bytes', stage_b)
        self.assertIn('pipe->set_shader_images(pipe, stage, 0, AGX_D3D12_UAV_SLOTS, 0, images);', stage_b)
        unbind = body(text, 'static void unbind_stage(')
        self.assertIn('pipe->set_shader_buffers(pipe, stage, 0, 2 * AGX_D3D12_UAV_SLOTS, NULL, 0);', unbind)
        self.assertIn('pipe->set_shader_images(pipe, stage, 0, 0, AGX_D3D12_UAV_SLOTS, NULL);', unbind)
        draw_fn = body(text, 'extern "C" HRESULT AgxD3d12OwnerDraw(')
        self.assertIn('draw->Ps.Uavs, ps_views, &ps_view_count', draw_fn)
        # C5b/C5d/C9: CBVs (capped at 64 KiB and the buffer end) and SRVs bind
        # through the draw stages' bind_stage before the launch and unbind
        # (views released) after it -- EXP1372 offline: the compute path's
        # own SRV loop made texture SRVs R32_UINT buffer views
        stage = body(text, 'static HRESULT bind_stage(')
        self.assertIn('room < 65536ull ? room : 65536ull', stage)
        self.assertIn('if (srvs[i].Texture) {', stage)
        self.assertIn('FormatTranslate(srvs[i].Format, FALSE) : PIPE_FORMAT_R32_UINT', stage)
        order = [dispatch.index(k) for k in ('bind_stage(pipe, MESA_SHADER_COMPUTE, cbvs, srvs, uavs, views, &view_count, compute->SrvDimensions, compute->UavDimensions)',
                                             'launch_grid', 'unbind_stage(pipe, MESA_SHADER_COMPUTE, views, view_count)')]
        self.assertEqual(order, sorted(order))
        self.assertNotIn('templ.format = PIPE_FORMAT_R32_UINT;', dispatch)
        # C9: compute samplers live for one launch; typed UAVs are images u#
        order = [dispatch.index(k) for k in ('create_sampler(pipe, &samplers[i])', 'bind_sampler_states(pipe, MESA_SHADER_COMPUTE, 0, state_count, states)',
                                             'launch_grid',
                                             'bind_sampler_states(pipe, MESA_SHADER_COMPUTE, 0, state_count, none)')]
        self.assertEqual(order, sorted(order))
        image = body(text, 'static bool uav_image(')
        self.assertIn('uav->Layers > resource->array_size - uav->FirstLayer', image)
        self.assertIn('resource->target != PIPE_BUFFER || !bytes', image)

    def test_textures(self):
        # C6a: textures use the D3D10 frontend's DXGI table; footprint copies
        # map the texture and buffer through the context (which orders them
        # after recorded GPU work) and bound-check both sides first.
        text = OWNER.read_text()
        self.assertIn('enum pipe_format FormatTranslate(DXGI_FORMAT Format, BOOL depth);', text)
        # EXP1368 build: u_format.h inline helpers carry CRT asserts that the
        # package's assertion gate refuses; block sizes come from the table
        self.assertNotIn('util_format_get_block', text)
        self.assertIn('util_format_description(format)', text)
        copy = body(text, 'extern "C" HRESULT AgxD3d12OwnerCopyTexture(')
        self.assertLess(copy.index('span > copy->Buffer->Bytes - copy->Offset'), copy.index('texture_map('))
        self.assertIn('copy->Pitch < row', copy)
        self.assertIn('->buffer_map(', copy)
        clear = body(text, 'extern "C" HRESULT AgxD3d12OwnerClearRenderTarget(')
        self.assertIn('layers > resource->array_size - first_layer', clear)
        # EXP1368: Asahi implements neither clear_render_target nor
        # clear_depth_stencil (the C6a clear called NULL); clears bind the
        # view as a one-layer framebuffer and use pipe->clear
        self.assertNotIn('->clear_render_target(', text)
        self.assertNotIn('->clear_depth_stencil(', text)
        layers = body(text, 'static bool clear_view_layers(')
        self.assertLess(layers.index('pipe->set_framebuffer_state(pipe, &fb);'), layers.index('pipe->clear(pipe, buffers'))
        self.assertIn('++owner->PendingDraws;', layers)
        # EXP1882: the Windows agx_clear faults the context on a scissor; a
        # whole-surface rect clears unscissored, a partial one is refused
        self.assertNotIn('pipe_scissor_state', layers)
        self.assertIn('AdmissionUmdDiagnostic("measure-d3d12-clear-rect", E_NOTIMPL', layers)
        # C57: integer views take the FLOAT[4] clear color converted (Asahi
        # reads pipe_color_union.ui/.i for them)
        self.assertIn('const int integer = clear_integer_kind(view_format);', clear)

    def test_graphics(self):
        # C6b: VS/PS through the repository's translator; D3D12 always
        # scissors; viewports as the D3D10 frontend (y flipped, half-z);
        # element strides come from the draw (cached per pipeline); the submit
        # drops framebuffer and vertex-buffer bindings so they never outlive
        # the heaps.
        text = OWNER.read_text()
        create = body(text, 'extern "C" HRESULT AgxD3d12OwnerCreateGraphics(')
        self.assertIn('rast.scissor = 1;', create)
        self.assertIn('rast.clip_halfz = 1;', create)
        self.assertIn('translate_shader(tokens, stage,', body(text, 'static void *create_shader('))
        self.assertIn('&graphics->VsBindings', create)
        self.assertIn('&graphics->PsBindings', create)
        draw = body(text, 'extern "C" HRESULT AgxD3d12OwnerDraw(')
        self.assertIn('viewport.scale[1] = -hh;', draw)
        self.assertIn('elements[r].src_stride = draw->Vb[slot].Stride;', draw)
        self.assertIn('->draw_vbo(', draw)
        # C6c: per-stage constant buffers / SRVs bound before draw_vbo and
        # dropped after it
        self.assertLess(draw.index('bind_stage(pipe, MESA_SHADER_VERTEX'), draw.index('->draw_vbo('))
        self.assertLess(draw.index('->draw_vbo('), draw.rindex('unbind_stage(pipe, MESA_SHADER_FRAGMENT, ps_views, ps_view_count)'))
        submit = body(text, 'extern "C" HRESULT AgxD3d12OwnerSubmitAndWait(')
        self.assertLess(submit.index('set_framebuffer_state(owner->Context, &empty)'), submit.index('->flush('))
        # C6e: sampler CSOs exist for one draw only (created, bound, released)
        self.assertLess(draw.index('create_sampler(pipe, &draw->Ps.Samplers[i])'), draw.index('->draw_vbo('))
        self.assertLess(draw.index('->draw_vbo('), draw.rindex('release_samplers('))
        sampler = body(text, 'static void *create_sampler(')
        self.assertIn('state.compare_mode = PIPE_TEX_COMPARE_R_TO_TEXTURE;', sampler)
        stage = body(text, 'static HRESULT bind_stage(')
        self.assertIn('srv_texture_template(srvs[i], dimensions ? dimensions[i] : DXBC_SRV_UNUSED, &texture, &templ)', stage)
        self.assertIn('templ->target = texture->target;', body(text, 'static int srv_texture_template('))

    def test_draw_framebuffer_cache_is_invalidated_by_every_other_binding(self):
        # WHAT REAL BUG OR INVARIANT WILL THIS TEST CATCH? The draw skips an
        # unchanged set_framebuffer_state (Asahi dirties all state on it).
        # Any other owner path that binds a framebuffer (clears, the
        # submit's empty one) must invalidate the cache, or the next draw
        # renders into the clear's or an empty framebuffer.
        text = OWNER.read_text()
        calls = [m.start() for m in re.finditer(r'set_framebuffer_state\(', text)]
        draw = body(text, 'extern "C" HRESULT AgxD3d12OwnerDraw(')
        draw_at = text.index(draw)
        for at in calls:
            if draw_at <= at < draw_at + len(draw):
                continue
            following = text[at:at + 200]
            self.assertIn('DrawFramebufferValid = false;', following, text[at - 80:at + 120])
        self.assertIn('memcmp(&owner->DrawFramebuffer, &fb, sizeof(fb))', draw)
        self.assertIn('owner->DrawFramebufferValid = true;', draw)

    def test_10bit_swapchain_converts_before_the_present_flush(self):
        # WHAT REAL BUG OR INVARIANT WILL THIS TEST CATCH? C40: a 10-bit
        # swapchain buffer is rendered into its own texture and presented
        # from RGBA8 storage (the presentation contract is 8-bit). A present
        # that flushed before the conversion blit, or storage created in the
        # application's format, would show stale or undecodable pixels.
        text = OWNER.read_text()
        prepare = body(text, 'extern "C" HRESULT AgxD3d12OwnerPreparePresent(')
        self.assertLess(prepare.index('owner->Context->blit(owner->Context, &blit);'),
                        prepare.index('AgxWin32AsahiContextFlushForPresent('))
        create = body(text, 'extern "C" HRESULT AgxD3d12OwnerCreatePresentationTexture(')
        self.assertLess(create.index('format = DXGI_FORMAT_R8G8B8A8_UNORM;'), create.index('create.Format = format;'))
        self.assertIn('record->Converted ? record->Converted : record->RenderResource', create)
        destroy = body(text, 'extern "C" void AgxD3d12OwnerDestroyPresentationTexture(')
        self.assertIn('AgxWin32AsahiResourceRelease(&record->Converted);', destroy)

    def test_indirect_draw_does_not_use_direct_counts_for_early_out(self):
        # WHAT REAL BUG OR INVARIANT WILL THIS TEST CATCH?
        # EXP1377: ExecuteIndirect records zero direct counts. The owner must
        # reach Gallium, which reads actual counts from the argument buffer.
        draw = body(OWNER.read_text(), 'extern "C" HRESULT AgxD3d12OwnerDraw(')
        guard = re.search(r'if \(([^;\n]*VertexCount[^;\n]*)\) return S_OK;', draw)
        self.assertIsNotNone(guard)
        compiler = shutil.which('clang++') or shutil.which('c++')
        self.assertIsNotNone(compiler, 'C++ compiler required for draw guard regression')
        source = r"""
struct Draw { unsigned VertexCount, InstanceCount; struct { void *Buffer; } Indirect; };
bool skipped(const Draw *draw) { return GUARD; }
int main() {
    for (unsigned indirect = 0; indirect < 2; ++indirect)
        for (unsigned vertices : {0u, 1u, 3u})
            for (unsigned instances : {0u, 1u, 2u}) {
                Draw d = {vertices, instances, {indirect ? (void *)&indirect : nullptr}};
                bool expected = !indirect && (!vertices || !instances);
                if (skipped(&d) != expected) return 1;
            }
    return 0;
}
""".replace('GUARD', guard.group(1))
        source = '#include <initializer_list>\n' + source
        with tempfile.TemporaryDirectory() as tmp:
            cpp, exe = Path(tmp) / 'guard.cpp', Path(tmp) / 'guard'
            cpp.write_text(source)
            subprocess.run([compiler, '-std=c++11', str(cpp), '-o', str(exe)], check=True, capture_output=True)
            result = subprocess.run([str(exe)], capture_output=True)
            self.assertEqual(result.returncode, 0, 'indirect draw skipped because direct counts are zero')

    def test_draw_without_input_layout_binds_empty_vertex_elements(self):
        # EXP1891B C59A: a PSO without an input layout (SV_VertexID quad) as
        # the context's first draw crashed in Asahi agx_update_vs, which
        # copies ctx->attributes->key unconditionally; the owner bound the
        # vertex elements only for count > 0 (and left a previous draw's
        # elements bound otherwise).
        owner = OWNER.read_text()
        draw = body(owner, 'extern "C" HRESULT AgxD3d12OwnerDraw(')
        self.assertIn('if (!count && !owner->EmptyVelems) owner->EmptyVelems = pipe->create_vertex_elements_state(pipe, 0, NULL);', draw)
        self.assertIn('pipe->bind_vertex_elements_state(pipe, count ? graphics->Velems : owner->EmptyVelems);', draw)
        self.assertNotIn('if (count) pipe->bind_vertex_elements_state', draw)
        destroy = owner[owner.index('owner->Backend.Closing = 1;'):]
        destroy = destroy[:destroy.index('AgxWin32AsahiContextDestroy')]
        self.assertIn('owner->Context->delete_vertex_elements_state(owner->Context, owner->EmptyVelems);', destroy)

    def test_uav_only_draw_rasterizes_within_the_viewport(self):
        # EXP1889 C60A: a draw with neither an RTV nor a DSV (UAV-only
        # rendering, legal D3D12) was refused at recording (reason 5) and the
        # owner had no framebuffer size for it. The size is the viewport's
        # far corner, rounded up, within the 16384 render window.
        ddi = (ROOT / 'drivers/apple-agx/d3d12/agx_d3d12_ddi.cpp').read_text()
        record = body(ddi, 'static void RecordDraw(')
        self.assertIn(': list->rtvs && !list->rtv[0].heap && list->dsv.kind != kDescriptorDsv ? 5u', record)
        owner = OWNER.read_text()
        helper = body(owner, 'static unsigned no_attachment_extent(')
        draw = body(owner, 'extern "C" HRESULT AgxD3d12OwnerDraw(')
        self.assertIn('if (!draw->NumRtvs && !draw->Dsv.Texture) {', draw)
        self.assertIn('width = no_attachment_extent(draw->Viewport[0], draw->Viewport[2]);', draw)
        self.assertIn('height = no_attachment_extent(draw->Viewport[1], draw->Viewport[3]);', draw)
        source = '#include <math.h>\n' + helper + r"""
int main() {
    if (no_attachment_extent(0.0f, 64.0f) != 64u) return 1;
    if (no_attachment_extent(0.5f, 63.75f) != 65u) return 2;   /* far edge 64.25 -> 65 */
    if (no_attachment_extent(-8.0f, 4.0f) != 1u) return 3;     /* off-screen: at least one pixel */
    if (no_attachment_extent(0.0f, 1e9f) != 16384u) return 4;  /* D3D12_REQ_RENDER_TO_BUFFER_WINDOW_WIDTH */
    if (no_attachment_extent(0.0f, 0.0f / 0.0f) != 1u) return 5;
    return 0;
}
"""
        compiler = shutil.which('clang++') or shutil.which('c++')
        self.assertIsNotNone(compiler)
        with tempfile.TemporaryDirectory() as tmp:
            cpp, exe = Path(tmp) / 'extent.cpp', Path(tmp) / 'extent'
            cpp.write_text(source)
            subprocess.run([compiler, '-std=c++11', str(cpp), '-o', str(exe)], check=True, capture_output=True)
            self.assertEqual(subprocess.run([str(exe)]).returncode, 0)

    def test_tier1_srv_upper_bound_is_usable(self):
        # EXP1378 follow-up: legal root SRV t127 was refused with reason5.
        # Execute the real ResolveRoot bound predicate, not a second planner.
        ddi = (ROOT / 'drivers/apple-agx/d3d12/agx_d3d12_ddi.cpp').read_text()
        limit = re.search(r'kSrvSlots = (\d+)', ddi)
        owner_limit = re.search(r'#define AGX_D3D12_SRV_SLOTS (\d+)',
                               OWNER.with_suffix('.h').read_text())
        self.assertEqual(limit.group(1), owner_limit.group(1))
        resolve = body(ddi, 'static UINT ResolveRoot(')
        predicate = re.search(r'if \((p.shader_register >= kSrvSlots)\) return 5u;', resolve)
        self.assertIsNotNone(predicate)
        source = r"""
static const unsigned kSrvSlots = LIMIT;
struct Parameter { unsigned shader_register; };
unsigned refused(unsigned reg) { Parameter p = {reg}; if (PREDICATE) return 5u; return 0u; }
int main() {
    for (unsigned reg = 0; reg < 128; ++reg) if (refused(reg)) return 1;
    return refused(128) == 5u && refused(0xffffffffu) == 5u ? 0 : 2;
}
""".replace('LIMIT', limit.group(1)).replace('PREDICATE', predicate.group(1))
        compiler = shutil.which('clang++') or shutil.which('c++')
        self.assertIsNotNone(compiler)
        with tempfile.TemporaryDirectory() as tmp:
            cpp, exe = Path(tmp) / 'slots.cpp', Path(tmp) / 'slots'
            cpp.write_text(source)
            subprocess.run([compiler, str(cpp), '-o', str(exe)], check=True, capture_output=True)
            result = subprocess.run([str(exe)], capture_output=True)
            self.assertEqual(result.returncode, 0, 'Tier1 root SRV t16..t127 must be usable')

    def test_minmax_variant_only_for_bound_minmax_samplers(self):
        # WHAT REAL BUG OR INVARIANT WILL THIS TEST CATCH? C65V: the heap
        # walk must read the reduction where AgxD3d12OwnerSamplerDescriptor
        # packs it (Valid bits 8-9), or a MINIMUM/MAXIMUM sampler silently
        # filters normally; the variant must keep the plain compile's slots
        # (root bindings were resolved against them) and a draw without
        # such a sampler must keep the plain pipeline.
        owner = OWNER.read_text()
        self.assertIn('out->Valid = static_cast<uint16_t>(1u | (dxbc_minmax_bits(sampler->Filter) << 8));', owner)
        ddi = (ROOT / 'drivers/apple-agx/d3d12/agx_d3d12_ddi.cpp').read_text()
        walk = body(ddi, 'static HRESULT SetStageHeapWalk(AGX_D3D12_OWNER *owner, unsigned stage, const AgxD3d12Dispatch *b, AgxD3d12HeapScratch **scratch,\n                                bool *minmax) {')
        self.assertIn('if (((h.samplers[h.binding.SamplerCount].Valid >> 8) & 3u) >= 2u) *minmax = true;', walk)
        variant = body(ddi, 'static void *MinmaxVariant(')
        self.assertIn('same = SUCCEEDED(hr) && SameGraphicsSlots(pso->minmax_graphics, pso->graphics);', variant)
        self.assertIn('desc.Flags = AGX_D3D12_SHADER_MINMAX;', variant)
        draw = body(ddi, 'static __declspec(noinline) HRESULT ReplayDraw(')
        self.assertIn('if (minmax)\n        if (void *variant = MinmaxVariant(owner, d.pso, false))', draw)
        dispatch = body(ddi, 'static __declspec(noinline) HRESULT ReplayDispatch(')
        self.assertIn('const void *variant = minmax ? MinmaxVariant(owner, dispatch.pso, true) : nullptr;', dispatch)

    def test_teardown_order(self):
        text = OWNER.read_text()
        destroy = body(text, 'extern "C" HRESULT AgxD3d12OwnerDestroy(')
        order = [destroy.index(k) for k in ('AgxWin32AsahiContextDestroy', 'AgxWin32AsahiScreenDestroy',
                                            'AdmissionUmdRuntimeDeviceFinalize', 'destroy_kernel_context(owner)')]
        self.assertEqual(order, sorted(order))

    def test_built_only_with_the_native_frontend(self):
        project = PROJECT.read_text()
        group = project[project.index("<ItemGroup Condition=\"'$(EnableNativeFrontend)' == 'true'\">"):]
        group = group[:group.index('</ItemGroup>')]
        self.assertIn('agx_d3d12_windows.cpp', group)
        self.assertIn('APPLE_AGX_D3D12_NATIVE_OWNER=1', project)


if __name__ == '__main__':
    unittest.main()
