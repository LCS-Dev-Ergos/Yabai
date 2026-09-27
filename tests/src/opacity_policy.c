struct opacity_test_server
{
    int listener;
    uint8_t opcodes[4];
};

static void *opacity_test_receive(void *context)
{
    struct opacity_test_server *server = context;

    for (int i = 0; i < 4; ++i) {
        struct pollfd ready = { .fd = server->listener, .events = POLLIN };
        if (poll(&ready, 1, 2000) != 1) break;

        int client = accept(server->listener, NULL, NULL);
        if (client == -1) break;

        struct timeval timeout = { .tv_sec = 2 };
        setsockopt(client, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
        int16_t length = 0;
        char bytes[SA_SOCKET_BUFF_LEN];
        if (recv(client, &length, sizeof(length), MSG_WAITALL) == sizeof(length)
            && length > 0 && length < sizeof(bytes)
            && recv(client, bytes, length, MSG_WAITALL) == length) {
            server->opcodes[i] = bytes[0];
            char response = i == 3 ? 'e' : 'k';
            send(client, &response, 1, 0);
        }

        close(client);
    }

    return NULL;
}

static TEST_SIG(opacity_policy)
{
    char *test_name = "opacity_policy";
    bool result = true;
    char directory[] = "/tmp/yabai-opacity-XXXXXX";
    if (!mkdtemp(directory)) return false;

    char saved_path[MAXLEN];
    memcpy(saved_path, g_sa_socket_file, sizeof(saved_path));
    snprintf(g_sa_socket_file, sizeof(g_sa_socket_file), "%s/socket", directory);
    struct sockaddr_un address = { .sun_family = AF_UNIX };
    snprintf(address.sun_path, sizeof(address.sun_path), "%s", g_sa_socket_file);
    struct opacity_test_server server = { .listener = socket(AF_UNIX, SOCK_STREAM, 0) };
    pthread_t thread;

    if (server.listener == -1 || bind(server.listener, (struct sockaddr *)&address, sizeof(address)) != 0
        || listen(server.listener, 4) != 0 || pthread_create(&thread, NULL, opacity_test_receive, &server) != 0) {
        result = false;
        goto cleanup;
    }

    struct window_manager wm = { .enable_window_opacity = true };
    struct window window = { .id = 42, .is_root = true,
                             .role = kAXWindowRole, .subrole = kAXStandardWindowSubrole };
    window_manager_set_window_opacity(&wm, &window, 1.0f);
    TEST_CHECK(window_manager_set_opacity(&wm, &window, 1.0f), true);
    struct sa_window_opacity target = { 42, 1.0f };
    TEST_CHECK(scripting_addition_set_opacity_batch(1, SA_OPACITY_START, .9f, .15f, 1.0f / 60, &target, 1), true);
    TEST_CHECK(scripting_addition_set_opacity_batch(1, SA_OPACITY_START, .9f, .15f, 1.0f / 60, &target, 1), false);
    pthread_join(thread, NULL);

    TEST_CHECK(server.opcodes[0], SA_OPCODE_WINDOW_OPACITY_FOCUS);
    TEST_CHECK(server.opcodes[1], SA_OPCODE_WINDOW_OPACITY);
    TEST_CHECK(server.opcodes[2], SA_OPCODE_WINDOW_OPACITY_BATCH);
    TEST_CHECK(server.opcodes[3], SA_OPCODE_WINDOW_OPACITY_BATCH);

cleanup:
    if (server.listener != -1) close(server.listener);
    unlink(g_sa_socket_file);
    rmdir(directory);
    memcpy(g_sa_socket_file, saved_path, sizeof(saved_path));
    return result;
}
