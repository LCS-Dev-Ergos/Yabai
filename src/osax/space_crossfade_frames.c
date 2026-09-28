// Crossfade frames, written by the fade worker with the lock held; see
// space_crossfade.c.

// Marks the crossfades whose display delivered a frame since the worker last
// looked.
static void space_crossfade_take_frames(uint64_t frames)
{
    for (int i = 0; i < SPACE_CROSSFADE_DISPLAYS; ++i) {
        struct space_crossfade *fade = &space_crossfades[i];
        if (!fade->count || !(frames & window_fade_display_bit(fade->display))) continue;

        fade->display_paced = true;
        fade->frame_ready = true;
    }
}

// Lowers *deadline to the next frame or end of each running crossfade.
// Returns whether any runs.
static bool space_crossfade_deadline(double *deadline)
{
    bool active = false;

    for (int i = 0; i < SPACE_CROSSFADE_DISPLAYS; ++i) {
        struct space_crossfade *fade = &space_crossfades[i];
        if (!fade->count) continue;

        active = true;
        double end = fade->started + fade->duration;
        if (end < *deadline) *deadline = end;
        if (fade->next_frame < *deadline) *deadline = fade->next_frame;
    }

    return active;
}

static float space_crossfade_alpha(struct space_crossfade *fade, double now)
{
    double t = fade->duration > 0.0 ? (now - fade->started) / fade->duration : 1.0;
    if (t <= 0.0) return fade->from;
    if (t >= 1.0) return fade->to;

    double eased = t * t * (3.0 - 2.0 * t);
    return fade->from + (fade->to - fade->from) * (float) eased;
}

// One transaction per crossfade and frame. The last frame also settles.
static void space_crossfade_tick(double now)
{
    for (int i = 0; i < SPACE_CROSSFADE_DISPLAYS; ++i) {
        struct space_crossfade *fade = &space_crossfades[i];
        if (!fade->count) continue;

        double end = fade->started + fade->duration;
        if (!fade->frame_ready && now < fade->next_frame && now < end) continue;

        // Like a window fade, a display-paced frame waits half a frame for
        // a late callback before the worker writes it, then one interval
        // while the callbacks stay late.
        bool callback = fade->frame_ready;
        fade->frame_ready = false;
        fade->next_frame = now + fade->interval * (fade->display_paced && callback ? 1.5 : 1.0);
        fade->current = space_crossfade_alpha(fade, now);

        if (now >= end) {
            space_crossfade_finish(fade);
            continue;
        }

        CFTypeRef transaction = SLSTransactionCreate(SLSMainConnectionID());
        if (!transaction) continue;

        SLSTransactionSetSpaceAlpha(transaction, fade->spaces[fade->count - 1], fade->current);
        os_signpost_event_emit(window_fade_log(), OS_SIGNPOST_ID_EXCLUSIVE, "crossfade frame",
                               "display %u alpha %.3f paced %d", fade->display, fade->current, fade->display_paced);
        space_crossfade_commit(transaction);
    }
}
