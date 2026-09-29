// Exercise the production callsites through narrow OS mocks in manifest.m.
struct test_ipc_payload {
    uint32_t event;
    uint32_t count;
    uint32_t proxy_wid[512];
    uint32_t real_wid[512];
};

static int test_ipc_lookup_count;
static int test_ipc_send_count;
static int test_ipc_deallocate_count;
static int test_ipc_wait_count;
static bool test_ipc_lookup_fails;
static bool test_ipc_send_fails;
static bool test_ipc_bad_size;
static struct test_ipc_payload test_ipc_payloads[4];

static kern_return_t test_ipc_lookup(mach_port_t *port)
{
    ++test_ipc_lookup_count;
    if (test_ipc_lookup_fails) return KERN_FAILURE;
    *port = 123;
    return KERN_SUCCESS;
}

static void test_ipc_send(mach_port_t port, void *data, uint32_t size)
{
    if (port != 123 || size != sizeof(struct test_ipc_payload)) test_ipc_bad_size = true;
    if (test_ipc_send_count < array_count(test_ipc_payloads)) {
        memcpy(&test_ipc_payloads[test_ipc_send_count], data, sizeof(struct test_ipc_payload));
    }
    ++test_ipc_send_count;
    // The production helper returns void even when mach_msg fails. Simulate
    // non-delivery without altering its caller's ownership obligation.
    if (test_ipc_send_fails) return;
}

static kern_return_t test_ipc_deallocate(mach_port_t port)
{
    if (port == 123) ++test_ipc_deallocate_count;
    return KERN_SUCCESS;
}

static void test_ipc_wait(void)
{
    ++test_ipc_wait_count;
}

static void test_ipc_reset(void)
{
    test_ipc_lookup_count = 0;
    test_ipc_send_count = 0;
    test_ipc_deallocate_count = 0;
    test_ipc_wait_count = 0;
    test_ipc_lookup_fails = false;
    test_ipc_send_fails = false;
    test_ipc_bad_size = false;
    memset(test_ipc_payloads, 0, sizeof(test_ipc_payloads));
}

TEST_FUNC(jankyborders_resource_lifetime,
    mach_port_t saved_port = g_bs_port;
    g_bs_port = 1;
    struct window_animation animations[1025] = {0};
    for (int i = 0; i < array_count(animations); ++i) {
        animations[i].proxy.id = 10000 + i;
        animations[i].wid = 20000 + i;
    }

    test_ipc_reset();
    test_ipc_lookup_fails = true;
    window_manager_notify_jankyborders(animations, 1, 1325, false, true);
    TEST_CHECK(test_ipc_send_count, 0);
    TEST_CHECK(test_ipc_deallocate_count, 0);
    TEST_CHECK(test_ipc_wait_count, 0);

    test_ipc_reset();
    test_ipc_send_fails = true;
    window_manager_notify_jankyborders(animations, 1, 1325, false, true);
    TEST_CHECK(test_ipc_send_count, 1);
    TEST_CHECK(test_ipc_deallocate_count, 1);
    TEST_CHECK(test_ipc_wait_count, 1);

    test_ipc_reset();
    window_manager_notify_jankyborders(animations, 0, 1325, false, false);
    TEST_CHECK(test_ipc_send_count, 1);
    TEST_CHECK(test_ipc_payloads[0].count, 0);
    TEST_CHECK(test_ipc_deallocate_count, 1);

    test_ipc_reset();
    animations[0].skip = true;
    window_manager_notify_jankyborders(animations, 1, 1325, true, false);
    TEST_CHECK(test_ipc_send_count, 1);
    TEST_CHECK(test_ipc_payloads[0].count, 0);
    TEST_CHECK(test_ipc_deallocate_count, 1);
    animations[0].skip = false;

    test_ipc_reset();
    animations[1].skip = true;
    window_manager_notify_jankyborders(animations, 1025, 1326, true, true);
    TEST_CHECK(test_ipc_lookup_count, 1);
    TEST_CHECK(test_ipc_send_count, 2);
    TEST_CHECK(test_ipc_deallocate_count, 1);
    TEST_CHECK(test_ipc_wait_count, 1);
    TEST_CHECK(test_ipc_bad_size, false);
    TEST_CHECK(test_ipc_payloads[0].event, 1326);
    TEST_CHECK(test_ipc_payloads[0].count, 512);
    TEST_CHECK(test_ipc_payloads[1].count, 512);
    TEST_CHECK(test_ipc_payloads[0].proxy_wid[0], 10000);
    TEST_CHECK(test_ipc_payloads[0].proxy_wid[1], 10002);
    TEST_CHECK(test_ipc_payloads[1].real_wid[511], 21024);

    test_ipc_reset();
    animations[1].skip = false;
    window_manager_notify_jankyborders(animations, 1025, 1325, false, false);
    TEST_CHECK(test_ipc_send_count, 3);
    TEST_CHECK(test_ipc_payloads[0].count, 512);
    TEST_CHECK(test_ipc_payloads[1].count, 512);
    TEST_CHECK(test_ipc_payloads[2].count, 1);
    TEST_CHECK(test_ipc_payloads[2].real_wid[0], 21024);
    TEST_CHECK(test_ipc_deallocate_count, 1);

    test_ipc_reset();
    for (int i = 0; i < 20; ++i) {
        window_manager_notify_jankyborders(animations, 1, 1325, false, false);
    }
    TEST_CHECK(test_ipc_lookup_count, 20);
    TEST_CHECK(test_ipc_send_count, 20);
    TEST_CHECK(test_ipc_deallocate_count, 20);
    g_bs_port = saved_port;
)

