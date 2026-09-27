#define CROSSFADE_FRAME (1.0 / 60.0)
#define CROSSFADE_EPSILON 0.0005

// Desktop 1 alone on screen and current; every Desktop opaque at level 0.
static void crossfade_reset(void)
{
    for (int i = 0; i < 16; ++i) {
        space_state[i].shown = false;
        space_state[i].alpha = 1.0f;
        space_state[i].level = 0;
    }

    space_state[1].shown = true;
    current_space = 1;
}

static bool crossfade_start(uint64_t source, uint64_t dest)
{
    return space_crossfade_start(1, CFSTR("display"), source, dest, 0.2f, (float) CROSSFADE_FRAME);
}

static void crossfade_expect(int index, enum space_op op, uint64_t sid, float value)
{
    assert(index < space_log_count);
    assert(space_log[index].op == op && space_log[index].sid == sid && space_log[index].value == value);
}

// Every Desktop but `shown` hidden, and all of them back at alpha 1, level 0.
static void crossfade_expect_settled(uint64_t shown)
{
    assert(!space_crossfade_find(1));

    for (uint64_t sid = 1; sid < 16; ++sid) {
        assert(space_state[sid].shown == (sid == shown));
        assert(space_state[sid].alpha == 1.0f && space_state[sid].level == 0);
    }

    assert(current_space == shown);
}

static void test_crossfade_lifecycle(void)
{
    crossfade_reset();
    int mark = space_log_count;
    int starts = display_starts;
    int stops = display_stops;

    // One transaction shows the destination transparent above the source,
    // makes it current and keeps the source shown.
    assert(crossfade_start(1, 2));
    assert(space_log_count == mark + 5 && display_starts == starts + 1);
    crossfade_expect(mark + 0, SPACE_ALPHA, 2, 0.0f);
    crossfade_expect(mark + 1, SPACE_LEVEL, 2, 1.0f);
    crossfade_expect(mark + 2, SPACE_SHOW, 2, 0.0f);
    crossfade_expect(mark + 3, SPACE_CURRENT, 2, 0.0f);
    crossfade_expect(mark + 4, SPACE_SHOW, 1, 0.0f);
    assert(space_state[1].shown && space_state[2].shown && current_space == 2);

    struct space_crossfade *fade = space_crossfade_find(1);
    assert(fade && fade->count == 2);

    double deadline = fade->started + 10.0;
    assert(space_crossfade_deadline(&deadline) && deadline == fade->next_frame);

    // The frames raise only the destination's alpha, monotonically.
    double started = fade->started;
    float last = 0.0f;

    for (int i = 1; i < 12; ++i) {
        int commits_before = commits;
        space_crossfade_tick(started + i * (CROSSFADE_FRAME + CROSSFADE_EPSILON));
        assert(commits == commits_before + 1);
        assert(space_state[2].alpha > last && space_state[2].alpha < 1.0f);
        assert(space_state[1].alpha == 1.0f && space_state[1].shown);
        last = space_state[2].alpha;
    }

    // A frame that is not due writes nothing.
    int commits_before = commits;
    space_crossfade_tick(started + 11 * (CROSSFADE_FRAME + CROSSFADE_EPSILON) + CROSSFADE_EPSILON);
    assert(commits == commits_before);

    // The last frame hides the source and restores alpha and level.
    space_crossfade_tick(started + fade->duration);
    crossfade_expect_settled(2);
    assert(display_stops == stops + 1);
}

