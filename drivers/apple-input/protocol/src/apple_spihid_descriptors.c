#include "apple_spihid.h"

void ai_descriptor_store_reset(struct ai_descriptor_store *store)
{
    if (store)
        AI_MEMSET(store, sizeof(*store));
}

enum ai_status ai_descriptor_store_put(struct ai_descriptor_store *store,
                                       uint8_t device,
                                       const uint8_t *bytes,
                                       size_t length)
{
    struct ai_descriptor_slot *slot;

    if (!store || !bytes || length == 0)
        return AI_ERR_ARGUMENT;
    if (length > AI_DESCRIPTOR_MAX)
        return AI_ERR_LENGTH;
    if (device == 1)
        slot = &store->keyboard;
    else if (device == 2)
        slot = &store->trackpad;
    else
        return AI_ERR_PROTOCOL;

    AI_MEMSET(slot, sizeof(*slot));
    AI_MEMCPY(slot->bytes, bytes, length);
    slot->device = device;
    slot->length = (uint16_t)length;
    slot->valid = true;
    return AI_OK;
}

const struct ai_descriptor_slot *ai_descriptor_store_get(
    const struct ai_descriptor_store *store, uint8_t device)
{
    const struct ai_descriptor_slot *slot;

    if (!store)
        return NULL;
    if (device == 1)
        slot = &store->keyboard;
    else if (device == 2)
        slot = &store->trackpad;
    else
        return NULL;
    return slot->valid ? slot : NULL;
}
