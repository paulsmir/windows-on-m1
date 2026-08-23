#include "apple_input_hw.h"

uint32_t AiSpiClockDivider(uint32_t source_hz, uint32_t bus_hz)
{
    uint64_t divider;

    if (!source_hz || !bus_hz)
        return 0;
    divider = ((uint64_t)source_hz + bus_hz - 1u) / bus_hz;
    if (divider > AI_SPI_CLKDIV_MAX)
        return AI_SPI_CLKDIV_MAX;
    return (uint32_t)divider;
}

uint32_t AiGpioPinOffset(uint32_t pin)
{
    return 4u * pin;
}

uint32_t AiGpioIrqAckOffset(uint32_t pin, uint32_t group)
{
    return 0x800u + 0x40u * group + 4u * (pin >> 5);
}

uint32_t AiGpioIrqAckMask(uint32_t pin)
{
    return 1u << (pin & 31u);
}

uint32_t AiGpioIrqMode(uint32_t group, uint32_t mode)
{
    return ((group << AI_GPIO_GROUP_SHIFT) & AI_GPIO_GROUP_MASK) |
           ((mode << AI_GPIO_MODE_SHIFT) & AI_GPIO_MODE_MASK);
}

int AiSpiRegisterRangeValid(uint32_t offset, size_t width, size_t resource_size)
{
    return width && offset <= resource_size && width <= resource_size - offset;
}

int AiSpiTransferLengthValid(size_t length)
{
    return length > 0 && length <= AI_SPI_MAX_TRANSFER_BYTES;
}

int AiSpiDeadlineExpired(uint64_t now_qpc, uint64_t deadline_qpc)
{
    return now_qpc >= deadline_qpc;
}

uint64_t AiSpiBoundDeadline(uint64_t now_qpc, uint64_t counts_per_second,
                            uint64_t requested_deadline_qpc)
{
    uint64_t whole;
    uint64_t remainder;
    uint64_t limit;
    uint64_t maximum = ~(uint64_t)0;

    if (!counts_per_second)
        return now_qpc;
    if (counts_per_second / 1000u > maximum / AI_SPI_TRANSFER_TIMEOUT_MS)
        return requested_deadline_qpc;
    whole = (counts_per_second / 1000u) * AI_SPI_TRANSFER_TIMEOUT_MS;
    remainder = ((counts_per_second % 1000u) * AI_SPI_TRANSFER_TIMEOUT_MS +
                 999u) / 1000u;
    if (whole > maximum - remainder)
        return requested_deadline_qpc;
    limit = whole + remainder;
    if (now_qpc > maximum - limit)
        limit = maximum;
    else
        limit += now_qpc;
    return requested_deadline_qpc < limit ? requested_deadline_qpc : limit;
}

uint32_t AiGpioOutputValue(uint32_t current, int high)
{
    current &= ~(AI_GPIO_MODE_MASK | 1u);
    current |= AI_GPIO_MODE_OUTPUT << AI_GPIO_MODE_SHIFT;
    if (high)
        current |= 1u;
    return current;
}

int AiGpioInputAssertedValue(uint32_t value)
{
    return !(value & 1u);
}
