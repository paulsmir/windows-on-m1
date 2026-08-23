#include <assert.h>
#include <stdint.h>
#include <stdio.h>

#include "apple_input_hw.h"

int main(void)
{
    assert(AiSpiClockDivider(120000000u, 8000000u) == 15u);
    assert(AiSpiClockDivider(120000000u, 7000000u) == 18u);
    assert(AiSpiClockDivider(1u, 1u) == 1u);
    assert(AiSpiClockDivider(0u, 1u) == 0u);
    assert(AiSpiClockDivider(1u, 0u) == 0u);
    assert(AiSpiClockDivider(0xffffffffu, 1u) == AI_SPI_CLKDIV_MAX);

    assert(AiGpioPinOffset(195u) == 0x30cu);
    assert(AiGpioPinOffset(13u) == 0x34u);
    assert(AiGpioIrqAckOffset(13u, 0u) == 0x800u);
    assert(AiGpioIrqAckMask(13u) == (1u << 13));
    assert(AiGpioIrqMode(AI_GPIO_IRQ_GROUP_STARTUP, AI_GPIO_MODE_IRQ_LOW) ==
           (AI_GPIO_MODE_IRQ_LOW << AI_GPIO_MODE_SHIFT));

    assert(AiSpiRegisterRangeValid(AI_SPI_REG_CONTROL, sizeof(uint32_t), 0x4000u));
    assert(AiSpiRegisterRangeValid(AI_SPI_REG_DELAY_POST, sizeof(uint32_t), 0x4000u));
    assert(!AiSpiRegisterRangeValid(0x3fffu, sizeof(uint32_t), 0x4000u));
    assert(!AiSpiRegisterRangeValid(0x4000u, sizeof(uint32_t), 0x4000u));

    assert(AiSpiTransferLengthValid(1u));
    assert(AiSpiTransferLengthValid(AI_SPI_MAX_TRANSFER_BYTES));
    assert(!AiSpiTransferLengthValid(0u));
    assert(!AiSpiTransferLengthValid(AI_SPI_MAX_TRANSFER_BYTES + 1u));
    assert(!AiSpiDeadlineExpired(99u, 100u));
    assert(AiSpiDeadlineExpired(100u, 100u));
    assert(AiSpiDeadlineExpired(101u, 100u));
    assert(AiSpiBoundDeadline(1000u, 10000u, 5000u) == 3000u);
    assert(AiSpiBoundDeadline(1000u, 10000u, 2000u) == 2000u);
    assert(AiSpiBoundDeadline(1000u, 0u, 5000u) == 1000u);

    assert(AiGpioOutputValue(0u, 1) ==
           ((AI_GPIO_MODE_OUTPUT << AI_GPIO_MODE_SHIFT) | 1u));
    assert(AiGpioOutputValue(0xffffffffu, 0) ==
           ((0xffffffffu & ~(AI_GPIO_MODE_MASK | 1u)) |
            (AI_GPIO_MODE_OUTPUT << AI_GPIO_MODE_SHIFT)));
    assert(AiGpioInputAssertedValue(0u));
    assert(!AiGpioInputAssertedValue(1u));

    puts("apple_input_hw_test: ok");
    return 0;
}
