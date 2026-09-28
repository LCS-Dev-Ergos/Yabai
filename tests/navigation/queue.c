#include <assert.h>
#include <math.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static bool fail_allocation;

static void *queue_allocate(size_t size)
{
    return fail_allocation ? NULL : malloc(size);
}

#define malloc queue_allocate
#include "../../src/navigation/admission.c"
#undef malloc

#include "queue_ordering.h"
#include "queue_threads.h"

// A repeat that starts a group, after the previous one was claimed, is marked
// for the navigation schedule; a press is not.
static void test_repeat_groups(void)
{
    uint64_t now = 200000000000ULL;

    assert(!space_navigation_queue_join(40, 1, now));
    space_navigation_queue_claim(40);
    assert(g_space_navigation_claim.active && !g_space_navigation_claim.repeat);

    assert(!space_navigation_queue_join(41, 1, now + 30000000));
    space_navigation_queue_claim(41);
    assert(g_space_navigation_claim.active && g_space_navigation_claim.repeat);
    assert(g_space_navigation_claim.steps == 1);

    // A repeat delayed on its way still counts as one; a press a person
    // makes right after the last one does not.
    assert(!space_navigation_queue_join(46, 1, now + 100000000));
    space_navigation_queue_claim(46);
    assert(g_space_navigation_claim.active && g_space_navigation_claim.repeat);

    assert(!space_navigation_queue_join(47, 1, now + 190000000));
    space_navigation_queue_claim(47);
    assert(g_space_navigation_claim.active && !g_space_navigation_claim.repeat);

    // Another direction, or a pause, is a separate press.
    assert(!space_navigation_queue_join(42, -1, now + 200000000));
    space_navigation_queue_claim(42);
    assert(g_space_navigation_claim.active && !g_space_navigation_claim.repeat);

    assert(!space_navigation_queue_join(43, -1, now + 400000000));
    space_navigation_queue_claim(43);
    assert(g_space_navigation_claim.active && !g_space_navigation_claim.repeat);

    // A query between repeats of a held key does not make the next one a press.
    assert(!space_navigation_queue_join(44, 0, now + 410000000));
    space_navigation_queue_claim(44);
    assert(!space_navigation_queue_join(45, -1, now + 430000000));
    space_navigation_queue_claim(45);
    assert(g_space_navigation_claim.active && g_space_navigation_claim.repeat);
    assert(!g_space_navigation_queue.first && !g_space_navigation_queue.last);
}

// Frames a request as the yabai client sends it.
static int request(char *bytes, const char **arguments, int count)
{
    int size = 1;
    for (int i = 0; i < count; ++i) size += strlen(arguments[i]) + 1;

    memcpy(bytes, &size, sizeof(size));
    char *cursor = bytes + sizeof(size);

    for (int i = 0; i < count; ++i) {
        size_t length = strlen(arguments[i]) + 1;
        memcpy(cursor, arguments[i], length);
        cursor += length;
    }

    *cursor = '\0';

    return (int) sizeof(size) + size;
}

static int direction(const char **arguments, int count)
{
    char bytes[256];
    int length = request(bytes, arguments, count);

    return space_navigation_request_direction(bytes, length);
}

#define DIRECTION(...) direction((const char *[]) { __VA_ARGS__ }, sizeof((const char *[]) { __VA_ARGS__ }) / sizeof(const char *))

int main(void)
{
    assert(DIRECTION("space", "--navigate", "focus", "next", "0.95", "0.1") == 1);
    assert(DIRECTION("space", "--navigate", "focus", "prev", "1", "0") == -1);
    assert(DIRECTION("space", "--navigate", "focus", "next", "crossfade", "0.2") == 1);
    assert(DIRECTION("space", "--navigate", "focus", "next", "crossfades", "0.2") == 0);

    assert(DIRECTION("space", "--navigate", "move", "next", "0.95", "0.1") == 0);
    assert(DIRECTION("space", "--navigate", "focus", "3", "0.95", "0.1") == 0);
    assert(DIRECTION("space", "--navigate", "focus", "next", "0", "0.1") == 0);
    assert(DIRECTION("space", "--navigate", "focus", "next", "0.95", "1.5") == 0);
    assert(DIRECTION("space", "--navigate", "focus", "next", "0.95x", "0.1") == 0);
    assert(DIRECTION("space", "--navigate", "focus", "next", "0.95") == 0);
    assert(DIRECTION("space", "--navigate", "focus", "next", "0.95", "0.1", "extra") == 0);
    assert(DIRECTION("space", "--focus", "next") == 0);
    assert(DIRECTION("query", "--spaces") == 0);

    // Incomplete, oversized or unterminated messages are posted as usual.
    char bytes[256];
    int length = request(bytes, (const char *[]) { "space", "--navigate", "focus", "next", "0.95", "0.1" }, 6);

    assert(space_navigation_request_direction(bytes, length - 1) == 0);
    assert(space_navigation_request_direction(bytes, 3) == 0);

    int size = 1000;
    memcpy(bytes, &size, sizeof(size));
    assert(space_navigation_request_direction(bytes, length) == 0);

    length = request(bytes, (const char *[]) { "space", "--navigate", "focus", "next", "0.95", "0.1" }, 6);
    bytes[length - 1] = 'x';
    assert(space_navigation_request_direction(bytes, length) == 0);

    // A held key: repeats keep the one waiting step.
    uint64_t now = 1000000000;

    assert(!space_navigation_queue_join(5, 1, now));
    assert(space_navigation_queue_join(6, 1, now + 30000000));
    assert(space_navigation_queue_join(7, 1, now + 60000000));
    assert(g_space_navigation_queue.first->steps == 1);

    // Separate presses add steps; the other direction takes one back.
    assert(space_navigation_queue_join(8, 1, now + 160000000));
    assert(space_navigation_queue_join(9, -1, now + 170000000));
    assert(space_navigation_queue_join(10, 1, now + 300000000));
    assert(g_space_navigation_queue.first->steps == 2);

    // Another request closes the group: later requests keep their order.
    assert(!space_navigation_queue_join(11, 0, now + 310000000));
    assert(!space_navigation_queue_join(12, 1, now + 320000000));

    space_navigation_queue_claim(12);
    assert(!g_space_navigation_claim.active);

    space_navigation_queue_claim(5);
    assert(g_space_navigation_claim.active && g_space_navigation_claim.steps == 2);

    space_navigation_queue_claim(11);
    assert(!g_space_navigation_claim.active);

    space_navigation_queue_claim(12);
    assert(g_space_navigation_claim.active && g_space_navigation_claim.steps == 1);

    // Once claimed, the next request waits as a new group.
    assert(!space_navigation_queue_join(13, -1, now + 400000000));
    assert(space_navigation_queue_join(14, 1, now + 410000000));

    space_navigation_queue_claim(13);
    assert(g_space_navigation_claim.active && g_space_navigation_claim.steps == 0);

    test_interleaved_groups();
    test_allocation_failure();
    test_concurrent_groups();
    test_repeat_groups();

    puts("navigation queue: parsing, repeats, presses and ordering checks passed");

    return 0;
}
