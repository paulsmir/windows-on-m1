#include "apple_input_device.h"
#include "apple_input_hw.h"
#include "j313_apple_input.generated.h"

static ULONG AiSpiRead(PAI_DEVICE_CONTEXT Context, ULONG Offset)
{
    return READ_REGISTER_NOFENCE_ULONG(
        (PULONG)(Context->SpiRegisters + Offset));
}

static VOID AiSpiWrite(PAI_DEVICE_CONTEXT Context, ULONG Offset, ULONG Value)
{
    WRITE_REGISTER_NOFENCE_ULONG(
        (PULONG)(Context->SpiRegisters + Offset), Value);
}

static ULONG AiSpiRxLevel(ULONG FifoStatus)
{
    return (FifoStatus & AI_SPI_FIFO_RX_LEVEL_MASK) >>
           AI_SPI_FIFO_RX_LEVEL_SHIFT;
}

static ULONG AiSpiTxLevel(ULONG FifoStatus)
{
    return (FifoStatus & AI_SPI_FIFO_TX_LEVEL_MASK) >>
           AI_SPI_FIFO_TX_LEVEL_SHIFT;
}

static VOID AiSpiStall(ULONG Microseconds)
{
    while (Microseconds) {
        ULONG slice = Microseconds > 50u ? 50u : Microseconds;
        KeStallExecutionProcessor(slice);
        Microseconds -= slice;
    }
}

static VOID AiSpiFinishChipSelect(PAI_DEVICE_CONTEXT Context)
{
    AiSpiWrite(Context, AI_SPI_REG_CONTROL, 0);
    AiSpiStall((ULONG)J313_APPLE_INPUT_CS_HOLD_US);
    AiSpiWrite(Context, AI_SPI_REG_PIN, AI_SPI_PIN_CS);
    AiSpiStall((ULONG)J313_APPLE_INPUT_CS_INACTIVE_US);
}

NTSTATUS AiSpiValidateReadOnly(PAI_DEVICE_CONTEXT Context)
{
    ULONG divider;

    if (!Context || !Context->ResourcesValidated || !Context->SpiRegisters)
        return STATUS_DEVICE_NOT_READY;
    if (!AiSpiRegisterRangeValid(AI_SPI_REG_CLKDIV, sizeof(ULONG),
                                 Context->MemoryLength[0]) ||
        !AiSpiRegisterRangeValid(AI_SPI_REG_FIFO_STATUS, sizeof(ULONG),
                                 Context->MemoryLength[0]))
        return STATUS_DEVICE_CONFIGURATION_ERROR;

    divider = READ_REGISTER_NOFENCE_ULONG(
        (PULONG)(Context->SpiRegisters + AI_SPI_REG_CLKDIV));
    if (divider > AI_SPI_CLKDIV_MAX)
        return STATUS_DEVICE_HARDWARE_ERROR;
    return STATUS_SUCCESS;
}

NTSTATUS AiSpiInitialize(PAI_DEVICE_CONTEXT Context)
{
    ULONG value;

    if (!Context || !Context->ResourcesValidated || !Context->SpiRegisters)
        return STATUS_DEVICE_NOT_READY;

    AiSpiWrite(Context, AI_SPI_REG_PIN, AI_SPI_PIN_CS);
    value = AiSpiRead(Context, AI_SPI_REG_SHIFT_CONFIG);
    value &= ~AI_SPI_SHIFT_OVERRIDE_CS;
    AiSpiWrite(Context, AI_SPI_REG_SHIFT_CONFIG, value);
    value = AiSpiRead(Context, AI_SPI_REG_PIN_CONFIG);
    value &= ~AI_SPI_PINCFG_CS_IDLE_HIGH;
    value |= AI_SPI_PINCFG_KEEP_CS;
    AiSpiWrite(Context, AI_SPI_REG_PIN_CONFIG, value);
    AiSpiWrite(Context, AI_SPI_REG_CONTROL, AI_SPI_CONTROL_FIFO_RESET);
    AiSpiWrite(Context, AI_SPI_REG_CONFIG, 0);
    AiSpiWrite(Context, AI_SPI_REG_IRQ_ENABLE, 0);
    AiSpiWrite(Context, AI_SPI_REG_FIFO_ENABLE, 0);
    AiSpiWrite(Context, AI_SPI_REG_DELAY_PRE, 0);
    AiSpiWrite(Context, AI_SPI_REG_DELAY_POST, 0);
    return STATUS_SUCCESS;
}

