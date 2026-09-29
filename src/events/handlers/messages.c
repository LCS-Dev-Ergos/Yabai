// Daemon messages and queued navigation completion events.
// Runs on the event-loop thread.

//
// NOTE: A request is an int length followed by that many bytes of NUL-separated
// arguments, ending with an empty one. The length is bounded and the message is
// terminated here, so a malformed request can neither exhaust temporary storage
// nor make the tokenizer read past the bytes received.
//
#define DAEMON_MESSAGE_MAX_LENGTH (64 * 1024)

static char *daemon_message_read(int sockfd)
{
    int bytes_read    = 0;
    int bytes_to_read = 0;

    if (read(sockfd, &bytes_to_read, sizeof(int)) != sizeof(int)) return NULL;
    if (bytes_to_read <= 0 || bytes_to_read > DAEMON_MESSAGE_MAX_LENGTH) return NULL;

    char *message = ts_alloc_unaligned(bytes_to_read + 2);

    do {
        int cur_read = read(sockfd, message+bytes_read, bytes_to_read-bytes_read);
        if (cur_read <= 0) break;

        bytes_read += cur_read;
    } while (bytes_read < bytes_to_read);

    if (bytes_read != bytes_to_read) return NULL;

    message[bytes_read] = '\0';
    message[bytes_read+1] = '\0';
    return message;
}

static EVENT_HANDLER(DAEMON_MESSAGE)
{
    TIME_FUNCTION;

    FILE *rsp = NULL;
    space_navigation_queue_claim(param1);
    char *message = daemon_message_read(param1);

    if (message && (rsp = fdopen(param1, "w"))) {
        debug_message(__FUNCTION__, message);
        handle_message(rsp, message);

        fflush(rsp);
        fclose(rsp);

        return;
    }

    socket_close(param1);
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
