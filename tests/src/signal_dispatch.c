static TEST_SIG(signal_socket_lifetime)
{
    char *test_name = "signal_socket_lifetime";
    bool result = true;
    int sockets[2];

    if (socketpair(AF_UNIX, SOCK_STREAM, 0, sockets) != 0) return false;

    char path[] = "/tmp/yabai-signal-test-XXXXXX";
    int output = mkstemp(path);
    if (output == -1) {
        close(sockets[0]);
        close(sockets[1]);
        return false;
    }

    char command[512];
    snprintf(command, sizeof(command),
             "printf '%%s' \"$YABAI_WINDOW_ID\" > '%s'; /bin/sleep 1", path);

    struct event_signal event = {
        .type = SIGNAL_SPACE_CHANGED,
        .arg_name = { "YABAI_WINDOW_ID" },
        .arg_value = { "42" }
    };
    struct signal subscriber = { .command = command };
    struct memory_pool saved_storage = g_signal_storage;
    struct signal *saved_signals = g_signal_event[event.type];

    g_signal_event[event.type] = NULL;
    buf_push(g_signal_event[event.type], subscriber);
    g_signal_storage = (struct memory_pool) {
        .memory = &event, .size = sizeof(event), .used = sizeof(event)
    };

    event_signal_flush();
    TEST_CHECK(g_signal_storage.used == 0, true);

    buf_free(g_signal_event[event.type]);
    g_signal_event[event.type] = saved_signals;
    g_signal_storage = saved_storage;

    // Wait for the action to start while the simulated queued client is open.
    char value[3] = {0};
    for (int i = 0; i < 200 && pread(output, value, 2, 0) != 2; ++i) {
        usleep(5000);
    }

    TEST_CHECK(strcmp(value, "42"), 0);
    close(sockets[0]);

    struct pollfd client = { .fd = sockets[1], .events = POLLIN };
    int ready = poll(&client, 1, 100);
    TEST_CHECK(ready, 1);
    if (ready > 0) {
        int received = read(sockets[1], value, sizeof(value));
        TEST_CHECK(received, 0);
    }

    close(sockets[1]);
    close(output);
    unlink(path);
    return result;
}