static NTSTATUS AiSpiTransferPhase(PAI_DEVICE_CONTEXT Context, const UCHAR *Tx,
                                  UCHAR *Rx, SIZE_T Length,
                                  ULONGLONG DeadlineQpc, BOOLEAN BeginChipSelect,
                                  BOOLEAN EndChipSelect)
{
    LARGE_INTEGER frequency;
    SIZE_T tx_done = 0;
    SIZE_T rx_done = 0;
    ULONG required = Tx ? AI_SPI_XFER_TX_COMPLETE : 0;
    ULONGLONG now;
    NTSTATUS status = STATUS_SUCCESS;

    if (!Context || !Context->ResourcesValidated || !Context->SpiRegisters)
        return STATUS_DEVICE_NOT_READY;
    if ((!Tx && !Rx) || !AiSpiTransferLengthValid(Length) || !DeadlineQpc)
        return STATUS_INVALID_PARAMETER;
    now = (ULONGLONG)KeQueryPerformanceCounter(&frequency).QuadPart;
    DeadlineQpc = AiSpiBoundDeadline(now, (ULONGLONG)frequency.QuadPart,
                                     DeadlineQpc);
    if (AiSpiDeadlineExpired(now, DeadlineQpc))
        return STATUS_IO_TIMEOUT;
    if (Rx)
        required |= AI_SPI_XFER_RX_COMPLETE;

    AiSpiWrite(Context, AI_SPI_REG_CONTROL, AI_SPI_CONTROL_FIFO_RESET);
    AiSpiWrite(Context, AI_SPI_REG_IRQ_FLAGS, MAXULONG);
    AiSpiWrite(Context, AI_SPI_REG_FIFO_FLAGS, MAXULONG);
    AiSpiWrite(Context, AI_SPI_REG_CLKDIV,
        AiSpiClockDivider((ULONG)J313_APPLE_INPUT_SPI_SOURCE_HZ,
                          (ULONG)J313_APPLE_INPUT_SPI_BUS_HZ));
    {
        ULONG shift = AiSpiRead(Context, AI_SPI_REG_SHIFT_CONFIG);
        shift &= ~AI_SPI_SHIFT_BITS_MASK;
        shift |= 8u << AI_SPI_SHIFT_BITS_SHIFT;
        AiSpiWrite(Context, AI_SPI_REG_SHIFT_CONFIG, shift);
    }
    AiSpiWrite(Context, AI_SPI_REG_RX_COUNT, Rx ? (ULONG)Length : 0);
    AiSpiWrite(Context, AI_SPI_REG_TX_COUNT, Tx ? (ULONG)Length : 0);

    while (Tx && tx_done < Length && tx_done < AI_SPI_FIFO_DEPTH)
        AiSpiWrite(Context, AI_SPI_REG_TX_DATA, Tx[tx_done++]);

    if (BeginChipSelect) {
        AiSpiWrite(Context, AI_SPI_REG_PIN, 0);
        AiSpiStall((ULONG)J313_APPLE_INPUT_CS_SETUP_US);
    }
    AiSpiWrite(Context, AI_SPI_REG_CONTROL, AI_SPI_CONTROL_RUN);

    for (;;) {
        ULONG fifo = AiSpiRead(Context, AI_SPI_REG_FIFO_STATUS);
        ULONG flags;
        ULONG rx_level = AiSpiRxLevel(fifo);
        ULONG tx_level = AiSpiTxLevel(fifo);

        while (Rx && rx_done < Length && rx_level--)
            Rx[rx_done++] = (UCHAR)AiSpiRead(Context, AI_SPI_REG_RX_DATA);
        while (Tx && tx_done < Length && tx_level < AI_SPI_FIFO_DEPTH) {
            AiSpiWrite(Context, AI_SPI_REG_TX_DATA, Tx[tx_done++]);
            tx_level++;
        }

        flags = AiSpiRead(Context, AI_SPI_REG_IRQ_FLAGS);
        if ((flags & required) == required)
            break;
        if (AiSpiDeadlineExpired(
                (ULONGLONG)KeQueryPerformanceCounter(NULL).QuadPart,
                DeadlineQpc)) {
            status = STATUS_IO_TIMEOUT;
            break;
        }
        YieldProcessor();
    }

    if (NT_SUCCESS(status) && Rx) {
        ULONG retries = AI_SPI_FIFO_DEPTH;
        while (rx_done < Length && retries--) {
            ULONG rx_level = AiSpiRxLevel(
                AiSpiRead(Context, AI_SPI_REG_FIFO_STATUS));
            while (rx_done < Length && rx_level--)
                Rx[rx_done++] = (UCHAR)AiSpiRead(Context, AI_SPI_REG_RX_DATA);
        }
        if (rx_done != Length)
            status = STATUS_IO_DEVICE_ERROR;
    }

    AiSpiWrite(Context, AI_SPI_REG_CONTROL, 0);
    if (EndChipSelect || !NT_SUCCESS(status))
        AiSpiFinishChipSelect(Context);
    if (!NT_SUCCESS(status))
        AiSpiWrite(Context, AI_SPI_REG_CONTROL, AI_SPI_CONTROL_FIFO_RESET);
    return status;
}

NTSTATUS AiSpiTransfer(PAI_DEVICE_CONTEXT Context, const UCHAR *Tx, UCHAR *Rx,
                       SIZE_T Length, ULONGLONG DeadlineQpc)
{
    return AiSpiTransferPhase(Context, Tx, Rx, Length, DeadlineQpc, TRUE, TRUE);
}

NTSTATUS AiSpiWritePacketReadStatus(PAI_DEVICE_CONTEXT Context,
                                    const UCHAR Packet[AI_PACKET_SIZE],
                                    UCHAR Status[AI_SPI_WRITE_STATUS_SIZE],
                                    ULONGLONG DeadlineQpc)
{
    NTSTATUS status;

    if (!Packet || !Status)
        return STATUS_INVALID_PARAMETER;
    status = AiSpiTransferPhase(Context, Packet, NULL, AI_PACKET_SIZE,
                                DeadlineQpc, TRUE, FALSE);
    if (!NT_SUCCESS(status))
        return status;
    AiSpiStall(AI_SPI_WRITE_STATUS_DELAY_US);
    status = AiSpiTransferPhase(Context, NULL, Status,
                                AI_SPI_WRITE_STATUS_SIZE, DeadlineQpc,
                                FALSE, TRUE);
    if (!NT_SUCCESS(status)) {
        AiSpiFinishChipSelect(Context);
        AiSpiWrite(Context, AI_SPI_REG_CONTROL, AI_SPI_CONTROL_FIFO_RESET);
    }
    return status;
}
