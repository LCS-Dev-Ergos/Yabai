// Starts or redirects a crossfade, see space_crossfade.c.

static void space_crossfade_animate(struct space_crossfade *fade, float from, float to,
                                    double duration, double interval)
{
    double now = window_fade_now();

    fade->from = from;
    fade->current = from;
    fade->to = to;
    fade->started = now;
    fade->duration = duration;
    fade->interval = interval;
    fade->next_frame = now + interval;
    fade->display_paced = false;
    fade->frame_ready = false;
}

static struct space_crossfade *space_crossfade_claim(uint32_t display, CFStringRef uuid, uint64_t source)
{
    for (int i = 0; i < SPACE_CROSSFADE_DISPLAYS; ++i) {
        struct space_crossfade *fade = &space_crossfades[i];
        if (fade->count) continue;

        CFTypeRef settlement = SLSTransactionCreate(SLSMainConnectionID());
        if (!settlement) return NULL;

        fade->display = display;
        fade->uuid = CFRetain(uuid);
        fade->settlement = settlement;
        fade->spaces[0] = source;
        fade->wallpapers[0] = 0;
        fade->count = 1;
        return fade;
    }

    return NULL;
}

// Turns the top Desktop around, towards opaque or back out, at the same pace:
// the Desktop underneath or the top one becomes current again.
static bool space_crossfade_turn(struct space_crossfade *fade, uint64_t dest, float to, double duration)
{
    CFTypeRef transaction = SLSTransactionCreate(SLSMainConnectionID());
    if (!transaction) return false;

    SLSTransactionSetManagedDisplayCurrentSpace(transaction, fade->uuid, dest);
    space_crossfade_commit(transaction);

    os_signpost_event_emit(window_fade_log(), OS_SIGNPOST_ID_EXCLUSIVE, "crossfade turn",
                           "display %u to %llu alpha %.3f", fade->display, dest, fade->current);
    space_crossfade_animate(fade, fade->current, to, duration * fabsf(to - fade->current), fade->interval);
    return true;
}

// Shows `dest` at alpha 0 and the ordinary Desktop level, above the older
// layers. Lower the older layers instead of raising the destination above
// global windows (such as SketchyBar) that belong to no managed Desktop.
// They are shown again in the same transaction, in case changing the current
// Desktop hid them. Its wallpaper, if given, is hidden while `dest` is hidden.
static bool space_crossfade_push(struct space_crossfade *fade, uint64_t dest, uint32_t wallpaper)
{
    CFTypeRef transaction = SLSTransactionCreate(SLSMainConnectionID());
    if (!transaction) return false;

    if (wallpaper) SLSSetWindowAlpha(SLSMainConnectionID(), wallpaper, 0.0f);

    SLSTransactionSetSpaceAlpha(transaction, dest, 0.0f);
    SLSTransactionSetSpaceAbsoluteLevel(transaction, dest, 0);
    SLSTransactionShowSpace(transaction, dest);
    SLSTransactionSetManagedDisplayCurrentSpace(transaction, fade->uuid, dest);

    for (int i = 0; i < fade->count; ++i) {
        SLSTransactionSetSpaceAbsoluteLevel(transaction, fade->spaces[i], i - fade->count);
        SLSTransactionShowSpace(transaction, fade->spaces[i]);
    }

    space_crossfade_commit(transaction);

    fade->wallpapers[fade->count] = wallpaper;
    fade->spaces[fade->count++] = dest;
    return true;
}

// Crossfades the display from `source`, the current Desktop, to `dest`. When
// SkyLight cannot create a transaction, it returns false with the screen left
// as it was, and the caller switches Desktop without an effect.
static bool space_crossfade_start(uint32_t display, CFStringRef uuid, uint64_t source, uint64_t dest,
                                  float duration, float interval)
{
    if (!display || !uuid || !source || !dest || source == dest) return false;
    if (!(duration > 0.0f && duration <= 1.0f)) return false;
    if (!(interval >= 1.0f / 240.0f && interval <= 1.0f)) return false;

    // WindowServer queries stay outside the lock the worker needs.
    uint32_t wallpaper = space_crossfade_wallpaper(display, dest);

    pthread_mutex_lock(&window_fade_lock);

    bool success = window_fade_start_worker();
    struct space_crossfade *fade = space_crossfade_find(display);
    uint64_t top = fade ? fade->spaces[fade->count - 1] : 0;

    if (!success || (fade && space_crossfade_target(fade) == dest)) {
        // No worker to animate with, or already on its way there.
    } else if (fade && fade->count == 2 && fade->to == 1.0f && fade->spaces[0] == dest) {
        success = space_crossfade_turn(fade, dest, 0.0f, duration);
    } else if (fade && fade->to == 0.0f && top == dest) {
        success = space_crossfade_turn(fade, dest, 1.0f, duration);
    } else {
        bool stack = fade && fade->to == 1.0f && fade->count < SPACE_CROSSFADE_LAYERS;
        for (int i = 0; stack && i < fade->count; ++i) {
            if (fade->spaces[i] == dest) stack = false;
        }

        // Anything else first ends where it was heading, then starts afresh.
        if (fade && !stack) space_crossfade_finish(fade);
        if (!stack) fade = space_crossfade_claim(display, uuid, source);

        success = fade && space_crossfade_push(fade, dest, wallpaper);

        if (success) {
            os_signpost_event_emit(window_fade_log(), OS_SIGNPOST_ID_EXCLUSIVE, "crossfade start",
                                   "display %u from %llu to %llu layers %d duration %.3f",
                                   display, source, dest, fade->count, duration);
            space_crossfade_animate(fade, 0.0f, 1.0f, duration, interval);
            window_fade_display_start(display);
        } else if (fade && fade->count == 1) {
            CFRelease(fade->settlement);
            fade->settlement = NULL;
            CFRelease(fade->uuid);
            fade->count = 0;
        }
    }

    window_fade_wake_worker();
    pthread_mutex_unlock(&window_fade_lock);
    return success;
}
