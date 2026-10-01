// The event loop reads a request and writes its reply within a bound: a client
// that stalls before sending, sends slowly or stops reading its reply costs at
// most that bound instead of holding every other event behind it. A request we
// cannot read, or a client we do not take, gets a reply that says why. TEST_CHECK
// evaluates its arguments again to report a failure, so every call with an
// effect runs once, before its check.

static double daemon_message_test_ms(uint64_t start)
{
    return (double) (read_os_timer() - start) / 1e6;
}

struct daemon_message_test_peer
{
    int sockfd;
    int interval_us;
    size_t received;
};

// Sends a request header for 100 bytes, then one byte per interval.
static void *daemon_message_test_trickle(void *context)
{
    struct daemon_message_test_peer *peer = context;
    int length = 100;
    if (write(peer->sockfd, &length, sizeof(length)) != sizeof(length)) return NULL;

    for (int i = 0; i < length; ++i) {
        usleep(peer->interval_us);
        if (write(peer->sockfd, "a", 1) != 1) break;
    }

    return NULL;
}

// Reads until the other end closes, counting the bytes.
static void *daemon_message_test_drain(void *context)
{
    struct daemon_message_test_peer *peer = context;
    char buffer[16384];

    for (;;) {
        ssize_t count = read(peer->sockfd, buffer, sizeof(buffer));
        if (count <= 0) break;

        peer->received += (size_t) count;
    }

    return NULL;
}

// The request as the client frames it: its length, then NUL-separated tokens
// and an empty last one.
static char daemon_message_test_request[] = "\x15\x00\x00\x00" "config\0debug_output\0";

static bool daemon_message_test_failure(const char *failure, const char *expected)
{
    bool matches = failure == expected || (failure && expected && strcmp(failure, expected) == 0);
    if (!matches) printf("                   failure '%s', expected '%s'\n", failure ? failure : "(none)", expected ? expected : "(none)");
    return matches;
}

