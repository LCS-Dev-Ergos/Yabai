struct sa_handshake_reply_writer
{
    int sockfd;
    const char *reply;
    size_t length;
    useconds_t delay;
};

static void *sa_handshake_write_reply(void *context)
{
    struct sa_handshake_reply_writer *writer = context;
    for (size_t i = 0; i < writer->length; ++i) {
        if (write(writer->sockfd, writer->reply + i, 1) != 1) break;
        if (writer->delay) usleep(writer->delay);
    }
    close(writer->sockfd);
    return NULL;
}

TEST_FUNC(sa_handshake_reply,
{
    char version[SA_SOCKET_BUFF_LEN];
    uint32_t attrib = 0;

    char reply[64];
    size_t version_length = strlen(OSAX_VERSION);
    uint32_t bits = 0x5D;
    memcpy(reply, OSAX_VERSION, version_length + 1);
    memcpy(reply + version_length + 1, &bits, sizeof(bits));
    reply[version_length + 1 + sizeof(bits)] = '\n';
    size_t reply_length = version_length + 1 + sizeof(bits) + 1;

    TEST_CHECK(scripting_addition_parse_handshake(reply, reply_length, version, sizeof(version), &attrib), true);
    TEST_CHECK(strcmp(version, OSAX_VERSION), 0);
    TEST_CHECK(attrib, 0x5D);

    static char unterminated[BUFSIZ];
    memset(unterminated, 'a', sizeof(unterminated));
    TEST_CHECK(scripting_addition_parse_handshake(unterminated, sizeof(unterminated), version, sizeof(version), &attrib), false);
    TEST_CHECK(scripting_addition_parse_handshake(reply, version_length + 3, version, sizeof(version), &attrib), false);
    TEST_CHECK(scripting_addition_parse_handshake(reply, reply_length, version, 4, &attrib), false);

    // A same-user process may bind the pathname, but it cannot impersonate Dock.
    char path[64];
    snprintf(path, sizeof(path), "/tmp/yabai-sa-handshake-%d.socket", getpid());
    unlink(path);
    struct sockaddr_un address = { .sun_family = AF_UNIX };
    snprintf(address.sun_path, sizeof(address.sun_path), "%s", path);
    int listener = socket(AF_UNIX, SOCK_STREAM, 0);
    if (listener == -1) return false;
    if (bind(listener, (struct sockaddr *) &address, sizeof(address)) != 0 || listen(listener, 4) != 0) {
        close(listener);
        unlink(path);
        return false;
    }
    char saved_path[sizeof(g_sa_socket_file)];
    memcpy(saved_path, g_sa_socket_file, sizeof(saved_path));
    snprintf(g_sa_socket_file, sizeof(g_sa_socket_file), "%s", path);
    TEST_CHECK(scripting_addition_request_handshake(version, sizeof(version), &attrib), false);
    memcpy(g_sa_socket_file, saved_path, sizeof(saved_path));
    close(listener);
    unlink(path);

    // Test the bounded reader independently of the process-identity check.
    int fds[2];
    if (socketpair(AF_UNIX, SOCK_STREAM, 0, fds) != 0) return false;
    struct sa_handshake_reply_writer writer = { fds[0], reply, reply_length, 0 };
    pthread_t server;
    pthread_create(&server, NULL, sa_handshake_write_reply, &writer);
    bool shaken = scripting_addition_read_handshake(fds[1], version, sizeof(version), &attrib, 1);
    pthread_join(server, NULL);
    close(fds[1]);
    TEST_CHECK(shaken, true);
    TEST_CHECK(strcmp(version, OSAX_VERSION), 0);
    TEST_CHECK(attrib, 0x5D);

    // Bytes arriving steadily must not extend the overall deadline.
    if (socketpair(AF_UNIX, SOCK_STREAM, 0, fds) != 0) return false;
    writer = (struct sa_handshake_reply_writer) { fds[0], reply, 5, 400000 };
    pthread_create(&server, NULL, sa_handshake_write_reply, &writer);
    struct timespec start, end;
    clock_gettime(CLOCK_MONOTONIC, &start);
    shaken = scripting_addition_read_handshake(fds[1], version, sizeof(version), &attrib, 1);
    clock_gettime(CLOCK_MONOTONIC, &end);
    pthread_join(server, NULL);
    close(fds[1]);
    TEST_CHECK(shaken, false);
    long elapsed_ms = (end.tv_sec - start.tv_sec) * 1000 + (end.tv_nsec - start.tv_nsec) / 1000000;
    TEST_CHECK(elapsed_ms < 1700, true);
});
