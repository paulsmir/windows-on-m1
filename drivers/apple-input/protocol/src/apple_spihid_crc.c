#include "apple_spihid.h"

uint16_t ai_crc16_usb(uint16_t seed, const void *data, size_t size)
{
    const uint8_t *bytes = data;
    uint16_t crc = seed;

    while (size--) {
        crc ^= *bytes++;
        for (unsigned int bit = 0; bit < 8; bit++)
            crc = (crc & 1u) ? (uint16_t)((crc >> 1) ^ 0xa001u)
                             : (uint16_t)(crc >> 1);
    }
    return crc;
}
