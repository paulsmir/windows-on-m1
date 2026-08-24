#include "apple_trackpad.h"

static uint16_t get_le16(const uint8_t *bytes)
{
    return (uint16_t)bytes[0] | (uint16_t)((uint16_t)bytes[1] << 8);
}

static int32_t get_le_i16(const uint8_t *bytes)
{
    uint16_t value = get_le16(bytes);

    if ((value & 0x8000u) != 0u)
        return (int32_t)value - 0x10000;
    return (int32_t)value;
}

bool ai_apple_trackpad_release_candidate(const uint8_t *report, size_t length)
{
    uint8_t contact_count;
    size_t expected_length;
    size_t index;

    if (!report || length < AI_APPLE_TRACKPAD_HEADER_SIZE - 2u)
        return false;

    contact_count = report[AI_APPLE_TRACKPAD_CONTACT_COUNT_OFFSET];
    if (contact_count > AI_APPLE_TRACKPAD_MAX_CONTACTS)
        return false;

    expected_length = AI_APPLE_TRACKPAD_HEADER_SIZE - 2u +
        (size_t)contact_count * AI_APPLE_TRACKPAD_CONTACT_STRIDE;
    if (length != expected_length)
        return false;
    if (contact_count == 0u)
        return true;

    for (index = 0; index < contact_count; ++index) {
        size_t touch_major = AI_APPLE_TRACKPAD_HEADER_SIZE +
            index * AI_APPLE_TRACKPAD_CONTACT_STRIDE +
            AI_APPLE_TRACKPAD_TOUCH_MAJOR_OFFSET;

        if (get_le16(report + touch_major) == 0u)
            return true;
    }
    return false;
}

enum ai_status ai_apple_trackpad_decode(
    const uint8_t *report, size_t length,
    struct ai_apple_trackpad_frame *out)
{
    uint8_t wire_count;
    uint8_t index;
    size_t expected_length;

    if (!report || !out)
        return AI_ERR_ARGUMENT;
    AI_MEMSET(out, sizeof(*out));
    if (length < AI_APPLE_TRACKPAD_HEADER_SIZE - 2u)
        return AI_ERR_LENGTH;

    wire_count = report[AI_APPLE_TRACKPAD_CONTACT_COUNT_OFFSET];
    if (wire_count > AI_APPLE_TRACKPAD_MAX_CONTACTS)
        return AI_ERR_LENGTH;
    expected_length = AI_APPLE_TRACKPAD_HEADER_SIZE - 2u +
        (size_t)wire_count * AI_APPLE_TRACKPAD_CONTACT_STRIDE;
    if (length != expected_length)
        return AI_ERR_LENGTH;
    if (report[1] != report[31] || report[1] > 1u)
        return AI_ERR_PROTOCOL;

    out->button = report[1] != 0u;
    for (index = 0; index < wire_count; ++index) {
        const uint8_t *contact = report + AI_APPLE_TRACKPAD_HEADER_SIZE +
            (size_t)index * AI_APPLE_TRACKPAD_CONTACT_STRIDE;

        if (get_le16(contact + AI_APPLE_TRACKPAD_TOUCH_MAJOR_OFFSET) == 0u)
            continue;
        out->contacts[out->count].x = get_le_i16(contact + 2u);
        out->contacts[out->count].y = get_le_i16(contact + 4u);
        ++out->count;
    }
    return AI_OK;
}

typedef signed long long ai_trackpad_cost_t;

#define AI_TRACKPAD_COST_MAX ((ai_trackpad_cost_t)0x3fffffffffffffffLL)

static ai_trackpad_cost_t coordinate_cost(
    const struct ai_trackpad_physical_slot *slot,
    const struct ai_apple_trackpad_contact *contact)
{
    ai_trackpad_cost_t dx = (ai_trackpad_cost_t)slot->x - contact->x;
    ai_trackpad_cost_t dy = (ai_trackpad_cost_t)slot->y - contact->y;
    unsigned long long ax = dx < 0 ? (unsigned long long)-dx :
        (unsigned long long)dx;
    unsigned long long ay = dy < 0 ? (unsigned long long)-dy :
        (unsigned long long)dy;
    unsigned long long x2;
    unsigned long long y2;

    if (ax > 1518500249ULL || ay > 1518500249ULL)
        return AI_TRACKPAD_COST_MAX;
    x2 = ax * ax;
    y2 = ay * ay;
    if (x2 > (unsigned long long)AI_TRACKPAD_COST_MAX - y2)
        return AI_TRACKPAD_COST_MAX;
    return (ai_trackpad_cost_t)(x2 + y2);
}

