#include <assert.h>
#include <stdint.h>
#include <stdio.h>

#include <Library/VirtualDisplayValidation.h>

int main(void)
{
    uint64_t size = 0;
    void *info = 0;

    assert(VirtualDisplayValidateQuery((void *)1, 0, &size, &info) == EFI_SUCCESS);
    assert(VirtualDisplayValidateQuery((void *)1, 1, &size, &info) == EFI_UNSUPPORTED);
    assert(VirtualDisplayValidateQuery((void *)1, 0, 0, &info) == EFI_INVALID_PARAMETER);
    assert(VirtualDisplayValidateQuery((void *)1, 0, &size, 0) == EFI_INVALID_PARAMETER);
    assert(VirtualDisplayValidateSetMode((void *)1, 0) == EFI_SUCCESS);
    assert(VirtualDisplayValidateSetMode((void *)1, 1) == EFI_UNSUPPORTED);
    assert(VirtualDisplayValidateSetMode(0, 0) == EFI_INVALID_PARAMETER);

    assert(VirtualDisplayValidateGeometry(2560, 1600, 10240, 32));
    assert(!VirtualDisplayValidateGeometry(0, 1600, 10240, 32));
    assert(!VirtualDisplayValidateGeometry(2560, 1600, 10239, 32));
    assert(!VirtualDisplayValidateGeometry(2560, 1600, 10240, 30));

    assert(VirtualDisplayValidateFramebufferRange(
        UINT64_C(0x85f000000), UINT64_C(0xfa0000),
        UINT64_C(0x850000000), UINT64_C(0x1af708000)));
    assert(!VirtualDisplayValidateFramebufferRange(
        0, UINT64_C(0xfa0000), UINT64_C(0x850000000), UINT64_C(0x1af708000)));
    assert(!VirtualDisplayValidateFramebufferRange(
        UINT64_C(0x85f000000), 0, UINT64_C(0x850000000), UINT64_C(0x1af708000)));
    assert(!VirtualDisplayValidateFramebufferRange(
        UINT64_MAX - UINT64_C(0x1000), UINT64_C(0x2000), 0, UINT64_MAX));
    assert(!VirtualDisplayValidateFramebufferRange(
        UINT64_C(0x84f000000), UINT64_C(0xfa0000),
        UINT64_C(0x850000000), UINT64_C(0x1af708000)));
    assert(!VirtualDisplayValidateFramebufferRange(
        UINT64_C(0x85f000000), UINT64_C(0x100000000),
        UINT64_C(0x850000000), UINT64_C(0x1af708000)));

    puts("virtual_display_firmware_contract_test: ok");
    return 0;
}
