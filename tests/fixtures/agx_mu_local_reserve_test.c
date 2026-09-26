#include <assert.h>
#include <stdint.h>

#include "AgxLocalReserveValidation.h"

int main(void)
{
    const UINT64 ram = UINT64_C(0x800000000);
    const UINT64 candidate = UINT64_C(0x8E0000000);
    const AGX_LOCAL_RANGE occupied[] = {
        {ram, UINT64_C(0x60000000)},
        {UINT64_C(0x860000000), UINT64_C(0x40000000)},
        {UINT64_C(0x8A0100000), UINT64_C(0x3FF00000)},
    };
    const AGX_LOCAL_RANGE overlap[] = {
        {candidate + UINT64_C(0x4000), UINT64_C(0x4000)},
    };

    assert(AgxLocalReserveRangeValid(candidate, AGX_LOCAL_RESERVE_BYTES,
                                      ram, UINT64_C(0x200000000), occupied,
                                      sizeof(occupied) / sizeof(occupied[0])));
    assert(!AgxLocalReserveRangeValid(candidate, AGX_LOCAL_RESERVE_BYTES,
                                       ram, UINT64_C(0x200000000), overlap, 1));
    assert(!AgxLocalReserveRangeValid(candidate + UINT64_C(0x4000),
                                       AGX_LOCAL_RESERVE_BYTES, ram,
                                       UINT64_C(0x200000000), occupied, 3));
    assert(!AgxLocalReserveRangeValid(UINT64_MAX - AGX_LOCAL_RESERVE_BYTES + 1,
                                       AGX_LOCAL_RESERVE_BYTES, ram,
                                       UINT64_C(0x200000000), occupied, 3));
    return 0;
}
