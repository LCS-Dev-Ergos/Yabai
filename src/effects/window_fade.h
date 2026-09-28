#ifndef EFFECTS_WINDOW_FADE_H
#define EFFECTS_WINDOW_FADE_H

#include "../core_types.h"

// Window fade (window_fade.c): the destination Desktop's windows start dimmed
// and fade in, drawn by the Dock payload (osax/window_fade*.c). It is the
// effect of a navigation step without a crossfade.
//
// Thread: event loop.
// State: space_navigation_effect_until, when the last fade started on each
// display may still run.
// Callers: step, which prepares the fade before Dock switches and starts or
// restores it afterwards. The fade asks the step which windows it dims and
// to which opacity they return, and sends them to Dock as opacity batches
// (sa_opacity.c).

struct space_navigation_effect
{
    struct sa_window_opacity windows[SA_OPACITY_BATCH_MAX];
    uint32_t count;
    float interval;
};

static bool space_navigation_prepare_effect(struct space_navigation_effect *effect, uint32_t display,
                                            uint32_t *ids, int count, uint32_t focus_id, float alpha, bool fade);
static bool space_navigation_start_effect(struct space_navigation_effect *effect, uint32_t display,
                                          uint32_t focus_id, float alpha, float duration, bool success);

#endif
