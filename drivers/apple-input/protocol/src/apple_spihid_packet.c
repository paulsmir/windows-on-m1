#include "apple_spihid.h"

static uint16_t get_le16(const uint8_t *bytes)
{
    return (uint16_t)bytes[0] | (uint16_t)((uint16_t)bytes[1] << 8);
}

static void put_le16(uint8_t *bytes, uint16_t value)
{
    bytes[0] = (uint8_t)value;
    bytes[1] = (uint8_t)(value >> 8);
}

bool ai_write_status_valid(const uint8_t *status, size_t size)
{
    static const uint8_t expected[] = {0xac, 0x27, 0x68, 0xd5};

    if (!status || size != sizeof(expected))
        return false;
    for (size_t index = 0; index < sizeof(expected); index++) {
        if (status[index] != expected[index])
            return false;
    }
    return true;
}

enum ai_status ai_packet_decode(const uint8_t raw[AI_PACKET_SIZE], struct ai_packet_view *out)
{
    if (!raw || !out)
        return AI_ERR_ARGUMENT;
    if (ai_crc16_usb(0, raw, AI_PACKET_SIZE - 2) != get_le16(raw + AI_PACKET_SIZE - 2))
        return AI_ERR_CRC;

    const uint16_t length = get_le16(raw + 6);
    if (length > AI_PACKET_DATA_SIZE)
        return AI_ERR_LENGTH;

    out->flags = raw[0];
    out->device = raw[1];
    out->offset = get_le16(raw + 2);
    out->remaining = get_le16(raw + 4);
    out->length = length;
    out->data = raw + 8;
    return AI_OK;
}

enum ai_status ai_message_decode(const uint8_t *raw, size_t size,
                                 struct ai_protocol_message *out)
{
    if (!raw || !out)
        return AI_ERR_ARGUMENT;
    if (size < 10)
        return AI_ERR_LENGTH;

    const uint16_t payload_length = get_le16(raw + 6);
    const size_t expected = (size_t)payload_length + 10;
    if (expected < payload_length || size != expected)
        return AI_ERR_LENGTH;
    if (ai_crc16_usb(0, raw, size - 2) != get_le16(raw + size - 2))
        return AI_ERR_CRC;

    out->type = raw[0];
    out->report = raw[1];
    out->device = raw[2];
    out->id = raw[3];
    out->response_length = get_le16(raw + 4);
    out->payload = raw + 8;
    out->payload_length = payload_length;
    return AI_OK;
}

enum ai_status ai_discovery_request_encode(const struct ai_discovery_request *request,
                                           uint8_t message_id,
                                           uint8_t raw[AI_PACKET_SIZE])
{
    if (!request || !raw)
        return AI_ERR_ARGUMENT;

    AI_MEMSET(raw, AI_PACKET_SIZE);
    raw[0] = AI_PACKET_WRITE;
    raw[1] = request->target;
    put_le16(raw + 6, 10);

    raw[8] = request->type;
    raw[9] = request->report;
    raw[10] = request->device;
    raw[11] = message_id;
    put_le16(raw + 12, request->response_length);
    put_le16(raw + 14, 0);
    put_le16(raw + 16, ai_crc16_usb(0, raw + 8, 8));
    put_le16(raw + AI_PACKET_SIZE - 2,
             ai_crc16_usb(0, raw, AI_PACKET_SIZE - 2));
    return AI_OK;
}
