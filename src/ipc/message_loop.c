// The daemon socket: the accept thread hands each connection to the event
// loop, which reads the message and dispatches it to its domain.
#include "../osax/socket_path.h"
#include "../osax/socket_identity.h"

//
// NOTE: Each connection posted to the event loop holds a descriptor until it
// is answered. Near the descriptor limit, 256 under launchd, the kernel drops
// a new client unanswered and the client reports success, so past this many
// waiting connections we answer at once that we are busy instead.
//
#define MESSAGE_LOOP_MAX_PENDING 128

static struct {
    int sockfd;
    bool is_running;
    pthread_t thread;
    int pending;
} g_message_loop;
static SecRequirementRef g_message_loop_requirement;

void handle_message(FILE *rsp, char *message)
{
    space_navigation_note_message(message);
    struct token domain = get_token(&message);
    if (token_equals(domain, DOMAIN_CONFIG)) {
        handle_domain_config(rsp, domain, message);
    } else if (token_equals(domain, DOMAIN_DISPLAY)) {
        handle_domain_display(rsp, domain, message);
    } else if (token_equals(domain, DOMAIN_SPACE)) {
        handle_domain_space(rsp, domain, message);
    } else if (token_equals(domain, DOMAIN_WINDOW)) {
        handle_domain_window(rsp, domain, message);
    } else if (token_equals(domain, DOMAIN_QUERY)) {
        handle_domain_query(rsp, domain, message);
    } else if (token_equals(domain, DOMAIN_RULE)) {
        handle_domain_rule(rsp, domain, message);
    } else if (token_equals(domain, DOMAIN_SIGNAL)) {
        handle_domain_signal(rsp, domain, message);
    } else {
        daemon_fail(rsp, "unknown domain '%.*s'\n", domain.length, domain.text);
    }
}

// Accept thread. A client we do not hand to the event loop learns why: a
// connection closed without a reply reads as a command with no output.
static void message_loop_refuse(int sockfd, const char *reason)
{
    int flags = fcntl(sockfd, F_GETFL);
    if (flags != -1) fcntl(sockfd, F_SETFL, flags | O_NONBLOCK);

    send(sockfd, reason, strlen(reason), MSG_NOSIGNAL);
    close(sockfd);
}

// Accept thread. Whether the event loop takes the connection. One it does not
// take has been refused, or joined a waiting navigation request.
static bool message_loop_admit(int sockfd)
{
    if (!yabai_socket_peer_is_trusted(sockfd, getuid(), g_message_loop_requirement)) {
        message_loop_refuse(sockfd, FAILURE_MESSAGE "client refused: it is not signed like the running yabai\n");
        return false;
    }

    if (space_navigation_accept(sockfd)) return false;

    if (__atomic_load_n(&g_message_loop.pending, __ATOMIC_RELAXED) >= MESSAGE_LOOP_MAX_PENDING) {
        message_loop_refuse(sockfd, FAILURE_MESSAGE "too many requests are waiting\n");
        return false;
    }

    __atomic_add_fetch(&g_message_loop.pending, 1, __ATOMIC_RELAXED);
    return true;
}

// Event loop, once a connection the accept thread posted is answered.
void message_loop_answered(void)
{
    __atomic_sub_fetch(&g_message_loop.pending, 1, __ATOMIC_RELAXED);
}

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunused-parameter"
static void *message_loop_run(void *context)
{
    while (g_message_loop.is_running) {
        int sockfd = accept(g_message_loop.sockfd, NULL, 0);
        if (sockfd == -1) continue;
        if (!message_loop_admit(sockfd)) continue;

        event_loop_post(&g_event_loop, DAEMON_MESSAGE, NULL, sockfd);
    }

    return NULL;
}
#pragma clang diagnostic pop

bool message_loop_begin(char *socket_path)
{
    if (!YABAI_ALLOW_UNSIGNED_LOCAL) {
        CFDataRef data = yabai_socket_copy_self_requirement_data();
        g_message_loop_requirement = yabai_socket_requirement_from_data(data);
        if (data) CFRelease(data);
        if (!g_message_loop_requirement) return false;
    }

    struct sockaddr_un socket_address;
    socket_address.sun_family = AF_UNIX;
    snprintf(socket_address.sun_path, sizeof(socket_address.sun_path), "%s", socket_path);
    if (!yabai_socket_remove_stale(socket_path, getuid())) return false;

    if ((g_message_loop.sockfd = socket(AF_UNIX, SOCK_STREAM, 0)) == -1) {
        return false;
    }

    if (bind(g_message_loop.sockfd, (struct sockaddr *) &socket_address, sizeof(socket_address)) == -1) {
        return false;
    }

    if (chmod(socket_path, 0600) != 0) {
        return false;
    }

    if (listen(g_message_loop.sockfd, SOMAXCONN) == -1) {
        return false;
    }

    fcntl(g_message_loop.sockfd, F_SETFD, FD_CLOEXEC | fcntl(g_message_loop.sockfd, F_GETFD));

    g_message_loop.is_running = true;
    pthread_create(&g_message_loop.thread, NULL, &message_loop_run, NULL);

    return true;
}
