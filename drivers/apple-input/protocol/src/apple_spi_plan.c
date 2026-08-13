#include "apple_spihid.h"

enum ai_status ai_spi_plan_transfer(uint32_t reference_hz, uint32_t target_hz,
                                    uint8_t bits_per_word, size_t byte_length,
                                    struct ai_spi_transfer_plan *out)
{
    if (!reference_hz || !target_hz || !bits_per_word ||
        bits_per_word > 32 || !out)
        return AI_ERR_ARGUMENT;

    uint8_t bytes_per_word = bits_per_word > 16 ? 4 :
                             bits_per_word > 8 ? 2 : 1;
    if (byte_length % bytes_per_word ||
        byte_length / bytes_per_word > AI_UINT16_MAX)
        return AI_ERR_LENGTH;

    uint64_t divider = ((uint64_t)reference_hz + target_hz - 1) / target_hz;
    if (divider > 0x7ff)
        divider = 0x7ff;

    *out = (struct ai_spi_transfer_plan){
        .clock_divider = (uint16_t)divider,
        .words = (uint16_t)(byte_length / bytes_per_word),
        .bytes_per_word = bytes_per_word,
        .poll = (uint64_t)200000 * bits_per_word * 8 <= target_hz,
    };
    return AI_OK;
}

size_t ai_spi_init_plan(struct ai_spi_register_op *out, size_t capacity)
{
    static const struct ai_spi_register_op plan[] = {
        {AI_SPI_REGISTER_WRITE, 0x00c, 0, 1u << 1},
        {AI_SPI_REGISTER_MASK, 0x150, 1u << 24, 0},
        {AI_SPI_REGISTER_MASK, 0x154, 1u << 9, 1u << 1},
        {AI_SPI_REGISTER_WRITE, 0x000, 0, (1u << 2) | (1u << 3)},
        {AI_SPI_REGISTER_WRITE, 0x004, 0, 1u << 5},
        {AI_SPI_REGISTER_WRITE, 0x138, 0, 0},
        {AI_SPI_REGISTER_WRITE, 0x130, 0, 0},
        {AI_SPI_REGISTER_WRITE, 0x160, 0, 0},
        {AI_SPI_REGISTER_WRITE, 0x168, 0, 0},
    };
    const size_t count = sizeof(plan) / sizeof(plan[0]);

    if (out && capacity >= count) {
        for (size_t index = 0; index < count; index++)
            out[index] = plan[index];
    }
    return count;
}
