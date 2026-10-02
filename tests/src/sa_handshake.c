// The handshake reply is the payload's version, a NUL and its attribute bits.
// Whatever listens on the payload socket writes it, and `--load-sa` reads it
// as root, so it is read within the bytes that arrived.

struct sa_handshake_listener
{
    int sockfd;
    const char *reply;
    size_t length;
    useconds_t interval;        // Between bytes of the reply; 0 sends it at once.
};

// Takes the handshake request, answers with the listener's reply and hangs up.
static void *sa_handshake_serve(void *context)
{
    struct sa_handshake_listener *listener = context;

    int sockfd = accept(listener->sockfd, NULL, NULL);
    if (sockfd == -1) return NULL;

    char request[3];
    if (read(sockfd, request, sizeof(request)) == sizeof(request)) {
        if (!listener->interval) {
            send(sockfd, listener->reply, listener->length, MSG_NOSIGNAL);
        } else {
            for (size_t i = 0; i < listener->length; ++i) {
                usleep(listener->interval);
                if (send(sockfd, listener->reply + i, 1, MSG_NOSIGNAL) != 1) break;
            }
        }
    }

    close(sockfd);
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

    // No NUL, too few attribute bytes, or a version longer than its buffer.
    static char unterminated[BUFSIZ];
    memset(unterminated, 'a', sizeof(unterminated));
    TEST_CHECK(scripting_addition_parse_handshake(unterminated, sizeof(unterminated), version, sizeof(version), &attrib), false);
    TEST_CHECK(scripting_addition_parse_handshake(reply, version_length + 3, version, sizeof(version), &attrib), false);
    TEST_CHECK(scripting_addition_parse_handshake(reply, reply_length, version, 4, &attrib), false);

    // Through the socket: a reply of 1023 bytes without a NUL is refused.
    char path[64];
    snprintf(path, sizeof(path), "/tmp/yabai-sa-handshake-%d.socket", getpid());
    unlink(path);

    struct sockaddr_un address = { .sun_family = AF_UNIX };
    snprintf(address.sun_path, sizeof(address.sun_path), "%s", path);

    struct sa_handshake_listener listener = { .sockfd = socket(AF_UNIX, SOCK_STREAM, 0), .reply = unterminated, .length = 1023 };
    if (listener.sockfd == -1) return false;
    if (bind(listener.sockfd, (struct sockaddr *) &address, sizeof(address)) != 0 || listen(listener.sockfd, 4) != 0) {
        close(listener.sockfd);
        unlink(path);
        return false;
    }

    char saved_path[sizeof(g_sa_socket_file)];
    memcpy(saved_path, g_sa_socket_file, sizeof(saved_path));
    snprintf(g_sa_socket_file, sizeof(g_sa_socket_file), "%s", path);

    pthread_t server;
    pthread_create(&server, NULL, sa_handshake_serve, &listener);
    bool shaken = scripting_addition_request_handshake(version, sizeof(version), &attrib, SA_HANDSHAKE_TIMEOUT_NS);
    pthread_join(server, NULL);
    TEST_CHECK(shaken, false);

    // A reply that trickles in, a byte every 100 ms, outlasts a timeout of
    // 300 ms for the whole reply, though every read gets its byte in time.
    listener.reply = reply;
    listener.length = reply_length;
    listener.interval = 100000;
    uint64_t began = read_os_timer();
    pthread_create(&server, NULL, sa_handshake_serve, &listener);
    shaken = scripting_addition_request_handshake(version, sizeof(version), &attrib, 300000000ULL);
    double waited = (read_os_timer() - began) / 1e6;
    pthread_join(server, NULL);
    TEST_CHECK(shaken, false);
    TEST_CHECK(waited < 600.0, true);
    listener.interval = 0;

    // The payload's own reply comes through whole.
    listener.reply = reply;
    listener.length = reply_length;
    attrib = 0;
    pthread_create(&server, NULL, sa_handshake_serve, &listener);
    shaken = scripting_addition_request_handshake(version, sizeof(version), &attrib, SA_HANDSHAKE_TIMEOUT_NS);
    pthread_join(server, NULL);
    TEST_CHECK(shaken, true);
    TEST_CHECK(strcmp(version, OSAX_VERSION), 0);
    TEST_CHECK(attrib, 0x5D);

    memcpy(g_sa_socket_file, saved_path, sizeof(saved_path));
    close(listener.sockfd);
    unlink(path);
});
