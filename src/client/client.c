// The client side of the daemon socket, shared by `yabai -m` and yabai-msg.
// It needs only libc and the socket path, so yabai-msg links no framework.
#include <errno.h>
#include <limits.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

#include "../misc/macros.h"
#include "../osax/socket_path.h"

// Packs the arguments after argv[0] as the daemon reads them: an int length,
// then each argument with its NUL, then an empty argument.
static char *client_request(int argc, char **argv, size_t *size)
{
    size_t length = 1;
    for (int i = 1; i < argc; ++i) length += strlen(argv[i]) + 1;
    if (length > INT_MAX) return NULL;

    char *request = malloc(sizeof(int) + length);
    if (!request) return NULL;

    int header = (int) length;
    memcpy(request, &header, sizeof(int));

    char *cursor = request + sizeof(int);
    for (int i = 1; i < argc; ++i) {
        size_t argument = strlen(argv[i]) + 1;
        memcpy(cursor, argv[i], argument);
        cursor += argument;
    }
    *cursor = '\0';

    *size = sizeof(int) + length;
    return request;
}

// A daemon that has gone fails the send without raising SIGPIPE.
static bool client_send(int sockfd, const char *bytes, size_t size)
{
    while (size > 0) {
        ssize_t count = send(sockfd, bytes, size, MSG_NOSIGNAL);
        if (count > 0) {
            bytes += count;
            size -= (size_t) count;
        } else if (count == -1 && errno == EINTR) {
            continue;
        } else {
            return false;
        }
    }

    return true;
}

// Reads until the daemon closes; false if the connection fails first.
static bool client_receive(int sockfd, char **reply, size_t *length)
{
    size_t capacity = 0;
    *reply = NULL;
    *length = 0;

    for (;;) {
        if (*length == capacity) {
            capacity = capacity ? capacity * 2 : 4096;
            char *grown = realloc(*reply, capacity);
            if (!grown) return false;
            *reply = grown;
        }

        ssize_t count = recv(sockfd, *reply + *length, capacity - *length, 0);
        if (count > 0) {
            *length += (size_t) count;
        } else if (count == 0) {
            return true;
        } else if (errno != EINTR) {
            return false;
        }
    }
}

// The daemon starts each failure it reports with FAILURE_MESSAGE, at the start
// of a line, see daemon_fail.
static bool client_reply_failed(const char *reply, size_t length)
{
    for (size_t i = 0; i < length; ++i) {
        if (reply[i] == FAILURE_MESSAGE[0] && (i == 0 || reply[i-1] == '\n')) return true;
    }

    return false;
}

// Prints the reply to `output`, or, if it reports a failure, to `errors` with
// the failure markers removed.
static int client_print_reply(const char *reply, size_t length, FILE *output, FILE *errors)
{
    bool failed = client_reply_failed(reply, length);
    FILE *stream = failed ? errors : output;

    for (size_t start = 0; start < length;) {
        const char *newline = memchr(reply + start, '\n', length - start);
        size_t end = newline ? (size_t) (newline - reply) + 1 : length;
        size_t marker = reply[start] == FAILURE_MESSAGE[0] ? 1 : 0;

        fwrite(reply + start + marker, 1, end - start - marker, stream);
        start = end;
    }

    fflush(stream);
    return failed ? EXIT_FAILURE : EXIT_SUCCESS;
}

//
// NOTE: The daemon gives a client one second to read its reply, so we read
// all of it before printing any. A reader of our output that stalls, such as a
// pager, then holds us instead of the daemon, and nothing is cut off.
//
static int client_exchange(int sockfd, const char *request, size_t size, FILE *output, FILE *errors)
{
    bool sent = client_send(sockfd, request, size);
    shutdown(sockfd, SHUT_WR);

    char *reply = NULL;
    size_t length = 0;
    int result = EXIT_FAILURE;

    if (!client_receive(sockfd, &reply, &length)) {
        fprintf(errors, "yabai-msg: lost the connection to yabai..\n");
    } else {
        // A daemon that refuses a request can close before taking all of it,
        // but it says why first.
        result = client_print_reply(reply, length, output, errors);
        if (!sent && result == EXIT_SUCCESS) {
            fprintf(errors, "yabai-msg: failed to send data..\n");
            result = EXIT_FAILURE;
        }
    }

    free(reply);
    return result;
}

// Sends the arguments after argv[0] to the daemon of this user and prints its
// reply; the result is the exit status.
static int client_send_message(int argc, char **argv)
{
    if (argc <= 1) {
        fprintf(stderr, "yabai-msg: no arguments given! abort..\n");
        return EXIT_FAILURE;
    }

    size_t size = 0;
    char *request = client_request(argc, argv, &size);
    if (!request) {
        fprintf(stderr, "yabai-msg: arguments are too long! abort..\n");
        return EXIT_FAILURE;
    }

    int result = EXIT_FAILURE;
    int sockfd = -1;
    struct sockaddr_un address = { .sun_family = AF_UNIX };

    if (!yabai_socket_path(getuid(), YABAI_SOCKET_DAEMON, address.sun_path, sizeof(address.sun_path), false)) {
        fprintf(stderr, "yabai-msg: private socket directory is unavailable..\n");
    } else if ((sockfd = socket(AF_UNIX, SOCK_STREAM, 0)) == -1) {
        fprintf(stderr, "yabai-msg: failed to open socket..\n");
    } else if (connect(sockfd, (struct sockaddr *) &address, sizeof(address)) == -1) {
        fprintf(stderr, "yabai-msg: failed to connect to socket..\n");
    } else {
        result = client_exchange(sockfd, request, size, stdout, stderr);
    }

    if (sockfd != -1) close(sockfd);
    free(request);
    return result;
}
