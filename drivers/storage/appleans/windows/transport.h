/* SPDX-License-Identifier: MIT */
#ifndef APPLE_ANS_WIRE_H
#define APPLE_ANS_WIRE_H
#include "../include/apple_ans_broker_abi.h"

/* All operations serialized by the owner's controller/interrupt lock. */
struct ans_wire_ops {
    ans_u32 (*read32)(void *ctx, ans_u32 offset);
    ans_u64 (*read64)(void *ctx, ans_u32 offset);
    void (*write32)(void *ctx, ans_u32 offset, ans_u32 value);
    void (*barrier)(void *ctx);
};
struct ans_wire {
    const struct ans_wire_ops *ops;
    void *ctx;
    ans_u64 generation, sequence;
    ans_u32 state, features;
    int bound, faulted;
};
enum ans_wire_result { ANS_WIRE_OK, ANS_WIRE_REJECTED, ANS_WIRE_UNCERTAIN,
                       ANS_WIRE_INVALID };
int ans_wire_open(struct ans_wire *w, const struct ans_wire_ops *ops, void *ctx);
int ans_wire_call(struct ans_wire *w, struct ans_broker_request *request,
                  struct ans_broker_response *response);
int ans_wire_pending(struct ans_wire *w, ans_u64 *pending);
int ans_wire_cqe(struct ans_wire *w, unsigned tag, ans_u8 out[16]);
#endif
