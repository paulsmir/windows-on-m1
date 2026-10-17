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
        # shader buffers, ends with a full barrier (D3D12 orders the next
        # command after it) and drops the bindings so they never outlive a
        # heap.
        text = OWNER.read_text()
        create = body(text, 'extern "C" HRESULT AgxD3d12OwnerCreateCompute(')
        self.assertIn('dxbc_translate_tokens(', create)
        self.assertIn('dxbc_nir_compute_info(', create)
        self.assertIn('->create_compute_state(', create)
        dispatch = body(text, 'extern "C" HRESULT AgxD3d12OwnerDispatch(')
        order = [dispatch.index(k) for k in ('bind_stage(pipe, MESA_SHADER_COMPUTE, cbvs, srvs, uavs, views, &view_count)',
                                             'bind_compute_state', 'launch_grid', 'memory_barrier(pipe, PIPE_BARRIER_ALL)',
                                             'unbind_stage(pipe, MESA_SHADER_COMPUTE, views, view_count)')]
        self.assertEqual(order, sorted(order))
        # C13: UAVs of every stage bind in bind_stage (buffers u#, typed
        # images u#) and unbind_stage drops both so no binding outlives a heap
        stage_b = body(text, 'static HRESULT bind_stage(')
        self.assertIn('uavs[i].Offset >= buffer->Bytes', stage_b)
        self.assertIn('pipe->set_shader_images(pipe, stage, 0, AGX_D3D12_UAV_SLOTS, 0, images);', stage_b)
        unbind = body(text, 'static void unbind_stage(')
        self.assertIn('pipe->set_shader_buffers(pipe, stage, 0, AGX_D3D12_UAV_SLOTS, NULL, 0);', unbind)
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
        order = [dispatch.index(k) for k in ('bind_stage(pipe, MESA_SHADER_COMPUTE, cbvs, srvs, uavs, views, &view_count)',
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
        layers = body(text, 'static void clear_view_layers(')
        self.assertLess(layers.index('pipe->set_framebuffer_state(pipe, &fb);'), layers.index('pipe->clear(pipe, buffers'))
        self.assertIn('++owner->PendingDraws;', layers)

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
        self.assertIn('dxbc_translate_tokens(', body(text, 'static void *create_shader('))
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
        self.assertIn('templ.target = texture->target;', stage)

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
