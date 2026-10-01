// The client shared by `yabai -m` and yabai-msg: the request it packs, how it
// reads a reply and where it prints it. A fake daemon answers on the other
// end of a socket pair.

struct client_test_daemon
{
    int sockfd;
    const char *reply;
    size_t length;
    bool read_request;
    size_t received;
};

// Reads the request unless told not to, sends the reply, then closes.
static void *client_test_daemon_run(void *context)
{
    struct client_test_daemon *daemon = context;
    char buffer[4096];

    if (daemon->read_request) {
        for (;;) {
            ssize_t count = read(daemon->sockfd, buffer, sizeof(buffer));
            if (count <= 0) break;
            daemon->received += (size_t) count;
        }
    }

    for (size_t sent = 0; sent < daemon->length;) {
        ssize_t count = send(daemon->sockfd, daemon->reply + sent, daemon->length - sent, MSG_NOSIGNAL);
        if (count <= 0) break;
        sent += (size_t) count;
    }

    close(daemon->sockfd);
    return NULL;
}

struct client_test_result
{
    int status;
    char *output;
    char *errors;
};

static struct client_test_result client_test_exchange(const char *request, size_t size, struct client_test_daemon daemon)
{
    struct client_test_result result = {0};
    size_t output_size = 0, errors_size = 0;
    FILE *output = open_memstream(&result.output, &output_size);
    FILE *errors = open_memstream(&result.errors, &errors_size);

    int sockets[2];
    socketpair(AF_UNIX, SOCK_STREAM, 0, sockets);
    daemon.sockfd = sockets[1];

    pthread_t thread;
    pthread_create(&thread, NULL, client_test_daemon_run, &daemon);
    result.status = client_exchange(sockets[0], request, size, output, errors);
    pthread_join(thread, NULL);
    close(sockets[0]);

    fclose(output);
    fclose(errors);
    return result;
}

static void client_test_free(struct client_test_result result)
{
    free(result.output);
    free(result.errors);
}

TEST_FUNC(client_request_and_reply,
{
    // The request as the daemon reads it.
    char *argv[] = { "-m", "config", "debug_output" };
    size_t size = 0;
    char *request = client_request(array_count(argv), argv, &size);
    char expected[] = "\x15\x00\x00\x00" "config\0debug_output\0";
    TEST_CHECK(size == sizeof(expected), true);
    TEST_CHECK(request && memcmp(request, expected, sizeof(expected)) == 0, true);

    // Output, and silence, succeed.
    struct client_test_result exchange = client_test_exchange(request, size, (struct client_test_daemon) {
        .reply = "off\n", .length = 4, .read_request = true
    });
    TEST_CHECK(exchange.status, EXIT_SUCCESS);
    TEST_CHECK(strcmp(exchange.output, "off\n"), 0);
    TEST_CHECK(strcmp(exchange.errors, ""), 0);
    client_test_free(exchange);

    exchange = client_test_exchange(request, size, (struct client_test_daemon) { .read_request = true });
    TEST_CHECK(exchange.status, EXIT_SUCCESS);
    TEST_CHECK(strcmp(exchange.output, ""), 0);
    client_test_free(exchange);

    // A failure on any line sends the whole reply to the errors, unmarked.
    const char *failures = "deprecation warning: x\n\x07invalid key-value pair 'a'\n\x07invalid regex pattern 'b'\n";
    exchange = client_test_exchange(request, size, (struct client_test_daemon) {
        .reply = failures, .length = strlen(failures), .read_request = true
    });
    TEST_CHECK(exchange.status, EXIT_FAILURE);
    TEST_CHECK(strcmp(exchange.output, ""), 0);
    TEST_CHECK(strcmp(exchange.errors, "deprecation warning: x\ninvalid key-value pair 'a'\ninvalid regex pattern 'b'\n"), 0);
    client_test_free(exchange);

    // A marker byte inside a line is output.
    const char *bell = "{\"title\":\"a\x07\"}\n";
    exchange = client_test_exchange(request, size, (struct client_test_daemon) {
        .reply = bell, .length = strlen(bell), .read_request = true
    });
    TEST_CHECK(exchange.status, EXIT_SUCCESS);
    TEST_CHECK(strcmp(exchange.output, bell), 0);
    client_test_free(exchange);
    free(request);

    // A reply far larger than the socket buffers arrives whole.
    size_t large = 1024 * 1024;
    char *reply = malloc(large);
    memset(reply, 'x', large);
    reply[large - 1] = '\n';
    char *small[] = { "-m", "query", "--windows" };
    request = client_request(array_count(small), small, &size);
    exchange = client_test_exchange(request, size, (struct client_test_daemon) {
        .reply = reply, .length = large, .read_request = true
    });
    TEST_CHECK(exchange.status, EXIT_SUCCESS);
    TEST_CHECK(exchange.output && strlen(exchange.output) == large && memcmp(exchange.output, reply, large) == 0, true);
    client_test_free(exchange);
    free(request);
    free(reply);

    // A daemon that refuses a long request without reading it: the send fails
    // without a SIGPIPE, and the refusal is still printed.
    char *long_argument = malloc(512 * 1024);
    memset(long_argument, 'a', 512 * 1024 - 1);
    long_argument[512 * 1024 - 1] = '\0';
    char *long_argv[] = { "-m", "rule", long_argument };
    request = client_request(array_count(long_argv), long_argv, &size);
    const char *refusal = "\x07request is longer than 64 KiB\n";
    exchange = client_test_exchange(request, size, (struct client_test_daemon) {
        .reply = refusal, .length = strlen(refusal), .read_request = false
    });
    TEST_CHECK(exchange.status, EXIT_FAILURE);
    TEST_CHECK(strcmp(exchange.errors, "request is longer than 64 KiB\n"), 0);
    client_test_free(exchange);

    // The same daemon saying nothing: the client cannot call that a success.
    exchange = client_test_exchange(request, size, (struct client_test_daemon) { .read_request = false });
    TEST_CHECK(exchange.status, EXIT_FAILURE);
    TEST_CHECK(strcmp(exchange.errors, "yabai-msg: failed to send data..\n"), 0);
    client_test_free(exchange);
    free(request);
    free(long_argument);
});