static void test_crossfade_turns(void)
{
    // Back to the source midway: the destination fades out from where it is,
    // as fast as it came, and the source is current again.
    crossfade_reset();
    assert(crossfade_start(1, 2));
    struct space_crossfade *fade = space_crossfade_find(1);
    space_crossfade_tick(fade->started + 0.1 + CROSSFADE_EPSILON);
    float midway = space_state[2].alpha;
    assert(midway > 0.4f && midway < 0.6f);

    assert(crossfade_start(2, 1));
    assert(fade->to == 0.0f && fade->from == midway && current_space == 1);
    assert(fabs(fade->duration - 0.2 * midway) < 0.00001);

    space_crossfade_tick(fade->started + fade->duration / 2 + CROSSFADE_EPSILON);
    float back = space_state[2].alpha;
    assert(back > 0.0f && back < midway);

    // Forward again turns it around once more.
    assert(crossfade_start(1, 2));
    assert(fade->to == 1.0f && fade->from == back && current_space == 2);
    space_crossfade_tick(fade->started + fade->duration + CROSSFADE_EPSILON);
    crossfade_expect_settled(2);

    // A turn that runs to its end leaves the source alone on screen.
    crossfade_reset();
    assert(crossfade_start(1, 2));
    fade = space_crossfade_find(1);
    space_crossfade_tick(fade->started + 0.05 + CROSSFADE_EPSILON);
    assert(crossfade_start(2, 1));
    space_crossfade_tick(fade->started + fade->duration + CROSSFADE_EPSILON);
    crossfade_expect_settled(1);

    // Asking for the Desktop it is already heading to changes nothing.
    crossfade_reset();
    assert(crossfade_start(1, 2));
    int created = transactions_created;
    assert(crossfade_start(1, 2));
    assert(transactions_created == created);
    space_crossfade_finish_all();
    crossfade_expect_settled(2);
}

static void test_crossfade_stacking(void)
{
    // A new Desktop during a crossfade goes on top; the one below keeps its
    // alpha, so the blend on screen continues.
    crossfade_reset();
    assert(crossfade_start(1, 2));
    struct space_crossfade *fade = space_crossfade_find(1);
    space_crossfade_tick(fade->started + 0.1 + CROSSFADE_EPSILON);
    float middle = space_state[2].alpha;

    int mark = space_log_count;
    assert(crossfade_start(2, 3));
    assert(fade->count == 3 && current_space == 3);
    crossfade_expect(mark + 0, SPACE_ALPHA, 3, 0.0f);
    crossfade_expect(mark + 1, SPACE_LEVEL, 3, 2.0f);
    crossfade_expect(mark + 2, SPACE_SHOW, 3, 0.0f);
    crossfade_expect(mark + 3, SPACE_CURRENT, 3, 0.0f);
    assert(space_state[1].shown && space_state[2].shown && space_state[3].shown);

    space_crossfade_tick(fade->started + 0.1 + CROSSFADE_EPSILON);
    assert(space_state[2].alpha == middle && space_state[3].alpha > 0.0f);

    space_crossfade_tick(fade->started + fade->duration);
    crossfade_expect_settled(3);

    // A fourth Desktop first ends the full stack where it was heading, then
    // fades in over it.
    crossfade_reset();
    assert(crossfade_start(1, 2));
    assert(crossfade_start(2, 3));
    assert(crossfade_start(3, 4));
    fade = space_crossfade_find(1);
    assert(fade && fade->count == 2 && fade->spaces[0] == 3 && fade->spaces[1] == 4);
    assert(!space_state[1].shown && !space_state[2].shown && space_state[3].shown);
    assert(space_state[2].alpha == 1.0f && space_state[3].alpha == 1.0f && space_state[3].level == 0);
    assert(space_state[4].shown && space_state[4].alpha == 0.0f && space_state[4].level == 1);

    // So does a Desktop already in the stack that is not the one below.
    assert(crossfade_start(4, 5));
    assert(crossfade_start(5, 3));
    fade = space_crossfade_find(1);
    assert(fade && fade->count == 2 && fade->spaces[0] == 5 && fade->spaces[1] == 3);
    space_crossfade_finish_all();
    crossfade_expect_settled(3);
}

