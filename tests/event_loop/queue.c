// The event loop's queue, see src/event_queue.c: order, growth, merged mouse
// moves, allocation failure and concurrent producers.
#include <assert.h>
#include <pthread.h>
#include <semaphore.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

static bool fail_allocation;

static void *queue_allocate(size_t size)
{
    return fail_allocation ? NULL : malloc(size);
}

#include "../../src/event_loop.h"
#define malloc queue_allocate
#include "../../src/event_queue.c"
#undef malloc

static struct event_queue queue;

static enum event_queue_result push(enum event_type type, int param1, bool merge, struct event *replaced)
{
    uint32_t capacity = 0;
    struct event event = { .type = type, .param1 = param1, .context = (void *)(intptr_t) param1 };
    enum event_queue_result result = event_queue_push(&queue, event, merge, replaced, &capacity);
    if (result == EVENT_QUEUE_GREW) assert(capacity == queue.capacity);
    return result;
}

static void expect_pop(enum event_type type, int param1)
{
    struct event event;
    assert(event_queue_pop(&queue, &event));
    assert(event.type == type && event.param1 == param1);
    assert(event.context == (void *)(intptr_t) param1);
}

static void reset(uint32_t capacity)
{
    if (queue.events) {
        free(queue.events);
        pthread_mutex_destroy(&queue.lock);
    }

    assert(event_queue_init(&queue, capacity));
}

// A full ring doubles and keeps the order, also when its events wrap around
// its end.
static void test_growth(void)
{
    struct event replaced;
    reset(4);

    for (int i = 0; i < 3; ++i) assert(push(WINDOW_CREATED, i, false, &replaced) == EVENT_QUEUE_ADDED);
    expect_pop(WINDOW_CREATED, 0);
    expect_pop(WINDOW_CREATED, 1);
    for (int i = 3; i < 6; ++i) assert(push(WINDOW_CREATED, i, false, &replaced) == EVENT_QUEUE_ADDED);
    assert(queue.count == 4 && queue.head == 2);

    assert(push(WINDOW_CREATED, 6, false, &replaced) == EVENT_QUEUE_GREW);
    assert(queue.capacity == 8 && queue.count == 5);
    for (int i = 2; i < 7; ++i) expect_pop(WINDOW_CREATED, i);

    struct event event;
    assert(!event_queue_pop(&queue, &event));
}

// A mouse move replaces only the move queued right before it.
static void test_merge(void)
{
    struct event replaced = { 0 };
    reset(8);

    assert(push(MOUSE_MOVED, 1, true, &replaced) == EVENT_QUEUE_ADDED);
    assert(push(MOUSE_MOVED, 2, true, &replaced) == EVENT_QUEUE_MERGED);
    assert(replaced.type == MOUSE_MOVED && replaced.param1 == 1);
    assert(push(MOUSE_DOWN, 3, false, &replaced) == EVENT_QUEUE_ADDED);
    assert(push(MOUSE_MOVED, 4, true, &replaced) == EVENT_QUEUE_ADDED);
    assert(push(MOUSE_MOVED, 5, true, &replaced) == EVENT_QUEUE_MERGED);
    assert(replaced.param1 == 4);

    // Without merging, a move stays even after another move.
    assert(push(MOUSE_MOVED, 6, false, &replaced) == EVENT_QUEUE_ADDED);

    expect_pop(MOUSE_MOVED, 2);
    expect_pop(MOUSE_DOWN, 3);
    expect_pop(MOUSE_MOVED, 5);
    expect_pop(MOUSE_MOVED, 6);

    // A move handled already is not replaced.
    assert(push(MOUSE_MOVED, 7, true, &replaced) == EVENT_QUEUE_ADDED);
    expect_pop(MOUSE_MOVED, 7);
    assert(push(MOUSE_MOVED, 8, true, &replaced) == EVENT_QUEUE_ADDED);
    expect_pop(MOUSE_MOVED, 8);
}

// A ring that cannot grow refuses the event and keeps the waiting ones.
static void test_allocation_failure(void)
{
    struct event replaced;
    reset(2);

    assert(push(WINDOW_CREATED, 1, false, &replaced) == EVENT_QUEUE_ADDED);
    assert(push(WINDOW_CREATED, 2, false, &replaced) == EVENT_QUEUE_ADDED);
    fail_allocation = true;
    assert(push(WINDOW_CREATED, 3, false, &replaced) == EVENT_QUEUE_FULL);
    fail_allocation = false;
    assert(queue.capacity == 2 && queue.count == 2);

    expect_pop(WINDOW_CREATED, 1);
    expect_pop(WINDOW_CREATED, 2);
}

#define PRODUCERS 4
#define PRODUCED 50000

// Another producer can grow the ring again before this one looks at it, so
// the capacity reported is checked for what it is, not against the ring.
static void *produce(void *context)
{
    struct event replaced;
    int producer = (int)(intptr_t) context;

    for (int i = 0; i < PRODUCED; ++i) {
        uint32_t capacity = 0;
        struct event event = { .type = DAEMON_MESSAGE + producer, .param1 = i, .context = (void *)(intptr_t) i };
        enum event_queue_result result = event_queue_push(&queue, event, false, &replaced, &capacity);
        assert(result == EVENT_QUEUE_ADDED || result == EVENT_QUEUE_GREW);
        if (result == EVENT_QUEUE_GREW) assert(capacity >= 32 && !(capacity & (capacity - 1)));
    }

    return NULL;
}

// Producers on several threads, one consumer: every event arrives once, in
// the order of its producer.
static void test_concurrent_producers(void)
{
    pthread_t threads[PRODUCERS];
    int next[PRODUCERS] = { 0 };
    int received = 0;
    reset(16);

    for (int i = 0; i < PRODUCERS; ++i) {
        assert(pthread_create(&threads[i], NULL, produce, (void *)(intptr_t) i) == 0);
    }

    while (received < PRODUCERS * PRODUCED) {
        struct event event;
        if (!event_queue_pop(&queue, &event)) continue;

        int producer = event.type - DAEMON_MESSAGE;
        assert(producer >= 0 && producer < PRODUCERS);
        assert(event.param1 == next[producer]++);
        ++received;
    }

    for (int i = 0; i < PRODUCERS; ++i) assert(pthread_join(threads[i], NULL) == 0);
}

int main(void)
{
    test_growth();
    test_merge();
    test_allocation_failure();
    test_concurrent_producers();

    puts("event queue: order, growth, merged moves, allocation failure and concurrent producers passed");
    return 0;
}
