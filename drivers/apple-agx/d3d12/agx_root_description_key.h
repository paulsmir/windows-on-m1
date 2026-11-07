/* Original UNLINKED exact-field0001 key. SPDX-License-Identifier: MIT
 * Include actual Windows/d3d12umddi.h first. NOT Microsoft root serialization
 * or a Windows root-signature admission/semantic validator. */
#pragma once
#include <cstddef>
#include <cstdint>
struct AgxRootKeySpan {const void *data;SIZE_T bytes;};
struct AgxRootKeyInput {
 AgxRootKeySpan args;          /* actual D3D12DDIARG_CREATE_ROOT_SIGNATURE_0001 */
 AgxRootKeySpan description;   /* actual D3D12DDI_ROOT_SIGNATURE, pointer bound */
 AgxRootKeySpan parameters;    /* pointer bound to description.pRootParameters */
 AgxRootKeySpan samplers;      /* pointer bound to description.pStaticSamplers */
 AgxRootKeySpan table_spans;   /* AgxRootKeySpan per parameter index; only table
                               * entries used, bound to table.pDescriptorRanges */
};
struct AgxRootKeyView {const unsigned char *data;SIZE_T bytes;};
enum class AgxRootKeyStatus {Ok,InvalidSpan,UnsupportedUnion,SizeOverflow,DestinationInvalid};
/* Caller owns valid typed input records and guarantees all structurally valid
 * spans readable, correctly aligned, stable and immutable for the whole call.
 * Span metadata itself may be invalid; wrapping/nonempty-null/short/mismatched
 * spans refuse before reading the corresponding record/array. No pointer probing.
 * Table-span metadata is an explicit readable array with one slot per parameter.
 * Output records must be writable and not alias input/destination. No retention,
 * allocator, locks, callback, source mutation or production threading guarantee.
 * All scalar fields are data: no root-cost/caps/flag/range/APPEND/endpoints/alias
 * policy is applied. Unknown parameter union kind refuses rather than guesses.
 * Raw APPEND, order, NodeMask, enum/flag words and exact FLOAT bits are preserved.
 */
AgxRootKeyStatus AgxRootKeyCalculate(const AgxRootKeyInput *,SIZE_T *bytes);
/* All structural preflight completes before any destination write. On failure,
 * view is empty and destination untouched. Success publishes the complete view
 * last; its storage stays caller-owned. Destination must not overlap any input
 * record/span (including unused readable tail). No partially valid key is owned.
 */
AgxRootKeyStatus AgxRootKeyEncode(const AgxRootKeyInput *,void *destination,
 SIZE_T capacity,AgxRootKeyView *);
