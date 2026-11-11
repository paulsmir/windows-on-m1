#!/usr/bin/env python3
"""Decode raw REG_BINARY Wom1DdiStatus v1. Counts include retry statuses.
Zero allocation means absent/multiple/unspecified, never inferred from a VA.
Slots are image-lifetime; compare counts before/after each arm, and reboot the
same package for a fresh first-failure timestamp if an earlier arm filled it.
"""
import argparse
import json
from pathlib import Path
import struct

DDIS = ['AddDevice', 'StartDevice', 'StopDevice', 'RemoveDevice', 'DispatchIoRequest', 'QueryChildRelations', 'QueryChildStatus', 'QueryDeviceDescriptor', 'SetPowerState', 'QueryAdapterInfo', 'SetPalette', 'SetPointerPosition', 'SetPointerShape', 'IsSupportedVidPn', 'RecommendFunctionalVidPn', 'EnumVidPnCofuncModality', 'SetVidPnSourceVisibility', 'CommitVidPn', 'UpdateActiveVidPnPresentPath', 'RecommendMonitorModes', 'UpdateMonitorLinkInfo', 'GetScanLine', 'StopCapture', 'QueryVidPnHWCapability', 'SetVidPnSourceAddress', 'SetVidPnSourceAddressWithMultiPlaneOverlay3', 'StopDeviceAndReleasePostDisplayOwnership', 'NotifyAcpiEvent', 'QueryInterface', 'CreateDevice', 'DestroyDevice', 'CreateAllocation', 'DestroyAllocation', 'DescribeAllocation', 'GetStandardAllocationDriverData', 'OpenAllocation', 'CloseAllocation', 'Patch', 'SubmitCommand', 'BuildPagingBuffer', 'PreemptCommand', 'Render', 'Present', 'CreateOverlay', 'UpdateOverlay', 'FlipOverlay', 'DestroyOverlay', 'ResetFromTimeout', 'RestartFromTimeout', 'Escape', 'CollectDbgInfo', 'QueryCurrentFence', 'ControlInterrupt', 'CreateContext', 'DestroyContext', 'RenderKm', 'QueryDependentEngineGroup', 'QueryEngineStatus', 'ResetEngine', 'CancelCommand', 'SetPowerComponentFState', 'PowerRuntimeControlRequest', 'GetNodeMetadata', 'SubmitCommandVirtual', 'CreateProcess', 'DestroyProcess', 'CalibrateGpuClock', 'CheckMultiPlaneOverlaySupport3', 'GetMultiPlaneOverlayCaps']
OPERATIONS = ["Transfer", "Fill", "DiscardContent", "ReadPhysical", "WritePhysical",
              "MapApertureSegment", "UnmapApertureSegment", "SpecialLockTransfer",
              "VirtualTransfer", "VirtualFill", "InitContextResource", "UpdatePageTable",
              "FlushTlb", "UpdateContextAllocation", "CopyPageTableEntries",
              "NotifyResidency", "SignalMonitoredFence", "MapApertureSegment2",
              "NotifyFenceResidency", "MapMmu", "UnmapMmu", "NotifyResidency2", "NotifyAlloc"]

def decode(data):
    if len(data) < 32:
        raise ValueError("truncated header")
    version, size, slots, stride, qpc, frequency = struct.unpack_from('<IIIIQQ', data)
    if (version, size, slots, stride) != (1, 6176, 128, 48) or len(data) != size:
        raise ValueError("unsupported layout or truncated receipt")
    result = dict(version=version, qpc=qpc, frequency=frequency, failures=[])
    for slot in range(slots):
        count, first, allocation, obj, operation, status, ready, reserved = struct.unpack_from('<qQQQIIII', data, 32+slot*stride)
        if not count:
            continue
        if ready not in (0, 1, 2) or reserved:
            raise ValueError("invalid slot publication")
        name = DDIS[slot] if slot < len(DDIS) else (
            'BuildPagingBuffer' if 80 <= slot < 112 else (
            'PagingInternalBeforeNormalization' if slot >= 112 else 'Reserved'))
        entry = dict(slot=slot, ddi=name, count=count, published=ready == 2)
        if ready == 2:
            entry.update(qpc=first, allocation=hex(allocation), object=hex(obj),
                         operation=operation, operation_name=OPERATIONS[operation] if operation < len(OPERATIONS) else None,
                         status=f'0x{status:08x}',
                         expected_paging_retry=status in (0xc01e0001, 0xc01e0102))
        result['failures'].append(entry)
    return result

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('binary', type=Path)
    args = parser.parse_args()
    print(json.dumps(decode(args.binary.read_bytes()), indent=2))

if __name__ == '__main__':
    main()
