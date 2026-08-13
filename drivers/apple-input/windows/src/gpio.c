#include "apple_input_device.h"
#include "apple_input_hw.h"
#include "j313_apple_input.generated.h"

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
