// A scripting-addition request must fit the payload's message size; one that
// does not is refused before it is built past its buffer or sent.

struct sa_request_listener
{
    int sockfd;
    int received;
};

// Reads one request as the payload does, length first, and hangs up.
static void *sa_request_serve(void *context)
{
    struct sa_request_listener *listener = context;

    int sockfd = accept(listener->sockfd, NULL, NULL);
    if (sockfd == -1) return NULL;

    int16_t length = 0;
    if (read(sockfd, &length, sizeof(length)) == sizeof(length)) {
        char message[SA_SOCKET_BUFF_LEN];
        int received = 0;

        while (received < length) {
            ssize_t count = read(sockfd, message + received, length - received);
            if (count <= 0) break;
            received += count;
        }

        listener->received = sizeof(length) + received;
    }

    close(sockfd);
    return NULL;
}

static TEST_SIG(sa_request_bounds)
{
    char *test_name = "sa_request_bounds";
    bool result = true;

    char path[64];
    snprintf(path, sizeof(path), "/tmp/yabai-sa-test-%d.socket", getpid());
    unlink(path);

    struct sockaddr_un address = { .sun_family = AF_UNIX };
    snprintf(address.sun_path, sizeof(address.sun_path), "%s", path);

    struct sa_request_listener listener = { .sockfd = socket(AF_UNIX, SOCK_STREAM, 0) };
    if (listener.sockfd == -1) return false;
    if (bind(listener.sockfd, (struct sockaddr *) &address, sizeof(address)) != 0 || listen(listener.sockfd, 4) != 0) {
        close(listener.sockfd);
        unlink(path);
        return false;
    }

    char saved_path[sizeof(g_sa_socket_file)];
    memcpy(saved_path, g_sa_socket_file, sizeof(saved_path));
    snprintf(g_sa_socket_file, sizeof(g_sa_socket_file), "%s", path);

    // Length, opcode, Space and count take 15 bytes: 1020 windows fill 4095
    // of the 4096, and the payload reads them all.
    static uint32_t windows[4000];
    pthread_t server;
    pthread_create(&server, NULL, sa_request_serve, &listener);
    TEST_CHECK(scripting_addition_move_window_list_to_space(1, windows, 1020), true);
    pthread_join(server, NULL);
    TEST_CHECK(listener.received, 4095);

    // One more window, or far more, is refused without connecting.
    TEST_CHECK(scripting_addition_move_window_list_to_space(1, windows, 1021), false);
    TEST_CHECK(scripting_addition_move_window_list_to_space(1, windows, 4000), false);

    struct pollfd pending = { .fd = listener.sockfd, .events = POLLIN };
    TEST_CHECK(poll(&pending, 1, 0), 0);

    memcpy(g_sa_socket_file, saved_path, sizeof(saved_path));
    close(listener.sockfd);
    unlink(path);
    return result;
}
