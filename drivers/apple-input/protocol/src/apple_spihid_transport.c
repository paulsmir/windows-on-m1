#include "apple_spihid.h"

#include <string.h>

void ai_transport_queue_reset(struct ai_transport_queue *queue)
{
    if (queue)
        memset(queue, 0, sizeof(*queue));
}

bool ai_transport_irq(struct ai_transport_queue *queue)
{
    if (!queue)
        return false;
    if (queue->pending || queue->worker_active) {
        queue->pending = true;
        queue->coalesced_irqs++;
        return false;
    }
    queue->pending = true;
    return true;
}

bool ai_transport_worker_begin(struct ai_transport_queue *queue)
{
    if (!queue || queue->worker_active || !queue->pending)
        return false;
    queue->pending = false;
    queue->worker_active = true;
    return true;
}

bool ai_transport_worker_complete(struct ai_transport_queue *queue,
                                  bool interrupt_asserted)
{
    if (!queue || !queue->worker_active)
        return false;
    queue->worker_active = false;
    if (interrupt_asserted)
        queue->pending = true;
    return queue->pending;
}
