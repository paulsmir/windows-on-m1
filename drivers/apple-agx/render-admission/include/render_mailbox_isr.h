#ifndef RENDER_MAILBOX_ISR_H
#define RENDER_MAILBOX_ISR_H
/* ASC mailbox recv-not-empty line (investigation/analysis/KMD-IRQ-COMPLETION.md,
 * "Default-on plan").  The ASC mailbox variant has no interrupt enable or
 * acknowledge register: the level line is dismissed only by popping the
 * outbox FIFO until its EMPTY bit is set.  This is the pure decision of what
 * one ISR step does with the FIFO; the handler repeats it until NOT_OURS.
 * Host-compiled by tests/test_render_irq_quiesce.py: no kernel types. */

typedef enum _ADMISSION_MAILBOX_ISR_ACTION {
  /* the line is not listed, nothing is mapped, or the FIFO is empty:
   * nothing to pop, the adapter is not interrupting on this line */
  AdmissionMailboxIsrNotOurs = 0,
  /* armed, ring has room: pop the message into the ring */
  AdmissionMailboxIsrMove,
  /* armed, ring full: pop and drop the message (RingDrops) and set the
   * sticky overflow, which the worker treats as a transport fault */
  AdmissionMailboxIsrDiscardFault,
  /* not armed (no consumer): pop and drop the message (Discarded) */
  AdmissionMailboxIsrDiscard
} ADMISSION_MAILBOX_ISR_ACTION;

/* One ISR pass pops at most this many messages, then returns TRUE. */
#define ADMISSION_MAILBOX_ISR_POP_CAP 256u

static __inline ADMISSION_MAILBOX_ISR_ACTION AdmissionMailboxIsrAction(
    int LineListed, int BaseMapped, int Armed, int FifoEmpty,
    unsigned int RingDepth, unsigned int RingSlots) {
  if (!LineListed || !BaseMapped || FifoEmpty)
    return AdmissionMailboxIsrNotOurs;
  if (!Armed)
    return AdmissionMailboxIsrDiscard;
  return RingDepth < RingSlots ? AdmissionMailboxIsrMove
                               : AdmissionMailboxIsrDiscardFault;
}

#endif