static int test_mouse_create_count;
static int test_mouse_release_count;
static int test_mouse_invalidate_count;
static int test_mouse_add_count;
static int test_mouse_remove_count;
static int test_mouse_disable_count;
static bool test_mouse_create_fails;
static bool test_mouse_tap_disabled;
static bool test_mouse_source_fails;

static CFMachPortRef test_mouse_create(void)
{
    ++test_mouse_create_count;
    return test_mouse_create_fails ? NULL : (CFMachPortRef)(uintptr_t)0x100;
}

static bool test_mouse_enabled(void)
{
    return !test_mouse_tap_disabled;
}

static CFRunLoopSourceRef test_mouse_create_source(void)
{
    return test_mouse_source_fails ? NULL : (CFRunLoopSourceRef)(uintptr_t)0x200;
}

static void test_mouse_invalidate(const void *value)
{
    if (value == (void *)(uintptr_t)0x100) ++test_mouse_invalidate_count;
}

static void test_mouse_release(const void *value)
{
    if (value == (void *)(uintptr_t)0x100 || value == (void *)(uintptr_t)0x200) ++test_mouse_release_count;
}

static void test_mouse_add_source(void) { ++test_mouse_add_count; }
static void test_mouse_remove_source(void) { ++test_mouse_remove_count; }
static void test_mouse_enable(bool enabled) { if (!enabled) ++test_mouse_disable_count; }

TEST_FUNC(mouse_tap_resource_lifetime,
    struct mouse_state state = {0};
    test_mouse_create_count = test_mouse_release_count = test_mouse_invalidate_count = 0;
    test_mouse_add_count = test_mouse_remove_count = test_mouse_disable_count = 0;

    test_mouse_create_fails = true;
    TEST_CHECK(mouse_handler_begin(&state, 0), false);
    TEST_CHECK(state.handle == NULL, true);
    test_mouse_create_fails = false;

    test_mouse_tap_disabled = true;
    TEST_CHECK(mouse_handler_begin(&state, 0), false);
    TEST_CHECK(state.handle == NULL, true);
    TEST_CHECK(state.runloop_source == NULL, true);
    TEST_CHECK(test_mouse_invalidate_count, 1);
    TEST_CHECK(test_mouse_release_count, 1);
    mouse_handler_end(&state);
    TEST_CHECK(test_mouse_release_count, 1);
    test_mouse_tap_disabled = false;

    test_mouse_source_fails = true;
    TEST_CHECK(mouse_handler_begin(&state, 0), false);
    TEST_CHECK(state.handle == NULL, true);
    TEST_CHECK(state.runloop_source == NULL, true);
    TEST_CHECK(test_mouse_invalidate_count, 2);
    TEST_CHECK(test_mouse_release_count, 2);
    test_mouse_source_fails = false;

    TEST_CHECK(mouse_handler_begin(&state, 0), true);
    TEST_CHECK(mouse_handler_begin(&state, 0), true);
    TEST_CHECK(test_mouse_create_count, 4);
    TEST_CHECK(test_mouse_add_count, 1);
    mouse_handler_end(&state);
    TEST_CHECK(state.handle == NULL, true);
    TEST_CHECK(state.runloop_source == NULL, true);
    TEST_CHECK(test_mouse_remove_count, 1);
    TEST_CHECK(test_mouse_disable_count, 1);
    TEST_CHECK(test_mouse_invalidate_count, 3);
    TEST_CHECK(test_mouse_release_count, 4);
    mouse_handler_end(&state);
    TEST_CHECK(test_mouse_release_count, 4);
)
