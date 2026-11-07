/* SPDX-License-Identifier: MIT */
#include "transport.h"
#if defined(_KERNEL_MODE)
#define COPY(d,s,n) RtlCopyMemory(d,s,n)
#define ZERO(d,n) RtlZeroMemory(d,n)
#else
#include <string.h>
#define COPY(d,s,n) memcpy(d,s,n)
#define ZERO(d,n) memset(d,0,n)
#endif

static int equal(const void *a, const void *b, unsigned n)
{
    const ans_u8 *x = a, *y = b;
    for (unsigned i = 0; i < n; i++) if (x[i] != y[i]) return 0;
    return 1;
}
static void read_words(struct ans_wire *w, ans_u32 offset, void *out, unsigned size)
{
    for (unsigned i = 0; i < size; i += 4) {
        ans_u32 value = w->ops->read32(w->ctx, offset + i);
        COPY((ans_u8 *)out + i, &value, 4);
    }
}
static int header(struct ans_wire *w, struct ans_broker_header *out)
{
    struct ans_broker_header other;
    read_words(w, 0, out, sizeof(*out));
    w->ops->barrier(w->ctx);
    read_words(w, 0, &other, sizeof(other));
    return equal(out, &other, sizeof(other)) && out->magic == ANS_MAGIC &&
        out->abi_major == ANS_ABI_MAJOR && out->abi_minor == ANS_ABI_MINOR &&
        out->size == sizeof(*out) && out->generation && !out->reserved &&
        out->state <= ANS_STATE_FAULTED &&
        /* Exactly one write policy: read-only, or an EL2-enforced write window. */
        (!(out->features & ANS_FEATURE_READ_ONLY) != !(out->features & ANS_FEATURE_WRITE_WINDOW));
}
int ans_wire_open(struct ans_wire *w, const struct ans_wire_ops *ops, void *ctx)
{
    struct ans_broker_header h;
    if (!w || !ops || !ops->read32 || !ops->read64 || !ops->write32 || !ops->barrier)
        return ANS_WIRE_INVALID;
    ZERO(w, sizeof(*w));
    w->ops = ops;
    w->ctx = ctx;
    if (!header(w, &h) || (h.state != ANS_STATE_READY && h.state != ANS_STATE_OFF) ||
        h.sequence == ANS_U64_MAX)
        return ANS_WIRE_INVALID;
    w->generation = h.generation;
    w->sequence = h.sequence;
    w->features = h.features;
    w->state = h.state;
    w->bound = 1;
    return ANS_WIRE_OK;
}
int ans_wire_call(struct ans_wire *w, struct ans_broker_request *r,
                  struct ans_broker_response *out)
{
    struct ans_broker_response second;
    if (!w || !w->bound || !r || !out || r->operation < ANS_OP_QUERY ||
        r->operation > ANS_OP_RELEASE || w->sequence == ANS_U64_MAX)
        return ANS_WIRE_INVALID;
    int recovery = r->operation == ANS_OP_RESET || r->operation == ANS_OP_RELEASE ||
                   r->operation == ANS_OP_QUIESCE;
    if (w->faulted && !recovery) return ANS_WIRE_UNCERTAIN;
    if (r->reserved || r->queue > 1 || r->tag >= ANS_QUEUE_DEPTH)
        return ANS_WIRE_INVALID;
    r->abi_major = ANS_ABI_MAJOR;
    r->abi_minor = ANS_ABI_MINOR;
    r->size = sizeof(*r);
    r->generation = w->generation;
    r->sequence = ++w->sequence;
    for (unsigned i = 0; i < sizeof(*r); i += 4) {
        ans_u32 value;
        COPY(&value, (const ans_u8 *)r + i, 4);
        w->ops->write32(w->ctx, ANS_REQUEST_OFFSET + i, value);
    }
    w->ops->barrier(w->ctx);
    w->ops->write32(w->ctx, ANS_DOORBELL_OFFSET, (ans_u32)r->sequence);
    w->ops->barrier(w->ctx);
    read_words(w, ANS_RESPONSE_OFFSET, out, sizeof(*out));
    w->ops->barrier(w->ctx);
    read_words(w, ANS_RESPONSE_OFFSET, &second, sizeof(second));
    if (!equal(out, &second, sizeof(second)) || out->sequence != r->sequence ||
        out->queue != r->queue || out->tag != r->tag || out->reserved ||
        out->result > ANS_RESULT_IO_ERROR || out->state > ANS_STATE_FAULTED)
        goto uncertain;
    int retires = out->result == ANS_RESULT_OK &&
        (r->operation == ANS_OP_RESET || r->operation == ANS_OP_RELEASE);
    if (retires) {
        if (w->generation == ANS_U64_MAX || out->generation != w->generation + 1 ||
            out->state != (ans_u32)(r->operation == ANS_OP_RESET ? ANS_STATE_READY : ANS_STATE_OFF))
            goto uncertain;
    } else if (out->generation != w->generation) goto uncertain;
    if (out->state == ANS_STATE_FAULTED) goto uncertain;
    if (out->result != ANS_RESULT_OK) return ANS_WIRE_REJECTED;
    if ((r->operation == ANS_OP_CLAIM && out->state != ANS_STATE_CLAIMED) ||
        (r->operation == ANS_OP_QUIESCE && out->state != ANS_STATE_QUIESCING) ||
        ((r->operation == ANS_OP_REGISTER_QUEUES || r->operation == ANS_OP_AUTHORIZE_SUBMIT) &&
         out->state != ANS_STATE_CLAIMED)) goto uncertain;
    w->state = out->state;
    if (retires) {
        struct ans_broker_header h;
        if (!header(w, &h) || h.generation != out->generation || h.state != out->state ||
            h.sequence != 0) goto uncertain;
        w->generation = h.generation;
        w->sequence = h.sequence;
        w->faulted = 0;
    }
    return ANS_WIRE_OK;
uncertain:
    w->faulted = 1;
    return ANS_WIRE_UNCERTAIN;
}
int ans_wire_pending(struct ans_wire *w, ans_u64 *pending)
{
    if (!w || !w->bound || !pending || w->faulted) return ANS_WIRE_INVALID;
    *pending = w->ops->read64(w->ctx, ANS_PENDING_OFFSET);
    w->ops->barrier(w->ctx);
    return ANS_WIRE_OK;
}
int ans_wire_cqe(struct ans_wire *w, unsigned tag, ans_u8 out[16])
{
    ans_u8 second[16];
    if (!w || !w->bound || w->faulted || tag >= ANS_QUEUE_DEPTH || !out)
        return ANS_WIRE_INVALID;
    read_words(w, ANS_COMPLETION_OFFSET + tag * 16, out, 16);
    w->ops->barrier(w->ctx);
    read_words(w, ANS_COMPLETION_OFFSET + tag * 16, second, 16);
    if (!equal(out, second, 16) || out[12] != tag || out[13]) {
        w->faulted = 1;
        return ANS_WIRE_UNCERTAIN;
    }
    return ANS_WIRE_OK;
}
