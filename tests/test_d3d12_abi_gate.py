"""Pinned D3D12 ABI coverage; no hardware or packaged driver is involved."""
from pathlib import Path
import re
import unittest

ROOT = Path(__file__).resolve().parents[1]
HEADER = Path('/Volumes/pwdev/public_windows/.local/reference/wdk26100/d3d12umddi.h')
SOURCE = ROOT / 'drivers/apple-agx/d3d12/agx_d3d12_ddi.cpp'
TABLES = ('D3D12DDI_ADAPTERFUNCS', 'D3D12DDI_DEVICE_FUNCS_CORE_0003',
          'D3D12DDI_COMMAND_LIST_FUNCS_3D_0003',
          'D3D12DDI_COMMAND_QUEUE_FUNCS_CORE_0001')


def members(text, name):
    body = re.search(r'typedef struct ' + name + r'\s*\{(.*?)\}\s*' + name + r'\s*;', text, re.S)
    assert body, name
    return re.findall(r'\b(pfn\w+)\s*;', body.group(1))


class D3d12AbiGate(unittest.TestCase):
    def test_every_negotiated_member_assigned_once(self):
        if not HEADER.exists():
            self.skipTest('pinned WDK header absent')
        source = SOURCE.read_text()
        header = HEADER.read_text(errors='replace')
        for table in TABLES:
            with self.subTest(table=table):
                expected = members(header, table)
                assignments = re.findall(r'AGX_ASSIGN\(' + table + r',\s*(pfn\w+)\s*,', source)
                if table == 'D3D12DDI_COMMAND_QUEUE_FUNCS_CORE_0001':
                    assignments += re.findall(r'f\.(pfnUnused2?)\s*=\s*nullptr;', source)
                self.assertEqual(set(assignments), set(expected))
                self.assertEqual(len(assignments), len(expected))

    def test_adapter_handle_is_a_driver_object(self):
        # A3 (EXP1343): a NULL driver adapter handle made the runtime refuse
        # the adapter before GetSupportedVersions.
        source = SOURCE.read_text()
        self.assertNotIn('hAdapter.pDrvPrivate = nullptr', source)
        self.assertIn('arg->hAdapter.pDrvPrivate = &adapter_object;', source)
        self.assertIn('adapter_object.runtime = arg->hRTAdapter;', source)

    def test_no_unimplemented_feature_is_advertised(self):
        source = SOURCE.read_text()
        self.assertIn('D3D12DDI_SUPPORTED_0003', source)
        # C1: the development profile (APPLE_AGX_D3D12=1, checked first in
        # OpenAdapter12) answers only the 3D pipeline caps with FL 11_0.
        self.assertEqual(source.count('D3D12DDI_3DPIPELINELEVEL_11_0'), 1)
        self.assertIn('static const D3D12DDI_3DPIPELINELEVEL kProfileLevel = D3D12DDI_3DPIPELINELEVEL_11_0;', source)
        caps = source[source.index('static HRESULT APIENTRY GetCaps('):]
        caps = caps[:caps.index('\n}\n')]
        self.assertEqual(set(re.findall(r'case (D3D12DDICAPS_TYPE_\w+)', caps)),
                         {'D3D12DDICAPS_TYPE_0081_3DPIPELINESUPPORT1', 'D3D12DDICAPS_TYPE_3DPIPELINESUPPORT',
                          'D3D12DDICAPS_TYPE_D3D12_OPTIONS', 'D3D12DDICAPS_TYPE_MEMORY_ARCHITECTURE',
                          'D3D12DDICAPS_TYPE_SHADER', 'D3D12DDICAPS_TYPE_ARCHITECTURE_INFO',
                          'D3D12DDICAPS_TYPE_GPUVA_CAPS', 'D3D12DDICAPS_TYPE_TEXTURE_LAYOUT',
                          'D3D12DDICAPS_TYPE_TEXTURE_LAYOUT_SETS'})
        # nothing optional is claimed (plan minimum: tier 1, no optional features)
        for claim in ('D3D12DDI_RESOURCE_BINDING_TIER_2', 'D3D12DDI_RESOURCE_HEAP_TIER_2',
                      'Doubles = TRUE', 'ROVs = TRUE', 'OutputMergerLogicOp = TRUE',
                      'D3D12DDI_TILED_RESOURCES_TIER_1', 'D3D12DDI_CONSERVATIVE_RASTERIZATION_TIER_1'):
            self.assertNotIn(claim, caps)
        opener = source[source.index('extern "C" HRESULT APIENTRY OpenAdapter12('):]
        self.assertLess(opener.index('APPLE_AGX_D3D12'), opener.index('FillD3D12DDI_ADAPTERFUNCS'))
        self.assertIn('return E_NOTIMPL;', source)
        self.assertIn('pfnSetErrorCb', source)
        self.assertIn('pfnSetCommandListErrorCb', source)
        # EXP1347: a refused DDI leaves an unbudgeted receipt naming its
        # table and member before the runtime sees the error.
        self.assertEqual(source.count('Rejected<decltype'), 1)  # only AGX_REJECT
        stub = source[source.index('struct Rejected<R (APIENTRY *)(A...), Table, Offset>'):]
        self.assertLess(stub.index('AdmissionUmdDiagnostic("measure-d3d12-rejected"'),
                        stub.index('pfnSetErrorCb(runtime_device'))
        # C1d (EXP1348): device initialisation calls these; they answer
        # without SetErrorCb and claim no optional capability.
        for member in ('pfnCheckFormatSupport', 'pfnCheckMultisampleQualityLevels', 'pfnQueryNodeMap'):
            self.assertIn('AGX_ASSIGN(D3D12DDI_DEVICE_FUNCS_CORE_0003, %s, %s);' % (member, member[3:]), source)
        self.assertIn('if (support) *support = 0;', source)
        self.assertIn('*levels = samples == 1 ? 1u : 0u;', source)
        self.assertIn('map[i] = 0;', source)
        # C1e (EXP1349 + d3d12core 26100 row-major validation): SubCaps[0]
        # nonzero, alignments nonzero powers of two within the runtime limits.
        sets = caps[caps.index('case D3D12DDICAPS_TYPE_TEXTURE_LAYOUT_SETS:'):]
        sets = sets[:sets.index('break;')]
        fields = dict(re.findall(r'd\.SubCaps\[0\]\.(\w+) = (\d+);', sets))
        self.assertEqual(set(fields), {'MaxElementSize', 'BaseOffsetAlignment',
                                       'PitchAlignment', 'DepthPitchAlignment'})
        limits = {'MaxElementSize': 0xffff, 'BaseOffsetAlignment': 512,
                  'PitchAlignment': 256, 'DepthPitchAlignment': 256}
        for name, value in fields.items():
            value = int(value)
            self.assertTrue(0 < value <= limits[name], name)
            if name != 'MaxElementSize':
                self.assertEqual(value & (value - 1), 0, name)
        self.assertNotIn('SubCaps[1]', sets)
        self.assertNotIn('d.Flags', sets)
        # C1f (EXP1350): the runtime creates a root signature during device
        # initialisation; the driver records the D3D12 root argument cost.
        for member in ('pfnCalcPrivateRootSignatureSize', 'pfnCreateRootSignature', 'pfnDestroyRootSignature'):
            self.assertIn('AGX_ASSIGN(D3D12DDI_DEVICE_FUNCS_CORE_0003, %s, %s);' % (member, member[3:]), source)
        cost = source[source.index('static UINT RootParameterDwords('):]
        cost = cost[:cost.index('\n}\n')]
        self.assertIn('D3D12DDI_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE: return 1;', cost)
        self.assertIn('D3D12DDI_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS: return p.Constants.Num32BitValues;', cost)
        self.assertIn('default: return 2;', cost)
        size = source[source.index('static SIZE_T APIENTRY CalcPrivateRootSignatureSize('):]
        size = size[:size.index('\n}\n')]
        self.assertIn('NumParameters', size)
        # C1g (EXP1351): shader objects keep the tokenized program (length in
        # pShaderCode[1]); hull/domain and GS stream output stay refused.
        for member, fn in (('pfnCalcPrivateShaderSize', 'CalcPrivateShaderSize'),
                           ('pfnCreateVertexShader', 'CreateShader'), ('pfnCreatePixelShader', 'CreateShader'),
                           ('pfnCreateGeometryShader', 'CreateShader'),
                           ('pfnCreateComputeShader', 'CreateComputeShader'), ('pfnDestroyShader', 'DestroyShader')):
            self.assertIn('AGX_ASSIGN(D3D12DDI_DEVICE_FUNCS_CORE_0003, %s, %s);' % (member, fn), source)
        for member in ('pfnCreateHullShader', 'pfnCreateDomainShader', 'pfnCreateGeometryShaderWithStreamOutput'):
            self.assertIn('AGX_REJECT(D3D12DDI_DEVICE_FUNCS_CORE_0003, %s)' % member, source)
        self.assertIn('memcpy(shader->tokens, code, code[1] * sizeof(UINT));', source)
        self.assertIn('(dwords ? dwords : 1) * sizeof(UINT)', source)
        # C1h (EXP1352): the four state objects a graphics pipeline state
        # references keep copies of their descriptions.
        for member in ('ElementLayout', 'BlendState', 'DepthStencilState', 'RasterizerState'):
            for prefix in ('pfnCalcPrivate%sSize', 'pfnCreate%s', 'pfnDestroy%s'):
                name = prefix % member
                self.assertNotIn('AGX_REJECT(D3D12DDI_DEVICE_FUNCS_CORE_0003, %s)' % name, source)
        for kind, desc in ((1, 'D3D12DDI_BLEND_DESC'), (2, 'D3D12DDI_DEPTH_STENCIL_DESC'), (3, 'D3D12DDI_RASTERIZER_DESC')):
            self.assertIn('AgxD3d12State<%s, %d>' % (desc, kind), source)
        for member in ('pfnCalcPrivatePipelineStateSize', 'pfnCreatePipelineState', 'pfnDestroyPipelineState'):
            self.assertIn('AGX_ASSIGN(D3D12DDI_DEVICE_FUNCS_CORE_0003, %s, %s);' % (member, member[3:]), source)
        # C1i (EXP1353): residency succeeds while no object owns a kernel
        # allocation; heap creation (which would) is still refused.
        for member in ('pfnMakeResident', 'pfnEvict'):
            self.assertIn('AGX_ASSIGN(D3D12DDI_DEVICE_FUNCS_CORE_0003, %s, %s);' % (member, member[3:]), source)
        self.assertIn('arg->WaitMask = 0;', source)
        # C2a: committed heaps are one KMD General-class allocation, resident
        # and mapped until destroyed (so residency stays a no-op), inside the
        # GPUVA winsys window; CPU-visible heaps obey the 16 MiB class bound;
        # textures and placed resources are refused.
        for member, fn in (('pfnCheckResourceAllocationInfo', 'CheckResourceAllocationInfo'),
                           ('pfnCalcPrivateHeapAndResourceSizes', 'CalcPrivateHeapAndResourceSizes'),
                           ('pfnCreateHeapAndResource', 'CreateHeapAndResource'),
                           ('pfnDestroyHeapAndResource', 'DestroyHeapAndResource'),
                           ('pfnMapHeap', 'MapHeap'), ('pfnUnmapHeap', 'UnmapHeap'),
                           ('pfnCheckResourceVirtualAddress', 'CheckResourceVirtualAddress')):
            self.assertIn('AGX_ASSIGN(D3D12DDI_DEVICE_FUNCS_CORE_0003, %s, %s);' % (member, fn), source)
        heap = source[source.index('static HRESULT CreateHeap('):]
        heap = heap[:heap.index('\n}\n')]
        self.assertIn('create.ClassId = AgxWin32BufferClassGeneral;', heap)
        self.assertIn('resident.Flags.MustSucceed = 1;', heap)
        self.assertLess(heap.index('k->pfnMakeResidentCb(device'), heap.index('k->pfnMapGpuVirtualAddressCb(device'))
        self.assertIn('map.SizeInPages = heap->bytes >> 12;', heap)
        self.assertIn('heap->cpu_visible ? kCpuVisibleMaximum', heap)
        self.assertIn('static constexpr UINT64 kVaMinimum = 0x10000ULL;', source)
        self.assertIn('static constexpr UINT64 kVaLimit = 1ULL << 39;', source)
        self.assertIn('static constexpr UINT64 kCpuVisibleMaximum = ADMISSION_WIN32_ALLOCATION_LEGACY_MAX_BYTES;', source)
        self.assertIn('info->ResourceDataSize = ~0ULL;', source)
        create = source[source.index('static HRESULT APIENTRY CreateHeapAndResource('):]
        create = create[:create.index('\n}\n')]
        self.assertIn('return E_NOTIMPL;', create)
        # EXP1356: a committed heap's ByteSize can be >= 2^48; it is sized by
        # its resource and the rounding cannot wrap to 0.
        self.assertIn('sized.ByteSize = (resource_arg->Width + kHeapPage - 1) & ~(kHeapPage - 1);', create)
        self.assertIn('heap->bytes = arg.ByteSize > kVaLimit ? ~0ULL :', heap)
        # EXP1357: the runtime asks an existing resource's allocation info
        # and kernel allocation handle before Map.
        self.assertIn('AGX_ASSIGN(D3D12DDI_DEVICE_FUNCS_CORE_0003, pfnCheckExistingResourceAllocationInfo, '
                      'CheckExistingResourceAllocationInfo);', source)
        self.assertIn('*allocation = resource && resource->heap ? resource->heap->allocation : 0;', source)
        # EXP1358: a buffer is one subresource at offset 0 spanning its width
        self.assertIn('AGX_ASSIGN(D3D12DDI_DEVICE_FUNCS_CORE_0003, pfnCheckSubresourceInfo, CheckSubresourceInfo);', source)
        self.assertIn('if (resource && subresource == 0) info->RowStride = info->DepthStride = resource->bytes;', source)
        self.assertIn('D3D12DDI_RT_BUFFER', create)
        # the paging queue is created with the first heap, not with the device
        device = source[source.index('static HRESULT APIENTRY CreateDevice('):]
        device = device[:device.index('\n}\n')]
        self.assertNotIn('PagingQueue', device)
        # C1j (EXP1354): descriptor sizes hold the AGX descriptors (texture
        # 24 + PBE 24 + buffer record; sampler 8 + border colour 16); views
        # written into heaps stay refused until implemented.
        sizes = source[source.index('static UINT DescriptorBytes('):]
        sizes = sizes[:sizes.index('\n}\n')]
        # C6e: every descriptor is a 64-byte CPU record (a sampler record is
        # kind + D3D12DDI_SAMPLER_DESC, 56 bytes)
        self.assertIn('return 64u;', sizes)
        for member in ('pfnGetDescriptorSizeInBytes', 'pfnCalcPrivateDescriptorHeapSize', 'pfnCreateDescriptorHeap',
                       'pfnDestroyDescriptorHeap', 'pfnGetCPUDescriptorHandleForHeapStart',
                       'pfnGetGPUDescriptorHandleForHeapStart'):
            self.assertIn('AGX_ASSIGN(D3D12DDI_DEVICE_FUNCS_CORE_0003, %s, %s);' % (member, member[3:]), source)
        # C5c/C5d implement CBV and buffer UAV/SRV views (test_c5c_descriptor_tables)
        # C6f: every view DDI is implemented (DSVs too)
        self.assertIn('AGX_ASSIGN(D3D12DDI_DEVICE_FUNCS_CORE_0003, pfnCreateDepthStencilView, CreateDepthStencilView);', source)
        heap = source[source.index('static HRESULT APIENTRY CreateDescriptorHeap('):]
        heap = heap[:heap.index('\n}\n')]
        self.assertIn('D3D12DDI_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE', heap)
        self.assertIn('hr = CreateHeap(device, memory, &heap->memory);', heap)
        # C3a (EXP1360): queue and fence objects record their arguments; no
        # submission exists, so queue-table Signal/Wait/Execute stay refused.
        for member in ('pfnCalcPrivateCommandQueueSize', 'pfnCreateCommandQueue', 'pfnDestroyCommandQueue',
                       'pfnCalcPrivateFenceSize', 'pfnCreateFence', 'pfnDestroyFence'):
            self.assertIn('AGX_ASSIGN(D3D12DDI_DEVICE_FUNCS_CORE_0003, %s, %s);' % (member, member[3:]), source)
        # C3b (EXP1361): the runtime signals through the kernel contexts a
        # queue owns; each queue owns one created for its runtime handle.
        queue = source[source.index('static HRESULT APIENTRY CreateCommandQueue('):]
        queue = queue[:queue.index('\n}\n')]
        self.assertIn('device->callbacks->pfnCreateContextVirtualCb(arg->hRTCommandQueue, &context)', queue)
        self.assertIn('contract.Magic = ADMISSION_WIN32_CONTEXT_MAGIC;', queue)
        # EXP1365: one Win32 generation per D3D12 device for all its contexts
        self.assertIn('contract.Generation = device->generation;', queue)
        self.assertIn('args.Generation = device->generation;', source)
        self.assertIn('pfnDestroyContextCb(queue->runtime, &destroy)', source)
        for member in ('pfnSignalFence', 'pfnWaitForFence'):
            self.assertIn('AGX_REJECT(D3D12DDI_COMMAND_QUEUE_FUNCS_CORE_0001, %s)' % member, source)
        # C4a: allocators and lists exist; a list records nothing yet, so
        # ExecuteCommandLists has no GPU work; every recording DDI but
        # Close/Reset stays fail-closed.
        self.assertIn('AGX_ASSIGN(D3D12DDI_COMMAND_QUEUE_FUNCS_CORE_0001, pfnExecuteCommandLists, ExecuteCommandLists);', source)
        self.assertIn('device->callbacks->pfnSetCommandListDDITableCb(arg->hRTCommandList, list_table);', source)
        for member in ('pfnCalcPrivateCommandAllocatorSize', 'pfnCreateCommandAllocator', 'pfnDestroyCommandAllocator',
                       'pfnResetCommandAllocator', 'pfnCalcPrivateCommandListSize', 'pfnCreateCommandList',
                       'pfnDestroyCommandList'):
            self.assertIn('AGX_ASSIGN(D3D12DDI_DEVICE_FUNCS_CORE_0003, %s, %s);' % (member, member[3:]), source)
        # C4b: heaps are buffers of the native owner (winsys BOs) so copies
        # and their residency see them; CopyBufferRegion records heap-relative
        # ranges and ExecuteCommandLists replays them through the owner, then
        # waits, before the runtime's queue signal.
        self.assertIn('#include "agx_d3d12_windows.h"', source)
        create = source[source.index('static HRESULT CreateHeap('):]
        create = create[:create.index('\n}\n')]
        self.assertLess(create.index('AgxD3d12OwnerCreateBuffer'), create.index('EXP1355: every early exit'))
        copy = source[source.index('static VOID APIENTRY CopyBufferRegion('):]
        copy = copy[:copy.index('\n}\n')]
        self.assertIn('bytes > d->bytes - dst_offset', copy)
        self.assertIn('bytes > r->bytes - src_offset', copy)
        self.assertIn('list->commands >= kListCommands', copy)
        execute = source[source.index('static VOID APIENTRY ExecuteCommandLists('):]
        execute = execute[:execute.index('\n}\n')]
        self.assertLess(execute.index('AgxD3d12OwnerCopy('), execute.index('AgxD3d12OwnerSubmitAndWait('))
        # C5: copies and dispatches replay in recording order through one loop
        self.assertLess(execute.index('ReplayDispatch(owner, command.dispatch)'), execute.index('AgxD3d12OwnerSubmitAndWait('))
        self.assertLess(execute.index('ReplayDraw(owner, command.draw)'), execute.index('AgxD3d12OwnerSubmitAndWait('))
        self.assertIn('command.kind == kCommandCopy', execute)
        # nothing can bind it yet: every command-list DDI stays fail-closed
        implemented = set(re.findall(r'AGX_ASSIGN\(D3D12DDI_COMMAND_LIST_FUNCS_3D_0003, (\w+), (?!AGX_REJECT)', source))
        # EXP1363: the runtime's default-state setters are accepted (recorded
        # as received state); every state consumer stays fail-closed.
        setters = {'pfnSetPredication', 'pfnSetDescriptorHeaps', 'pfnSOSetTargets'}
        compute = {'pfnSetPipelineState', 'pfnSetComputeRootSignature', 'pfnSetComputeRootUnorderedAccessView',
                   'pfnDispatch', 'pfnClearRootArguments', 'pfnSetComputeRoot32BitConstant',
                   'pfnSetComputeRoot32BitConstants', 'pfnSetComputeRootConstantBufferView',
                   'pfnSetComputeRootDescriptorTable', 'pfnSetComputeRootShaderResourceView'}
        compute |= {'pfnClearRenderTargetView', 'pfnCopyTextureRegion'}  # C6a
        # C6b: graphics state setters (real; each still records its default-state bit) and DrawInstanced
        compute |= {'pfnIaSetTopology', 'pfnRsSetViewports', 'pfnRsSetScissorRects', 'pfnOmSetBlendFactor',
                    'pfnOmSetStencilRef', 'pfnOMSetRenderTargets', 'pfnIASetVertexBuffers', 'pfnDrawInstanced',
                    'pfnSetGraphicsRootSignature'}
        # C6c: graphics root arguments
        compute |= {'pfnSetGraphicsRoot32BitConstant', 'pfnSetGraphicsRoot32BitConstants',
                    'pfnSetGraphicsRootConstantBufferView', 'pfnSetGraphicsRootShaderResourceView',
                    'pfnSetGraphicsRootDescriptorTable'}
        compute |= {'pfnIASetIndexBuffer', 'pfnDrawIndexedInstanced'}  # C6d
        compute |= {'pfnClearDepthStencilView'}  # C6f
        compute |= {'pfnResourceCopy'}  # C8b
        self.assertEqual(implemented, {'pfnCloseCommandList', 'pfnResetCommandList', 'pfnCopyBufferRegion',
                                       'pfnResourceBarrier'} | setters | compute)
        for member in setters:
            self.assertIn('AGX_LIST_STATE(%s)' % member, source)
        for consumer in ('pfnExecuteIndirect', 'pfnExecuteBundle'):
            self.assertIn('AGX_REJECT(D3D12DDI_COMMAND_LIST_FUNCS_3D_0003, %s)' % consumer, source)

    def test_c5_compute_dispatch(self):
        # C5 (D3D12-C5-compute.md). Invariants: the pipeline keeps its own
        # copy of the compute program (the runtime may destroy the shader
        # object first); a new root signature resets the root arguments; a
        # root UAV argument must name a UAV parameter; Dispatch binds only
        # root UAVs in register space 0 at slot u# and refuses (fail-closed,
        # with a receipt) any other root parameter until C5b, and resolves
        # each root VA to a registered owner heap.
        source = SOURCE.read_text()
        def fn(name):
            start = source.index(name)
            return source[start:source.index('\n}\n', start)]
        size = fn('static SIZE_T APIENTRY CalcPrivatePipelineStateSize(')
        self.assertIn('cs->dwords', size)
        create = fn('static HRESULT APIENTRY CreatePipelineState(')
        self.assertIn('memcpy(pso->cs, cs->tokens, cs->dwords * sizeof(UINT));', create)
        root = fn('static VOID APIENTRY SetComputeRootSignature(')
        self.assertIn('memset(list->compute_root_va, 0, sizeof(list->compute_root_va));', root)
        uav = fn('static VOID APIENTRY SetComputeRootUnorderedAccessView(')
        self.assertIn('D3D12DDI_ROOT_PARAMETER_TYPE_UAV', uav)
        view = fn('static void SetRootBufferView(')
        self.assertIn('root->parameter[index].type != type', view)
        self.assertIn('index >= root->parameters', view)
        dispatch = fn('static VOID APIENTRY Dispatch(')
        self.assertIn('ResolveRoot(list, root, list->compute_root_va, list->compute_root_dwords, kAnyStage, &dispatch)', dispatch)
        resolve = fn('static UINT ResolveRoot(')
        self.assertIn('if (p.shader_register >= kSrvSlots) return 5u;', resolve)
        self.assertIn('BindTable(list, root, p, va[i], out)', resolve)
        self.assertIn('if (p.shader_register >= kUavSlots) return 5u;', resolve)
        self.assertIn('HeapAt(list->device, va[i], &out->uav_offset[p.shader_register])', resolve)
        # C5b: root CBVs resolve like root UAVs; root constants keep their
        # parameter's DWORD range, copied into the recorded dispatch
        self.assertIn('if (p.shader_register >= kCbSlots) return 5u;', resolve)
        self.assertIn('p.offset + p.dwords > kRootArguments', resolve)
        self.assertIn('memcpy(out->root_dwords, dwords, kRootArguments * sizeof(UINT));', resolve)
        constants = fn('static void SetRoot32BitConstants(')
        self.assertIn('count > root->parameter[index].dwords - offset', constants)
        self.assertIn('!= D3D12DDI_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS', constants)
        clear = fn('static VOID APIENTRY ClearRootArguments(')
        self.assertIn('memset(list->compute_root_dwords, 0', clear)
        self.assertNotIn('compute_root =', clear)
        self.assertIn('"measure-d3d12-dispatch"', dispatch)
        self.assertIn('list->commands >= kListCommands', dispatch)
        reset = fn('static VOID APIENTRY ResetCommandList(')
        self.assertIn('list->compute_root = nullptr;', reset)
        # owner heaps are registered while they exist
        self.assertIn('RegisterHeap(device, heap);', fn('static HRESULT CreateHeap('))
        self.assertIn('UnregisterHeap(device, heap);', fn('static void ReleaseHeap('))
        destroy = fn('static VOID APIENTRY DestroyPipelineState(D3D12DDI_HDEVICE handle')
        self.assertIn('AgxD3d12OwnerDestroyCompute(', destroy)

    def test_c5c_descriptor_tables(self):
        # C5c invariants: the root signature sizes and keeps every table
        # range with APPEND offsets resolved; a table binds only CBV/UAV
        # ranges in space 0 inside its slot counts and its descriptor heap;
        # descriptors are read at execution (root signature 1.0 descriptors
        # are volatile until then); views record the buffer heap and byte
        # range and refuse typed/texture/counter UAVs with a receipt; copies
        # move whole descriptors.
        source = SOURCE.read_text()
        def fn(name):
            start = source.index(name)
            return source[start:source.index('\n}\n', start)]
        size = fn('static SIZE_T APIENTRY CalcPrivateRootSignatureSize(')
        self.assertIn('ranges * sizeof(AgxD3d12Range)', size)
        create = fn('static HRESULT APIENTRY CreateRootSignature(')
        self.assertIn('D3D12DDI_DESCRIPTOR_RANGE_OFFSET_APPEND', create)
        self.assertIn('? next : in.OffsetInDescriptorsFromTableStart', create)
        table = fn('static UINT BindTable(')
        self.assertIn('range.type != D3D12DDI_DESCRIPTOR_RANGE_TYPE_CBV) return 4u;', table)
        self.assertIn('uav ? kUavSlots : srv ? kSrvSlots : kCbSlots', table)
        self.assertIn('range.space || range.base_register >= slots', table)
        self.assertIn('at + sizeof(AgxD3d12Descriptor) > heap->bytes', table)
        execute = fn('static void OwnerBindings(')
        self.assertIn('view->kind == kDescriptorUavBuffer', execute)
        self.assertIn('view->kind == kDescriptorCbv', execute)
        uav = fn('static VOID APIENTRY CreateUnorderedAccessView(')
        for reason in ('!= D3D12DDI_RD_BUFFER ? 4u', '!stride ? 5u', 'hDrvCounterResource.pDrvPrivate ? 7u',
                       'bytes > resource->bytes - offset'):
            self.assertIn(reason, uav)
        self.assertIn('"measure-d3d12-uav"', uav)
        cbv = fn('static VOID APIENTRY CreateConstantBufferView(')
        self.assertIn('HeapAt(device, desc->BufferLocation, &offset)', cbv)
        self.assertIn('static_cast<SIZE_T>(count) * DescriptorBytes(type)', fn('static VOID APIENTRY CopyDescriptorsSimple('))
        # C5d: buffer SRVs: raw (4-byte words) or structured; typed and
        # texture SRVs refuse with a receipt
        srv = fn('static VOID APIENTRY CreateShaderResourceView(')
        for reason in ('!= D3D12DDI_RD_BUFFER ? 4u', '!stride ? 5u', 'bytes > resource->bytes - offset'):
            self.assertIn(reason, srv)
        self.assertIn('"measure-d3d12-srv"', srv)
        self.assertIn('view->kind == kDescriptorSrvBuffer', execute)
        for member in ('pfnCreateConstantBufferView', 'pfnCreateUnorderedAccessView', 'pfnCopyDescriptors',
                       'pfnCopyDescriptorsSimple', 'pfnCreateShaderResourceView'):
            self.assertIn('AGX_ASSIGN(D3D12DDI_DEVICE_FUNCS_CORE_0003, %s, %s);' % (member, member[3:]), source)

    def test_c6a_textures(self):
        # C6a invariants (D3D12-C6-graphics.md): only single-sample 2D
        # textures of formats the owner creates get an allocation size (any
        # other texture keeps UINT64_MAX, which refuses creation); a texture
        # is one owner texture and never a VA range; RTVs are read when a
        # clear is recorded; CopyTextureRegion handles exactly one buffer
        # footprint side and one texture subresource side (subresource =
        # mip + slice * mips) and refuses others with a receipt.
        source = SOURCE.read_text()
        def fn(name):
            start = source.index(name)
            return source[start:source.index('\n}\n', start)]
        size = fn('static UINT64 TextureBytes(')
        for guard in ('arg->ResourceType != D3D12DDI_RT_TEXTURE2D', 'arg->SampleDesc.Count != 1',
                      'AgxD3d12FormatBlockBytes(arg->Format)'):
            self.assertIn(guard, size)
        info = fn('static VOID APIENTRY CheckResourceAllocationInfo(')
        self.assertIn('info->ResourceDataSize = ~0ULL;', info)
        create = fn('static HRESULT APIENTRY CreateHeapAndResource(')
        self.assertIn('AgxD3d12OwnerCreateTexture2D(', create)
        texture_path = create[create.index('AgxD3d12OwnerCreateTexture2D('):create.index('return hr;')]
        self.assertNotIn('RegisterHeap', texture_path)
        clear = fn('static VOID APIENTRY ClearRenderTargetView(')
        self.assertIn('command.clear.rtv = *rtv;', clear)
        self.assertIn('rtv->kind != kDescriptorRtv', clear)
        copy = fn('static VOID APIENTRY CopyTextureRegion(')
        self.assertIn('dst.footprint && src.footprint ? 4u /* buffer <-> buffer footprints */', copy)
        # C8b: texture <-> texture regions record a GPU region copy
        self.assertIn('slot->kind = kCommandTextureRegionCopy;', copy)
        self.assertIn('copy.level = sub % texture.resource->levels;', copy)
        self.assertIn('copy.layer = sub / texture.resource->levels;', copy)
        self.assertIn('"measure-d3d12-copy-texture"', copy)
        side = fn('static bool CopySide(')
        self.assertIn('D3D12DDI_RL_SELECT_SUBRESOURCE', side)
        self.assertIn('D3D12DDI_RL_PLACED_PHYSICAL_SUBRESOURCE_PITCHED', side)
        execute = fn('static VOID APIENTRY ExecuteCommandLists(')
        self.assertIn('AgxD3d12OwnerClearRenderTarget(', execute)
        self.assertIn('AgxD3d12OwnerCopyTexture(', execute)
        # buffers carry their type so a footprint side is never a texture
        self.assertIn('resource->type = D3D12DDI_RT_BUFFER;', create)

    def test_c6b_draw(self):
        # C6b invariants: a pipeline keeps copies of its VS/PS programs and
        # state descriptions (the runtime may destroy the objects); RTVs are
        # read when OMSetRenderTargets is recorded; every real setter still
        # records its default-state bit (EXP1363); a draw snapshots the list
        # state and refuses graphics root parameters, GS/HS/DS and a missing
        # RTV with a receipt; depth-stencil views refuse until C6c.
        source = SOURCE.read_text()
        def fn(name):
            start = source.index(name)
            return source[start:source.index('\n}\n', start)]
        size = fn('static SIZE_T APIENTRY CalcPrivatePipelineStateSize(')
        self.assertIn('(vs ? vs->dwords : 0) + (ps ? ps->dwords : 0)', size)
        create = fn('static HRESULT APIENTRY CreatePipelineState(')
        for copy in ('memcpy(pso->cs + pso->cs_dwords, vs->tokens', 'memcpy(&pso->blend,', 'memcpy(&pso->rasterizer,',
                     'memcpy(&pso->depth_stencil,', 'memcpy(pso->element, layout->element'):
            self.assertIn(copy, create)
        rt = fn('static VOID APIENTRY OMSetRenderTargets(')
        self.assertIn('list->rtv[i] = view && view->kind == kDescriptorRtv ? *view', rt)
        self.assertIn('AGX_LIST_BIT(pfnOMSetRenderTargets)', rt)
        for setter, bit in (('RsSetViewports', 'pfnRsSetViewports'), ('RsSetScissorRects', 'pfnRsSetScissorRects'),
                            ('IaSetTopology', 'pfnIaSetTopology'), ('OmSetBlendFactor', 'pfnOmSetBlendFactor'),
                            ('OmSetStencilRef', 'pfnOmSetStencilRef'), ('IASetVertexBuffers', 'pfnIASetVertexBuffers')):
            self.assertIn('AGX_LIST_BIT(%s)' % bit, fn('static VOID APIENTRY %s(' % setter))
        draw = fn('static void RecordDraw(')
        # C6c: the graphics root signature resolves per stage by visibility
        self.assertIn('D3D12DDI_SHADER_VISIBILITY_VERTEX, &vs_bind);', draw)
        self.assertIn('D3D12DDI_SHADER_VISIBILITY_PIXEL, &ps_bind);', draw)
        resolve = fn('static UINT ResolveRoot(')
        self.assertIn('p.visibility != D3D12DDI_SHADER_VISIBILITY_ALL && p.visibility != stage) continue;', resolve)
        self.assertIn('if (stage != kAnyStage) return 4u; /* graphics UAVs: not yet */', resolve)
        sig = fn('static VOID APIENTRY SetGraphicsRootSignature(')
        self.assertIn('memset(list->graphics_root_va, 0', sig)
        replay = fn('static __declspec(noinline) HRESULT ReplayDraw(')
        self.assertIn('OwnerBindings(d.vs_bind, &vs_bind);', replay)
        self.assertIn('OwnerBindings(d.ps_bind, &ps_bind);', replay)
        # the draw's owner records live on the heap, not in the replay frame
        # (STACK_FRAMES gate, EXP1369 build: 12944 bytes before)
        self.assertIn('HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(AgxD3d12DrawScratch))', replay)
        self.assertIn('pso->desc.hGeometryShader.pDrvPrivate', draw)
        self.assertIn('"measure-d3d12-draw"', draw)
        self.assertIn('memcpy(draw.vb, list->vb, sizeof(draw.vb));', draw)
        self.assertIn('AgxD3d12OwnerDraw(', fn('static __declspec(noinline) HRESULT ReplayDraw('))
        # recorded commands share storage
        self.assertIn('    union {\n        AgxD3d12Copy copy;', source)

    def test_c6d_indexed(self):
        # C6d invariants: only R16_UINT/R32_UINT index buffers bind (VA ->
        # owner heap); an indexed draw without one refuses; the strip cut
        # value comes from the pipeline's IBStripCutValue; the owner turns the
        # index buffer's byte offset into whole indices and refuses a
        # misaligned one.
        source = SOURCE.read_text()
        def fn(name):
            start = source.index(name)
            return source[start:source.index('\n}\n', start)]
        ib = fn('static VOID APIENTRY IASetIndexBuffer(')
        self.assertIn('DXGI_FORMAT_R16_UINT ? 2u : view->Format == DXGI_FORMAT_R32_UINT ? 4u : 0u', ib)
        self.assertIn('AGX_LIST_BIT(pfnIASetIndexBuffer)', ib)
        self.assertIn('indexed && !list->index_heap ? 6u', fn('static void RecordDraw('))
        self.assertIn('d.pso->desc.IBStripCutValue', fn('static __declspec(noinline) HRESULT ReplayDraw('))
        owner = (ROOT / 'drivers/apple-agx/mesa/winsys/agx_d3d12_windows.cpp').read_text()
        draw = owner[owner.index('extern "C" HRESULT AgxD3d12OwnerDraw('):]
        draw = draw[:draw.index('\n}\n')]
        self.assertIn('draw->IndexOffset % draw->IndexSize', draw)
        self.assertIn('range.start += static_cast<unsigned>(draw->IndexOffset / draw->IndexSize);', draw)
        self.assertIn('range.index_bias = draw->BaseVertex;', draw)

    def test_c6e_textures_and_samplers(self):
        # C6e invariants: 2D texture SRVs record format/mips/layers (clamped
        # to the resource); samplers are records of their description; root
        # signatures keep their static samplers (sized and copied); sampler
        # tables and static samplers resolve per stage visibility; compute
        # refuses samplers until supported; static border colours map to
        # the D3D12 constants.
        source = SOURCE.read_text()
        def fn(name):
            start = source.index(name)
            return source[start:source.index('\n}\n', start)]
        srv = fn('static VOID APIENTRY CreateShaderResourceView(')
        self.assertIn('out->kind = kDescriptorSrvTexture;', srv)
        self.assertIn('resource->levels - v.MostDetailedMip', srv)
        self.assertIn('out->desc = *arg->pSamplerDesc;', fn('static VOID APIENTRY CreateSampler('))
        self.assertIn('samplers * sizeof(D3D12DDI_STATIC_SAMPLER)', fn('static SIZE_T APIENTRY CalcPrivateRootSignatureSize('))
        self.assertIn('memcpy(StaticSamplersOf(root), desc.pStaticSamplers', fn('static HRESULT APIENTRY CreateRootSignature('))
        resolve = fn('static UINT ResolveRoot(')
        self.assertIn('out->static_sampler[s.ShaderRegister] = &s;', resolve)
        self.assertIn('return 4u; /* compute samplers: not yet */', resolve)
        table = fn('static UINT BindTable(')
        self.assertIn('D3D12DDI_DESCRIPTOR_RANGE_TYPE_SAMPLER', table)
        bindings = fn('static void OwnerBindings(')
        self.assertIn('view->kind == kDescriptorSrvTexture', bindings)
        self.assertIn('{0, 0, 0, 0}, {0, 0, 0, 1}, {1, 1, 1, 1}, {0, 0, 0, 1}, {1, 1, 1, 1}', bindings)
        self.assertIn('AGX_ASSIGN(D3D12DDI_DEVICE_FUNCS_CORE_0003, pfnCreateSampler, CreateSampler);', source)

    def test_c6f_depth(self):
        # C6f invariants: depth textures carry the depth-stencil bind (typeless
        # formats translate as depth); DSVs are read when OMSetRenderTargets
        # or ClearDepthStencilView is recorded; a draw needs an RTV or a DSV;
        # a clear needs the depth or stencil flag.
        source = SOURCE.read_text()
        def fn(name):
            start = source.index(name)
            return source[start:source.index('\n}\n', start)]
        self.assertIn('D3D12DDI_RESOURCE_FLAG_0003_DEPTH_STENCIL) ? AGX_D3D12_BIND_DEPTH_STENCIL', fn('static HRESULT APIENTRY CreateHeapAndResource('))
        self.assertIn('out->kind = kDescriptorDsv;', fn('static VOID APIENTRY CreateDepthStencilView('))
        rt = fn('static VOID APIENTRY OMSetRenderTargets(')
        self.assertIn('if (view->kind == kDescriptorDsv) list->dsv = *view;', rt)
        clear = fn('static VOID APIENTRY ClearDepthStencilView(')
        self.assertIn('command.clear_depth.dsv = *dsv;', clear)
        self.assertIn('!(flags & 3u)', clear)
        self.assertIn("list->dsv.kind != kDescriptorDsv ? 5u", fn('static void RecordDraw('))
        owner = (ROOT / 'drivers/apple-agx/mesa/winsys/agx_d3d12_windows.cpp').read_text()
        self.assertIn('FormatTranslate(format, (bind & AGX_D3D12_BIND_DEPTH_STENCIL) ? TRUE : FALSE)', owner)
        self.assertIn('fb.zsbuf.texture = texture;', owner)
        self.assertIn('desc->swizzle[1] != PIPE_SWIZZLE_NONE', owner)  # stencil only where present

    def test_c8b_resource_copy(self):
        # C8b invariants: CopyResource copies whole buffers (the smaller size)
        # or every common mip/layer of two 2D textures; buffer <-> texture
        # refuses; texture copies replay through the owner's GPU region copy.
        source = SOURCE.read_text()
        def fn(name):
            start = source.index(name)
            return source[start:source.index('\n}\n', start)]
        rc = fn('static VOID APIENTRY ResourceCopy(')
        self.assertIn('(d->type == D3D12DDI_RT_TEXTURE2D) != (s->type == D3D12DDI_RT_TEXTURE2D) ? 4u', rc)
        self.assertIn('rc.whole_levels = s->levels < d->levels ? s->levels : d->levels;', rc)
        self.assertIn('d->bytes < s->bytes ? d->bytes : s->bytes', rc)
        self.assertIn('AgxD3d12OwnerCopyTextureRegion(', fn('static VOID APIENTRY ExecuteCommandLists('))
        owner = (ROOT / 'drivers/apple-agx/mesa/winsys/agx_d3d12_windows.cpp').read_text()
        region = owner[owner.index('extern "C" HRESULT AgxD3d12OwnerCopyTextureRegion('):]
        region = region[:region.index('\n}\n')]
        self.assertIn('->resource_copy_region(', region)
        self.assertIn('block_bytes(d->format) != block_bytes(s->format)', region)

    def test_c8c_placed_buffers(self):
        # C8c invariants: a placed buffer (no heap description) resolves
        # ReuseBufferGPUVA to its parent heap with a bounds check; its heap
        # object aliases the parent and destroying it releases nothing; a
        # heap created alone gets a resource record spanning it.
        source = SOURCE.read_text()
        def fn(name):
            start = source.index(name)
            return source[start:source.index('\n}\n', start)]
        create = fn('static HRESULT APIENTRY CreateHeapAndResource(')
        self.assertIn('resource_arg->ReuseBufferGPUVA.BaseAddress.UMD.hResource.pDrvPrivate', create)
        self.assertIn('resource_arg->Width > base->heap->bytes - offset ? 6u', create)
        self.assertIn('heap->alias = TRUE;', create)
        self.assertIn('"measure-d3d12-placed"', create)
        self.assertIn('resource->bytes = heap->bytes;', create)
        self.assertIn('heap->allocation && !heap->alias', fn('static VOID APIENTRY DestroyHeapAndResource('))

    def test_c8a_growing_lists(self):
        # C8a invariants: a list owns a growing command array (doubling from
        # 64), every recorder appends through it and stops with a list error
        # when the heap refuses; Reset keeps the capacity; DestroyCommandList
        # frees the array.
        source = SOURCE.read_text()
        def fn(name):
            start = source.index(name)
            return source[start:source.index('\n}\n', start)]
        append = fn('static AgxD3d12Command *AppendCommand(')
        self.assertIn('list->capacity ? list->capacity * 2u : 64u', append)
        self.assertIn('ListError(list, E_OUTOFMEMORY);', append)
        self.assertNotIn('list->command[list->commands++];\n', source.replace(append, ''))
        self.assertIn('HeapFree(GetProcessHeap(), 0, list->command);', fn('static VOID APIENTRY DestroyCommandList('))
        self.assertNotIn('capacity', fn('static VOID APIENTRY ResetCommandList('))

    def test_package_and_receipts(self):
        package = ROOT / 'drivers/apple-agx/render-admission/umd'
        exports = re.findall(r'(?m)^\s*(OpenAdapter\w+)\s*$',
                             (package / 'AppleAgxRenderAdmissionUmd.def').read_text())
        native = (package / 'AppleAgxRenderAdmissionUmd.vcxproj').read_text()
        wow64 = (package / 'build-umd-x86.ps1').read_text()
        implementations = {
            'OpenAdapter10_2': (r'render-admission\umd\src\umd.c', r'src\umd.c',
                                ROOT / 'drivers/apple-agx/render-admission/umd/src/umd.c'),
            'OpenAdapter12': (r'd3d12\agx_d3d12_ddi.cpp',
                              r'..\..\d3d12\agx_d3d12_ddi.cpp', SOURCE),
        }
        self.assertEqual(set(exports), set(implementations))
        for export, (wow64_unit, native_unit, implementation) in implementations.items():
            with self.subTest(export=export):
                self.assertIn(wow64_unit, wow64)
                self.assertIn(native_unit, native)
                self.assertRegex(implementation.read_text(),
                                 r'\bHRESULT APIENTRY ' + export + r'\s*\(')
        source = SOURCE.read_text()
        opened = source.split('extern "C" HRESULT APIENTRY OpenAdapter12', 1)[1]
        self.assertLess(opened.index('GetEnvironmentVariableA("APPLE_AGX_D3D12"'), opened.index('FillD3D12DDI_ADAPTERFUNCS'))
        for stage in ('d3d12-open-adapter', 'd3d12-versions', 'd3d12-caps',
                      'd3d12-create-device', 'd3d12-close-adapter'):
            with self.subTest(stage=stage):
                self.assertIn(f'AdmissionUmdDiagnostic("{stage}"', source)
