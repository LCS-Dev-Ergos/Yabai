#include <CoreFoundation/CoreFoundation.h>
#include <CoreGraphics/CoreGraphics.h>
#include <assert.h>
#include <errno.h>
#include <math.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "../../src/osax/common.h"

#ifndef FADE_DISPLAY_LINK
static int display_starts, display_stops;

static void window_fade_display_start(uint32_t display)
{
    ++display_starts;
}

static void window_fade_display_stop(uint32_t display)
{
    ++display_stops;
}
#endif

static float alphas[64];
static int alpha_calls;
static int alpha_reads;
static int thread_calls;
static int single_writes;
static int commits;
static useconds_t alpha_delay;
static bool fail_get, fail_set, fail_thread, fail_alloc;

// Lets a test start effects without a worker thread, and tick them itself.
static bool fake_worker;

static int SLSMainConnectionID(void)
{
    return 0;
}

static int SLSGetWindowAlpha(int cid, uint32_t wid, float *alpha)
{
    assert(wid < 64);
    ++alpha_reads;
    *alpha = alphas[wid];
    return fail_get;
}

static int SLSSetWindowAlpha(int cid, uint32_t wid, float alpha)
{
    assert(wid < 64);
    assert(isfinite(alpha) && alpha >= 0.0f && alpha <= 1.0f);

    ++alpha_calls;
    ++single_writes;
    if (alpha_delay) usleep(alpha_delay);
    if (!fail_set) alphas[wid] = alpha;
    return fail_set;
}

// Desktop operations queued in the open transaction, in order. A commit
// applies them to a model of WindowServer's Desktops and appends them to the
// log of committed operations.
enum space_op { SPACE_ALPHA, SPACE_LEVEL, SPACE_SHOW, SPACE_HIDE, SPACE_CURRENT };

struct space_record
{
    enum space_op op;
    uint64_t sid;
    float value;
};

static struct space_record space_queued[64];
static int space_queued_count;
static struct space_record space_log[256];
static int space_log_count;
static int transactions_created;
static bool fail_transaction;

static struct
{
    bool shown;
    float alpha;
    int level;
} space_state[16];

static uint64_t current_space;

static CFTypeRef SLSTransactionCreate(int cid)
{
    assert(space_queued_count == 0);
    ++transactions_created;
    return fail_transaction ? NULL : CFRetain(kCFNull);
}

static int space_queue(CFTypeRef transaction, enum space_op op, uint64_t sid, float value)
{
    assert(transaction && sid && sid < 16 && space_queued_count < 64);
    space_queued[space_queued_count++] = (struct space_record) { op, sid, value };
    return 0;
}

static int SLSTransactionSetSpaceAlpha(CFTypeRef transaction, uint64_t sid, float alpha)
{
    assert(isfinite(alpha) && alpha >= 0.0f && alpha <= 1.0f);
    return space_queue(transaction, SPACE_ALPHA, sid, alpha);
}

static int SLSTransactionSetSpaceAbsoluteLevel(CFTypeRef transaction, uint64_t sid, int level)
{
    return space_queue(transaction, SPACE_LEVEL, sid, (float) level);
}

static int SLSTransactionShowSpace(CFTypeRef transaction, uint64_t sid)
{
    return space_queue(transaction, SPACE_SHOW, sid, 0.0f);
}

static int SLSTransactionHideSpace(CFTypeRef transaction, uint64_t sid)
{
    return space_queue(transaction, SPACE_HIDE, sid, 0.0f);
}

static int SLSTransactionSetManagedDisplayCurrentSpace(CFTypeRef transaction, CFStringRef display, uint64_t sid)
{
    assert(display);
    return space_queue(transaction, SPACE_CURRENT, sid, 0.0f);
}

static void space_apply(struct space_record *record)
{
    switch (record->op) {
    case SPACE_ALPHA: space_state[record->sid].alpha = record->value; break;
    case SPACE_LEVEL: space_state[record->sid].level = (int) record->value; break;
    case SPACE_SHOW: space_state[record->sid].shown = true; break;
    case SPACE_HIDE: space_state[record->sid].shown = false; break;
    case SPACE_CURRENT: current_space = record->sid; break;
    }

    assert(space_log_count < 256);
    space_log[space_log_count++] = *record;
}

// Like SkyLight's, the commit returns no status: its value is never 0.
static int SLSTransactionCommit(CFTypeRef transaction, int synchronous)
{
    assert(transaction);
    ++commits;
    if (alpha_delay) usleep(alpha_delay);

    for (int i = 0; i < space_queued_count; ++i) {
        space_apply(&space_queued[i]);
    }

    space_queued_count = 0;
    return 0x48;
}

static float SLSSpaceGetAlpha(int cid, uint64_t sid)
{
    assert(sid && sid < 16);
    return space_state[sid].alpha;
}

static int SLSSpaceGetAbsoluteLevel(int cid, uint64_t sid)
{
    assert(sid && sid < 16);
    return space_state[sid].level;
}

// The Desktops SLSCopyManagedDisplaySpaces reports, all on one display, with
// their types. It reports nothing while managed_space_count is -1.
static struct
{
    uint64_t sid;
    int type;
} managed_spaces[16];

static int managed_space_count;

