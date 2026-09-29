// Drive the real registration and cleanup code without contacting another
// process's AX server. The mock records the number of messages per attempt.
static int ax_add_calls, ax_remove_calls, ax_timeout_calls;
static int ax_fail_at, ax_fail_at_second;
static AXError ax_failure, ax_create_failure;
static float ax_timeout;
static AXError ax_timeout_failure, ax_reset_failure;
static struct process *ax_retry_process;
static dispatch_block_t ax_retry_block;
static uint64_t ax_retry_delay;
static int ax_retry_dispatches, ax_retry_posts;

static void test_ax_retry_dispatch(uint64_t delay, dispatch_block_t block)
{
    assert(!ax_retry_block);
    ax_retry_delay = delay;
    ax_retry_block = Block_copy(block);
    ++ax_retry_dispatches;
}

static struct process *test_ax_retry_find(ProcessSerialNumber *psn)
{
    return ax_retry_process && psn_equals(psn, &ax_retry_process->psn) ? ax_retry_process : NULL;
}

static void test_ax_retry_post(struct process *process)
{
    assert(process == ax_retry_process);
    ++ax_retry_posts;
}

static void test_ax_retry_fire(void)
{
    assert(ax_retry_block);
    dispatch_block_t block = ax_retry_block;
    ax_retry_block = NULL;
    block();
    Block_release(block);
}

static AXError test_ax_set_messaging_timeout(AXUIElementRef element, float timeout)
{
    assert(element);
    ax_timeout = timeout;
    ++ax_timeout_calls;
    return timeout ? ax_timeout_failure : ax_reset_failure;
}

static AXError test_ax_observer_create(pid_t pid, AXObserverCallback callback, AXObserverRef *observer)
{
    assert(pid == getpid() && callback);
    if (ax_create_failure) return ax_create_failure;
    CFRunLoopSourceContext context = {0};
    *observer = (AXObserverRef) CFRunLoopSourceCreate(NULL, 0, &context);
    assert(*observer);
    return kAXErrorSuccess;
}

static AXError test_ax_observer_add_notification(AXObserverRef observer, AXUIElementRef element,
                                                  CFStringRef notification, void *context)
{
    assert(observer && element && notification && context);
    assert(ax_timeout == 0.25f);
    int call = ax_add_calls++;
    return call == ax_fail_at || call == ax_fail_at_second ? ax_failure : kAXErrorSuccess;
}

static AXError test_ax_observer_remove_notification(AXObserverRef observer, AXUIElementRef element,
                                                     CFStringRef notification)
{
    assert(observer && element && notification);
    assert(ax_timeout == 0.25f); // Cleanup may contact the failed app too.
    ++ax_remove_calls;
    return kAXErrorSuccess;
}

static CFRunLoopSourceRef test_ax_observer_get_run_loop_source(AXObserverRef observer)
{
    return (CFRunLoopSourceRef) observer;
}

static struct application *test_ax_application(void)
{
    struct application *application = calloc(1, sizeof(*application));
    application->ref = AXUIElementCreateApplication(getpid());
    application->pid = getpid();
    application->name = "AX mock";
    return application;
}

static void test_ax_reset(int fail_at, AXError failure, AXError create_failure)
{
    ax_add_calls = ax_remove_calls = ax_timeout_calls = 0;
    ax_timeout = 0;
    ax_fail_at = fail_at;
    ax_fail_at_second = -1;
    ax_failure = failure;
    ax_create_failure = create_failure;
    ax_timeout_failure = ax_reset_failure = kAXErrorSuccess;
}

