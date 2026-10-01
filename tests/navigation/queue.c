#include <assert.h>
#include <math.h>
#include <limits.h>
#include <poll.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

static bool fail_allocation;
static uint64_t accept_time;
static double key_up_seconds = 1000.0;

enum
{
    kCGEventSourceStateHIDSystemState = 1,
    kCGEventKeyUp = 11
};

// When a key was last released, as the HID system reports it.
static double CGEventSourceSecondsSinceLastEventType(int state, int type)
{
    assert(state == kCGEventSourceStateHIDSystemState && type == kCGEventKeyUp);
    return key_up_seconds;
}

static uint64_t read_os_timer(void)
{
    return accept_time;
}

static void socket_close(int sockfd)
{
    shutdown(sockfd, SHUT_RDWR);
    close(sockfd);
}

static void *queue_allocate(size_t size)
{
    return fail_allocation ? NULL : malloc(size);
}

#define malloc queue_allocate
#include "../../src/navigation/admission.h"
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

    // A key released since the last request makes a press of one that came
    // as soon as a repeat, as load can make it; so does a release shortly
    // before that request, which a slow client delivered late. A release
    // before that leaves it a repeat.
    uint64_t at = now + 1000000000;
    assert(!space_navigation_queue_join(48, 1, at));
    space_navigation_queue_claim(48);

    key_up_seconds = 0.03;
    assert(!space_navigation_queue_join(49, 1, at + 60000000));
    space_navigation_queue_claim(49);
    assert(g_space_navigation_claim.active && !g_space_navigation_claim.repeat);

    key_up_seconds = 0.2;
    assert(!space_navigation_queue_join(50, 1, at + 120000000));
    space_navigation_queue_claim(50);
    assert(g_space_navigation_claim.active && !g_space_navigation_claim.repeat);

    key_up_seconds = 0.25;
    assert(!space_navigation_queue_join(51, 1, at + 150000000));
    space_navigation_queue_claim(51);
    assert(g_space_navigation_claim.active && g_space_navigation_claim.repeat);
    key_up_seconds = 1000.0;
}

