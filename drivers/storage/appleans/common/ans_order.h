/* SPDX-License-Identifier: MIT */
#ifndef APPLE_ANS_ORDER_H
#define APPLE_ANS_ORDER_H
#include "../include/apple_ans_broker_abi.h"
/* One LU, one Windows initiator; all calls require the caller's interrupt lock.
 * These are portable semantic values, NOT WDK QueueAction wire constants.
 * Caller explicitly maps SRB_SIMPLE/ORDERED/HEAD_OF_QUEUE_TAG_REQUEST. Without
 * SRB_FLAGS_QUEUE_ACTION_ENABLE use UNTAGGED; unknown/ACA is unsupported.
 * Untagged admission requires an empty LU; mixed admission returns BUSY.
 * Sources: Microsoft Storport's Interface with Storport Miniport Drivers;
 * Seagate 75789509b.pdf 7.7.2 (SAM): queued HEAD LIFO, no active preemption;
 * T10/04-187r1 resolves ORDERED waiting for all HEADs, not just older HEADs.
 * SNT1.5 6.1.1 CMDQUE is Command Management Model; it does not advertise ACA,
 * task abort, or extended inquiry HOQSUP/ORDSUP/SIMPSUP by implication.
 * https://learn.microsoft.com/en-us/windows-hardware/drivers/storage/storport-s-interface-with-storport-miniport-drivers
 * https://www.seagate.com/support/disc/manuals/scsi/75789509b.pdf
 * https://www.t10.org/ftp/t10/document.04/04-187r1.pdf
 * https://nvmexpress.org/wp-content/uploads/NVM_Express_-_SCSI_Translation_Reference-1_5_20150624_Gold.pdf
 */
enum ans_order_attribute {
    ANS_ORDER_SIMPLE, ANS_ORDER_ORDERED, ANS_ORDER_HEAD, ANS_ORDER_UNTAGGED
};
enum ans_order_result {
    ANS_ORDER_OK, ANS_ORDER_EMPTY, ANS_ORDER_BUSY, ANS_ORDER_INVALID,
    ANS_ORDER_STALE, ANS_ORDER_FAULT, ANS_ORDER_STOP_REQUIRED, ANS_ORDER_EXHAUSTED
};
enum ans_order_state {
    ANS_ORDER_FREE, ANS_ORDER_QUEUED, ANS_ORDER_SELECTED,
    ANS_ORDER_ACTIVE, ANS_ORDER_QUARANTINED
};
struct ans_order_token { ans_u64 epoch, sequence; ans_u16 tag; };
struct ans_order_slot {
    ans_u64 sequence;
    enum ans_order_attribute attribute;
    enum ans_order_state state;
};
struct ans_order {
    ans_u64 epoch, sequence;
    unsigned faulted;
    struct ans_order_slot slot[ANS_QUEUE_DEPTH];
};
/* Init only fresh storage. Epoch is independent of device QueueTag values. */
int ans_order_init(struct ans_order *o, ans_u64 epoch);
int ans_order_admit(struct ans_order *o, unsigned tag, enum ans_order_attribute attribute,
                    struct ans_order_token *token);
/* Reserves one dispatch decision. Caller must resolve SELECTED by mark_submitted
 * before entering uncertain hardware publication, or cancel_queued if no publish
 * occurred. No next pick until SELECTED is resolved. Keep the lock through this
 * decision/publication boundary. New HEAD never preempts already ACTIVE work. */
int ans_order_pick(struct ans_order *o, struct ans_order_token *token);
int ans_order_mark_submitted(struct ans_order *o, struct ans_order_token token);
/* Only after authenticated CQE + successful REAP, or a local metadata response
 * actually completed without hardware, or an authenticated rejection proving
 * no publication. Errors that definitively end a task also complete it. */
int ans_order_complete(struct ans_order *o, struct ans_order_token token);
int ans_order_cancel_queued(struct ans_order *o, struct ans_order_token token);
/* Ambiguous publication/completion/reset freezes all dispatch and preserves slots. */
int ans_order_quarantine(struct ans_order *o, struct ans_order_token token);
void ans_order_fault(struct ans_order *o);
/* Stop proof is mandatory even if apparently empty; caller completes affected
 * SRBs and releases their backing separately. Epoch increments; never wraps. */
int ans_order_reset(struct ans_order *o, int dma_stopped);
#endif
