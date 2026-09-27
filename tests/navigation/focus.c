#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define kCPSUserGenerated 0x200

typedef struct
{
    uint32_t high;
    uint32_t low;
} ProcessSerialNumber;

static struct
{
    ProcessSerialNumber focused_window_psn;
    uint32_t focused_window_id;
} g_window_manager;

// Every call the deferred focus makes, in order.
static struct
{
    char kind;
    uint32_t psn;
    uint32_t window_id;
    uint8_t event;
} calls[16];

static int call_count;
static int scheduled_generation = -1;
static uint64_t scheduled_delay;

static void record(char kind, ProcessSerialNumber *psn, uint32_t window_id, uint8_t event)
{
    assert(call_count < 16);
    calls[call_count++] = (typeof(calls[0])) { kind, psn->low, window_id, event };
}

static bool psn_equals(ProcessSerialNumber *a, ProcessSerialNumber *b)
{
    return a->high == b->high && a->low == b->low;
}

static int SLPSPostEventRecordTo(ProcessSerialNumber *psn, uint8_t *bytes)
{
    uint32_t window_id;
    memcpy(&window_id, bytes + 0x3c, sizeof(window_id));

    assert(bytes[0x04] == 0xf8 && bytes[0x08] == 0x0d);
    record('e', psn, window_id, bytes[0x8a]);

    return 0;
}

static int _SLPSSetFrontProcessWithOptions(ProcessSerialNumber *psn, uint32_t window_id, uint32_t mode)
{
    assert(mode == kCPSUserGenerated);
    record('f', psn, window_id, 0);

    return 0;
}

static void window_manager_focus_window_without_raise(ProcessSerialNumber *psn, uint32_t window_id)
{
    record('w', psn, window_id, 0);
}

#include "../../src/space_navigation_focus.c"

static void window_manager_make_key_window(ProcessSerialNumber *psn, uint32_t window_id)
{
    record('k', psn, window_id, 0);
}

static void space_navigation_focus_schedule(int generation, uint64_t delay_ns)
{
    scheduled_generation = generation;
    scheduled_delay = delay_ns;
}

static void reset(void)
{
    g_window_manager.focused_window_psn = (ProcessSerialNumber) { 0, 5 };
    g_window_manager.focused_window_id = 50;
    call_count = 0;
    scheduled_generation = -1;
}

int main(void)
{
    ProcessSerialNumber same = { 0, 5 };
    ProcessSerialNumber other = { 0, 6 };

    // Another application: the ordinary focus, nothing deferred.
    reset();
    space_navigation_focus_window(&other, 60);
    assert(call_count == 1 && calls[0].kind == 'w' && calls[0].window_id == 60);
    assert(scheduled_generation == -1);

    // The same application: deactivate now, activate 10 ms later.
    reset();
    space_navigation_focus_window(&same, 51);
    assert(call_count == 1 && calls[0].kind == 'e' && calls[0].event == 0x02);
    assert(calls[0].psn == 5 && calls[0].window_id == 50);
    assert(scheduled_generation >= 0 && scheduled_delay == 10000000ULL);

    space_navigation_focus_resume(scheduled_generation);
    assert(call_count == 4);
    assert(calls[1].kind == 'e' && calls[1].event == 0x01 && calls[1].window_id == 51);
    assert(calls[2].kind == 'f' && calls[2].window_id == 51);
    assert(calls[3].kind == 'k' && calls[3].window_id == 51);

    // A newer navigation or command cancels the pending half.
    reset();
    space_navigation_focus_window(&same, 51);
    int stale = scheduled_generation;
    space_navigation_focus_cancel();
    space_navigation_focus_resume(stale);
    assert(call_count == 1);

    reset();
    space_navigation_focus_window(&same, 51);
    stale = scheduled_generation;
    space_navigation_focus_window(&same, 52);
    space_navigation_focus_resume(stale);
    assert(call_count == 2);
    space_navigation_focus_resume(scheduled_generation);
    assert(call_count == 5 && calls[4].window_id == 52);

    // A click on a third window wins; the target already focused does not.
    reset();
    space_navigation_focus_window(&same, 51);
    g_window_manager.focused_window_id = 70;
    space_navigation_focus_resume(scheduled_generation);
    assert(call_count == 1);

    reset();
    space_navigation_focus_window(&same, 51);
    g_window_manager.focused_window_id = 51;
    space_navigation_focus_resume(scheduled_generation);
    assert(call_count == 4);

    puts("navigation focus: deferred activation, cancellation and focus change checks passed");

    return 0;
}
