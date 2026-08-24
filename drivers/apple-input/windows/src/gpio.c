#include "apple_input_device.h"
#include "apple_input_hw.h"
#include "j313_apple_input.generated.h"

static NTSTATUS AiGpioDelay(ULONGLONG Microseconds)
{
    LARGE_INTEGER interval;

    if (KeGetCurrentIrql() != PASSIVE_LEVEL)
        return STATUS_INVALID_DEVICE_STATE;
    interval.QuadPart = -(LONGLONG)(Microseconds * 10u);
    return KeDelayExecutionThread(KernelMode, FALSE, &interval);
}

static VOID AiGpioSetReset(PAI_DEVICE_CONTEXT Context, BOOLEAN High)
{
    PULONG reg = (PULONG)(Context->ApGpioRegisters +
        AiGpioPinOffset((ULONG)J313_APPLE_INPUT_AP_GPIO_PIN));
    ULONG value = READ_REGISTER_NOFENCE_ULONG(reg);
    WRITE_REGISTER_NOFENCE_ULONG(reg, AiGpioOutputValue(value, High));
}

NTSTATUS AiGpioValidateReadOnly(PAI_DEVICE_CONTEXT Context)
{
    ULONG ap_pin;
    ULONG nub_pin;

    if (!Context || !Context->ResourcesValidated || !Context->ApGpioRegisters ||
        !Context->NubGpioRegisters)
        return STATUS_DEVICE_NOT_READY;
    if (AiGpioPinOffset((ULONG)J313_APPLE_INPUT_AP_GPIO_PIN) + sizeof(ULONG) >
            Context->MemoryLength[1] ||
        AiGpioPinOffset((ULONG)J313_APPLE_INPUT_NUB_GPIO_PIN) + sizeof(ULONG) >
            Context->MemoryLength[2])
        return STATUS_DEVICE_CONFIGURATION_ERROR;

    ap_pin = READ_REGISTER_NOFENCE_ULONG((PULONG)(Context->ApGpioRegisters +
        AiGpioPinOffset((ULONG)J313_APPLE_INPUT_AP_GPIO_PIN)));
    nub_pin = READ_REGISTER_NOFENCE_ULONG((PULONG)(Context->NubGpioRegisters +
        AiGpioPinOffset((ULONG)J313_APPLE_INPUT_NUB_GPIO_PIN)));
    UNREFERENCED_PARAMETER(ap_pin);
    UNREFERENCED_PARAMETER(nub_pin);
    return STATUS_SUCCESS;
}

NTSTATUS AiGpioResetInputController(PAI_DEVICE_CONTEXT Context)
{
    NTSTATUS status;

    if (!Context || !Context->ResourcesValidated || !Context->ApGpioRegisters)
        return STATUS_DEVICE_NOT_READY;
    AiGpioSetReset(Context, TRUE);
    status = AiGpioDelay(J313_APPLE_INPUT_RESET_HIGH_US);
    if (!NT_SUCCESS(status))
        return status;
    AiGpioSetReset(Context, FALSE);
    status = AiGpioDelay(J313_APPLE_INPUT_RESET_LOW_US);
    if (!NT_SUCCESS(status)) {
        AiGpioSetReset(Context, TRUE);
        return status;
    }
    AiGpioSetReset(Context, TRUE);
    return AiGpioDelay(J313_APPLE_INPUT_BOOT_WAIT_US);
}

NTSTATUS AiGpioEnableInputInterrupt(PAI_DEVICE_CONTEXT Context)
{
    PULONG reg;
    ULONG value;
    ULONG mask;

    if (!Context || !Context->ResourcesValidated || !Context->NubGpioRegisters)
        return STATUS_DEVICE_NOT_READY;
    if (AiGpioPinOffset((ULONG)J313_APPLE_INPUT_NUB_GPIO_PIN) + sizeof(ULONG) >
            Context->MemoryLength[2])
        return STATUS_DEVICE_CONFIGURATION_ERROR;

    reg = (PULONG)(Context->NubGpioRegisters +
        AiGpioPinOffset((ULONG)J313_APPLE_INPUT_NUB_GPIO_PIN));
    value = READ_REGISTER_NOFENCE_ULONG(reg);
    mask = AI_GPIO_MODE_MASK | AI_GPIO_GROUP_MASK | AI_GPIO_PERIPH_MASK |
           AI_GPIO_DATA | AI_GPIO_INPUT_ENABLE;
    value &= ~mask;
    value |= AiGpioIrqMode((ULONG)J313_APPLE_INPUT_IRQ_STARTUP_GROUP,
                           AI_GPIO_MODE_IRQ_LOW) |
             AI_GPIO_INPUT_ENABLE;
    WRITE_REGISTER_NOFENCE_ULONG(reg, value);
    AiGpioAcknowledge(Context);
    return STATUS_SUCCESS;
}

BOOLEAN AiGpioInputAsserted(PAI_DEVICE_CONTEXT Context)
{
    PULONG reg;

    if (!Context || !Context->ResourcesValidated || !Context->NubGpioRegisters)
        return FALSE;
    reg = (PULONG)(Context->NubGpioRegisters +
        AiGpioPinOffset((ULONG)J313_APPLE_INPUT_NUB_GPIO_PIN));
    return AiGpioInputAssertedValue(READ_REGISTER_NOFENCE_ULONG(reg)) ? TRUE : FALSE;
}

VOID AiGpioAcknowledge(PAI_DEVICE_CONTEXT Context)
{
    ULONG offset;

    if (!Context || !Context->ResourcesValidated || !Context->NubGpioRegisters)
        return;
    offset = AiGpioIrqAckOffset((ULONG)J313_APPLE_INPUT_NUB_GPIO_PIN,
        (ULONG)J313_APPLE_INPUT_IRQ_STARTUP_GROUP);
    if (!AiSpiRegisterRangeValid(offset, sizeof(ULONG), Context->MemoryLength[2]))
        return;
    WRITE_REGISTER_NOFENCE_ULONG(
        (PULONG)(Context->NubGpioRegisters + offset),
        AiGpioIrqAckMask((ULONG)J313_APPLE_INPUT_NUB_GPIO_PIN));
}
