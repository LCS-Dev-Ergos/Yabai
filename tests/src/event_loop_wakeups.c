// The event loop sleeps on a semaphore no other process can reach, and wakes
// once for each event it has to handle: a mouse move merged into one still
// queued adds no wake-up.

static int test_event_loop_take_wakeups(struct event_loop *loop)
{
    int wakeups = 0;
    while (dispatch_semaphore_wait(loop->semaphore, DISPATCH_TIME_NOW) == 0) ++wakeups;
    return wakeups;
}

TEST_FUNC(event_loop_wakeups,
{
    // The name the event loop used to open, created first as another process
    // could.
    sem_unlink("yabai_event_loop_semaphore");
    sem_t *named = sem_open("yabai_event_loop_semaphore", O_CREAT | O_EXCL, 0600, 0);
    TEST_CHECK(named != SEM_FAILED, true);

    struct event_loop loop = {0};
    bool initialized = event_loop_init(&loop);
    TEST_CHECK(initialized, true);
    if (!initialized) return false;

    CFStringRef moves[3];
    for (int i = 0; i < 3; ++i) {
        moves[i] = CFStringCreateWithFormat(NULL, NULL, CFSTR("move %d"), i);
        event_loop_post(&loop, MOUSE_MOVED, (void *) moves[i], 0);
    }
    event_loop_post(&loop, WINDOW_CREATED, NULL, 7);

    int wakeups = test_event_loop_take_wakeups(&loop);
    TEST_CHECK(wakeups, 2);

    if (named != SEM_FAILED) {
        int taken = sem_trywait(named);
        TEST_CHECK(taken, -1);
        sem_close(named);
        sem_unlink("yabai_event_loop_semaphore");
    }

    // The queue holds the last move and the window, in order.
    struct event event;
    bool popped = event_queue_pop(&loop.queue, &event);
    TEST_CHECK(popped && event.type == MOUSE_MOVED && event.context == moves[2], true);
    if (popped && event.context) CFRelease(event.context);

    popped = event_queue_pop(&loop.queue, &event);
    TEST_CHECK(popped && event.type == WINDOW_CREATED && event.param1 == 7, true);
    popped = event_queue_pop(&loop.queue, &event);
    TEST_CHECK(popped, false);

    // A move after the queue emptied needs its own wake-up.
    CFStringRef late = CFStringCreateWithCString(NULL, "late", kCFStringEncodingUTF8);
    event_loop_post(&loop, MOUSE_MOVED, (void *) late, 0);
    wakeups = test_event_loop_take_wakeups(&loop);
    TEST_CHECK(wakeups, 1);
    if (event_queue_pop(&loop.queue, &event) && event.context) CFRelease(event.context);

    free(loop.queue.events);
    pthread_mutex_destroy(&loop.queue.lock);
    dispatch_release(loop.semaphore);
});
