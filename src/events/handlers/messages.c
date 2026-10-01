// Daemon messages and queued navigation completion events.
// Runs on the event-loop thread.

//
// NOTE: A request is an int length followed by that many bytes of NUL-separated
// arguments, ending with an empty one. The length is bounded and the message is
// terminated here, so a malformed request can neither exhaust temporary storage
// nor make the tokenizer read past the bytes received.
//
#define DAEMON_MESSAGE_MAX_LENGTH (64 * 1024)

//
// NOTE: Everything we do here holds the event loop. A client sends its whole
// request right after connecting and reads the reply until we close, so we
// give reading the request and writing the reply one second each, in total
// rather than per call. A client that stalls, trickles its request or stops
// reading a long reply loses its request or the rest of its reply, instead of
// holding every other event until it exits.
//
#define DAEMON_MESSAGE_TIMEOUT_MS 1000

// A blocking send waits until the whole buffer is queued, MSG_DONTWAIT or not,
// so the connection itself is made non-blocking; poll does the waiting.
static void daemon_message_set_nonblocking(int sockfd)
{
    int flags = fcntl(sockfd, F_GETFL);
    if (flags != -1) fcntl(sockfd, F_SETFL, flags | O_NONBLOCK);
}

// Waits until the socket is ready for `events` or the deadline passes.
static bool daemon_message_wait(int sockfd, short events, uint64_t deadline)
{
    for (;;) {
        uint64_t now = read_os_timer();
        if (now >= deadline) return false;

        struct pollfd ready = { .fd = sockfd, .events = events };
        int timeout = (int) ((deadline - now + 999999) / 1000000);
        int result = poll(&ready, 1, timeout);

        if (result > 0) return true;
        if (result == -1 && errno != EINTR) return false;
    }
}

// NULL once all `length` bytes are in, or the failure to reply with.
static const char *daemon_message_receive(int sockfd, void *bytes, int length, uint64_t deadline)
{
    int received = 0;

    while (received < length) {
        if (!daemon_message_wait(sockfd, POLLIN, deadline)) return FAILURE_MESSAGE "request timed out\n";

        ssize_t count = recv(sockfd, (char *) bytes + received, length - received, 0);
        if (count > 0) {
            received += (int) count;
        } else if (count == 0 || (errno != EAGAIN && errno != EINTR)) {
            return FAILURE_MESSAGE "request ended early\n";
        }
    }

    return NULL;
}

// The request, or NULL with `failure` set to the reply that tells the client
// why: a connection closed without one reads as a command with no output.
static char *daemon_message_read(int sockfd, int timeout_ms, const char **failure)
{
    uint64_t deadline = read_os_timer() + (uint64_t) timeout_ms * 1000000;
    int bytes_to_read = 0;

    daemon_message_set_nonblocking(sockfd);
    if ((*failure = daemon_message_receive(sockfd, &bytes_to_read, sizeof(int), deadline))) return NULL;

    if (bytes_to_read <= 0) {
        *failure = FAILURE_MESSAGE "request is malformed\n";
        return NULL;
    }

    if (bytes_to_read > DAEMON_MESSAGE_MAX_LENGTH) {
        *failure = FAILURE_MESSAGE "request is longer than 64 KiB\n";
        return NULL;
    }

    char *message = ts_alloc_unaligned(bytes_to_read + 2);
    if ((*failure = daemon_message_receive(sockfd, message, bytes_to_read, deadline))) return NULL;

    message[bytes_to_read] = '\0';
    message[bytes_to_read+1] = '\0';
    return message;
}

// A client that has already gone fails the send without raising SIGPIPE,
// even in a process that does not ignore it.
static bool daemon_message_reply(int sockfd, const char *bytes, size_t length, int timeout_ms)
{
    uint64_t deadline = read_os_timer() + (uint64_t) timeout_ms * 1000000;
    daemon_message_set_nonblocking(sockfd);

    size_t sent = 0;
    while (sent < length) {
        if (!daemon_message_wait(sockfd, POLLOUT, deadline)) return false;

        ssize_t count = send(sockfd, bytes + sent, length - sent, MSG_NOSIGNAL);
        if (count > 0) {
            sent += (size_t) count;
        } else if (count == 0 || (errno != EAGAIN && errno != EINTR)) {
            return false;
        }
    }

    return true;
}

// The reply is built in memory and written once the command has run, so the
// command never waits on the client.
static EVENT_HANDLER(DAEMON_MESSAGE)
{
    TIME_FUNCTION;

    space_navigation_queue_claim(param1);

    const char *failure = NULL;
    char *message = daemon_message_read(param1, DAEMON_MESSAGE_TIMEOUT_MS, &failure);

    if (message) {
        char *response = NULL;
        size_t response_size = 0;
        FILE *rsp = open_memstream(&response, &response_size);

        if (rsp) {
            debug_message(__FUNCTION__, message);
            handle_message(rsp, message);
            fclose(rsp);

            daemon_message_reply(param1, response, response_size, DAEMON_MESSAGE_TIMEOUT_MS);
        }

        free(response);
    } else if (failure) {
        daemon_message_reply(param1, failure, strlen(failure), DAEMON_MESSAGE_TIMEOUT_MS);
    }

    socket_close(param1);
    message_loop_answered();
}

// The second half of a navigation's focus change between two windows of one
// application, see navigation/activation.c.
static EVENT_HANDLER(SPACE_NAVIGATION_FOCUS)
{
    space_navigation_focus_resume(param1);
}

// The next step of queued navigation, see navigation/schedule.c.
static EVENT_HANDLER(SPACE_NAVIGATION_DISPATCH)
{
    space_navigation_schedule_timer();
}

// A navigation step's snapshot came back or reached its deadline, see
// navigation/step.c.
static EVENT_HANDLER(SPACE_NAVIGATION_CAPTURED)
{
    space_navigation_step_captured(param1);
}