TEST_FUNC(application_observation, {
    // One unresponsive registration costs one bounded AX message. Any earlier
    // successful registrations are removed before the application is freed.
    test_ax_reset(3, kAXErrorCannotComplete, kAXErrorSuccess);
    struct application *application = test_ax_application();
    TEST_CHECK(application_observe(application), false);
    TEST_CHECK(ax_add_calls, 4);
    TEST_CHECK(application->ax_retry, true);
    TEST_CHECK(application->notification, 7);
    TEST_CHECK(ax_timeout_calls, 1);
    TEST_CHECK(ax_timeout == 0.25f, true);
    application_unobserve(application);
    TEST_CHECK(ax_remove_calls, 3);
    TEST_CHECK(ax_timeout_calls, 3);
    TEST_CHECK((int)ax_timeout, 0);
    application_destroy(application);

    // A fresh attempt recovers and registers all notifications, then cleanup
    // removes every registration and its run-loop source.
    test_ax_reset(-1, kAXErrorSuccess, kAXErrorSuccess);
    application = test_ax_application();
    TEST_CHECK(application_observe(application), true);
    TEST_CHECK(ax_add_calls, 7);
    TEST_CHECK(application->ax_retry, false);
    application_unobserve(application);
    TEST_CHECK(ax_remove_calls, 7);
    application_destroy(application);

    test_ax_reset(-1, kAXErrorSuccess, kAXErrorCannotComplete);
    application = test_ax_application();
    TEST_CHECK(application_observe(application), false);
    TEST_CHECK(ax_add_calls, 0);
    TEST_CHECK(application->ax_retry, true);
    application_unobserve(application);
    application_destroy(application);

    test_ax_reset(-1, kAXErrorSuccess, kAXErrorCannotComplete);
    ax_reset_failure = kAXErrorFailure;
    application = test_ax_application();
    TEST_CHECK(application_observe(application), false);
    TEST_CHECK(application->ax_retry, true);
    application_unobserve(application);
    application_destroy(application);

    test_ax_reset(-1, kAXErrorSuccess, kAXErrorSuccess);
    ax_timeout_failure = kAXErrorCannotComplete;
    application = test_ax_application();
    TEST_CHECK(application_observe(application), false);
    TEST_CHECK(ax_add_calls, 0);
    TEST_CHECK(application->ax_retry, true);
    application_unobserve(application);
    application_destroy(application);

    test_ax_reset(-1, kAXErrorSuccess, kAXErrorSuccess);
    ax_reset_failure = kAXErrorCannotComplete;
    application = test_ax_application();
    TEST_CHECK(application_observe(application), false);
    TEST_CHECK(ax_add_calls, 7);
    TEST_CHECK(application->ax_retry, true);
    application_unobserve(application);
    TEST_CHECK(ax_remove_calls, 7);
    application_destroy(application);

    // Permanent notification refusal does not schedule an AX retry.
    test_ax_reset(0, kAXErrorNotificationUnsupported, kAXErrorSuccess);
    ax_fail_at_second = 3;
    application = test_ax_application();
    TEST_CHECK(application_observe(application), false);
    TEST_CHECK(ax_add_calls, 7);
    TEST_CHECK(application->ax_retry, false);
    application_unobserve(application);
    TEST_CHECK(ax_remove_calls, 5);
    application_destroy(application);

    TEST_CHECK(application_ax_retry_delay_ns(1) == 100000000ULL, true);
    TEST_CHECK(application_ax_retry_delay_ns(2) == 200000000ULL, true);
    TEST_CHECK(application_ax_retry_delay_ns(7) == 6400000000ULL, true);
    TEST_CHECK(application_ax_retry_delay_ns(20) == 6400000000ULL, true);

    // The scheduled block captures only identity, not the process pointer.
    // An exited process may be freed before it runs.
    struct process *process = calloc(1, sizeof(*process));
    process->psn = (ProcessSerialNumber){.highLongOfPSN = 1, .lowLongOfPSN = 2};
    process->pid = 40;
    ax_retry_process = process;
    ax_retry_dispatches = ax_retry_posts = 0;
    application_ax_schedule_retry(process);
    application_ax_schedule_retry(process);
    TEST_CHECK(ax_retry_dispatches, 1);
    TEST_CHECK(ax_retry_delay == 100000000ULL, true);
    TEST_CHECK(process->ax_retry_pending, true);
    ax_retry_process = NULL;
    free(process);
    test_ax_retry_fire();
    TEST_CHECK(ax_retry_posts, 0);

    // A reused PSN with another PID must not receive the old retry.
    process = calloc(1, sizeof(*process));
    process->psn = (ProcessSerialNumber){.highLongOfPSN = 3, .lowLongOfPSN = 4};
    process->pid = 41;
    ax_retry_process = process;
    application_ax_schedule_retry(process);
    struct process replacement = *process;
    replacement.pid = 42;
    replacement.ax_retry_pending = false;
    replacement.ax_retry_count = 0;
    ax_retry_process = &replacement;
    test_ax_retry_fire();
    TEST_CHECK(ax_retry_posts, 0);
    TEST_CHECK(replacement.ax_retry_pending, false);
    free(process);

    // A process that becomes responsive keeps its identity and retries once
    // per scheduled block; successful observation resets the backoff.
    ax_retry_process = &replacement;
    application_ax_schedule_retry(&replacement);
    TEST_CHECK(ax_retry_delay == 100000000ULL, true);
    replacement.terminated = true;
    test_ax_retry_fire();
    TEST_CHECK(ax_retry_posts, 0);
    replacement.terminated = false;
    replacement.ax_retry_pending = false;
    application_ax_schedule_retry(&replacement);
    TEST_CHECK(ax_retry_delay == 200000000ULL, true);
    test_ax_retry_fire();
    TEST_CHECK(ax_retry_posts, 1);
    TEST_CHECK(replacement.ax_retry_pending, false);
    test_ax_reset(-1, kAXErrorSuccess, kAXErrorSuccess);
    application = test_ax_application();
    TEST_CHECK(application_observe(application), true);
    application_ax_retry_succeeded(&replacement);
    TEST_CHECK(replacement.ax_retry_count, 0);
    application_unobserve(application);
    application_destroy(application);
    ax_retry_process = NULL;
})
