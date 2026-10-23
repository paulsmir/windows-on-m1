"""Shader-visible descriptor heaps: cached shadow, mirrored heap forms.

WHAT REAL BUG OR INVARIANT WILL THIS TEST CATCH? EXP1854/1855: the replay
walked every descriptor of a bound heap per draw in write-combined memory
(measure-d3d12-heap-walk: DynamicIndexing 7 ms per frame). CPU handles now
name a cached shadow and only the heap forms reach the GPU buffer, so every
path that writes a CBV/SRV/UAV descriptor into a shader-visible heap must
mirror its heap form, or shaders read a stale descriptor.
"""
from pathlib import Path
import re
import unittest

DDI = Path(__file__).resolve().parents[1] / 'drivers/apple-agx/d3d12/agx_d3d12_ddi.cpp'


def function(text, head):
    start = text.index(head)
    return text[start:text.index('\n}\n', start)]


class DescriptorShadow(unittest.TestCase):
    def test_every_descriptor_write_mirrors_its_heap_form(self):
        text = DDI.read_text()
        for head in ('static void FillHeapForm(D3D12DDI_HDEVICE handle',
                     'static VOID APIENTRY CopyDescriptorsSimple(D3D12DDI_HDEVICE handle',
                     'static VOID APIENTRY CopyDescriptors(D3D12DDI_HDEVICE handle'):
            self.assertIn('MirrorHeapForms(MirroredHeapAt(', function(text, head), head)
        walk = function(text, 'static HRESULT SetStageHeapWalk(AGX_D3D12_OWNER *owner, unsigned stage, const AgxD3d12Dispatch *b, AgxD3d12HeapScratch **scratch,\n                                bool *minmax) {')
        self.assertRegex(walk, r'PackHeapForm\(owner, view\);\s*MirrorHeapForms\(b->descriptors, view, 1u\);')
        # the view creators reach the heap through FillHeapForm only
        for name in ('CreateShaderResourceViewInHeap', 'CreateConstantBufferViewInHeap',
                     'CreateUnorderedAccessViewInHeap'):
            self.assertIn('FillHeapForm(handle, dest,', function(text, 'static VOID APIENTRY ' + name + '('), name)

    def test_shadow_replaces_the_cpu_handles_of_shader_visible_heaps(self):
        create = function(DDI.read_text(), 'static HRESULT APIENTRY CreateDescriptorHeap(')
        self.assertIn('heap->memory.mirror = static_cast<unsigned char *>(heap->cpu);', create)
        self.assertIn('heap->memory.cpu = heap->cpu = shadow;', create)
        self.assertIn('memcpy(heap->memory.mirror, heap->cpu, static_cast<SIZE_T>(heap->memory.bytes));', create)


if __name__ == '__main__':
    unittest.main()
