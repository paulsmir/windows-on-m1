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