TEST_FUNC(daemon_message_bounded_read,
{
    if (!g_temp_storage.memory) TEST_CHECK(ts_init(KILOBYTES(64)), true);
    const char *failure = NULL;
    bool matches;
    int sockets[2];

    // Connected, but nothing sent: the read gives up at its bound.
    TEST_CHECK(socketpair(AF_UNIX, SOCK_STREAM, 0, sockets), 0);
    uint64_t start = read_os_timer();
    char *message = daemon_message_read(sockets[1], 100, &failure);
    double elapsed = daemon_message_test_ms(start);

    TEST_CHECK(message == NULL, true);
    TEST_CHECK(elapsed >= 90.0 && elapsed < 1000.0, true);
    matches = daemon_message_test_failure(failure, FAILURE_MESSAGE "request timed out\n");
    TEST_CHECK(matches, true);
    close(sockets[0]);
    close(sockets[1]);

    // A request that arrives a byte at a time is bounded as a whole, not per
    // read: 100 bytes 20 ms apart would take two seconds.
    TEST_CHECK(socketpair(AF_UNIX, SOCK_STREAM, 0, sockets), 0);
    int on = 1;
    setsockopt(sockets[0], SOL_SOCKET, SO_NOSIGPIPE, &on, sizeof(on));

    struct daemon_message_test_peer peer = { .sockfd = sockets[0], .interval_us = 20000 };
    pthread_t thread;
    pthread_create(&thread, NULL, daemon_message_test_trickle, &peer);

    start = read_os_timer();
    message = daemon_message_read(sockets[1], 150, &failure);
    elapsed = daemon_message_test_ms(start);

    TEST_CHECK(message == NULL, true);
    TEST_CHECK(elapsed >= 140.0 && elapsed < 1000.0, true);
    matches = daemon_message_test_failure(failure, FAILURE_MESSAGE "request timed out\n");
    TEST_CHECK(matches, true);
    close(sockets[1]);
    pthread_join(thread, NULL);
    close(sockets[0]);

    // A complete request is read whole and terminated twice more, as the
    // tokenizer expects.
    TEST_CHECK(socketpair(AF_UNIX, SOCK_STREAM, 0, sockets), 0);
    ssize_t written = write(sockets[0], daemon_message_test_request, sizeof(daemon_message_test_request));
    TEST_CHECK(written == (ssize_t) sizeof(daemon_message_test_request), true);

    message = daemon_message_read(sockets[1], 100, &failure);
    TEST_CHECK(message != NULL, true);
    TEST_CHECK(failure == NULL, true);
    if (message) {
        TEST_CHECK(strcmp(message, "config"), 0);
        TEST_CHECK(strcmp(message + 7, "debug_output"), 0);
        TEST_CHECK(message[20] == '\0' && message[21] == '\0' && message[22] == '\0', true);
    }
    close(sockets[0]);
    close(sockets[1]);

    // A request whose sender hangs up early is refused at once.
    TEST_CHECK(socketpair(AF_UNIX, SOCK_STREAM, 0, sockets), 0);
    written = write(sockets[0], daemon_message_test_request, 8);
    TEST_CHECK(written == 8, true);
    close(sockets[0]);

    start = read_os_timer();
    message = daemon_message_read(sockets[1], 1000, &failure);
    elapsed = daemon_message_test_ms(start);

    TEST_CHECK(message == NULL, true);
    TEST_CHECK(elapsed < 500.0, true);
    matches = daemon_message_test_failure(failure, FAILURE_MESSAGE "request ended early\n");
    TEST_CHECK(matches, true);
    close(sockets[1]);

    // A length out of bounds is refused before anything else is read.
    struct { int length; const char *failure; } lengths[] = {
        { 0, FAILURE_MESSAGE "request is malformed\n" },
        { -5, FAILURE_MESSAGE "request is malformed\n" },
        { DAEMON_MESSAGE_MAX_LENGTH + 1, FAILURE_MESSAGE "request is longer than 64 KiB\n" },
    };
    for (int i = 0; i < array_count(lengths); ++i) {
        TEST_CHECK(socketpair(AF_UNIX, SOCK_STREAM, 0, sockets), 0);
        written = write(sockets[0], &lengths[i].length, sizeof(int));
        TEST_CHECK(written == (ssize_t) sizeof(int), true);

        message = daemon_message_read(sockets[1], 1000, &failure);
        TEST_CHECK(message == NULL, true);
        matches = daemon_message_test_failure(failure, lengths[i].failure);
        TEST_CHECK(matches, true);
        close(sockets[0]);
        close(sockets[1]);
    }

    ts_reset();
});

TEST_FUNC(daemon_message_bounded_reply,
{
    size_t size = 4 * 1024 * 1024;
    char *reply = malloc(size);
    if (!reply) return false;

    memset(reply, 'x', size);
    int sockets[2];

    // A client that never reads its reply costs the bound, not its lifetime.
    TEST_CHECK(socketpair(AF_UNIX, SOCK_STREAM, 0, sockets), 0);
    uint64_t start = read_os_timer();
    bool sent = daemon_message_reply(sockets[1], reply, size, 100);
    double elapsed = daemon_message_test_ms(start);

    TEST_CHECK(sent, false);
    TEST_CHECK(elapsed >= 90.0 && elapsed < 1000.0, true);
    close(sockets[0]);
    close(sockets[1]);

    // A client that has gone fails the reply without a signal.
    TEST_CHECK(socketpair(AF_UNIX, SOCK_STREAM, 0, sockets), 0);
    close(sockets[0]);
    sent = daemon_message_reply(sockets[1], reply, 64, 100);
    TEST_CHECK(sent, false);
    close(sockets[1]);

    // A reading client receives a reply far larger than the socket buffers.
    TEST_CHECK(socketpair(AF_UNIX, SOCK_STREAM, 0, sockets), 0);
    struct daemon_message_test_peer peer = { .sockfd = sockets[0] };
    pthread_t thread;
    pthread_create(&thread, NULL, daemon_message_test_drain, &peer);

    sent = daemon_message_reply(sockets[1], reply, size, 5000);
    close(sockets[1]);
    pthread_join(thread, NULL);

    TEST_CHECK(sent, true);
    TEST_CHECK(peer.received == size, true);
    close(sockets[0]);

    free(reply);
});

