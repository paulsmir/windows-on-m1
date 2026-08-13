#pragma once

#ifdef AI_KERNEL_MODE
#include <ntddk.h>
typedef UINT32 uint32_t;
typedef UINT64 uint64_t;
#else
#include <stddef.h>
#include <stdint.h>
#endif

#define AI_SPI_REG_CONTROL       0x000u
#define AI_SPI_REG_CONFIG        0x004u
#define AI_SPI_REG_STATUS        0x008u
#define AI_SPI_REG_PIN           0x00cu
#define AI_SPI_REG_TX_DATA       0x010u
#define AI_SPI_REG_RX_DATA       0x020u
#define AI_SPI_REG_CLKDIV        0x030u
#define AI_SPI_REG_RX_COUNT      0x034u
#define AI_SPI_REG_WORD_DELAY    0x038u
#define AI_SPI_REG_TX_COUNT      0x04cu
#define AI_SPI_REG_FIFO_STATUS   0x10cu
#define AI_SPI_REG_IRQ_ENABLE    0x130u
#define AI_SPI_REG_IRQ_FLAGS     0x134u
#define AI_SPI_REG_FIFO_ENABLE   0x138u
#define AI_SPI_REG_FIFO_FLAGS    0x13cu
#define AI_SPI_REG_SHIFT_CONFIG  0x150u
#define AI_SPI_REG_PIN_CONFIG    0x154u
#define AI_SPI_REG_DELAY_PRE     0x160u
#define AI_SPI_REG_DELAY_POST    0x168u

#define AI_SPI_CLKDIV_MAX 0x7ffu
#define AI_SPI_FIFO_DEPTH 16u
#define AI_SPI_TRANSFER_TIMEOUT_MS 200u

#define AI_GPIO_MODE_SHIFT 1u
#define AI_GPIO_MODE_MASK  (7u << AI_GPIO_MODE_SHIFT)
#define AI_GPIO_MODE_OUTPUT 1u
#define AI_GPIO_MODE_IRQ_LOW 3u
#define AI_GPIO_GROUP_SHIFT 16u
#define AI_GPIO_GROUP_MASK  (7u << AI_GPIO_GROUP_SHIFT)
#define AI_GPIO_IRQ_GROUP_STARTUP 0u

uint32_t AiSpiClockDivider(uint32_t source_hz, uint32_t bus_hz);
uint32_t AiGpioPinOffset(uint32_t pin);
uint32_t AiGpioIrqAckOffset(uint32_t pin, uint32_t group);
uint32_t AiGpioIrqAckMask(uint32_t pin);
uint32_t AiGpioIrqMode(uint32_t group, uint32_t mode);
int AiSpiRegisterRangeValid(uint32_t offset, size_t width, size_t resource_size);
