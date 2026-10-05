#include "hv_agx_firmware_prefix.h"
#include <assert.h>
#include <string.h>

static unsigned reads;
static unsigned long long live[2] = {0x800004403ULL, 0x800008403ULL};
static unsigned char read_live(void *ctx, unsigned long long base,
                              unsigned long long entries[2]) {
    (void)ctx;
    assert(base == 0x800000000ULL);
    ++reads;
    entries[0] = live[0]; entries[1] = live[1];
    return 1;
}
int main(void) {
    AGX_FW_PREFIX wire = {0}, bad;
    unsigned long long value = 0;
    unsigned i;
    for (i = 0; i < sizeof(wire); i += 8) {
        assert(hv_agx_firmware_prefix_read(0x800000000ULL, 0x40000, 17,
                   read_live, 0, i, &value, 0, 3));
        memcpy((char *)&wire + i, &value, 8);
    }
    assert(AgxFwPrefixValid(&wire, sizeof(wire), 17));
    assert(wire.Entries[0] == 0x800004403ULL);
    assert(wire.Entries[1] == 0x800008403ULL);
    assert(!AgxFwPrefixValid(&wire, sizeof(wire)-1, 17));
    assert(!AgxFwPrefixValid(&wire, sizeof(wire), 18));
#define BAD(field, val) bad = wire; bad.field = val; \
    assert(!AgxFwPrefixValid(&bad, sizeof(bad), 17))
    BAD(Version, 2); BAD(Size, 63); BAD(PrefixBytes, 8);
    BAD(Base, 0x800000001ULL); BAD(Length, 0x40001);
    BAD(Base, 1ULL << 40); BAD(Ready, 2); BAD(Epoch, 0);
    BAD(Entries[0], 0x800004407ULL); BAD(Entries[0], 0x900004403ULL);
    BAD(Entries[0], 0x800008403ULL);
    i = reads;
    assert(!hv_agx_firmware_prefix_read(0x800000000ULL,0x40000,17,
               read_live,0,0,&value,1,3));
    assert(!hv_agx_firmware_prefix_read(0x800000000ULL,0x40000,17,
               read_live,0,1,&value,0,3));
    assert(!hv_agx_firmware_prefix_read(0x800000000ULL,0x40000,17,
               read_live,0,64,&value,0,3));
    assert(reads == i); /* rejected transport requests never touch source */
    live[0] = 0x80000c403ULL;
    assert(hv_agx_firmware_prefix_read(0x800000000ULL,0x40000,19,
               read_live,0,48,&value,0,3));
    assert(value == 0x80000c403ULL); /* fresh, not cached previous boot */
    assert(hv_agx_firmware_prefix_read(0x800000000ULL,0x40000,0,
               read_live,0,40,&value,0,3));
    assert(value == 0); /* unavailable power lifetime is not ready */
    return 0;
}