// A claim tells how quickly its presses came: the shortest time between two
// of its requests; first and last timestamps preserve its ingress interval.
static void test_gaps(void)
{
    uint64_t now = 300000000000ULL;

    assert(!space_navigation_queue_join(60, 1, now));
    space_navigation_queue_claim(60);
    assert(g_space_navigation_claim.active && g_space_navigation_claim.gap > 1000000000ULL);

    assert(!space_navigation_queue_join(61, 1, now + 400000000));
    space_navigation_queue_claim(61);
    assert(g_space_navigation_claim.gap == UINT64_MAX);

    assert(!space_navigation_queue_join(62, -1, now + 900000000));
    assert(space_navigation_queue_join(63, -1, now + 1200000000));
    assert(space_navigation_queue_join(64, -1, now + 1320000000));
    space_navigation_queue_claim(62);
    assert(g_space_navigation_claim.steps == -3 && g_space_navigation_claim.gap == 120000000);

    // A non-navigation request carries no ingress metadata.
    assert(!space_navigation_queue_join(65, 0, now + 1400000000));
    space_navigation_queue_claim(65);
    assert(!g_space_navigation_claim.active && g_space_navigation_claim.gap == UINT64_MAX);
    assert(!g_space_navigation_claim.time && !g_space_navigation_claim.first_time);
    assert(!space_navigation_queue_join(66, 1, now + 1500000000));
    space_navigation_queue_claim(66);
    assert(g_space_navigation_claim.gap == UINT64_MAX);
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

// A client connection whose request is already sent. connection[0] is the
// daemon's end.
static void connect_request(int connection[2], const char **arguments, int count)
{
    char bytes[256];
    int length = request(bytes, arguments, count);

    assert(socketpair(AF_UNIX, SOCK_STREAM, 0, connection) == 0);
    assert(send(connection[1], bytes, length, 0) == length);
}

// The accept thread answers a press that joins the waiting request by closing
// its connection, and leaves any other request unread for the event loop.
static void test_accept(void)
{
    const char *next[] = { "space", "--navigate", "focus", "next", "crossfade", "0.2" };
    const char *query[] = { "query", "--spaces" };
    int first[2], joined[2], other[2];
    char byte;

    while (g_space_navigation_queue.first) space_navigation_queue_claim(g_space_navigation_queue.first->owner);

    accept_time = 900000000000ULL;
    connect_request(first, next, 6);
    assert(!space_navigation_accept(first[0]));

    accept_time += 200000000;
    connect_request(joined, next, 6);
    assert(space_navigation_accept(joined[0]));
    assert(recv(joined[1], &byte, 1, 0) == 0);

    accept_time += 200000000;
    connect_request(other, query, 2);
    assert(!space_navigation_accept(other[0]));

    assert(recv(first[0], &byte, 1, MSG_PEEK) == 1 && recv(other[0], &byte, 1, MSG_PEEK) == 1);

    space_navigation_queue_claim(first[0]);
    struct space_navigation_claim claim = space_navigation_queue_claimed();
    assert(claim.active && claim.steps == 2 && !claim.repeat);

    space_navigation_queue_claim(other[0]);
    assert(!space_navigation_queue_claimed().active);
    assert(!g_space_navigation_queue.first);

    close(joined[1]);
    for (int i = 0; i < 2; ++i) {
        close(first[i]);
        close(other[i]);
    }
}

static void test_numeric_groups(void)
{
    uint64_t at = 1000000000000ULL;
    assert(!space_navigation_queue_join(80, 1, at));
    assert(space_navigation_queue_join(81, 1, at + 200000000));
    assert(!space_navigation_queue_join(82, SPACE_NAVIGATION_ABSOLUTE, at + 210000000));
    assert(!space_navigation_queue_join(83, SPACE_NAVIGATION_ABSOLUTE, at + 220000000));
    assert(!space_navigation_queue_join(84, -1, at + 230000000));
    space_navigation_queue_claim(80);
    assert(g_space_navigation_claim.active && g_space_navigation_claim.steps == 2);
    assert(g_space_navigation_claim.first_time == at && g_space_navigation_claim.time == at + 200000000);
    assert(g_space_navigation_claim.gap == 200000000);
    space_navigation_queue_claim(82);
    assert(!g_space_navigation_claim.active && !g_space_navigation_claim.steps);
    assert(g_space_navigation_claim.time == at + 210000000 && g_space_navigation_claim.gap == UINT64_MAX);
    space_navigation_queue_claim(83);
    assert(!g_space_navigation_claim.active && g_space_navigation_claim.time == at + 220000000);
    space_navigation_queue_claim(84);
    assert(g_space_navigation_claim.active && g_space_navigation_claim.steps == -1 && !g_space_navigation_claim.repeat);
    assert(!g_space_navigation_queue.first);
}

int main(void)
{
    assert(DIRECTION("space", "--navigate", "focus", "next", "0.95", "0.1") == 1);
    assert(DIRECTION("space", "--navigate", "focus", "prev", "1", "0") == -1);
    assert(DIRECTION("space", "--navigate", "focus", "next", "crossfade", "0.2") == 1);
    assert(DIRECTION("space", "--navigate", "focus", "next", "crossfades", "0.2") == 0);
    assert(DIRECTION("space", "--navigate", "focus", "next", "veil", "0.2") == 1);
    assert(DIRECTION("space", "--navigate", "focus", "prev", "veil", "0") == -1);
    assert(DIRECTION("space", "--navigate", "focus", "3", "veil", "0.25") == SPACE_NAVIGATION_ABSOLUTE);
    assert(DIRECTION("space", "--navigate", "focus", "next", "veils", "0.2") == 0);
    assert(DIRECTION("space", "--navigate", "focus", "next", "veil", "1.5") == 0);
    assert(DIRECTION("space", "--navigate", "move", "next", "veil", "0.2") == 0);

    assert(DIRECTION("space", "--navigate", "move", "next", "0.95", "0.1") == 0);
    assert(DIRECTION("space", "--navigate", "focus", "3", "0.95", "0.1") == SPACE_NAVIGATION_ABSOLUTE);
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
    test_gaps();
    test_accept();
    test_numeric_groups();

    puts("navigation queue: parsing, repeats, presses, gaps, ordering and accept checks passed");

    return 0;
}
