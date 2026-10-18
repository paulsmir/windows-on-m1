#include <assert.h>
#include <stdint.h>

#include "apple_agx_local_reserve_abi.h"

int main(void)
{
    assert(APPLE_AGX_LOCAL_RESERVE_VERSION == 3);
    assert(APPLE_AGX_LOCAL_RESERVE_MAX_BYTES == 0x40000000ULL);
    assert(APPLE_AGX_LOCAL_RESERVE_MIN_BYTES == 0x08800000ULL);
    APPLE_AGX_LOCAL_RESERVE_RECEIPT receipt = {
        APPLE_AGX_LOCAL_RESERVE_MAGIC, APPLE_AGX_LOCAL_RESERVE_VERSION, 1,
        UINT64_C(0x8e0000000), UINT64_C(0x8e0000000),
        APPLE_AGX_LOCAL_RESERVE_MAX_BYTES,
    };
    /* The 1 GiB reserve keeps its layout: segment 2 0x3b800000, the backend
     * arena at 0x3f800000. */
    assert(AppleAgxLocalReserveSegmentBytes(receipt.Bytes) == 0x3b800000ULL);
    assert(AppleAgxLocalReserveBackendOffset(receipt.Bytes) == 0x3f800000ULL);
    assert(AppleAgxLocalReserveMatchesResource(&receipt,
                                                receipt.GuestIpa,
                                                receipt.Bytes));
    assert(!AppleAgxLocalReserveMatchesResource(&receipt,
                                                 UINT64_C(0x204000000),
                                                 receipt.Bytes));
    receipt.Valid = 0;
    assert(!AppleAgxLocalReserveMatchesResource(&receipt,
                                                 receipt.GuestIpa,
                                                 receipt.Bytes));
    receipt.Valid = 1;
    receipt.HostPa += UINT64_C(0x4000);
    assert(!AppleAgxLocalReserveMatchesResource(&receipt,
                                                 receipt.GuestIpa,
                                                 receipt.Bytes));
    receipt.HostPa = receipt.GuestIpa + 0x10000;
    assert(!AppleAgxLocalReserveMatchesResource(&receipt, receipt.GuestIpa, receipt.Bytes));
    receipt.HostPa = receipt.GuestIpa += 0x10000;
    assert(AppleAgxLocalReserveMatchesResource(&receipt, receipt.GuestIpa, receipt.Bytes));
    receipt.Version = 2;
    assert(!AppleAgxLocalReserveMatchesResource(&receipt, receipt.GuestIpa, receipt.Bytes));
    receipt.Version = 4;
    assert(!AppleAgxLocalReserveMatchesResource(&receipt, receipt.GuestIpa, receipt.Bytes));
    receipt.Version = APPLE_AGX_LOCAL_RESERVE_VERSION;
    /* M2-f2c: the receipt carries the size; the resource must match it. */
    receipt.Bytes = UINT64_C(0x18000000); /* 384 MiB */
    assert(AppleAgxLocalReserveSegmentBytes(receipt.Bytes) == 0x13800000ULL);
    assert(AppleAgxLocalReserveBackendOffset(receipt.Bytes) == 0x17800000ULL);
    assert(AppleAgxLocalReserveMatchesResource(&receipt, receipt.GuestIpa, receipt.Bytes));
    assert(!AppleAgxLocalReserveMatchesResource(&receipt, receipt.GuestIpa,
                                                 APPLE_AGX_LOCAL_RESERVE_MAX_BYTES));
    /* Below the floor, above the maximum or unaligned: refused. */
    receipt.Bytes = APPLE_AGX_LOCAL_RESERVE_MIN_BYTES - 0x10000;
    assert(!AppleAgxLocalReserveMatchesResource(&receipt, receipt.GuestIpa, receipt.Bytes));
    receipt.Bytes = APPLE_AGX_LOCAL_RESERVE_MIN_BYTES;
    assert(AppleAgxLocalReserveMatchesResource(&receipt, receipt.GuestIpa, receipt.Bytes));
    receipt.Bytes = APPLE_AGX_LOCAL_RESERVE_MAX_BYTES + 0x10000;
    assert(!AppleAgxLocalReserveMatchesResource(&receipt, receipt.GuestIpa, receipt.Bytes));
    receipt.Bytes = UINT64_C(0x18004000);
    assert(!AppleAgxLocalReserveMatchesResource(&receipt, receipt.GuestIpa, receipt.Bytes));
    assert(!AppleAgxLocalReserveBytesValid(0));
    return 0;
}
