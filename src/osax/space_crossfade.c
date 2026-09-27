// Fork: cross-fades one Desktop into another, as Dock animates its own Space
// transitions.
//
// Every Desktop has its own wallpaper window, so each one is an opaque layer.
// One transaction shows the destination above the Desktop it replaces, at
// alpha 0, and makes it current; each frame then raises its alpha, and once it
// is opaque the Desktops below are hidden. Blending two opaque layers never
// lets the wallpaper through, as fading the destination's windows in did.
//
// A navigation during a crossfade stacks its destination on top, so the blend
// on screen continues without a jump; going back to the Desktop underneath
// fades the top one out instead. All of this runs under window_fade_lock, and
// the fade worker writes the frames.

#define SPACE_CROSSFADE_DISPLAYS 8
#define SPACE_CROSSFADE_LAYERS   3

struct space_crossfade
{
    uint32_t display;
    CFStringRef uuid;

    // The Desktops shown on the display, bottom first; the top one animates.
    uint64_t spaces[SPACE_CROSSFADE_LAYERS];
    int count;

    float from;
    float to;
    float current;
    double started;
    double duration;
    double interval;
    double next_frame;
    bool display_paced;
    bool frame_ready;
};

static struct space_crossfade space_crossfades[SPACE_CROSSFADE_DISPLAYS];

static struct space_crossfade *space_crossfade_find(uint32_t display)
{
    for (int i = 0; i < SPACE_CROSSFADE_DISPLAYS; ++i) {
        if (space_crossfades[i].count && space_crossfades[i].display == display) return &space_crossfades[i];
    }

    return NULL;
}

static bool space_crossfade_active(uint32_t display)
{
    return space_crossfade_find(display) != NULL;
}

// The Desktop the crossfade ends on.
static uint64_t space_crossfade_target(struct space_crossfade *fade)
{
    return fade->spaces[fade->to == 0.0f ? fade->count - 2 : fade->count - 1];
}

static bool space_crossfade_commit(CFTypeRef transaction)
{
    bool committed = SLSTransactionCommit(transaction, 0) == 0;
    CFRelease(transaction);
    return committed;
}

static void space_crossfade_forget(struct space_crossfade *fade)
{
    CFRelease(fade->uuid);
    fade->count = 0;
    window_fade_display_stop(fade->display);
}

// Leaves only the target shown, every layer opaque at the ordinary level 0,
// and forgets the crossfade.
static void space_crossfade_settle(struct space_crossfade *fade, CFTypeRef transaction)
{
    uint64_t target = space_crossfade_target(fade);
    os_signpost_event_emit(window_fade_log(), OS_SIGNPOST_ID_EXCLUSIVE, "crossfade settle",
                           "display %u space %llu layers %d", fade->display, target, fade->count);

    for (int i = 0; i < fade->count; ++i) {
        if (fade->spaces[i] != target) SLSTransactionHideSpace(transaction, fade->spaces[i]);
        SLSTransactionSetSpaceAlpha(transaction, fade->spaces[i], 1.0f);
        SLSTransactionSetSpaceAbsoluteLevel(transaction, fade->spaces[i], 0);
    }

    space_crossfade_forget(fade);
}

static void space_crossfade_finish(struct space_crossfade *fade)
{
    CFTypeRef transaction = SLSTransactionCreate(SLSMainConnectionID());

    if (!transaction) {
        space_crossfade_forget(fade);
        return;
    }

    space_crossfade_settle(fade, transaction);
    space_crossfade_commit(transaction);
}

// Other Desktop operations first end every crossfade at its target.
static void space_crossfade_finish_all(void)
{
    pthread_mutex_lock(&window_fade_lock);

    for (int i = 0; i < SPACE_CROSSFADE_DISPLAYS; ++i) {
        if (space_crossfades[i].count) space_crossfade_finish(&space_crossfades[i]);
    }

    pthread_mutex_unlock(&window_fade_lock);
}

#include "space_crossfade_start.c"
#include "space_crossfade_frames.c"