// The handler answers on the request's connection, closes it and counts it
// answered.
TEST_FUNC(daemon_message_round_trip,
{
    if (!g_temp_storage.memory) TEST_CHECK(ts_init(KILOBYTES(64)), true);
    bool saved_verbose = g_verbose;
    g_verbose = false;
    int sockets[2];

    TEST_CHECK(socketpair(AF_UNIX, SOCK_STREAM, 0, sockets), 0);
    ssize_t written = write(sockets[0], daemon_message_test_request, sizeof(daemon_message_test_request));
    TEST_CHECK(written == (ssize_t) sizeof(daemon_message_test_request), true);
    g_message_loop.pending = 1;
    EVENT_HANDLER_DAEMON_MESSAGE(NULL, sockets[1]);
    TEST_CHECK(g_message_loop.pending, 0);

    char reply[64] = {0};
    char rest[4];
    int count = (int) read(sockets[0], reply, sizeof(reply) - 1);
    int closed = (int) read(sockets[0], rest, sizeof(rest));

    TEST_CHECK(count, 4);
    TEST_CHECK(strcmp(reply, "off\n"), 0);
    TEST_CHECK(closed, 0);
    close(sockets[0]);

    // A request it cannot read is answered with the reason.
    TEST_CHECK(socketpair(AF_UNIX, SOCK_STREAM, 0, sockets), 0);
    int malformed = 0;
    written = write(sockets[0], &malformed, sizeof(malformed));
    TEST_CHECK(written == (ssize_t) sizeof(malformed), true);
    g_message_loop.pending = 1;
    EVENT_HANDLER_DAEMON_MESSAGE(NULL, sockets[1]);
    TEST_CHECK(g_message_loop.pending, 0);

    memset(reply, 0, sizeof(reply));
    count = (int) read(sockets[0], reply, sizeof(reply) - 1);
    TEST_CHECK(strcmp(reply, FAILURE_MESSAGE "request is malformed\n"), 0);
    close(sockets[0]);

    g_verbose = saved_verbose;
    ts_reset();
});

// Past the waiting connections we can hold, a client is told we are busy
// instead of being dropped by the kernel at the descriptor limit.
TEST_FUNC(daemon_message_busy,
{
    int sockets[2];
    int saved_pending = g_message_loop.pending;

    TEST_CHECK(socketpair(AF_UNIX, SOCK_STREAM, 0, sockets), 0);
    g_message_loop.pending = MESSAGE_LOOP_MAX_PENDING - 1;
    bool admitted = message_loop_admit(sockets[1]);
    TEST_CHECK(admitted, true);
    TEST_CHECK(g_message_loop.pending, MESSAGE_LOOP_MAX_PENDING);
    close(sockets[0]);
    close(sockets[1]);

    TEST_CHECK(socketpair(AF_UNIX, SOCK_STREAM, 0, sockets), 0);
    admitted = message_loop_admit(sockets[1]);
    TEST_CHECK(admitted, false);
    TEST_CHECK(g_message_loop.pending, MESSAGE_LOOP_MAX_PENDING);
    if (admitted) close(sockets[1]);

    char reply[64] = {0};
    char rest[4];
    int count = (int) read(sockets[0], reply, sizeof(reply) - 1);
    int closed = (int) read(sockets[0], rest, sizeof(rest));
    TEST_CHECK(count > 0, true);
    TEST_CHECK(strcmp(reply, FAILURE_MESSAGE "too many requests are waiting\n"), 0);
    TEST_CHECK(closed, 0);
    close(sockets[0]);

    g_message_loop.pending = saved_pending;
});