static void test_crossfade_failures(void)
{
    // Arguments that cannot describe a crossfade.
    crossfade_reset();
    int created = transactions_created;
    assert(!space_crossfade_start(0, CFSTR("display"), 1, 2, 0.2f, (float) CROSSFADE_FRAME));
    assert(!space_crossfade_start(1, NULL, 1, 2, 0.2f, (float) CROSSFADE_FRAME));
    assert(!space_crossfade_start(1, CFSTR("display"), 1, 1, 0.2f, (float) CROSSFADE_FRAME));
    assert(!space_crossfade_start(1, CFSTR("display"), 1, 2, 0.0f, (float) CROSSFADE_FRAME));
    assert(!space_crossfade_start(1, CFSTR("display"), 1, 2, NAN, (float) CROSSFADE_FRAME));
    assert(!space_crossfade_start(1, CFSTR("display"), 1, 2, 1.5f, (float) CROSSFADE_FRAME));
    assert(!space_crossfade_start(1, CFSTR("display"), 1, 2, 0.2f, 0.0f));
    assert(transactions_created == created && !space_crossfade_find(1));

    // Without a transaction, a start leaves the screen as it was.
    fail_transaction = true;
    assert(!crossfade_start(1, 2));
    fail_transaction = false;
    crossfade_expect_settled(1);

    // Without a transaction, frames are skipped; the end still forgets it.
    crossfade_reset();
    assert(crossfade_start(1, 2));
    struct space_crossfade *fade = space_crossfade_find(1);
    double started = fade->started;
    fail_transaction = true;
    space_crossfade_tick(started + CROSSFADE_FRAME + CROSSFADE_EPSILON);
    assert(space_crossfade_find(1));
    space_crossfade_tick(started + fade->duration);
    fail_transaction = false;
    assert(!space_crossfade_find(1));

    // No worker: no crossfade, and the caller switches without one.
    crossfade_reset();
    fake_worker = false;
    window_fade_worker_started = false;
    fail_thread = true;
    assert(!crossfade_start(1, 2));
    fail_thread = false;
    fake_worker = true;
    crossfade_expect_settled(1);
}

static void test_crossfade_pacing(void)
{
    // Display callbacks pace the frames; half a frame late, the worker writes.
    crossfade_reset();
    assert(crossfade_start(1, 2));
    struct space_crossfade *fade = space_crossfade_find(1);
    double started = fade->started;

    space_crossfade_take_frames(window_fade_display_bit(1));
    int commits_before = commits;
    space_crossfade_tick(started + 0.001);
    assert(commits == commits_before + 1 && fade->display_paced);

    space_crossfade_tick(started + 0.002);
    assert(commits == commits_before + 1);

    space_crossfade_tick(started + 0.001 + CROSSFADE_FRAME);
    assert(commits == commits_before + 1);
    space_crossfade_tick(started + 0.001 + 1.5 * CROSSFADE_FRAME + CROSSFADE_EPSILON);
    assert(commits == commits_before + 2);

    // Another display's callback does not advance it.
    space_crossfade_take_frames(window_fade_display_bit(2));
    space_crossfade_tick(started + 0.001 + 1.6 * CROSSFADE_FRAME);
    assert(commits == commits_before + 2);

    space_crossfade_finish_all();
    crossfade_expect_settled(2);
}

static void test_crossfade_restore(void)
{
    // Desktops left transparent or raised go back to rest in one transaction;
    // Desktops at rest and fullscreen ones are left alone.
    crossfade_reset();
    managed_space_count = 6;
    for (int i = 0; i < 6; ++i) {
        managed_spaces[i].sid = (uint64_t) i + 1;
        managed_spaces[i].type = i == 4 ? 4 : 0;
    }

    space_state[3].alpha = 0.0f;
    space_state[3].level = 1;
    space_state[4].level = 2;
    space_state[5].alpha = 0.0f;

    int created = transactions_created;
    int committed = commits;
    assert(space_crossfade_restore() == 2);
    assert(transactions_created == created + 1 && commits == committed + 1);
    assert(space_state[3].alpha == 1.0f && space_state[3].level == 0);
    assert(space_state[4].alpha == 1.0f && space_state[4].level == 0);
    assert(space_state[5].alpha == 0.0f);

    // Nothing to restore, no transaction.
    space_state[5].alpha = 1.0f;
    assert(space_crossfade_restore() == 0 && transactions_created == created + 1);

    // A running crossfade keeps its Desktops as they are.
    assert(crossfade_start(1, 2));
    assert(space_state[2].alpha == 0.0f && space_crossfade_restore() == 0);
    space_crossfade_finish_all();
    crossfade_expect_settled(2);

    // Nor is anything restored without the Desktop list.
    created = transactions_created;
    space_state[2].alpha = 0.5f;
    managed_space_count = -1;
    assert(space_crossfade_restore() == 0 && transactions_created == created);
    managed_space_count = 0;
    space_state[2].alpha = 1.0f;
}

static void test_crossfade(void)
{
    int calls = thread_calls;
    fake_worker = true;

    test_crossfade_lifecycle();
    test_crossfade_turns();
    test_crossfade_stacking();
    test_crossfade_failures();
    test_crossfade_pacing();
    test_crossfade_restore();

    // Later tests start the real worker, and count its creation alone.
    fake_worker = false;
    window_fade_worker_started = false;
    thread_calls = calls;
}
