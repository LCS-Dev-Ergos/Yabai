#include "fixture.h"
#include "concurrency.c"
#include "crossfade.c"
#include "navigation.c"

static void test_elapsed_time(void)
{
    struct window_fade_context *fade = add_fade(1);
    assert(window_fade_alpha(fade, 9.0) == 0.2f);
    assert(window_fade_alpha(fade, 10.0) == 0.2f);
    assert(fabsf(window_fade_alpha(fade, 10.05) - 0.9f) < 0.00001f);
    assert(window_fade_alpha(fade, 11.0) == 1.0f);

    window_fade_tick(10.0);
    assert(alpha_calls == 0);

    window_fade_tick(10.05);
    assert(alpha_calls == 1 && window_fades);

    // A late frame goes directly to the endpoint, with no catch-up queue.
    window_fade_tick(11.0);
    assert(alpha_calls == 2 && alphas[1] == 1.0f && !window_fades);

    add_fade(2);
    add_fade(3);
    window_fade_tick(11.0);
    assert(alphas[2] == 1.0f && alphas[3] == 1.0f && !window_fades);
}

static void test_cancellation_and_errors(void)
{
    add_fade(1);
    window_fade_set(1, 0.4f, 0.0f);
    window_fade_tick(11.0);
    assert(!window_fades && alphas[1] == 0.4f);

    int calls = alpha_calls;
    window_fade_set(1, NAN, 0.1f);
    window_fade_set(1, 1.1f, 0.1f);
    window_fade_set(1, 0.5f, INFINITY);
    window_fade_set(1, 0.5f, -0.1f);
    window_fade_set(0, 0.5f, 0.1f);
    assert(alpha_calls == calls && !window_fades);

    add_fade(1);
    fail_set = true;
    window_fade_tick(10.05);
    assert(!window_fades);
    fail_set = false;

    add_fade(1);
    fail_get = true;
    window_fade_set(1, 0.8f, 0.1f);
    assert(!window_fades);
    fail_get = false;

    fail_thread = true;
    window_fade_set(1, 0.8f, 0.1f);
    assert(!window_fades && !window_fade_worker_started && alphas[1] == 0.8f);
    fail_thread = false;
}

static void test_frame_writes(void)
{
    // Every window due in a frame gets a write of its own; no transaction,
    // whose commit could not tell whether it applied.
    add_fade(1);
    add_fade(2);
    int singles = single_writes;
    int created = transactions_created;
    window_fade_tick(10.05);
    assert(single_writes == singles + 2 && transactions_created == created);
    assert(fabsf(alphas[1] - 0.9f) < 0.00001f && alphas[2] == alphas[1]);

    window_fade_tick(11.0);
    assert(single_writes == singles + 4 && alphas[1] == 1.0f && alphas[2] == 1.0f && !window_fades);
    assert(transactions_created == created);
}

int main(void)
{
    test_elapsed_time();
    test_cancellation_and_errors();
    test_frame_writes();
    test_navigation_ownership();
    test_navigation_cadence();
    test_crossfade();
    test_concurrent_requests();
    test_display_frame_without_lock();
    test_navigation_batch();

    puts("fade: elapsed time, easing, cancellation, errors, frame writes, crossfades and concurrency passed");
    return 0;
}
