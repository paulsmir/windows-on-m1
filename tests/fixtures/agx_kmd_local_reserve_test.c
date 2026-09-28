#include <assert.h>
#include <stdint.h>

#include "apple_agx_local_reserve_abi.h"

int main(void)
{
    assert(APPLE_AGX_LOCAL_RESERVE_VERSION == 2);
    assert(APPLE_AGX_LOCAL_RESERVE_BYTES == 0x40000000ULL);
    APPLE_AGX_LOCAL_RESERVE_RECEIPT receipt = {
        APPLE_AGX_LOCAL_RESERVE_MAGIC, APPLE_AGX_LOCAL_RESERVE_VERSION, 1,
        UINT64_C(0x8e0000000), UINT64_C(0x8e0000000),
        APPLE_AGX_LOCAL_RESERVE_BYTES,
    };
    assert(AppleAgxLocalReserveMatchesResource(&receipt,
                                                receipt.GuestIpa,
                                                APPLE_AGX_LOCAL_RESERVE_BYTES));
    assert(!AppleAgxLocalReserveMatchesResource(&receipt,
                                                 UINT64_C(0x204000000),
                                                 APPLE_AGX_LOCAL_RESERVE_BYTES));
    receipt.Valid = 0;
    assert(!AppleAgxLocalReserveMatchesResource(&receipt,
                                                 receipt.GuestIpa,
                                                 APPLE_AGX_LOCAL_RESERVE_BYTES));
    receipt.Valid = 1;
    receipt.HostPa += UINT64_C(0x4000);
    assert(!AppleAgxLocalReserveMatchesResource(&receipt,
                                                 receipt.GuestIpa,
                                                 APPLE_AGX_LOCAL_RESERVE_BYTES));
    receipt.HostPa = receipt.GuestIpa + 0x10000;
    assert(!AppleAgxLocalReserveMatchesResource(&receipt, receipt.GuestIpa, receipt.Bytes));
    receipt.HostPa = receipt.GuestIpa += 0x10000;
    assert(AppleAgxLocalReserveMatchesResource(&receipt, receipt.GuestIpa, receipt.Bytes));
    receipt.Version = 1;
    assert(!AppleAgxLocalReserveMatchesResource(&receipt, receipt.GuestIpa, receipt.Bytes));
    receipt.Version = 3;
    assert(!AppleAgxLocalReserveMatchesResource(&receipt, receipt.GuestIpa, receipt.Bytes));
    return 0;
}
