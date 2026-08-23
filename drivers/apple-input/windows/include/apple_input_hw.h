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
#define AI_SPI_MAX_TRANSFER_BYTES 256u
#define AI_SPI_TRANSFER_TIMEOUT_MS 200u
#define AI_SPI_WRITE_STATUS_DELAY_US 200u
#define AI_SPI_WRITE_STATUS_SIZE 4u

#define AI_SPI_CONTROL_RUN      (1u << 0)
#define AI_SPI_CONTROL_TX_RESET (1u << 2)
#define AI_SPI_CONTROL_RX_RESET (1u << 3)
#define AI_SPI_CONTROL_FIFO_RESET \
    (AI_SPI_CONTROL_TX_RESET | AI_SPI_CONTROL_RX_RESET)
#define AI_SPI_PIN_CS (1u << 1)
#define AI_SPI_FIFO_TX_FULL (1u << 4)
#define AI_SPI_FIFO_TX_LEVEL_SHIFT 8u
#define AI_SPI_FIFO_TX_LEVEL_MASK (0xffu << AI_SPI_FIFO_TX_LEVEL_SHIFT)
#define AI_SPI_FIFO_RX_EMPTY (1u << 20)
#define AI_SPI_FIFO_RX_LEVEL_SHIFT 24u
#define AI_SPI_FIFO_RX_LEVEL_MASK (0xffu << AI_SPI_FIFO_RX_LEVEL_SHIFT)
#define AI_SPI_XFER_RX_COMPLETE (1u << 0)
#define AI_SPI_XFER_TX_COMPLETE (1u << 1)
#define AI_SPI_SHIFT_BITS_SHIFT 16u
#define AI_SPI_SHIFT_BITS_MASK (0x3fu << AI_SPI_SHIFT_BITS_SHIFT)
#define AI_SPI_SHIFT_OVERRIDE_CS (1u << 24)
#define AI_SPI_PINCFG_KEEP_CS (1u << 1)
#define AI_SPI_PINCFG_CS_IDLE_HIGH (1u << 9)

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
int AiSpiTransferLengthValid(size_t length);
int AiSpiDeadlineExpired(uint64_t now_qpc, uint64_t deadline_qpc);
uint64_t AiSpiBoundDeadline(uint64_t now_qpc, uint64_t counts_per_second,
                            uint64_t requested_deadline_qpc);
uint32_t AiGpioOutputValue(uint32_t current, int high);
int AiGpioInputAssertedValue(uint32_t value);