static CFDictionaryRef dictionary_of(CFStringRef key0, CFTypeRef value0, CFStringRef key1, CFTypeRef value1)
{
    const void *keys[] = { key0, key1 };
    const void *values[] = { value0, value1 };
    return CFDictionaryCreate(NULL, keys, values, key1 ? 2 : 1,
                              &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);
}

static CFArrayRef SLSCopyManagedDisplaySpaces(int cid)
{
    if (managed_space_count < 0) return NULL;

    CFMutableArrayRef spaces = CFArrayCreateMutable(NULL, 0, &kCFTypeArrayCallBacks);

    for (int i = 0; i < managed_space_count; ++i) {
        CFNumberRef sid = CFNumberCreate(NULL, kCFNumberSInt64Type, &managed_spaces[i].sid);
        CFNumberRef type = CFNumberCreate(NULL, kCFNumberIntType, &managed_spaces[i].type);
        CFDictionaryRef space = dictionary_of(CFSTR("id64"), sid, CFSTR("type"), type);

        CFArrayAppendValue(spaces, space);
        CFRelease(space);
        CFRelease(type);
        CFRelease(sid);
    }

    CFDictionaryRef display = dictionary_of(CFSTR("Spaces"), spaces, NULL, NULL);
    CFArrayRef displays = CFArrayCreate(NULL, (const void **) &display, 1, &kCFTypeArrayCallBacks);

    CFRelease(display);
    CFRelease(spaces);
    return displays;
}

// Windows on the model's Desktops, for SLSCopyWindowsWithOptionsAndTags,
// SLSGetWindowLevel and SLSGetWindowBounds, on a 1000 x 500 display.
static struct
{
    uint64_t sid;
    uint32_t wid;
    int level;
    CGRect frame;
} model_windows[16];

static int model_window_count;

static int model_window(uint32_t wid)
{
    for (int i = 0; i < model_window_count; ++i) {
        if (model_windows[i].wid == wid) return i;
    }

    return -1;
}

static CFArrayRef SLSCopyWindowsWithOptionsAndTags(int cid, uint32_t owner, CFArrayRef spaces, uint32_t options,
                                                   uint64_t *set_tags, uint64_t *clear_tags)
{
    assert(spaces && CFArrayGetCount(spaces) == 1);

    uint64_t sid = 0;
    CFNumberGetValue(CFArrayGetValueAtIndex(spaces, 0), kCFNumberSInt64Type, &sid);

    CFMutableArrayRef windows = CFArrayCreateMutable(NULL, 0, &kCFTypeArrayCallBacks);

    for (int i = 0; i < model_window_count; ++i) {
        if (model_windows[i].sid != sid) continue;

        CFNumberRef wid = CFNumberCreate(NULL, kCFNumberSInt32Type, &model_windows[i].wid);
        CFArrayAppendValue(windows, wid);
        CFRelease(wid);
    }

    return windows;
}

static int SLSGetWindowLevel(int cid, uint32_t wid, int *level)
{
    int i = model_window(wid);
    if (i < 0) return kCGErrorIllegalArgument;

    *level = model_windows[i].level;
    return kCGErrorSuccess;
}

static int SLSGetWindowBounds(int cid, uint32_t wid, CGRect *frame)
{
    int i = model_window(wid);
    if (i < 0) return kCGErrorIllegalArgument;

    *frame = model_windows[i].frame;
    return kCGErrorSuccess;
}

static CGRect model_display_bounds(uint32_t display)
{
    return CGRectMake(0, 0, 1000, 500);
}

static int model_window_level_for_key(CGWindowLevelKey key)
{
    return key == kCGDesktopWindowLevelKey ? -2147483623 : 0;
}

static int create_worker(pthread_t *thread, const pthread_attr_t *attributes,
                         void *(*entry)(void *), void *context)
{
    ++thread_calls;
    if (fake_worker) return 0;
    return fail_thread ? EAGAIN : pthread_create(thread, attributes, entry, context);
}

static void *allocate_fade(size_t count, size_t size)
{
    return fail_alloc ? NULL : calloc(count, size);
}

#define pthread_create create_worker
#define calloc allocate_fade
#define CGDisplayBounds model_display_bounds
#define CGWindowLevelForKey model_window_level_for_key
#include "../../src/osax/window_fade.c"
#ifdef FADE_DISPLAY_LINK
#include "../../src/osax/window_fade_display.m"
#endif
#include "../../src/osax/window_fade_navigation.c"
#include "../../src/osax/space_crossfade.c"
#undef pthread_create
#undef calloc
#undef CGDisplayBounds
#undef CGWindowLevelForKey

static struct window_fade_context *add_fade(uint32_t wid)
{
    struct window_fade_context *fade = calloc(1, sizeof(*fade));
    assert(fade);

    fade->wid = wid;
    fade->from = fade->current = 0.2f;
    fade->to = 1.0f;
    fade->started = 10.0;
    fade->duration = 0.1;
    fade->next = window_fades;
    window_fades = fade;
    alphas[wid] = fade->from;
    return fade;
}

static void wait_idle(void)
{
    double deadline = window_fade_now() + 5.0;

    for (;;) {
        pthread_mutex_lock(&window_fade_lock);
        bool idle = window_fades == NULL;
        pthread_mutex_unlock(&window_fade_lock);

        if (idle) return;
        assert(window_fade_now() < deadline);
        usleep(1000);
    }
}
