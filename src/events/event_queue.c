// The event loop's queue. Producers on any thread add a copy of their event
// under the lock, and the event loop takes the oldest one. A full ring doubles
// instead of reusing slots, so an event loop that falls behind costs memory,
// never an event: handlers own what an event carries (a retained CGEvent, a
// client's socket), and dropping one would leak it or leave a client waiting.
//
// Mouse moves arrive at the pointer's rate whether or not anything uses them,
// and only the latest position matters. A move replaces the one queued right
// before it, so a blocked event loop finds one move waiting, not every one the
// pointer made meanwhile.

// Far more events than wait while the event loop keeps up; growing past it is
// logged.
#define EVENT_QUEUE_CAPACITY 4096

static bool event_queue_init(struct event_queue *queue, uint32_t capacity)
{
    *queue = (struct event_queue) { .capacity = capacity };
    queue->events = malloc(capacity * sizeof(struct event));

    return queue->events && pthread_mutex_init(&queue->lock, NULL) == 0;
}

// Called with the lock held. The slot `offset` places after the oldest event,
// for an offset within the ring.
static uint32_t event_queue_slot(struct event_queue *queue, uint32_t offset)
{
    uint32_t slot = queue->head + offset;
    return slot < queue->capacity ? slot : slot - queue->capacity;
}

// Called with the lock held. Copies the waiting events to the start of a ring
// twice as large.
static bool event_queue_grow(struct event_queue *queue)
{
    if (queue->capacity > UINT32_MAX / 2) return false;

    uint32_t capacity = queue->capacity * 2;
    struct event *events = malloc(capacity * sizeof(struct event));
    if (!events) return false;

    for (uint32_t i = 0; i < queue->count; ++i) {
        events[i] = queue->events[event_queue_slot(queue, i)];
    }

    free(queue->events);
    queue->events   = events;
    queue->capacity = capacity;
    queue->head     = 0;

    return true;
}

// Any thread. With `merge`, an event of the same type as the last one queued
// takes its place and hands it back in `replaced`, for the caller to release.
// On EVENT_QUEUE_GREW, `capacity` is the new size of the ring.
static enum event_queue_result event_queue_push(struct event_queue *queue, struct event event, bool merge,
                                                struct event *replaced, uint32_t *capacity)
{
    enum event_queue_result result = EVENT_QUEUE_ADDED;

    pthread_mutex_lock(&queue->lock);

    if (merge && queue->count) {
        struct event *last = &queue->events[event_queue_slot(queue, queue->count - 1)];
        if (last->type == event.type) {
            *replaced = *last;
            *last = event;
            result = EVENT_QUEUE_MERGED;
            goto out;
        }
    }

    if (queue->count == queue->capacity) {
        if (!event_queue_grow(queue)) {
            result = EVENT_QUEUE_FULL;
            goto out;
        }

        *capacity = queue->capacity;
        result = EVENT_QUEUE_GREW;
    }

    queue->events[event_queue_slot(queue, queue->count)] = event;
    ++queue->count;

out:
    pthread_mutex_unlock(&queue->lock);
    return result;
}

// Event loop. False when no event waits.
static bool event_queue_pop(struct event_queue *queue, struct event *event)
{
    pthread_mutex_lock(&queue->lock);

    bool popped = queue->count > 0;
    if (popped) {
        *event = queue->events[queue->head];
        queue->head = event_queue_slot(queue, 1);
        --queue->count;
    }

    pthread_mutex_unlock(&queue->lock);
    return popped;
}