/*
 * Rectangular Hungarian assignment. Rows never exceed columns.  The small,
 * fixed work arrays keep trajectory matching bounded and allocation-free.
 */
static void minimum_assignment(
    const struct ai_trackpad_tracker *tracker,
    const struct ai_apple_trackpad_frame *frame,
    const uint8_t *old_slots, uint8_t old_count,
    int8_t old_to_new[AI_APPLE_TRACKPAD_MAX_CONTACTS],
    int8_t new_to_old[AI_APPLE_TRACKPAD_MAX_CONTACTS])
{
    ai_trackpad_cost_t cost[AI_APPLE_TRACKPAD_MAX_CONTACTS]
                             [AI_APPLE_TRACKPAD_MAX_CONTACTS];
    ai_trackpad_cost_t u[AI_APPLE_TRACKPAD_MAX_CONTACTS + 1u] = {0};
    ai_trackpad_cost_t v[AI_APPLE_TRACKPAD_MAX_CONTACTS + 1u] = {0};
    ai_trackpad_cost_t minv[AI_APPLE_TRACKPAD_MAX_CONTACTS + 1u];
    uint8_t p[AI_APPLE_TRACKPAD_MAX_CONTACTS + 1u] = {0};
    uint8_t way[AI_APPLE_TRACKPAD_MAX_CONTACTS + 1u] = {0};
    bool used[AI_APPLE_TRACKPAD_MAX_CONTACTS + 1u];
    bool rows_are_old = old_count <= frame->count;
    uint8_t rows = rows_are_old ? old_count : frame->count;
    uint8_t columns = rows_are_old ? frame->count : old_count;
    uint8_t i;
    uint8_t j;

    for (i = 0; i < rows; ++i) {
        for (j = 0; j < columns; ++j) {
            uint8_t old_index = rows_are_old ? i : j;
            uint8_t new_index = rows_are_old ? j : i;

            cost[i][j] = coordinate_cost(
                &tracker->slots[old_slots[old_index]],
                &frame->contacts[new_index]);
        }
    }

    for (i = 1; i <= rows; ++i) {
        uint8_t j0 = 0;

        p[0] = i;
        for (j = 0; j <= columns; ++j) {
            minv[j] = AI_TRACKPAD_COST_MAX;
            used[j] = false;
        }
        do {
            uint8_t i0;
            uint8_t j1 = 0;
            ai_trackpad_cost_t delta = AI_TRACKPAD_COST_MAX;

            used[j0] = true;
            i0 = p[j0];
            for (j = 1; j <= columns; ++j) {
                ai_trackpad_cost_t current;

                if (used[j])
                    continue;
                current = cost[i0 - 1u][j - 1u] - u[i0] - v[j];
                if (current < minv[j]) {
                    minv[j] = current;
                    way[j] = j0;
                }
                if (minv[j] < delta ||
                    (minv[j] == delta && j1 == 0u)) {
                    delta = minv[j];
                    j1 = j;
                }
            }
            for (j = 0; j <= columns; ++j) {
                if (used[j]) {
                    u[p[j]] += delta;
                    v[j] -= delta;
                } else {
                    minv[j] -= delta;
                }
            }
            j0 = j1;
        } while (p[j0] != 0u);

        do {
            uint8_t j1 = way[j0];

            p[j0] = p[j1];
            j0 = j1;
        } while (j0 != 0u);
    }

    for (j = 1; j <= columns; ++j) {
        uint8_t row;
        uint8_t old_index;
        uint8_t new_index;

        if (p[j] == 0u)
            continue;
        row = p[j] - 1u;
        old_index = rows_are_old ? row : j - 1u;
        new_index = rows_are_old ? j - 1u : row;
        old_to_new[old_index] = (int8_t)new_index;
        new_to_old[new_index] = (int8_t)old_index;
    }
}

