#include "apple_input_device.h"
#include "apple_input_hw.h"
#include "j313_apple_input.generated.h"

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