static void append_output(struct ai_trackpad_output_frame *out, bool tip,
                          uint8_t id, int32_t x, int32_t y)
{
    struct ai_trackpad_output_contact *contact = &out->contacts[out->count++];

    contact->tip = tip;
    contact->id = id;
    contact->x = x;
    contact->y = y;
    if (tip)
        ++out->active_count;
}

static void sort_output_by_id(struct ai_trackpad_output_frame *out)
{
    uint8_t i;

    for (i = 1; i < out->count; ++i) {
        struct ai_trackpad_output_contact value = out->contacts[i];
        uint8_t j = i;

        while (j > 0u && out->contacts[j - 1u].id > value.id) {
            out->contacts[j] = out->contacts[j - 1u];
            --j;
        }
        out->contacts[j] = value;
    }
}

enum ai_status ai_trackpad_tracker_update(
    struct ai_trackpad_tracker *tracker,
    const struct ai_apple_trackpad_frame *frame,
    struct ai_trackpad_output_frame *out)
{
    uint8_t old_slots[AI_APPLE_TRACKPAD_MAX_CONTACTS];
    int8_t old_to_new[AI_APPLE_TRACKPAD_MAX_CONTACTS];
    int8_t new_to_old[AI_APPLE_TRACKPAD_MAX_CONTACTS];
    bool id_available_before[AI_PTP_MAX_CONTACTS];
    uint8_t old_count = 0;
    uint8_t index;

    if (!tracker || !frame || !out)
        return AI_ERR_ARGUMENT;
    AI_MEMSET(out, sizeof(*out));
    if (frame->count > AI_APPLE_TRACKPAD_MAX_CONTACTS)
        return AI_ERR_LENGTH;
    out->button = frame->button;
    for (index = 0; index < AI_APPLE_TRACKPAD_MAX_CONTACTS; ++index) {
        old_to_new[index] = -1;
        new_to_old[index] = -1;
        if (tracker->slots[index].active)
            old_slots[old_count++] = index;
    }
    for (index = 0; index < AI_PTP_MAX_CONTACTS; ++index)
        id_available_before[index] = true;
    for (index = 0; index < old_count; ++index) {
        const struct ai_trackpad_physical_slot *slot =
            &tracker->slots[old_slots[index]];

        if (slot->admitted && slot->windows_id < AI_PTP_MAX_CONTACTS)
            id_available_before[slot->windows_id] = false;
    }

    minimum_assignment(tracker, frame, old_slots, old_count,
                       old_to_new, new_to_old);

    for (index = 0; index < old_count; ++index) {
        struct ai_trackpad_physical_slot *slot =
            &tracker->slots[old_slots[index]];

        if (old_to_new[index] >= 0) {
            const struct ai_apple_trackpad_contact *contact =
                &frame->contacts[(uint8_t)old_to_new[index]];

            slot->x = contact->x;
            slot->y = contact->y;
            if (slot->admitted)
                append_output(out, true, slot->windows_id, slot->x, slot->y);
        } else {
            if (slot->admitted)
                append_output(out, false, slot->windows_id, slot->x, slot->y);
            slot->active = false;
            slot->admitted = false;
        }
    }

    for (index = 0; index < frame->count; ++index) {
        struct ai_trackpad_physical_slot *slot = NULL;
        uint8_t slot_index;
        uint8_t id;

        if (new_to_old[index] >= 0)
            continue;
        for (slot_index = 0; slot_index < AI_APPLE_TRACKPAD_MAX_CONTACTS;
             ++slot_index) {
            if (!tracker->slots[slot_index].active) {
                slot = &tracker->slots[slot_index];
                break;
            }
        }
        if (!slot)
            return AI_ERR_PROTOCOL;
        slot->active = true;
        slot->admitted = false;
        slot->x = frame->contacts[index].x;
        slot->y = frame->contacts[index].y;
        for (id = 0; id < AI_PTP_MAX_CONTACTS; ++id) {
            if (id_available_before[id]) {
                id_available_before[id] = false;
                slot->admitted = true;
                slot->windows_id = id;
                append_output(out, true, id, slot->x, slot->y);
                break;
            }
        }
    }

    for (index = 0; index < AI_APPLE_TRACKPAD_MAX_CONTACTS; ++index) {
        if (tracker->slots[index].active && !tracker->slots[index].admitted)
            ++out->suppressed_count;
    }
    sort_output_by_id(out);
    return AI_OK;
}
